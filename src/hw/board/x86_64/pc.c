/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <modvm/core/board.h>
#include <modvm/core/vm.h>
#include <modvm/core/device.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/misc/debug_exit.h>
#include <modvm/core/loader.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/hw/char/serial.h>
#include <modvm/core/irq.h>
#include <modvm/hw/pci/pio_bridge.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_pci.h>
#include <modvm/hw/virtio/virtio_blk.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>
#include <modvm/util/types.h>

#include "mptable.h"

#undef pr_fmt
#define pr_fmt(fmt) "pc_board: " fmt

/*
 * Guest RAM below 3 GiB is split around the reserved 0x9f000-0xfffff range.
 * The 3-4 GiB hole is left for MMIO, including the APIC mappings.
 * RAM exceeding the low-memory capacity starts at 4 GiB.
 */
#define PC_LOW_RAM_MAX 0xC0000000ULL
#define PC_HIGH_RAM_BASE 0x100000000ULL

struct pc_irq_route {
	struct vm_ctx *vm;
	uint32_t gsi;
};

static const uint8_t pc_pirq_routing[4] = { 5, 9, 10, 11 };

static void pc_irq_handler(void *data, int level)
{
	struct pc_irq_route *route = data;
	int ret = accel_set_irq(&route->vm->accel, TO_GSI(route->gsi), level);
	if (ret < 0)
		vm_report_error(route->vm, ret);
}

static void pc_route_pci_irqs(struct pci_bus *bus)
{
	struct pci_device *pos;

	list_for_each_entry(pos, &bus->devices, node)
	{
		uint8_t pin;
		uint8_t slot;
		uint8_t irq_line;

		pin = pci_bus_read_config_locked(bus, pos->devfn, PCI_INTERRUPT_PIN, 1);

		if (pin > 0 && pin <= 4) {
			slot = PCI_SLOT(pos->devfn);
			irq_line = pc_pirq_routing[(slot + pin - 1) % 4];

			pci_bus_write_config_locked(bus, pos->devfn, PCI_INTERRUPT_LINE, irq_line, 1);

			pr_info("firmware routed devfn %u pin %u -> GSI %u\n", PCI_DEVFN_VAL(pos->devfn), pin, irq_line);
		}
	}
}

static int pc_add_virtio_blk(struct vm_ctx *ctx, struct pci_bus *pci_bus, struct block_backend *blk_backend)
{
	struct virtio_device *vdev_blk;
	struct device *vpci_dev;
	struct virtio_pci_pdata vpci_pdata;
	int ret;

	vpci_dev = device_alloc(ctx, "virtio-pci");
	if (!vpci_dev)
		return -VM_ENOMEM;
	vdev_blk = virtio_blk_create(ctx, blk_backend);
	if (!vdev_blk)
		return -VM_ENOMEM;

	vpci_pdata.pci_bus = pci_bus;
	vpci_pdata.vdev = vdev_blk;
	vpci_pdata.devfn = PCI_AUTO_DEVFN;
	vpci_pdata.bar0_base = PCI_AUTO_MMIO;
	vpci_pdata.interrupt_pin = 1;

	ret = device_realize(vpci_dev, &vpci_pdata);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int pc_add_virtio_net(struct vm_ctx *ctx, struct pci_bus *pci_bus, struct net_backend *net_backend)
{
	struct virtio_device *vdev_net;
	struct device *vpci_dev;
	struct virtio_pci_pdata vpci_pdata;
	int ret;

	vpci_dev = device_alloc(ctx, "virtio-pci");
	if (!vpci_dev)
		return -VM_ENOMEM;
	vdev_net = virtio_net_create(ctx, net_backend);
	if (!vdev_net)
		return -VM_ENOMEM;

	vpci_pdata.pci_bus = pci_bus;
	vpci_pdata.vdev = vdev_net;
	vpci_pdata.devfn = PCI_AUTO_DEVFN;
	vpci_pdata.bar0_base = PCI_AUTO_MMIO;
	vpci_pdata.interrupt_pin = 1;

	ret = device_realize(vpci_dev, &vpci_pdata);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * pc_init - assemble the legacy x86 personal computer topology
 * @ctx: the context instance being constructed
 *
 * Splits guest RAM around the PCI hole and wires up
 * standard legacy ISA/LPC components.
 *
 * Return: 0 upon successful assembly, or a negative error code.
 */
static int pc_init(struct vm_ctx *ctx)
{
	struct device *uart, *exit_dev, *pci_bridge;
	struct serial_pdata uart_pdata;
	struct debug_exit_pdata exit_pdata;
	struct pio_bridge_pdata bridge_pdata;
	struct pc_irq_route *route;
	struct pci_bus *pci_root_bus = NULL;
	uint64_t ram_size = ctx->config.ram_size;
	uint64_t low_ram;
	uint64_t high_ram = 0;
	int ret;
	int i;
	size_t d_idx;

	if (!ctx->config.nr_vcpus || ctx->config.nr_vcpus > PC_MAX_VCPUS)
		return -VM_EINVAL;

	if (ram_size >= PC_LOW_RAM_MAX) {
		low_ram = PC_LOW_RAM_MAX;
		high_ram = ram_size - PC_LOW_RAM_MAX;
	} else {
		low_ram = ram_size;
	}

	if (low_ram < 0x100000)
		return -VM_EINVAL;
	ret = vm_add_ram(ctx, TO_GPA(0), 0x9f000, MEM_EXEC);
	if (ret < 0)
		return ret;
	ret = vm_add_ram(ctx, TO_GPA(0x9f000), 0x61000, MEM_EXEC | MEM_RESERVED);
	if (ret < 0)
		return ret;
	ret = low_ram == 0x100000 ? 0 : vm_add_ram(ctx, TO_GPA(0x100000), low_ram - 0x100000, MEM_EXEC);
	if (ret < 0)
		return ret;

	if (high_ram > 0) {
		ret = vm_add_ram(ctx, TO_GPA(PC_HIGH_RAM_BASE), high_ram, MEM_EXEC);
		if (ret < 0)
			return ret;
	}

	ret = accel_setup_irqchip(&ctx->accel);
	if (ret < 0)
		return ret;

	uart = device_alloc(ctx, "uart-16550a");
	if (!uart)
		return -VM_ENOMEM;

	route = res_zalloc(&uart->resources, sizeof(*route));
	if (!route) {
		return -VM_ENOMEM;
	}
	route->vm = ctx;
	route->gsi = 4;

	uart_pdata.io_space = IO_PIO;
	uart_pdata.base = TO_GPA(0x3f8);
	uart_pdata.reg_shift = 0;
	uart_pdata.console = ctx->config.console;
	uart_pdata.io_ctx = ctx->io_ctx;
	uart_pdata.irq = irq_alloc(&uart->resources, pc_irq_handler, route);
	if (!uart_pdata.irq) {
		return -VM_ENOMEM;
	}

	ret = device_realize(uart, &uart_pdata);
	if (ret < 0) {
		return ret;
	}

	pci_bridge = device_alloc(ctx, "pci-pio-bridge");
	if (!pci_bridge)
		return -VM_ENOMEM;

	bridge_pdata.config_addr_port = TO_GPA(0xCF8);
	bridge_pdata.mmio_base = TO_GPA(PC_LOW_RAM_MAX);
	bridge_pdata.mmio_size = PC_HIGH_RAM_BASE - PC_LOW_RAM_MAX;
	bridge_pdata.out_bus = &pci_root_bus;

	for (i = 0; i < 4; i++) {
		route = res_zalloc(&pci_bridge->resources, sizeof(*route));
		if (!route) {
			return -VM_ENOMEM;
		}
		route->vm = ctx;
		route->gsi = pc_pirq_routing[i];
		bridge_pdata.pirq[i] = irq_alloc(&pci_bridge->resources, pc_irq_handler, route);
		if (!bridge_pdata.pirq[i]) {
			return -VM_ENOMEM;
		}
	}

	ret = device_realize(pci_bridge, &bridge_pdata);
	if (ret < 0) {
		return ret;
	}

	for (d_idx = 0; d_idx < ctx->config.nr_drives; d_idx++) {
		ret = pc_add_virtio_blk(ctx, pci_root_bus, ctx->config.drives[d_idx]);
		if (ret < 0) {
			pr_err("failed to mount virtio block device %zu\n", d_idx);
			return ret;
		}
	}

	for (d_idx = 0; d_idx < ctx->config.nr_nets; d_idx++) {
		ret = pc_add_virtio_net(ctx, pci_root_bus, ctx->config.nets[d_idx]);
		if (ret < 0) {
			pr_err("failed to mount virtio net device %zu\n", d_idx);
			return ret;
		}
	}

	if (pci_root_bus) {
		pc_route_pci_irqs(pci_root_bus);
		ret = pc_build_mptable(ctx, pci_root_bus);
		if (ret < 0)
			return ret;
	}

	exit_dev = device_alloc(ctx, "debug-exit");
	if (!exit_dev)
		return -VM_ENOMEM;

	exit_pdata.io_space = IO_PIO;
	exit_pdata.base = TO_GPA(0x500);

	ret = device_realize(exit_dev, &exit_pdata);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * pc_boot - invoke the selected loader for the PC board
 * @ctx: the context instance to boot
 *
 * Return: 0 on success, or a negative error code.
 */
static int pc_boot(struct vm_ctx *ctx)
{
	int ret;

	/* Delegate entirely to the pluggable loader framework */
	if (ctx->config.loader_name && ctx->config.loader_opts) {
		ret = loader_execute(ctx, ctx->config.loader_name, ctx->config.loader_opts);
		if (ret < 0) {
			pr_err("board boot failed during firmware handoff\n");
			return ret;
		}
	} else {
		pr_err("no loader or firmware specified, aborting boot\n");
		return -VM_ENOENT;
	}

	return 0;
}

static const struct board_ops pc_ops = {
	.init = pc_init,
	.boot = pc_boot,
};

static const struct board_desc pc_board = {
	.name = "pc",
	.desc = "Standard x86 Personal Computer",
	.ops = &pc_ops,
};

static void __attribute__((constructor)) pc_register(void)
{
	board_register(&pc_board);
}
