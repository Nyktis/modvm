/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/util/err.h>
#include <modvm/errno.h>
#include <stdlib.h>

#include <modvm/core/device.h>
#include <modvm/core/io_map.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/core/irq.h>
#include <modvm/hw/pci/pio_bridge.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include <modvm/util/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "pio_bridge: " fmt

/**
 * struct pio_bridge_ctx - state container for the x86 PIO PCI Host Bridge
 * @config_addr: latched address for the subsequent data port access
 * @bus: the core PCI bus segment managed by this bridge
 * @host_bridge: PCI configuration-space identity of the bridge itself
 * @pirq: array mapping the 4 standard PCI routing lines (PIRQA-PIRQD) to system GSIs
 * @asserted: per-PIRQ bitsets of asserting PCI functions, for shared interrupt aggregation
 */
struct pio_bridge_ctx {
	uint32_t config_addr;
	struct pci_bus bus;
	struct pci_device host_bridge;
	struct irq *pirq[4];
	uint64_t asserted[4][4];
};

static void pio_bridge_set_irq_cb(void *data, struct pci_device *pci_dev, int level)
{
	struct pio_bridge_ctx *ctx = data;
	uint8_t slot;
	uint8_t pin;
	uint8_t pirq_idx;

	if (unlikely(!pci_dev))
		return;

	pin = pci_dev->interrupt_pin;
	if (unlikely(pin == 0 || pin > 4))
		return;

	slot = PCI_SLOT(pci_dev->devfn);
	pirq_idx = (slot + pin - 1) % 4;

	unsigned source = PCI_DEVFN_VAL(pci_dev->devfn);
	uint64_t bit = 1ULL << (source % 64);
	if (level)
		ctx->asserted[pirq_idx][source / 64] |= bit;
	else
		ctx->asserted[pirq_idx][source / 64] &= ~bit;
	bool asserted = false;
	for (unsigned i = 0; i < 4; i++)
		asserted |= ctx->asserted[pirq_idx][i] != 0;
	if (ctx->pirq[pirq_idx])
		irq_set_level(ctx->pirq[pirq_idx], asserted);
}

static uint64_t pio_bridge_read(struct io_region *region, uint64_t offset, uint8_t size)
{
	struct device *dev = region->dev;
	struct pio_bridge_ctx *ctx = dev->priv;
	uint8_t bus_num, devfn, reg;

	if (offset == 0) {
		if (likely(size == 4))
			return ctx->config_addr;
		return ~0ULL;
	}

	if (likely(offset >= 4)) {
		if (unlikely(!(ctx->config_addr & 0x80000000)))
			return ~0ULL;

		bus_num = (ctx->config_addr >> 16) & 0xFF;
		devfn = (ctx->config_addr >> 8) & 0xFF;
		reg = (ctx->config_addr & 0xFC) + (offset - 4);

		if (unlikely(bus_num != 0))
			return ~0ULL;

		return pci_bus_read_config_locked(&ctx->bus, TO_PCI_DEVFN_RAW(devfn), reg, size);
	}

	return ~0ULL;
}

static void pio_bridge_write(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size)
{
	struct device *dev = region->dev;
	struct pio_bridge_ctx *ctx = dev->priv;
	uint8_t bus_num, devfn, reg;

	if (offset == 0) {
		if (likely(size == 4))
			ctx->config_addr = (uint32_t)val;
		return;
	}

	if (likely(offset >= 4)) {
		if (unlikely(!(ctx->config_addr & 0x80000000)))
			return;

		bus_num = (ctx->config_addr >> 16) & 0xFF;
		devfn = (ctx->config_addr >> 8) & 0xFF;
		reg = (ctx->config_addr & 0xFC) + (offset - 4);

		if (unlikely(bus_num != 0))
			return;

		pci_bus_write_config_locked(&ctx->bus, TO_PCI_DEVFN_RAW(devfn), reg, (uint32_t)val, size);
	}
}

static const struct device_ops pio_bridge_ops = {
	.read = pio_bridge_read,
	.write = pio_bridge_write,
};

static int pio_bridge_realize(struct device *dev, void *pdata)
{
	struct pio_bridge_pdata *plat = pdata;
	struct pio_bridge_ctx *ctx;
	int ret;
	int i;

	if (WARN_ON(!plat))
		return -VM_EINVAL;

	ctx = res_zalloc(&dev->resources, sizeof(*ctx));
	if (!ctx)
		return -VM_ENOMEM;

	/* Bind the interrupt routing closure to the abstract bus */
	ret = pci_bus_init_locked(&ctx->bus, dev, plat->mmio_base, plat->mmio_size, pio_bridge_set_irq_cb, ctx);
	if (ret < 0)
		return ret;
	ctx->config_addr = 0;

	ctx->host_bridge.devfn = TO_PCI_DEVFN(0, 0);
	ctx->host_bridge.owner = dev;
	/* Generic virtual host bridge (1b36:0008), class 0600. */
	ctx->host_bridge.config_space[0x00] = 0x36;
	ctx->host_bridge.config_space[0x01] = 0x1b;
	ctx->host_bridge.config_space[0x02] = 0x08;
	ctx->host_bridge.config_space[0x0b] = 0x06;
	ret = pci_device_register_locked(&ctx->bus, &ctx->host_bridge);
	if (ret < 0)
		return ret;

	for (i = 0; i < 4; i++)
		ctx->pirq[i] = plat->pirq[i];

	dev->ops = &pio_bridge_ops;
	dev->priv = ctx;

	struct io_region *region = io_map_register_region_locked(IO_PIO, plat->config_addr_port, 8, dev);
	if (IS_ERR(region))
		return PTR_ERR(region);

	if (plat->out_bus)
		*plat->out_bus = &ctx->bus;

	pr_info("pio pci host bridge online at ports 0x%x/0x%x\n", (unsigned int)GPA_VAL(plat->config_addr_port), (unsigned int)(GPA_VAL(plat->config_addr_port) + 4));
	return 0;
}

static const struct device_desc pio_bridge_desc = {
	.name = "pci-pio-bridge",
	.realize = pio_bridge_realize,
};

static void __attribute__((constructor)) register_pio_bridge_desc(void)
{
	device_register(&pio_bridge_desc);
}
