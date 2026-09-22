/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/io/block.h>
#include <modvm/io/net.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static struct vm_ctx *active;
static int mode, boot_calls, run_calls, accel_freed, cpu_freed;
static int fake_accel_init(struct accel *a)
{
	a->priv = malloc(8);
	if (!a->priv)
		return -VM_ENOMEM;
	if (mode == 4)
		return -VM_EIO;
	(void)a;
	return 0;
}
static void fake_accel_destroy(struct accel *a)
{
	if (a->priv) {
		accel_freed++;
		free(a->priv);
		a->priv = NULL;
	}
	(void)a;
}
static int cpu_init(struct vcpu *v)
{
	v->priv = malloc(8);
	if (!v->priv)
		return -VM_ENOMEM;
	return mode == 5 ? -VM_ENXIO : 0;
}
static void cpu_destroy(struct vcpu *v)
{
	if (v->priv) {
		cpu_freed++;
		free(v->priv);
		v->priv = NULL;
	}
}
static int cpu_run(struct vcpu *v)
{
	CHECK(!atomic_load(v->accel->stop_requested));
	run_calls++;
	vm_request_shutdown(active);
	return 0;
}
static int cpu_kick(struct vcpu *v, struct host_thread *thread)
{
	CHECK(thread && atomic_load(v->accel->stop_requested));
	return mode == 7 ? -VM_EIO : 0;
}
static int board_init(struct vm_ctx *vm)
{
	(void)vm;
	return 0;
}
static int boot(struct vm_ctx *vm)
{
	boot_calls++;
	CHECK(!strcmp(vm->config.loader_name, "snapshot-loader"));
	CHECK(!strcmp(vm->config.loader_opts, "snapshot-options"));
	if (mode == 1 || mode == 2)
		vm_request_shutdown(vm);
	if (mode == 3) {
		vm_report_error(vm, -VM_ENOSPC);
		return -VM_EIO;
	}
	return 0;
}
static int mac(struct net_backend *net, uint8_t out[6])
{
	(void)net;
	memset(out, 2, 6);
	return 0;
}
int main(void)
{
	CHECK(log_init() == 0);
	const struct accel_ops ao = { .init = fake_accel_init, .destroy = fake_accel_destroy };
	const struct vcpu_ops vo = { .init = cpu_init, .destroy = cpu_destroy, .run = cpu_run, .kick = cpu_kick };
	const struct accel_desc backend = { .name = "lifecycle-test", .accel_ops = &ao, .vcpu_ops = &vo };
	const struct board_ops bo = { .init = board_init, .boot = boot };
	const struct board_desc board = { .name = "lifecycle-board", .ops = &bo };
	accel_register(&backend);
	for (mode = 0; mode < 8; mode++) {
		boot_calls = run_calls = accel_freed = cpu_freed = 0;
		struct vm_ctx vm;
		active = &vm;
		struct block_backend disk = { 0 };
		const struct net_ops no = { .get_mac = mac };
		struct net_backend net = { .ops = &no };
		struct block_backend **drives = malloc(sizeof(*drives));
		struct net_backend **nets = malloc(sizeof(*nets));
		CHECK(drives && nets);
		drives[0] = &disk;
		nets[0] = &net;
		char *accel = strdup(backend.name), *loader = strdup("snapshot-loader"), *opts = strdup("snapshot-options");
		CHECK(accel && loader && opts);
		struct vm_config cfg = {
			.accel_name = accel, .nr_vcpus = 1, .board = &board, .loader_name = loader, .loader_opts = opts, .drives = drives, .nr_drives = 1, .nets = nets, .nr_nets = 1
		};
		int ret = vm_init(&vm, &cfg);
		CHECK(ret == (mode == 4 ? -VM_EIO : mode == 5 ? -VM_ENXIO : 0));
		memset(accel, 'x', strlen(accel));
		memset(loader, 'x', strlen(loader));
		memset(opts, 'x', strlen(opts));
		drives[0] = NULL;
		nets[0] = NULL;
		free(accel);
		free(loader);
		free(opts);
		free(drives);
		free(nets);
		if (!ret) {
			CHECK(!strcmp(vm.config.accel_name, backend.name));
			CHECK(vm.config.drives[0] == &disk && vm.config.nets[0] == &net);
			if (mode == 0)
				vm_request_shutdown(&vm);
			CHECK(vm_run(&vm) == (mode == 3 ? -VM_ENOSPC : mode == 7 ? -VM_EIO : 0));
			CHECK(atomic_load(&vm.stop_requested));
			CHECK(vm.state == VM_STOPPED);
			CHECK(boot_calls == (mode == 0 ? 0 : 1));
			CHECK(run_calls == (mode >= 6 ? 1 : 0));
		}
		vm_destroy(&vm);
		CHECK(accel_freed == 1 && cpu_freed == (mode == 4 ? 0 : 1));
	}
	log_destroy();
	puts("VM stop requests, partial initialization, configuration snapshots and first error passed");
	return 0;
}
