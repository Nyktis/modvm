/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_IO_MAP_H
#define MODVM_CORE_IO_MAP_H

#include <stdbool.h>
#include <modvm/util/list.h>
#include <modvm/util/types.h>

struct host_mutex;
struct device;

/**
 * struct io_map - device mappings in the port and memory I/O spaces
 * @io_lock: borrowed VM lock serializing I/O, topology and event callbacks
 * @pio_regions: list of mapped port I/O regions
 * @mmio_regions: list of mapped memory-mapped I/O regions
 *
 * Separate from RAM mappings in mem_space and protocol buses such as PCI.
 */
struct io_map {
	/* Serializes emulated I/O, topology and event callbacks. */
	struct host_mutex *io_lock;
	struct list_head pio_regions;
	struct list_head mmio_regions;
};

/**
 * enum io_space - memory and port address spaces
 * @IO_PIO: port I/O space, relied upon by legacy x86 architectures
 * @IO_MMIO: memory-mapped I/O space used universally
 */
enum io_space {
	IO_PIO,
	IO_MMIO,
};

/**
 * struct io_region - device-owned range in a guest I/O address space
 * @node: linked list node for routing iterations
 * @dev: the peripheral device owning this region
 * @base: starting address in the selected I/O space
 * @size: size of the claimed region in bytes
 * @type: indicates whether this is port I/O or memory-mapped I/O
 * @enabled: whether accesses are dispatched; disabled regions still reserve their address range
 */
struct io_region {
	struct list_head node;
	struct device *dev;
	gpa_t base;
	uint64_t size;
	enum io_space type;
	bool enabled;
};

/* Mapping changes require the owning VM I/O lock, or exclusive
 * access during construction/teardown before/after all concurrent users. */
int io_map_relocate_region_locked(struct io_region *region, gpa_t new_base);
struct io_region *io_map_register_region_locked(enum io_space type, gpa_t base, uint64_t size, struct device *dev);
uint64_t io_map_read(struct io_map *io_map, enum io_space type, gpa_t addr, uint8_t size);
void io_map_write(struct io_map *io_map, enum io_space type, gpa_t addr, uint64_t val, uint8_t size);

#endif /* MODVM_CORE_IO_MAP_H */
