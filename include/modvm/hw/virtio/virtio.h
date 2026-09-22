/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_H
#define MODVM_HW_VIRTIO_VIRTIO_H

#include <modvm/errno.h>
#include <modvm/core/device.h>
#include <modvm/core/irq.h>
#include <modvm/util/types.h>

#define VIRTIO_MMIO_MAGIC 0x74726976 /* "virt" */
#define VIRTIO_MMIO_VERSION_1 2 /* Virtio 1.0 (v2) */
#define VIRTIO_VENDOR_ID 0x554D4551 /* "QEMU" */

#define VIRTIO_MAX_VQS 8
#define VIRTIO_F_VERSION_1 (1ULL << 32)

enum virtio_status_bits {
	VIRTIO_STATUS_ACKNOWLEDGE = 1U << 0,
	VIRTIO_STATUS_DRIVER = 1U << 1,
	VIRTIO_STATUS_DRIVER_OK = 1U << 2,
	VIRTIO_STATUS_FEATURES_OK = 1U << 3,
	VIRTIO_STATUS_NEEDS_RESET = 1U << 6,
	VIRTIO_STATUS_FAILED = 1U << 7,
};

struct virtio_device;
struct virtqueue;

/**
 * struct virtio_device_ops - callbacks for VirtIO device models (e.g., block, network)
 * @realize: initialize device-specific state and queues
 * @stop: stop device activity; must tolerate partial realization
 * @reset: reset device-specific state on a guest reset request
 * @get_features: retrieve the feature bitmask supported by the device
 * @read_config: read from the device-specific configuration space
 * @write_config: write to the device-specific configuration space
 * @notify_queue: handle a guest kick (queue doorbell) for a specific virtqueue
 */
/* realize/get_features/notify_queue are mandatory. stop/reset and config access
 * are optional; absent config access reads zero and ignores writes. stop must
 * tolerate partial initialization. Only realize may add queues. */
struct virtio_device_ops {
	int (*realize)(struct virtio_device *vdev);
	void (*stop)(struct virtio_device *vdev);
	void (*reset)(struct virtio_device *vdev);
	uint64_t (*get_features)(struct virtio_device *vdev);
	uint64_t (*read_config)(struct virtio_device *vdev, uint64_t offset, uint8_t size);
	void (*write_config)(struct virtio_device *vdev, uint64_t offset, uint32_t val, uint8_t size);
	void (*notify_queue)(struct virtio_device *vdev, uint16_t queue_idx);
};

/**
 * struct virtio_transport_ops - callbacks supplied by a VirtIO transport
 * @notify_queue: report used-buffer activity through a queue interrupt
 * @notify_config: report configuration changes through a configuration interrupt
 */
/* Both callbacks are mandatory and validated when the transport adopts a device. */
struct virtio_transport_ops {
	void (*notify_queue)(void *transport_data);
	void (*notify_config)(void *transport_data);
};

enum virtio_device_state { VIRTIO_DEVICE_NEW, VIRTIO_DEVICE_INITIALIZING, VIRTIO_DEVICE_ACTIVE, VIRTIO_DEVICE_STOPPED };

/**
 * struct virtio_device - VirtIO device model state
 * @state: host object lifecycle, independent of the guest protocol status
 * @owner: resource pool owning frontend cleanup; transferred to the transport on adoption
 * @resources: frontend-private allocations released after queues are destroyed
 * @parent_dev: the underlying transport device (e.g., MMIO device)
 * @transport_ops: transport operations table
 * @transport_data: transport callback context
 * @mem: borrowed guest memory space used for descriptor and payload access
 * @device_ops: the device-specific operations table
 * @priv: device-specific private state
 * @device_id: standard Virtio subsystem identifier (e.g., 2 for Block)
 * @status: guest protocol status, including device-generated NEEDS_RESET
 * @driver_features: guest-selected features; immutable once FEATURES_OK is accepted
 * @vqs: array of managed virtqueues
 * @nr_vqs: number of allocated virtqueues; queue enablement is tracked separately
 */
struct virtio_device {
	enum virtio_device_state state;
	struct res_pool *owner;
	struct res_pool resources;
	struct device *parent_dev;

	const struct virtio_transport_ops *transport_ops;
	void *transport_data;
	struct mem_space *mem;

	const struct virtio_device_ops *device_ops;
	void *priv;
	uint32_t device_id;
	uint8_t status;
	uint64_t driver_features;

	struct virtqueue *vqs[VIRTIO_MAX_VQS];
	uint16_t nr_vqs;
};

/* Protocol state, readiness reads and transport callbacks require the owning
 * VM I/O lock, or exclusive access before execution/after all users stop. */
void virtio_device_set_features_locked(struct virtio_device *vdev, uint32_t selector, uint32_t value);
void virtio_device_set_status_locked(struct virtio_device *vdev, uint8_t status);

static inline bool virtio_device_ready_locked(const struct virtio_device *vdev)
{
	return vdev->state == VIRTIO_DEVICE_ACTIVE && (vdev->status & (VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_NEEDS_RESET | VIRTIO_STATUS_FAILED)) == VIRTIO_STATUS_DRIVER_OK;
}

static inline void virtio_device_fail_locked(struct virtio_device *vdev)
{
	vdev->status |= VIRTIO_STATUS_NEEDS_RESET;
	if ((vdev->status & VIRTIO_STATUS_DRIVER_OK))
		vdev->transport_ops->notify_config(vdev->transport_data);
}

/* The frontend borrows its backend, which must outlive it.
 * Allocation and early destruction require exclusive access to the VM owner
 * pool and the unattached frontend. They do not acquire the VM I/O lock.
 * The _locked lifecycle operations require the same scope as protocol state;
 * add_queue is valid only inside the realize callback. */
struct virtio_device *virtio_device_alloc(struct vm_ctx *ctx, uint32_t id, const struct virtio_device_ops *ops, size_t priv_size);
int virtio_device_realize_locked(struct virtio_device *vdev);
void virtio_device_stop_locked(struct virtio_device *vdev);
/* Return -VM_EBUSY after adoption; destroy the owning transport instead. */
int virtio_device_destroy(struct virtio_device *vdev);
int virtio_device_add_queue_locked(struct virtio_device *vdev, uint16_t size);
/* Transfers the VM-owned frontend to its transport without allocation. */
int virtio_device_adopt_locked(struct device *dev, struct virtio_device *vdev, const struct virtio_transport_ops *ops, void *data);

/**
 * struct virtio_mmio_pdata - platform routing data for a Virtio-MMIO transport
 * @base: the starting address in the MMIO address space
 * @irq: the pre-wired interrupt line to signal the processor
 * @vdev: VirtIO device model to attach to this transport
 */
struct virtio_mmio_pdata {
	gpa_t base;
	struct irq *irq;
	struct virtio_device *vdev;
};

#endif /* MODVM_HW_VIRTIO_VIRTIO_H */
