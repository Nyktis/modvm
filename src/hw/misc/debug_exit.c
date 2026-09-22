/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/core/device.h>
#include <modvm/errno.h>
#include <stdlib.h>

#include <modvm/core/io_map.h>
#include <modvm/core/vm.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/misc/debug_exit.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

struct debug_exit_ctx {
	struct vm_ctx *ctx;
};

static void debug_exit_write(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size)
{
	struct device *dev = region->dev;
	struct debug_exit_ctx *priv = dev->priv;

	(void)offset;
	(void)size;

	pr_info("guest requested power off (exit code 0x%lx)\n", val);
	vm_request_shutdown(priv->ctx);
}

static const struct device_ops debug_exit_ops = {
	.write = debug_exit_write,
};

/**
 * debug_exit_realize - allocate and register the debug exit peripheral
 * @dev: the abstract device object assigned by the core framework
 * @pdata: immutable routing configuration
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
static int debug_exit_realize(struct device *dev, void *pdata)
{
	struct debug_exit_pdata *plat = pdata;
	struct debug_exit_ctx *priv;

	if (WARN_ON(!plat))
		return -VM_EINVAL;

	priv = res_zalloc(&dev->resources, sizeof(*priv));
	if (!priv)
		return -VM_ENOMEM;

	priv->ctx = dev->ctx;

	dev->ops = &debug_exit_ops;
	dev->priv = priv;

	struct io_region *region = io_map_register_region_locked(plat->io_space, plat->base, 1, dev);
	if (IS_ERR(region))
		return PTR_ERR(region);

	pr_info("debug exit device mapped in %s space at 0x%08llx\n", plat->io_space == IO_MMIO ? "mmio" : "pio", (unsigned long long)GPA_VAL(plat->base));

	return 0;
}

static const struct device_desc debug_exit_desc = {
	.name = "debug-exit",
	.realize = debug_exit_realize,
};

static void __attribute__((constructor)) register_debug_exit_desc(void)
{
	device_register(&debug_exit_desc);
}
