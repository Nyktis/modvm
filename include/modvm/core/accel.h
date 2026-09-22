/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_ACCEL_H
#define MODVM_CORE_ACCEL_H

#include <modvm/errno.h>
#include <stdatomic.h>
#include <modvm/util/types.h>

struct mem_region;
struct accel;
struct vcpu_ops;

/**
 * struct accel_ops - hardware accelerator backend operations
 * @init: initialize the hardware acceleration context
 * @destroy: ends accelerator lifetime and frees private resources
 * @map_ram: optional RAM registration; returns 0 or negative project error
 * @setup_irqchip: synthesize the architectural interrupt controller
 * @set_irq: assert or deassert a specific hardware interrupt line
 */
/* init/destroy are mandatory. destroy handles partial init and releases all
 * native RAM references before returning. map_ram borrows the region and backing
 * until destroy; on failure it retains neither. Missing map_ram rejects RAM
 * creation with -VM_ENOTSUP. IRQ operations are also optional. */
struct accel_ops {
	int (*init)(struct accel *accel);
	void (*destroy)(struct accel *accel);
	int (*map_ram)(struct accel *accel, const struct mem_region *region);
	int (*setup_irqchip)(struct accel *accel);
	int (*set_irq)(struct accel *accel, gsi_t gsi, int level);
};

/**
 * struct accel_desc - acceleration backend description
 * @name: name unique within the accelerator registry (e.g., "kvm")
 * @accel_ops: pointer to the acceleration operations table
 * @vcpu_ops: virtual processor operations associated with this backend
 */
struct accel_desc {
	const char *name;
	const struct accel_ops *accel_ops;
	const struct vcpu_ops *vcpu_ops;
};

/**
 * struct accel - per-VM acceleration state
 * @desc: description of the selected acceleration backend
 * @io_map: guest device mappings for routing MMIO/PIO exits
 * @priv: opaque pointer to the underlying accelerator state
 * @stop_requested: borrowed VM stop flag; accelerator may read but never reset it
 */
struct accel {
	const struct accel_desc *desc;
	struct io_map *io_map;
	void *priv;
	const atomic_bool *stop_requested;
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void accel_register(const struct accel_desc *desc);
const struct accel_desc *accel_find(const char *name);

/* The owner registers destruction before init. Published private state remains
 * owned on failure and is released by destroy, not by the failed init path. */
int accel_init(struct accel *accel, const char *name, struct io_map *io_map, const atomic_bool *stop_requested);
int accel_setup_irqchip(struct accel *accel);
int accel_set_irq(struct accel *accel, gsi_t gsi, int level);
void accel_destroy(struct accel *accel);

#endif /* MODVM_CORE_ACCEL_H */
