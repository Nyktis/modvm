/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/memory.h>
#include <modvm/core/vm.h>
#include <modvm/host/page.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/compiler.h>
#include <modvm/util/types.h>

#include "memory_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "memory: " fmt

/**
 * mem_space_init - initialize a guest physical memory space
 * @space: the memory space context to initialize
 *
 * Return: 0 on success, or a negative error code.
 */
int mem_space_init(struct mem_space *space)
{
	if (WARN_ON(!space))
		return -VM_EINVAL;

	INIT_LIST_HEAD(&space->regions);
	space->total_ram = 0;
	space->host_page_size = host_page_size();
	if (!space->host_page_size)
		return -VM_EIO;

	return 0;
}

/**
 * mem_region_is_overlap - check if two memory ranges overlap
 * @base1: start address of first region
 * @size1: length of first region
 * @base2: start address of second region
 * @size2: length of second region
 *
 * Return: true if overlapping, false otherwise.
 */
static bool mem_region_is_overlap(gpa_t base1, size_t size1, gpa_t base2, size_t size2)
{
	return GPA_CMP(base1, <, gpa_add(base2, size2)) && GPA_CMP(base2, <, gpa_add(base1, size1));
}

/**
 * vm_add_ram - allocate and map a guest physical memory region
 * @ctx: the VM being constructed
 * @gpa: the starting guest physical address
 * @size: capacity of the memory bank in bytes
 * @flags: memory permission and attribute flags
 *
 * Validates alignment and topological overlap before allocating OS-level
 * page-aligned anonymous memory and registering the region with the accelerator.
 *
 * Return: 0 on success, negative error code on conflict or exhaustion.
 */
int vm_add_ram(struct vm_ctx *ctx, gpa_t gpa, size_t size, uint32_t flags)
{
	struct mem_region *reg;
	struct mem_region *pos;
	int ret;

	if (!ctx || !size)
		return -VM_EINVAL;
	if (ctx->state != VM_BUILDING || atomic_load(&ctx->stop_requested))
		return -VM_EBUSY;
	struct mem_space *space = &ctx->mem_space;
	if (!space->host_page_size || !ctx->accel.desc)
		return -VM_EINVAL;
	if (!ctx->accel.desc->accel_ops->map_ram)
		return -VM_ENOTSUP;
	if (size > SIZE_MAX - space->total_ram)
		return -VM_EOVERFLOW;

	if (UINT64_MAX - GPA_VAL(gpa) < size) {
		pr_err("memory region 0x%llx + size 0x%zx wraps around address limit\n", (unsigned long long)GPA_VAL(gpa), size);
		return -VM_EOVERFLOW;
	}

	if (GPA_VAL(gpa) % space->host_page_size != 0 || size % space->host_page_size != 0) {
		pr_err("region (gpa 0x%llx, size 0x%zx) strictly requires %zu bytes alignment\n", (unsigned long long)GPA_VAL(gpa), size, space->host_page_size);
		return -VM_EINVAL;
	}

	list_for_each_entry(pos, &space->regions, node)
	{
		if (mem_region_is_overlap(gpa, size, pos->gpa, pos->size)) {
			pr_err("topology overlap detected at gpa 0x%llx\n", (unsigned long long)GPA_VAL(gpa));
			return -VM_EBUSY;
		}
	}

	struct io_region *io;
	list_for_each_entry(io, &ctx->io_map.mmio_regions, node)
	{
		if (mem_region_is_overlap(gpa, size, io->base, io->size))
			return -VM_EBUSY;
	}

	reg = calloc(1, sizeof(*reg));
	if (!reg)
		return -VM_ENOMEM;

	reg->hva = host_page_alloc(size);
	if (IS_ERR(reg->hva)) {
		ret = PTR_ERR(reg->hva);
		pr_err("failed to allocate host backing memory for gpa 0x%llx\n", (unsigned long long)GPA_VAL(gpa));
		free(reg);
		return ret;
	}

	/* Anonymous mappings start zeroed; leave pages untouched for demand allocation. */

	reg->gpa = gpa;
	reg->size = size;
	reg->flags = flags;

	ret = ctx->accel.desc->accel_ops->map_ram(&ctx->accel, reg);
	if (ret < 0) {
		/* Failed registration retains no native reference to the backing memory. */
		host_page_free(reg->hva, reg->size);
		free(reg);
		return ret;
	}

	list_add_tail(&reg->node, &space->regions);
	space->total_ram += size;

	pr_debug("mounted hardware ram: 0x%08llx - 0x%08llx (%zu MB)\n", (unsigned long long)GPA_VAL(gpa), (unsigned long long)(GPA_VAL(gpa) + size - 1), size / (1024 * 1024));

	return 0;
}

/* Keep region lookup private to the memory subsystem. */
static struct mem_region *find_region(struct mem_space *space, gpa_t gpa)
{
	if (!space)
		return NULL;
	struct mem_region *reg;
	uint64_t addr = GPA_VAL(gpa);
	list_for_each_entry(reg, &space->regions, node)
	{
		uint64_t base = GPA_VAL(reg->gpa);
		if (addr >= base && addr - base < reg->size)
			return reg;
	}
	return NULL;
}

/**
 * mem_space_destroy - release all guest physical memory mappings
 * @space: the memory space to destroy
 */
void mem_space_destroy(struct mem_space *space)
{
	struct mem_region *pos, *n;

	if (WARN_ON(!space))
		return;

	list_for_each_entry_safe(pos, n, &space->regions, node)
	{
		list_del(&pos->node);

		host_page_free(pos->hva, pos->size);
		free(pos);
	}

	space->total_ram = 0;
}

/* Resolve one contiguous, permission-checked chunk of a larger transfer. */
void *mem_map_chunk(struct mem_space *space, gpa_t gpa, size_t len, bool write, size_t *out_len)
{
	if (!out_len)
		return NULL;
	*out_len = 0;
	if (!len || len > UINT64_MAX - GPA_VAL(gpa))
		return NULL;
	struct mem_region *reg = find_region(space, gpa);
	if (!reg || (write && (reg->flags & MEM_READONLY)))
		return NULL;
	size_t offset = gpa_offset(gpa, reg->gpa);
	size_t available = reg->size - offset;
	*out_len = len < available ? len : available;
	return (uint8_t *)reg->hva + offset;
}

/* Resolve an entire span; never truncate a fixed-size guest structure. */
void *mem_map_range(struct mem_space *space, gpa_t gpa, size_t len, bool write)
{
	size_t available;
	void *hva = mem_map_chunk(space, gpa, len, write, &available);
	return available == len ? hva : NULL;
}
