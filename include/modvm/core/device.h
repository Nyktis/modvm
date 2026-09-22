/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVICE_H
#define MODVM_CORE_DEVICE_H
#include <stdbool.h>

#include <modvm/util/list.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/types.h>

struct vm_ctx;
struct device;
struct device_desc;
struct io_region;

/**
 * struct device_ops - standardized callbacks for hardware emulation
 * @stop: quiesce callbacks/interrupts before resources are released; partial init safe
 * @read: handle a read; region identifies the mapping, offset is relative to it
 * @write: handle a write using the same stable region identity
 */
struct device_ops {
	void (*stop)(struct device *dev);
	uint64_t (*read)(struct io_region *region, uint64_t offset, uint8_t size);
	void (*write)(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size);
};

enum device_state { DEVICE_NEW, DEVICE_INITIALIZING, DEVICE_ACTIVE, DEVICE_FAILED, DEVICE_DESTROYING };

/* Allocation transfers ownership to ctx immediately, even before realization.
 * destroy permits early removal and consumes the pointer, including children. */
/**
 * struct device - virtual device instance
 * @node: linked list node to attach to the context subsystem
 * @resources: anchor for all dynamically allocated device resources
 * @ctx: the parent virtual machine context containing this device
 * @desc: description of the device implementation
 * @ops: pointer to the device implementation methods
 * @priv: device-specific private state
 * @state: core-managed lifecycle state, including partial realization
 * @parent: optional same-VM parent; children are destroyed before the parent
 */
struct device {
	struct list_head node;
	struct res_pool resources;

	struct vm_ctx *ctx;
	const struct device_desc *desc;
	const struct device_ops *ops;
	void *priv;
	enum device_state state;
	struct device *parent;
};

/**
 * struct device_desc - device implementation description
 * @name: name unique within the device registry
 * @realize: initialize an allocated device and register its resources
 */
struct device_desc {
	const char *name;
	int (*realize)(struct device *dev, void *pdata);
};

/* Device hooks run under the VM I/O lock. Nested topology operations must use
 * these held-lock entry points; ordinary entry points acquire the lock. */
struct device *device_alloc_locked(struct vm_ctx *ctx, const char *name);
int device_realize_locked(struct device *dev, void *pdata);
void device_destroy_locked(struct device *dev);

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void device_register(const struct device_desc *desc);
struct device *device_alloc(struct vm_ctx *ctx, const char *name);
int device_realize(struct device *dev, void *pdata);
void device_destroy(struct device *dev);

#endif /* MODVM_CORE_DEVICE_H */
