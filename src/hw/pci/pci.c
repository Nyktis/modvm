/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/core/device.h>
#include <modvm/util/err.h>
#include <modvm/errno.h>
#include <modvm/core/vm.h>

#include <modvm/hw/pci/pci.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include <modvm/util/compiler.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "pci: " fmt

/**
 * pci_bus_init_locked - initialize a virtual PCI bus topology
 * @bus: the PCI bus instance to initialize
 * @owner: host bridge device owning this bus and its synchronization domain
 * @mmio_base: starting physical address for dynamic MMIO window allocations
 * @mmio_size: size of the automatic MMIO allocation window
 * @set_irq_cb: host bridge hook for interrupt routing
 * @irq_data: host bridge closure data
 */
int pci_bus_init_locked(struct pci_bus *bus, struct device *owner, gpa_t mmio_base, uint64_t mmio_size, pci_set_irq_cb_t set_irq_cb, void *irq_data)
{
	if (!bus || !owner || !owner->ctx || GPA_VAL(mmio_base) > (1ULL << 32) || mmio_size > (1ULL << 32) - GPA_VAL(mmio_base))
		return -VM_EINVAL;

	INIT_LIST_HEAD(&bus->devices);
	bus->set_irq_cb = set_irq_cb;
	bus->irq_data = irq_data;

	bus->owner = owner;
	bus->mmio_base = mmio_base;
	bus->mmio_limit = gpa_add(mmio_base, mmio_size);
	return 0;
}

/* Address reservations are the registered regions themselves; removal and
 * relocation therefore return space to the window without a second allocator. */
int pci_register_bar_locked(struct pci_device *dev, unsigned index, uint32_t size, gpa_t requested_base)
{
	if (!dev || !dev->bus || !dev->owner || index >= 6 || size < 16 || (size & (size - 1)))
		return -VM_EINVAL;
	if (dev->bars[index].region)
		return -VM_EEXIST;
	struct vm_ctx *ctx = dev->owner->ctx;
	uint64_t base = GPA_VAL(requested_base);
	if (!GPA_IS_VALID(requested_base)) {
		base = GPA_VAL(dev->bus->mmio_base);
		for (;;) {
			if (base > UINT32_MAX - (uint64_t)(size - 1))
				return -VM_ENOSPC;
			base = (base + size - 1) & ~(uint64_t)(size - 1);
			if (base + size > GPA_VAL(dev->bus->mmio_limit) || base + size > (1ULL << 32))
				return -VM_ENOSPC;
			uint64_t next = base;
			struct io_region *reg;
			list_for_each_entry(reg, &ctx->io_map.mmio_regions, node)
			{
				if (base < GPA_VAL(reg->base) + reg->size && GPA_VAL(reg->base) < base + size)
					next = GPA_VAL(reg->base) + reg->size;
			}
			struct mem_region *ram;
			list_for_each_entry(ram, &ctx->mem_space.regions, node)
			{
				if (base < GPA_VAL(ram->gpa) + ram->size && GPA_VAL(ram->gpa) < base + size && GPA_VAL(ram->gpa) + ram->size > next)
					next = GPA_VAL(ram->gpa) + ram->size;
			}
			if (next == base)
				break;
			base = next;
		}
	}
	if (base % size || base > (1ULL << 32) - size)
		return -VM_EINVAL;
	struct io_region *region = io_map_register_region_locked(IO_MMIO, TO_GPA(base), size, dev->owner);
	if (IS_ERR(region))
		return PTR_ERR(region);
	dev->bars[index].region = region;
	region->enabled = (dev->config_space[4] & 2) != 0;
	for (unsigned i = 0; i < 4; i++)
		dev->config_space[0x10 + index * 4 + i] = base >> (i * 8);
	return 0;
}

static void pci_unregister(void *data)
{
	struct pci_device *dev = data;
	pci_device_set_irq_locked(dev, 0);
	list_del(&dev->node);
	dev->bus = NULL;
	for (unsigned i = 0; i < 6; i++) {
		dev->bars[i].region = NULL;
		dev->bars[i].probe = false;
	}
}

/**
 * pci_device_register_locked - attach a PCI endpoint to the abstract bus
 * @bus: the target PCI bus
 * @pci_dev: the endpoint device to attach
 *
 * Sets the interrupt-pin field and assigns a devfn if requested. Registers
 * endpoint removal with the owning device resource pool.
 *
 * Return: 0 on success, or a negative error code.
 */
int pci_device_register_locked(struct pci_bus *bus, struct pci_device *pci_dev)
{
	struct pci_device *pos;

	if (!bus || !pci_dev || !pci_dev->owner || !bus->owner || pci_dev->owner->ctx != bus->owner->ctx || pci_dev->interrupt_pin > 4)
		return -VM_EINVAL;

	if (pci_dev->bus)
		return -VM_EBUSY;

	if (PCI_DEVFN_CMP(pci_dev->devfn, ==, PCI_AUTO_DEVFN)) {
		unsigned slot;
		for (slot = 1; slot < 32; slot++) {
			bool occupied = false;
			list_for_each_entry(pos, &bus->devices, node)
			{
				if (PCI_SLOT(pos->devfn) == slot)
					occupied = true;
			}
			if (!occupied)
				break;
		}
		if (slot == 32)
			return -VM_ENOSPC;
		pci_dev->devfn = TO_PCI_DEVFN(slot, 0);
	}

	list_for_each_entry(pos, &bus->devices, node)
	{
		if (PCI_DEVFN_CMP(pos->devfn, ==, pci_dev->devfn)) {
			pr_err("PCI slot collision detected at devfn %u\n", PCI_DEVFN_VAL(pci_dev->devfn));
			return -VM_EBUSY;
		}
	}

	pci_dev->bus = bus;

	pci_dev->config_space[PCI_INTERRUPT_PIN] = pci_dev->interrupt_pin;
	pci_dev->config_space[PCI_INTERRUPT_LINE] = 0;

	int ret = res_add_action(&pci_dev->owner->resources, pci_unregister, pci_dev);
	if (ret < 0) {
		pci_dev->bus = NULL;
		return ret;
	}
	if (pci_dev->owner != bus->owner)
		pci_dev->owner->parent = bus->owner;
	list_add_tail(&pci_dev->node, &bus->devices);
	pr_info("registered PCI device at devfn %u (pin %u)\n", PCI_DEVFN_VAL(pci_dev->devfn), pci_dev->interrupt_pin);

	return 0;
}

static struct pci_device *pci_bus_find_device(struct pci_bus *bus, pci_devfn_t devfn)
{
	struct pci_device *pos;

	list_for_each_entry(pos, &bus->devices, node)
	{
		if (PCI_DEVFN_CMP(pos->devfn, ==, devfn))
			return pos;
	}
	return NULL;
}

/**
 * pci_bus_read_config_locked - route a configuration space read to an endpoint
 * @bus: the PCI bus containing the target device
 * @devfn: the target device and function number
 * @offset: byte offset within the 256-byte configuration space
 * @size: size of the read request
 *
 * Return: the requested register value, or ~0U if unmapped.
 */
uint32_t pci_bus_read_config_locked(struct pci_bus *bus, pci_devfn_t devfn, uint8_t offset, uint8_t size)
{
	struct pci_device *pci_dev;

	if (!bus || (size != 1 && size != 2 && size != 4) || offset % size || offset + size > PCI_CONFIG_SPACE_SIZE)
		return ~0U;

	pci_dev = pci_bus_find_device(bus, devfn);
	if (unlikely(!pci_dev))
		return ~0U;

	if (offset >= 0x40 && pci_dev->ops && pci_dev->ops->read_config)
		return pci_dev->ops->read_config(pci_dev, offset, size);

	uint32_t value = 0;
	for (unsigned i = 0; i < size; i++) {
		unsigned off = offset + i;
		uint8_t byte = pci_dev->config_space[off];
		if (off >= 0x10 && off < 0x28) {
			unsigned bar = (off - 0x10) / 4;
			if (pci_dev->bars[bar].region && pci_dev->bars[bar].probe)
				byte = (~(uint32_t)(pci_dev->bars[bar].region->size - 1)) >> ((off % 4) * 8);
		}
		value |= (uint32_t)byte << (i * 8);
	}
	return value;
}

/**
 * pci_bus_write_config_locked - route a configuration space write to an endpoint
 * @bus: the PCI bus containing the target device
 * @devfn: the target device and function number
 * @offset: byte offset within the 256-byte configuration space
 * @val: the payload to write
 * @size: size of the write request
 */
void pci_bus_write_config_locked(struct pci_bus *bus, pci_devfn_t devfn, uint8_t offset, uint32_t val, uint8_t size)
{
	struct pci_device *pci_dev;

	if (!bus || (size != 1 && size != 2 && size != 4) || offset % size || offset + size > PCI_CONFIG_SPACE_SIZE)
		return;

	pci_dev = pci_bus_find_device(bus, devfn);
	if (unlikely(!pci_dev))
		return;

	if (offset >= 0x40) {
		if (pci_dev->ops && pci_dev->ops->write_config)
			pci_dev->ops->write_config(pci_dev, offset, val, size);
		return;
	}
	if (offset >= 0x10 && offset < 0x28) {
		unsigned bar = (offset - 0x10) / 4;
		struct io_region *region = pci_dev->bars[bar].region;
		if (!region)
			return;
		if (size == 4 && val == UINT32_MAX) {
			pci_dev->bars[bar].probe = true;
			return;
		}
		pci_dev->bars[bar].probe = false;
		unsigned shift = (offset % 4) * 8;
		uint32_t mask = UINT32_MAX >> (32 - size * 8);
		uint32_t base = (GPA_VAL(region->base) & ~(mask << shift)) | ((val & mask) << shift);
		base &= ~(uint32_t)(region->size - 1);
		if (io_map_relocate_region_locked(region, TO_GPA(base)))
			return;
		for (unsigned i = 0; i < 4; i++)
			pci_dev->config_space[0x10 + bar * 4 + i] = base >> (i * 8);
		return;
	}
	for (unsigned i = 0; i < size; i++) {
		unsigned off = offset + i;
		uint8_t byte = val >> (i * 8);
		if (off == 4)
			pci_dev->config_space[off] = byte & 6;
		if (off == 5)
			pci_dev->config_space[off] = byte & 4;
		if (off == PCI_INTERRUPT_LINE)
			pci_dev->config_space[off] = byte;
	}
	if (offset < 6 && offset + size > 4) {
		for (unsigned i = 0; i < 6; i++) {
			if (pci_dev->bars[i].region)
				pci_dev->bars[i].region->enabled = (pci_dev->config_space[4] & 2) != 0;
		}
		pci_device_set_irq_locked(pci_dev, pci_dev->irq_level);
	}
}

/**
 * pci_device_set_irq_locked - delegate interrupt assertion to the parent bus
 * @pci_dev: the endpoint device triggering the interrupt
 * @level: interrupt level (0 deasserted, 1 asserted)
 *
 * Records the endpoint assertion and applies INTx masking before forwarding
 * the level to the host bridge.
 */
void pci_device_set_irq_locked(struct pci_device *pci_dev, int level)
{
	struct pci_bus *bus;

	if (unlikely(!pci_dev || !pci_dev->bus))
		return;

	bus = pci_dev->bus;
	pci_dev->irq_level = level != 0;
	if (pci_dev->config_space[5] & 4)
		level = 0;

	if (likely(bus->set_irq_cb))
		bus->set_irq_cb(bus->irq_data, pci_dev, level);
}
