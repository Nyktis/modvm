/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/errno.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/core/io_map.h>
#include <modvm/io/net.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/io/ctx.h>
#include "fixture.h"
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int calloc_at, create_at, poll_error, cpu_error;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size)
{
	if (calloc_at && !--calloc_at)
		return NULL;
	return __real_calloc(n, size);
}

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *d)
{
	if (create_at && !--create_at)
		return EAGAIN;
	return __real_pthread_create(t, a, f, d);
}

static struct vm_ctx *joining_vm;
int __real_host_thread_join(struct host_thread *thread);
int __wrap_host_thread_join(struct host_thread *thread)
{
	CHECK(joining_vm);
	for (unsigned i = 0; i < joining_vm->config.nr_vcpus; i++)
		CHECK(joining_vm->vcpu_threads[i] != thread);
	int ret = __real_host_thread_join(thread);
	/* Exercise shutdown exactly after the native thread ID ceased to exist. */
	vm_request_shutdown(joining_vm);
	return ret;
}

int __real_poll(struct pollfd *, nfds_t, int);
int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout)
{
	if (poll_error) {
		errno = EIO;
		return -1;
	}
	return __real_poll(fds, n, timeout);
}

int __wrap___poll_chk(struct pollfd *fds, nfds_t n, int timeout, size_t size)
{
	(void)size;
	return __wrap_poll(fds, n, timeout);
}

static int init(struct accel *a)
{
	(void)a;
	return 0;
}

static void destroy(struct accel *a)
{
	(void)a;
}

static int map_ram(struct accel *a, const struct mem_region *region)
{
	/* This test accelerator does not execute guest memory. */
	(void)a;
	(void)region;
	return 0;
}

static int cpu_init(struct vcpu *v)
{
	(void)v;
	return 0;
}

static int run(struct vcpu *v)
{
	if (cpu_error)
		return -VM_EIO;
	while (!atomic_load(v->accel->stop_requested))
		sched_yield();
	return 0;
}

static int fail_bind(struct net_backend *net, struct io_receiver *binding)
{
	(void)net;
	(void)binding;
	return -VM_ENOSPC;
}

static void unbind(struct net_backend *net)
{
	(void)net;
}

static int setup_irqchip(struct accel *accel)
{
	(void)accel;
	return 0;
}

static int inject_irq(struct accel *accel, gsi_t gsi, int level)
{
	(void)accel;
	(void)gsi;
	(void)level;
	return -VM_EHOSTDOWN;
}

static int get_mac(struct net_backend *net, uint8_t mac[6])
{
	(void)net;
	for (unsigned i = 0; i < 6; i++)
		mac[i] = i + 2;
	return 0;
}

static void transport_irq(void *data)
{
	(void)data;
}
static const struct virtio_transport_ops transport = { .notify_queue = transport_irq, .notify_config = transport_irq };

int main(void)
{
	const struct accel_ops ops = { .init = init, .destroy = destroy, .map_ram = map_ram, .setup_irqchip = setup_irqchip, .set_irq = inject_irq };
	const struct vcpu_ops cpus = { .init = cpu_init, .run = run };
	const struct accel_desc backend = { .name = "failure-test", .accel_ops = &ops, .vcpu_ops = &cpus };
	struct vm_config cfg = { .accel_name = "failure-test", .nr_vcpus = 2 };
	log_init();
	accel_register(&backend);
	alarm(10);
	struct vm_ctx partial;
	CHECK(vm_init(&partial, NULL) == -VM_EINVAL);
	vm_request_shutdown(&partial);
	vm_destroy(&partial);
	struct vm_config bad = cfg;
	bad.nr_nets = 1;
	CHECK(vm_init(&partial, &bad) == -VM_EINVAL);
	vm_request_shutdown(&partial);
	vm_destroy(&partial);
	/* Every allocation failure in initialization must support destruction. */
	for (int n = 1; n < 24; n++) {
		struct vm_ctx vm;
		calloc_at = n;
		int ret = vm_init(&vm, &cfg);
		calloc_at = 0;
		CHECK(ret == 0 || ret == -VM_ENOMEM);
		vm_request_shutdown(&vm);
		vm_destroy(&vm);
	}
	/* Each partially allocated frontend can be released; failed binding rolls back. */
	for (int n = 1; n <= 4; n++) {
		struct vm_ctx vm = { 0 };
		res_pool_init(&vm.resources);
		CHECK(test_io_init(&vm) == 0);
		struct device parent = { .ctx = &vm };
		res_pool_init(&parent.resources);
		const struct net_ops netops = { .get_mac = get_mac, .unbind = unbind, .bind = fail_bind };
		struct net_backend net = { .ops = &netops };
		struct virtio_device *v = virtio_net_create(&vm, &net);
		CHECK(v);
		CHECK(virtio_device_adopt_locked(&parent, v, &transport, NULL) == 0);
		calloc_at = n;
		int ret = virtio_device_realize_locked(v);
		calloc_at = 0;
		CHECK(ret == -VM_ENOMEM || ret == -VM_ENOSPC);
		CHECK(!atomic_load(&net.binding));
		CHECK(virtio_device_destroy(v) == -VM_EBUSY);
		res_release_all(&parent.resources);
		res_release_all(&vm.resources);
	}

	for (int mode = 0; mode < 3; mode++) {
		struct vm_ctx vm;
		CHECK(vm_init(&vm, &cfg) == 0);
		create_at = mode == 0 ? 2 : 0;
		poll_error = mode == 1;
		cpu_error = mode == 2;
		joining_vm = &vm;
		if (mode == 1)
			io_ctx_fail(vm.io_ctx, -VM_EIO);
		CHECK(vm_run(&vm) == (mode == 0 ? -VM_EAGAIN : -VM_EIO));
		CHECK(!vm.vcpu_threads[0] && !vm.vcpu_threads[1]);
		vm_destroy(&vm);
	}
	struct vm_ctx failed;
	CHECK(vm_init(&failed, &cfg) == 0);
	vm_report_error(&failed, -VM_ENXIO);
	vm_report_error(&failed, -VM_EIO);
	CHECK(vm_run(&failed) == -VM_ENXIO);
	vm_destroy(&failed);
	struct vm_config pc = cfg;
	pc.board = board_find("pc");
	pc.ram_size = 2 * 1024 * 1024;
	CHECK(vm_init(&failed, &pc) == 0);
	/* Enabling UART THRE raises IRQ 4 through the actual PC board route. */
	io_map_write(&failed.io_map, IO_PIO, TO_GPA(0x3f9), 2, 1);
	CHECK(atomic_load(&failed.run_error) == -VM_EHOSTDOWN);
	CHECK(vm_run(&failed) == -VM_EHOSTDOWN);
	vm_destroy(&failed);
	alarm(0);
	log_destroy();
	puts("allocation rollback, partial thread startup, poll failure and CPU failure passed");
	return 0;
}
