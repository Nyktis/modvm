/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MEMORY_H
#define MODVM_CORE_MEMORY_H
#include <stdbool.h>

#include <modvm/util/list.h>
#include <modvm/util/types.h>

#define MEM_READONLY (1U << 0)
#define MEM_EXEC (1U << 1)
/* Mapped RAM retained for firmware/platform use, excluded from guest allocators. */
#define MEM_RESERVED (1U << 2)

struct mem_space;
struct mem_region;

/**
 * struct mem_region - contiguous block of guest physical memory
 * @node: linked list node for memory space iterations
 * @gpa: guest physical address
 * @size: size of the memory block in bytes
 * @hva: host virtual address backing this region
 * @flags: access permissions and memory traits
 */
struct mem_region {
	struct list_head node;
	gpa_t gpa;
	size_t size;
	void *hva;
	uint32_t flags;
};

/**
 * struct mem_space - guest physical memory space
 * @regions: list of registered memory regions
 * @total_ram: sum of registered region sizes in bytes, including reserved regions
 * @host_page_size: native page size of the underlying operating system
 */
struct mem_space {
	struct list_head regions;
	size_t total_ram;
	size_t host_page_size;
};

/* RAM topology is frozen after VM construction. Returned mappings are borrowed
 * until VM destruction; all users and the accelerator stop before RAM is freed. */
void *mem_map_range(struct mem_space *space, gpa_t gpa, size_t len, bool write);
/* Maps up to the region boundary; failure sets out_len to zero. */
void *mem_map_chunk(struct mem_space *space, gpa_t gpa, size_t len, bool write, size_t *out_len);

#endif /* MODVM_CORE_MEMORY_H */
