/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_TEST_FIXTURE_H
#define MODVM_TEST_FIXTURE_H
#include <modvm/host/mutex.h>
#include <modvm/core/vm.h>
#include <modvm/util/err.h>
static inline void test_io_cleanup(void *data)
{
	io_ctx_destroy(data);
}

static inline int test_io_init(struct vm_ctx *vm)
{
	vm->io_ctx = io_ctx_create(NULL, NULL);
	if (IS_ERR(vm->io_ctx))
		return PTR_ERR(vm->io_ctx);
	vm->io_map.io_lock = io_ctx_get_device_lock(vm->io_ctx);
	return res_add_action_or_reset(&vm->resources, test_io_cleanup, vm->io_ctx);
}
#endif
