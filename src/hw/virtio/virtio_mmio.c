/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/util/err.h>
#include <modvm/errno.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/io_map.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/util/compiler.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/types.h>

#include "virtqueue.h"
#include "virtio_mmio_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_mmio: " fmt

struct virtio_mmio_ctx {
	struct virtio_device *vdev;
	struct irq *irq;

	uint32_t device_features_sel;
	uint32_t driver_features_sel;
	uint32_t queue_sel;
	uint32_t interrupt_status;

	/* Temporary state for Virtio 1.0 64-bit address assembly */
	uint32_t queue_desc_lo[VIRTIO_MAX_VQS];
	uint32_t queue_desc_hi[VIRTIO_MAX_VQS];
	uint32_t queue_avail_lo[VIRTIO_MAX_VQS];
	uint32_t queue_avail_hi[VIRTIO_MAX_VQS];
	uint32_t queue_used_lo[VIRTIO_MAX_VQS];
	uint32_t queue_used_hi[VIRTIO_MAX_VQS];
	uint32_t queue_num[VIRTIO_MAX_VQS];
	bool queue_ready[VIRTIO_MAX_VQS];
};

static void virtio_mmio_transport_notify_queue(void *transport_data)
{
	struct virtio_mmio_ctx *ctx = transport_data;

	ctx->interrupt_status |= VIRTIO_MMIO_INT_VRING;
	irq_set_level(ctx->irq, 1);
}

static void virtio_mmio_failed(void *data)
{
	struct virtio_mmio_ctx *ctx = data;
	ctx->interrupt_status |= 2;
	irq_set_level(ctx->irq, 1);
}

static const struct virtio_transport_ops virtio_mmio_transport = {
	.notify_queue = virtio_mmio_transport_notify_queue,
	.notify_config = virtio_mmio_failed,
};

static uint64_t virtio_mmio_read(struct io_region *region, uint64_t offset, uint8_t size)
{
	struct device *dev = region->dev;
	struct virtio_mmio_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint64_t features;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		if (likely(vdev->device_ops->read_config))
			return vdev->device_ops->read_config(vdev, offset - VIRTIO_MMIO_CONFIG, size);
		return 0;
	}

	if (size != 4 || offset % 4)
		return 0;
	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		return VIRTIO_MMIO_MAGIC;
	case VIRTIO_MMIO_VERSION:
		return VIRTIO_MMIO_VERSION_1;
	case VIRTIO_MMIO_DEVICE_ID:
		return vdev->device_id;
	case VIRTIO_MMIO_VENDOR_ID:
		return VIRTIO_VENDOR_ID;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		features = vdev->device_ops->get_features(vdev);
		/* Feature bit 32 (VIRTIO_F_VERSION_1) must be advertised for v1 */
		features |= VIRTIO_F_VERSION_1;
		if (ctx->device_features_sel == 0)
			return (uint32_t)(features & 0xFFFFFFFF);
		else if (ctx->device_features_sel == 1)
			return (uint32_t)((features >> 32) & 0xFFFFFFFF);
		return 0;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		return ctx->queue_sel < vdev->nr_vqs ? virtqueue_get_max_size(vdev->vqs[ctx->queue_sel]) : 0;
	case VIRTIO_MMIO_QUEUE_READY:
		if (ctx->queue_sel < vdev->nr_vqs)
			return ctx->queue_ready[ctx->queue_sel] ? 1 : 0;
		return 0;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		return ctx->interrupt_status;
	case VIRTIO_MMIO_STATUS:
		return vdev->status;
	default:
		return 0;
	}
}

static void virtio_mmio_write(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size)
{
	struct device *dev = region->dev;
	struct virtio_mmio_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint32_t q_sel = ctx->queue_sel;
	uint64_t desc_gpa, avail_gpa, used_gpa;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		if (likely(vdev->device_ops->write_config))
			vdev->device_ops->write_config(vdev, offset - VIRTIO_MMIO_CONFIG, val, size);
		return;
	}

	if (size != 4 || offset % 4)
		return;
	switch (offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		ctx->device_features_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		ctx->driver_features_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		virtio_device_set_features_locked(vdev, ctx->driver_features_sel, (uint32_t)val);
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		ctx->queue_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_num[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_desc_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_desc_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_avail_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_avail_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_used_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel])
			ctx->queue_used_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		if (q_sel < vdev->nr_vqs && val == 0) {
			virtqueue_disable(vdev->vqs[q_sel]);
			ctx->queue_ready[q_sel] = false;
		}
		if (q_sel < vdev->nr_vqs && !ctx->queue_ready[q_sel] && val == 1) {
			desc_gpa = ((uint64_t)ctx->queue_desc_hi[q_sel] << 32) | ctx->queue_desc_lo[q_sel];
			avail_gpa = ((uint64_t)ctx->queue_avail_hi[q_sel] << 32) | ctx->queue_avail_lo[q_sel];
			used_gpa = ((uint64_t)ctx->queue_used_hi[q_sel] << 32) | ctx->queue_used_lo[q_sel];

			if (ctx->queue_num[q_sel] > UINT16_MAX || virtqueue_set_size(vdev->vqs[q_sel], ctx->queue_num[q_sel]))
				break;
			if (virtqueue_set_addrs(vdev->vqs[q_sel], TO_GPA(desc_gpa), TO_GPA(avail_gpa), TO_GPA(used_gpa)) == 0)
				ctx->queue_ready[q_sel] = true;
		}
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		if (val < vdev->nr_vqs && ctx->queue_ready[val] && virtio_device_ready_locked(vdev))
			vdev->device_ops->notify_queue(vdev, (uint16_t)val);
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		ctx->interrupt_status &= ~val;
		if (ctx->interrupt_status == 0)
			irq_set_level(ctx->irq, 0);
		break;
	case VIRTIO_MMIO_STATUS:
		virtio_device_set_status_locked(vdev, val);
		if (!val) {
			struct irq *irq = ctx->irq;
			memset(ctx, 0, sizeof(*ctx));
			ctx->vdev = vdev;
			ctx->irq = irq;
			irq_set_level(irq, 0);
		}
		break;
	default:
		break;
	}
}

static void virtio_mmio_stop(struct device *dev)
{
	struct virtio_mmio_ctx *ctx = dev->priv;
	virtio_device_stop_locked(ctx->vdev);
	irq_set_level(ctx->irq, 0);
}

static const struct device_ops virtio_mmio_ops = {
	.stop = virtio_mmio_stop,
	.read = virtio_mmio_read,
	.write = virtio_mmio_write,
};

static int virtio_mmio_realize(struct device *dev, void *pdata)
{
	struct virtio_mmio_pdata *plat = pdata;
	struct virtio_mmio_ctx *ctx;
	struct virtio_device *vdev;
	int ret;

	if (WARN_ON(!plat || !plat->vdev || !plat->irq))
		return -VM_EINVAL;

	ctx = res_zalloc(&dev->resources, sizeof(*ctx));
	if (!ctx)
		return -VM_ENOMEM;

	ret = virtio_device_adopt_locked(dev, plat->vdev, &virtio_mmio_transport, ctx);
	if (ret < 0)
		return ret;
	vdev = plat->vdev;

	ctx->vdev = vdev;
	ctx->irq = plat->irq;

	dev->ops = &virtio_mmio_ops;
	dev->priv = ctx;

	/* Realize the VirtIO device model and its queues. */
	ret = virtio_device_realize_locked(vdev);
	if (ret < 0)
		return ret;

	/* 0x100 bytes of transport registers followed by 0x100 bytes of device configuration. */
	struct io_region *region = io_map_register_region_locked(IO_MMIO, plat->base, 0x200, dev);
	if (IS_ERR(region))
		return PTR_ERR(region);

	pr_info("virtio-mmio transport mapped at 0x%08llx for device %u\n", (unsigned long long)GPA_VAL(plat->base), vdev->device_id);
	return 0;
}

static const struct device_desc virtio_mmio_desc = {
	.name = "virtio-mmio",
	.realize = virtio_mmio_realize,
};

static void __attribute__((constructor)) register_virtio_mmio_desc(void)
{
	device_register(&virtio_mmio_desc);
}
