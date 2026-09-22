/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/io/net.h>
#include <modvm/io/char.h>
#include <modvm/io/block.h>
#include <modvm/core/device.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/io/ctx.h>

#include "memory_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "vm: " fmt

static void io_failed(void *data, int error)
{
	vm_report_error(data, error);
}

static void io_cleanup(void *data)
{
	io_ctx_destroy(data);
}

static void mutex_cleanup(void *data)
{
	host_mutex_destroy(data);
}

static void memory_cleanup(void *data)
{
	mem_space_destroy(data);
}

static void accel_cleanup(void *data)
{
	accel_destroy(data);
}

static void vcpu_cleanup(void *data)
{
	vcpu_destroy(data);
}

struct vcpu_run_data {
	struct vm_ctx *ctx;
	struct vcpu *vcpu;
};

static void *vcpu_thread_fn(void *data)
{
	struct vcpu_run_data *run_data = data;
	int ret;

	host_mutex_lock(run_data->ctx->thread_lock);
	host_mutex_unlock(run_data->ctx->thread_lock);
	if (atomic_load(&run_data->ctx->stop_requested))
		return NULL;
	ret = vcpu_run(run_data->vcpu);
	if (ret < 0) {
		pr_err("fatal execution error on vcpu %d, initiating emergency shutdown\n", run_data->vcpu->id);
		vm_report_error(run_data->ctx, ret);
	}

	return NULL;
}

/**
 * vm_init - assemble the virtual machine topology and core runtime
 * @ctx: the context object to initialize
 * @config: the immutable configuration parameters
 *
 * Allocates processors, initializes the core acceleration context,
 * and invokes the board-specific initialization hook to wire up RAM
 * and peripherals.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int vm_init(struct vm_ctx *ctx, const struct vm_config *config)
{
	unsigned int i;
	int ret;

	if (!ctx)
		return -VM_EINVAL;

	memset(ctx, 0, sizeof(*ctx));
	atomic_init(&ctx->run_error, 0);
	atomic_init(&ctx->stop_requested, false);
	INIT_LIST_HEAD(&ctx->mem_space.regions);

	INIT_LIST_HEAD(&ctx->devices);
	INIT_LIST_HEAD(&ctx->io_map.pio_regions);
	INIT_LIST_HEAD(&ctx->io_map.mmio_regions);
	res_pool_init(&ctx->resources);
	if (!config || !config->accel_name || !config->nr_vcpus || config->nr_nets > SIZE_MAX / sizeof(*config->nets) || config->nr_drives > SIZE_MAX / sizeof(*config->drives) ||
	    (config->nr_nets && !config->nets) || (config->nr_drives && !config->drives))
		return -VM_EINVAL;
	if (config->board && (!config->board->ops || !config->board->ops->init))
		return -VM_EINVAL;
	size_t run_bytes;
	if (__builtin_mul_overflow((size_t)config->nr_vcpus, sizeof(struct vcpu_run_data), &run_bytes))
		return -VM_EOVERFLOW;
	ctx->config = *config;
	/* Copy configuration storage, retaining borrowed backend objects. */
	const char *strings[] = { config->accel_name, config->loader_name, config->loader_opts };
	const char **copies[] = { &ctx->config.accel_name, &ctx->config.loader_name, &ctx->config.loader_opts };
	for (size_t n = 0; n < 3; n++) {
		*copies[n] = NULL;
		if (!strings[n])
			continue;
		size_t len = strlen(strings[n]) + 1;
		char *copy = res_zalloc(&ctx->resources, len);
		if (!copy)
			return -VM_ENOMEM;
		memcpy(copy, strings[n], len);
		*copies[n] = copy;
	}
	ctx->config.drives = NULL;
	ctx->config.nets = NULL;
	if (config->nr_drives) {
		ctx->config.drives = res_zalloc(&ctx->resources, config->nr_drives * sizeof(*config->drives));
		if (!ctx->config.drives)
			return -VM_ENOMEM;
		memcpy(ctx->config.drives, config->drives, config->nr_drives * sizeof(*config->drives));
	}
	if (config->nr_nets) {
		ctx->config.nets = res_zalloc(&ctx->resources, config->nr_nets * sizeof(*config->nets));
		if (!ctx->config.nets)
			return -VM_ENOMEM;
		memcpy(ctx->config.nets, config->nets, config->nr_nets * sizeof(*config->nets));
	}
	if (IS_ERR(config->console))
		return -VM_EINVAL;
	for (size_t n = 0; n < config->nr_drives; n++) {
		struct block_backend *blk = config->drives[n];
		if (!blk || IS_ERR(blk))
			return -VM_EINVAL;
	}

	for (size_t n = 0; n < config->nr_nets; n++) {
		uint8_t mac[6], other[6];
		if (!config->nets[n] || IS_ERR(config->nets[n]))
			return -VM_EINVAL;
		ret = config->nets[n]->ops->get_mac(config->nets[n], mac);
		if (ret < 0)
			return ret;
		for (size_t j = 0; j < n; j++) {
			ret = config->nets[j]->ops->get_mac(config->nets[j], other);
			if (ret < 0)
				return ret;
			if (!memcmp(mac, other, 6))
				return -VM_EEXIST;
		}
	}
	ctx->io_ctx = io_ctx_create(io_failed, ctx);
	if (IS_ERR(ctx->io_ctx)) {
		ret = PTR_ERR(ctx->io_ctx);
		ctx->io_ctx = NULL;
		return ret;
	}
	ret = res_add_action_or_reset(&ctx->resources, io_cleanup, ctx->io_ctx);
	if (ret < 0) {
		ctx->io_ctx = NULL;
		return ret;
	}
	ctx->io_map.io_lock = io_ctx_get_device_lock(ctx->io_ctx);
	ctx->thread_lock = host_mutex_create();
	if (IS_ERR(ctx->thread_lock)) {
		ret = PTR_ERR(ctx->thread_lock);
		ctx->thread_lock = NULL;
		return ret;
	}
	ret = res_add_action_or_reset(&ctx->resources, mutex_cleanup, ctx->thread_lock);
	if (ret < 0) {
		ctx->thread_lock = NULL;
		return ret;
	}

	/* Register RAM first: reverse cleanup destroys vCPUs and accelerator before RAM. */
	ret = res_add_action(&ctx->resources, memory_cleanup, &ctx->mem_space);
	if (ret < 0)
		return ret;
	ret = mem_space_init(&ctx->mem_space);
	if (ret < 0)
		return ret;

	ret = res_add_action(&ctx->resources, accel_cleanup, &ctx->accel);
	if (ret < 0)
		return ret;
	ret = accel_init(&ctx->accel, ctx->config.accel_name, &ctx->io_map, &ctx->stop_requested);
	if (ret < 0) {
		pr_err("failed to instantiate hardware acceleration engine\n");
		return ret;
	}

	/*
	 * We delegate physical memory mapping to the specific board class,
	 * allowing it to handle architectural quirks like the x86 PCI hole.
	 */
	if (ctx->config.board && ctx->config.board->ops && ctx->config.board->ops->init) {
		ret = ctx->config.board->ops->init(ctx);
		if (ret < 0) {
			pr_err("board physical topology wiring failed\n");
			return ret;
		}
	}

	ctx->vcpus = res_zalloc(&ctx->resources, ctx->config.nr_vcpus * sizeof(struct vcpu *));
	ctx->vcpu_threads = res_zalloc(&ctx->resources, ctx->config.nr_vcpus * sizeof(struct host_thread *));
	if (!ctx->vcpus || !ctx->vcpu_threads)
		return -VM_ENOMEM;

	for (i = 0; i < ctx->config.nr_vcpus; i++) {
		ctx->vcpus[i] = res_zalloc(&ctx->resources, sizeof(struct vcpu));
		if (!ctx->vcpus[i])
			return -VM_ENOMEM;

		ret = res_add_action(&ctx->resources, vcpu_cleanup, ctx->vcpus[i]);
		if (ret < 0)
			return ret;
		ret = vcpu_init(ctx->vcpus[i], &ctx->accel, i);
		if (ret < 0) {
			pr_err("failed to instantiate vcpu %u\n", i);
			return ret;
		}
	}

	/* Complete board setup that requires initialized vCPUs. */
	if (ctx->config.board && ctx->config.board->ops && ctx->config.board->ops->late_init) {
		ret = ctx->config.board->ops->late_init(ctx);
		if (ret < 0) {
			pr_err("board late initialization hook failed\n");
			return ret;
		}
	}

	ret = atomic_load(&ctx->run_error);
	if (ret < 0)
		return ret;
	ctx->state = VM_READY;
	pr_info("context assembled with %zu bytes ram and %u vcpus\n", ctx->config.ram_size, ctx->config.nr_vcpus);

	return 0;
}

/* Registered before starting any worker; one exit path owns shutdown and joins. */
static void run_cleanup(void *data)
{
	struct vm_ctx *ctx = data;
	vm_request_shutdown(ctx);
	for (unsigned i = 0; i < ctx->config.nr_vcpus; i++) {
		host_mutex_lock(ctx->thread_lock);
		struct host_thread *thread = ctx->vcpu_threads[i];
		ctx->vcpu_threads[i] = NULL;
		host_mutex_unlock(ctx->thread_lock);
		if (thread) {
			int ret = host_thread_join(thread);
			if (ret < 0)
				panic("cannot join vCPU thread: %d", ret);
			host_thread_destroy(thread);
		}
	}
	ctx->state = VM_STOPPED;
}

/**
 * vm_run - start vCPU threads and dispatch queued device work
 * @ctx: the fully initialized context object
 *
 * Return: 0 upon successful shutdown, or a negative error code.
 */
int vm_run(struct vm_ctx *ctx)
{
	struct vcpu_run_data *run_data;
	struct res_pool run_resources;
	unsigned int i;
	int ret;

	if (WARN_ON(!ctx))
		return -VM_EINVAL;

	if (ctx->state != VM_READY)
		return -VM_EINVAL;
	if (atomic_load(&ctx->stop_requested)) {
		ctx->state = VM_STOPPED;
		return atomic_load(&ctx->run_error);
	}
	res_pool_init(&run_resources);
	run_data = res_zalloc(&run_resources, ctx->config.nr_vcpus * sizeof(*run_data));
	if (!run_data) {
		vm_report_error(ctx, -VM_ENOMEM);
		ctx->state = VM_STOPPED;
		return atomic_load(&ctx->run_error);
	}
	ret = res_add_action(&run_resources, run_cleanup, ctx);
	if (ret < 0) {
		vm_report_error(ctx, ret);
		res_release_all(&run_resources);
		ctx->state = VM_STOPPED;
		return atomic_load(&ctx->run_error);
	}
	ctx->state = VM_RUNNING;
	pr_info("powering on the virtual machine context...\n");

	if (atomic_load(&ctx->stop_requested))
		goto out;

	if (ctx->config.board && ctx->config.board->ops && ctx->config.board->ops->boot) {
		ret = ctx->config.board->ops->boot(ctx);
		if (ret < 0) {
			pr_err("board boot and firmware injection failed\n");
			goto out;
		}
	}

	ret = 0;
	host_mutex_lock(ctx->thread_lock);

	for (i = 0; i < ctx->config.nr_vcpus && !atomic_load(&ctx->stop_requested); i++) {
		run_data[i].ctx = ctx;
		run_data[i].vcpu = ctx->vcpus[i];

		ctx->vcpu_threads[i] = host_thread_create(vcpu_thread_fn, &run_data[i]);

		if (IS_ERR(ctx->vcpu_threads[i])) {
			pr_err("failed to spawn kernel thread for vcpu %u\n", i);
			ret = PTR_ERR(ctx->vcpu_threads[i]);
			ctx->vcpu_threads[i] = NULL;
			break;
		}
	}

	host_mutex_unlock(ctx->thread_lock);

	if (!ret)
		ret = io_ctx_run(ctx->io_ctx);
out:
	if (ret < 0)
		vm_report_error(ctx, ret);
	res_release_all(&run_resources);
	return atomic_load(&ctx->run_error);
}

/**
 * vm_request_shutdown - request device dispatch and vCPU threads to stop
 * @ctx: the active context object
 */
void vm_request_shutdown(struct vm_ctx *ctx)
{
	unsigned int i;

	if (WARN_ON(!ctx))
		return;

	atomic_store(&ctx->stop_requested, true);

	/* Serialize handle publication and wakeup against thread startup/cleanup. */
	if (ctx->vcpu_threads) {
		host_mutex_lock(ctx->thread_lock);
		for (i = 0; i < ctx->config.nr_vcpus; i++) {
			if (ctx->vcpu_threads[i] && ctx->vcpus[i]->ops->kick) {
				int ret = ctx->vcpus[i]->ops->kick(ctx->vcpus[i], ctx->vcpu_threads[i]);
				if (ret < 0) {
					int expected = 0;
					atomic_compare_exchange_strong(&ctx->run_error, &expected, ret);
				}
			}
		}
		host_mutex_unlock(ctx->thread_lock);
	}
	io_ctx_stop(ctx->io_ctx);
}

/* Fatal asynchronous failures use the same first-error/stop path. */
void vm_report_error(struct vm_ctx *ctx, int error)
{
	if (!ctx || error >= 0)
		return;
	int expected = 0;
	atomic_compare_exchange_strong(&ctx->run_error, &expected, error);
	vm_request_shutdown(ctx);
}

/**
 * vm_destroy - destroy devices and release VM-owned resources
 * @ctx: the context to destroy
 */
void vm_destroy(struct vm_ctx *ctx)
{
	struct device *dev;

	if (WARN_ON(!ctx))
		return;

	/*
	 * Outer perimeter teardown: Dismantle peripheral devices first so they
	 * unregister from buses before memory maps disappear.
	 */
	while (!list_empty(&ctx->devices)) {
		dev = list_last_entry(&ctx->devices, struct device, node);
		device_destroy(dev);
	}

	/*
	 * Core teardown: releases VM resources in reverse acquisition order.
	 * Worker threads have already been joined and freed by vm_run.
	 */
	res_release_all(&ctx->resources);
	ctx->vcpu_threads = NULL;
	ctx->vcpus = NULL;
	ctx->io_ctx = NULL;
	ctx->io_map.io_lock = NULL;
	ctx->thread_lock = NULL;
	ctx->config = (struct vm_config){ 0 };
	ctx->state = VM_STOPPED;
}
