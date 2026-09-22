/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/core/device.h>
#include <modvm/util/err.h>
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include <stdlib.h>

#include <modvm/core/io_map.h>
#include <modvm/util/res_pool.h>
#include <modvm/core/vm.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include <modvm/util/compiler.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "io_map: " fmt

static void io_region_unregister(void *data)
{
	struct io_region *reg = data;
	list_del(&reg->node);
	pr_debug("automatically unregistered I/O region at 0x%llx\n", (unsigned long long)GPA_VAL(reg->base));
}

/**
 * io_map_register_region_locked - map a device onto the system address space
 * @type: indicates whether this is PIO or MMIO
 * @base: absolute starting address on the guest I/O address space
 * @size: size of the claimed region in bytes
 * @dev: the peripheral device owning this region
 *
 * Scans the specific topology for overlapping regions to prevent hardware
 * resource collisions. The mapping is strictly tied to the device lifecycle.
 *
 * Return: a device-owned stable region handle, or ERR_PTR(-VM_E*).
 */
struct io_region *io_map_register_region_locked(enum io_space type, gpa_t base, uint64_t size, struct device *dev)
{
	struct vm_ctx *ctx;
	struct io_region *reg;
	struct io_region *pos;
	struct list_head *list;
	int ret;

	if (WARN_ON(!dev || !dev->ctx || !dev->ops || size == 0 || (type != IO_PIO && type != IO_MMIO)))
		return ERR_PTR(-VM_EINVAL);

	if (GPA_VAL(base) > UINT64_MAX - size)
		return ERR_PTR(-VM_EOVERFLOW);
	ctx = dev->ctx;
	list = (type == IO_PIO) ? &ctx->io_map.pio_regions : &ctx->io_map.mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (GPA_CMP(base, <, gpa_add(pos->base, pos->size)) && GPA_CMP(gpa_add(base, size), >, pos->base)) {
			pr_err("I/O mapping conflict detected at base 0x%llx\n", (unsigned long long)GPA_VAL(base));
			return ERR_PTR(-VM_EBUSY);
		}
	}

	if (type == IO_MMIO) {
		struct mem_region *ram;
		list_for_each_entry(ram, &ctx->mem_space.regions, node)
		{
			if (GPA_VAL(base) < GPA_VAL(ram->gpa) + ram->size && GPA_VAL(ram->gpa) < GPA_VAL(base) + size)
				return ERR_PTR(-VM_EBUSY);
		}
	}

	reg = res_zalloc(&dev->resources, sizeof(*reg));
	if (!reg)
		return ERR_PTR(-VM_ENOMEM);

	reg->dev = dev;
	reg->base = base;
	reg->size = size;
	reg->type = type;
	reg->enabled = true;

	list_add_tail(&reg->node, list);

	ret = res_add_action(&dev->resources, io_region_unregister, reg);
	if (ret < 0) {
		list_del(&reg->node);
		return ERR_PTR(ret);
	}

	pr_debug("registered '%s' to space %d at 0x%llx\n", dev->desc ? dev->desc->name : "unknown", type, (unsigned long long)GPA_VAL(base));
	return reg;
}

/**
 * io_map_read_locked - route a read operation to the owning peripheral
 * @io_map: the address space topology
 * @type: the target address space
 * @addr: the absolute requested address
 * @size: the size of the read request in bytes
 *
 * Return: the value supplied by the device, or ~0ULL if unmapped/out-of-bounds.
 */
static uint64_t io_map_read_locked(struct io_map *io_map, enum io_space type, gpa_t addr, uint8_t size)
{
	struct io_region *pos;
	struct list_head *list;

	if (WARN_ON(!io_map))
		return ~0ULL;

	list = (type == IO_PIO) ? &io_map->pio_regions : &io_map->mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (pos->enabled && GPA_CMP(addr, >=, pos->base) && GPA_CMP(addr, <, gpa_add(pos->base, pos->size))) {
			uint64_t offset = gpa_offset(addr, pos->base);

			if (unlikely(size > pos->size - offset)) {
				pr_warn("cross-boundary read intercepted at offset 0x%llx\n", (unsigned long long)offset);
				return ~0ULL;
			}

			if (likely(pos->dev->ops->read))
				return pos->dev->ops->read(pos, offset, size);

			return ~0ULL;
		}
	}

	/* Floating bus paradigm: unmapped electrical lines return high */
	return ~0ULL;
}

/**
 * io_map_write_locked - route a write operation to the owning peripheral
 * @io_map: the address space topology
 * @type: the target address space
 * @addr: the absolute requested address
 * @val: the payload to write
 * @size: the size of the write request in bytes
 */
static void io_map_write_locked(struct io_map *io_map, enum io_space type, gpa_t addr, uint64_t val, uint8_t size)
{
	struct io_region *pos;
	struct list_head *list;

	if (WARN_ON(!io_map))
		return;

	list = (type == IO_PIO) ? &io_map->pio_regions : &io_map->mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (pos->enabled && GPA_CMP(addr, >=, pos->base) && GPA_CMP(addr, <, gpa_add(pos->base, pos->size))) {
			uint64_t offset = gpa_offset(addr, pos->base);

			if (unlikely(size > pos->size - offset)) {
				pr_warn("cross-boundary write intercepted at offset 0x%llx\n", (unsigned long long)offset);
				return;
			}

			if (likely(pos->dev->ops->write))
				pos->dev->ops->write(pos, offset, val, size);

			return;
		}
	}
}

uint64_t io_map_read(struct io_map *io_map, enum io_space type, gpa_t addr, uint8_t size)
{
	if (!io_map || (type != IO_PIO && type != IO_MMIO) || (size != 1 && size != 2 && size != 4 && size != 8))
		return ~0ULL;
	host_mutex_lock(io_map->io_lock);
	uint64_t value = io_map_read_locked(io_map, type, addr, size);
	host_mutex_unlock(io_map->io_lock);
	return value;
}

void io_map_write(struct io_map *io_map, enum io_space type, gpa_t addr, uint64_t val, uint8_t size)
{
	if (!io_map || (type != IO_PIO && type != IO_MMIO) || (size != 1 && size != 2 && size != 4 && size != 8))
		return;
	host_mutex_lock(io_map->io_lock);
	io_map_write_locked(io_map, type, addr, val, size);
	host_mutex_unlock(io_map->io_lock);
}

/* Caller holds the VM I/O lock during mapping changes. */
int io_map_relocate_region_locked(struct io_region *target, gpa_t new_base)
{
	if (!target || !target->dev)
		return -VM_EINVAL;
	struct vm_ctx *ctx = target->dev->ctx;
	uint64_t base = GPA_VAL(new_base);
	if (base > UINT64_MAX - target->size)
		return -VM_EOVERFLOW;
	struct list_head *head = target->type == IO_PIO ? &ctx->io_map.pio_regions : &ctx->io_map.mmio_regions;
	struct io_region *reg;
	list_for_each_entry(reg, head, node)
	{
		if (reg != target && base < GPA_VAL(reg->base) + reg->size && GPA_VAL(reg->base) < base + target->size)
			return -VM_EBUSY;
	}
	if (target->type == IO_MMIO) {
		struct mem_region *ram;
		list_for_each_entry(ram, &ctx->mem_space.regions, node)
		{
			if (base < GPA_VAL(ram->gpa) + ram->size && GPA_VAL(ram->gpa) < base + target->size)
				return -VM_EBUSY;
		}
	}
	target->base = new_base;
	return 0;
}
