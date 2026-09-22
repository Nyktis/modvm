/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/core/device.h>
#include <modvm/util/err.h>
#include <modvm/errno.h>
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static uint64_t read_bar(struct io_region *region, uint64_t off, uint8_t size)
{
	struct device *dev = region->dev;
	struct pci_device *pci = dev->priv;
	(void)size;
	return 0xab00 + off + (pci && region == pci->bars[1].region ? 0x100 : 0);
}

static int level;
static void irq(void *data, struct pci_device *dev, int value)
{
	(void)data;
	(void)dev;
	level = value;
}

int main(void)
{
	log_init();
	struct vm_ctx vm = { 0 }, foreign = { 0 };
	INIT_LIST_HEAD(&vm.io_map.mmio_regions);
	INIT_LIST_HEAD(&vm.io_map.pio_regions);
	INIT_LIST_HEAD(&vm.mem_space.regions);
	res_pool_init(&vm.resources);
	CHECK(test_io_init(&vm) == 0);
	struct mem_region ram = { .gpa = TO_GPA(0x100000), .size = 4096 };
	list_add_tail(&ram.node, &vm.mem_space.regions);
	const struct device_ops ops = { .read = read_bar };
	struct device owner = { .ctx = &vm }, a = { .ctx = &vm, .ops = &ops }, b = { .ctx = &vm, .ops = &ops }, wrong = { .ctx = &foreign };
	res_pool_init(&a.resources);
	res_pool_init(&b.resources);
	struct pci_bus bus;
	CHECK(pci_bus_init_locked(&bus, &owner, TO_GPA(0x100000), 0x3000, irq, NULL) == 0);
	struct pci_device pa = { .owner = &a, .devfn = PCI_AUTO_DEVFN, .interrupt_pin = 1 };
	struct pci_device pb = { .owner = &b, .devfn = PCI_AUTO_DEVFN };
	a.priv = &pa;
	struct pci_device bad = { .owner = &wrong, .devfn = PCI_AUTO_DEVFN };
	CHECK(pci_device_register_locked(&bus, &bad) == -VM_EINVAL);
	CHECK(pci_device_register_locked(&bus, &pa) == 0);
	CHECK(pci_device_register_locked(&bus, &pa) == -VM_EBUSY);
	CHECK(pci_register_bar_locked(&pa, 0, 4096, PCI_AUTO_MMIO) == 0);
	struct io_region *handle = pa.bars[0].region;
	CHECK(GPA_VAL(handle->base) == 0x101000 && !handle->enabled);
	CHECK(pci_device_register_locked(&bus, &pb) == 0);
	CHECK(pci_register_bar_locked(&pb, 0, 4096, PCI_AUTO_MMIO) == 0);
	CHECK(GPA_VAL(pb.bars[0].region->base) == 0x102000);
	CHECK(pci_register_bar_locked(&pa, 1, 4096, PCI_AUTO_MMIO) == -VM_ENOSPC);
	CHECK(pci_register_bar_locked(&pa, 1, 3000, PCI_AUTO_MMIO) == -VM_EINVAL);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x101003), 1) == UINT64_MAX);
	pci_bus_write_config_locked(&bus, pa.devfn, 4, 2, 2);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x101003), 1) == 0xab03);
	pci_bus_write_config_locked(&bus, pa.devfn, 0x10, UINT32_MAX, 4);
	CHECK(pci_bus_read_config_locked(&bus, pa.devfn, 0x10, 4) == 0xfffff000);
	CHECK(pci_bus_read_config_locked(&bus, pa.devfn, 0x11, 1) == 0xf0);
	CHECK(pci_bus_read_config_locked(&bus, pa.devfn, 0x12, 2) == 0xffff);
	CHECK(pa.bars[0].region == handle && GPA_VAL(handle->base) == 0x101000);
	pci_bus_write_config_locked(&bus, pa.devfn, 0x10, 0x102000, 4);
	CHECK(GPA_VAL(handle->base) == 0x101000); /* Collision leaves original mapping. */
	pci_bus_write_config_locked(&bus, pa.devfn, 0x10, 0x100000, 4);
	CHECK(GPA_VAL(handle->base) == 0x101000); /* RAM conflict. */
	pci_device_set_irq_locked(&pa, 1);
	CHECK(level == 1);
	pci_bus_write_config_locked(&bus, pa.devfn, 5, 4, 1);
	CHECK(level == 0 && pa.irq_level);
	pci_bus_write_config_locked(&bus, pa.devfn, 5, 0, 1);
	CHECK(level == 1);
	pci_bus_write_config_locked(&bus, pa.devfn, 0x10, 0x200000, 4);
	CHECK(pa.bars[0].region == handle && GPA_VAL(handle->base) == 0x200000);
	CHECK(pci_register_bar_locked(&pa, 1, 4096, PCI_AUTO_MMIO) == 0);
	CHECK(GPA_VAL(pa.bars[1].region->base) == 0x101000); /* Relocation returned its slot. */
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x200003), 1) == 0xab03);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x101003), 1) == 0xac03);
	res_release_all(&b.resources);
	CHECK(!pb.bus && !pb.bars[0].region);
	CHECK(pci_register_bar_locked(&pa, 2, 4096, PCI_AUTO_MMIO) == 0);
	CHECK(GPA_VAL(pa.bars[2].region->base) == 0x102000); /* Removal returned its slot. */
	res_release_all(&a.resources);
	CHECK(!pa.bus && !pa.bars[0].region && !pa.bars[1].region && !pa.bars[2].region && level == 0 && list_empty(&bus.devices) && list_empty(&vm.io_map.mmio_regions));
	res_release_all(&vm.resources);
	log_destroy();
	puts("generic PCI BARs, decode, INTx masks, stable regions, collision checks and address reuse passed");
	return 0;
}
