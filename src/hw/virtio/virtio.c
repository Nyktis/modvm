/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/util/res_pool.h>
#include <modvm/core/vm.h>
#include "virtqueue.h"

static void virtio_cleanup(void *data);

struct virtio_device *virtio_device_alloc(struct vm_ctx *ctx, uint32_t id, const struct virtio_device_ops *ops, size_t size)
{
	if (!ctx || !ops || !ops->realize || !ops->get_features || !ops->notify_queue)
		return NULL;
	struct virtio_device *vdev = calloc(1, sizeof(*vdev));
	if (!vdev)
		return NULL;
	res_pool_init(&vdev->resources);
	vdev->priv = res_zalloc(&vdev->resources, size);
	if (!vdev->priv) {
		free(vdev);
		return NULL;
	}
	vdev->device_id = id;
	vdev->device_ops = ops;
	vdev->owner = &ctx->resources;
	vdev->mem = &ctx->mem_space;
	if (res_add_action_or_reset(vdev->owner, virtio_cleanup, vdev))
		return NULL;
	return vdev;
}

int virtio_device_add_queue_locked(struct virtio_device *vdev, uint16_t size)
{
	if (!vdev || vdev->state != VIRTIO_DEVICE_INITIALIZING || !vdev->mem || vdev->nr_vqs == VIRTIO_MAX_VQS)
		return -VM_EINVAL;
	struct virtqueue *vq = virtqueue_create(vdev->mem, size);
	if (!vq)
		return -VM_ENOMEM;
	vdev->vqs[vdev->nr_vqs++] = vq;
	return 0;
}

int virtio_device_realize_locked(struct virtio_device *vdev)
{
	if (!vdev || vdev->state != VIRTIO_DEVICE_NEW || !vdev->parent_dev)
		return -VM_EINVAL;
	vdev->state = VIRTIO_DEVICE_INITIALIZING;
	int ret = vdev->device_ops->realize(vdev);
	if (ret < 0)
		virtio_device_stop_locked(vdev);
	else
		vdev->state = VIRTIO_DEVICE_ACTIVE;
	return ret;
}

void virtio_device_stop_locked(struct virtio_device *vdev)
{
	if (!vdev || (vdev->state != VIRTIO_DEVICE_ACTIVE && vdev->state != VIRTIO_DEVICE_INITIALIZING))
		return;
	vdev->state = VIRTIO_DEVICE_STOPPED;
	if (vdev->device_ops->stop)
		vdev->device_ops->stop(vdev);
}

static void virtio_cleanup(void *data)
{
	struct virtio_device *vdev = data;
	virtio_device_stop_locked(vdev);
	for (unsigned i = 0; i < vdev->nr_vqs; i++)
		virtqueue_destroy(vdev->vqs[i]);
	res_release_all(&vdev->resources);
	free(vdev);
}

int virtio_device_destroy(struct virtio_device *vdev)
{
	if (!vdev)
		return 0;
	if (vdev->parent_dev)
		return -VM_EBUSY;
	int ret = res_cancel_action(vdev->owner, virtio_cleanup, vdev);
	if (ret < 0)
		return ret;
	virtio_cleanup(vdev);
	return 0;
}

int virtio_device_adopt_locked(struct device *dev, struct virtio_device *vdev, const struct virtio_transport_ops *ops, void *data)
{
	if (!dev || !dev->ctx || !vdev || !ops || !ops->notify_queue || !ops->notify_config || vdev->parent_dev || vdev->state != VIRTIO_DEVICE_NEW ||
	    vdev->owner != &dev->ctx->resources)
		return -VM_EINVAL;
	int ret = res_transfer_action(vdev->owner, &dev->resources, virtio_cleanup, vdev);
	if (!ret) {
		vdev->owner = &dev->resources;
		vdev->parent_dev = dev;
		vdev->transport_ops = ops;
		vdev->transport_data = data;
	}
	return ret;
}

void virtio_device_set_status_locked(struct virtio_device *vdev, uint8_t status)
{
	if (!status) {
		vdev->status = 0;
		vdev->driver_features = 0;
		for (unsigned i = 0; i < vdev->nr_vqs; i++)
			virtqueue_reset(vdev->vqs[i]);
		if (vdev->device_ops->reset)
			vdev->device_ops->reset(vdev);
		return;
	}
	/* Status bits accumulate until reset; NEEDS_RESET belongs to the device. */
	status = (status & (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_FAILED)) | vdev->status;
	if (!(status & VIRTIO_STATUS_ACKNOWLEDGE))
		status &= ~(VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
	if (!(status & VIRTIO_STATUS_DRIVER))
		status &= ~(VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
	if ((status & VIRTIO_STATUS_FEATURES_OK) && !(vdev->status & VIRTIO_STATUS_FEATURES_OK)) {
		uint64_t offered = vdev->device_ops->get_features(vdev) | VIRTIO_F_VERSION_1;
		if (!(vdev->driver_features & VIRTIO_F_VERSION_1) || (vdev->driver_features & ~offered))
			status &= ~VIRTIO_STATUS_FEATURES_OK;
	}
	if (!(status & VIRTIO_STATUS_FEATURES_OK))
		status &= ~VIRTIO_STATUS_DRIVER_OK;
	vdev->status = status;
}

void virtio_device_set_features_locked(struct virtio_device *vdev, uint32_t selector, uint32_t value)
{
	if (vdev->state != VIRTIO_DEVICE_ACTIVE || (vdev->status & VIRTIO_STATUS_FEATURES_OK) || selector > 1)
		return;
	unsigned shift = selector * 32;
	vdev->driver_features = (vdev->driver_features & ~((uint64_t)UINT32_MAX << shift)) | ((uint64_t)value << shift);
}
