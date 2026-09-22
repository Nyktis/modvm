/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/io_map.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_pci.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/compiler.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/types.h>

#include "virtqueue.h"
#include "virtio_pci_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_pci: " fmt

struct virtio_pci_queue_cfg {
	le16_t size, msix_vector, enable, notify_off;
	le32_t desc_lo, desc_hi, avail_lo, avail_hi, used_lo, used_hi;
};

struct virtio_pci_global_cfg {
	le32_t device_feature_select, device_feature;
	le32_t guest_feature_select, reserved_driver_features;
	le16_t msix_config, num_queues;
	uint8_t reserved_status, config_generation;
	le16_t queue_select;
};

/**
 * struct virtio_pci_ctx - state container for the Virtio-PCI transport layer
 * @pci_dev: standard PCI endpoint structure
 * @vdev: frontend virtio device payload
 * @common_cfg: cached common configuration block mapped to BAR0
 * @queues: independent queue configuration registers
 * @isr_status: pending interrupt status
 */
struct virtio_pci_ctx {
	struct pci_device pci_dev;
	struct virtio_device *vdev;

	struct virtio_pci_global_cfg common_cfg;
	struct virtio_pci_queue_cfg queues[VIRTIO_MAX_VQS];
	uint8_t isr_status;
};

static void virtio_pci_transport_notify_queue(void *transport_data)
{
	struct virtio_pci_ctx *ctx = transport_data;

	ctx->isr_status |= 1;
	pci_device_set_irq_locked(&ctx->pci_dev, 1);
}

static void virtio_pci_failed(void *data)
{
	struct virtio_pci_ctx *ctx = data;
	ctx->isr_status |= 2;
	pci_device_set_irq_locked(&ctx->pci_dev, 1);
}

static const struct virtio_transport_ops virtio_pci_transport = {
	.notify_queue = virtio_pci_transport_notify_queue,
	.notify_config = virtio_pci_failed,
};

static uint64_t virtio_pci_bar0_read(struct io_region *region, uint64_t offset, uint8_t size)
{
	struct device *dev = region->dev;
	struct virtio_pci_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint64_t val = 0;
	uint64_t features;

	if (offset < sizeof(struct virtio_pci_common_cfg)) {
		if (offset == 20 && size == 1)
			return vdev->status;
		if (offset == offsetof(struct virtio_pci_common_cfg, device_feature)) {
			features = vdev->device_ops->get_features(vdev);
			features |= VIRTIO_F_VERSION_1; /* Advertise VIRTIO_F_VERSION_1 */

			if (le32_to_cpu(ctx->common_cfg.device_feature_select) == 0)
				val = (uint32_t)(features & 0xFFFFFFFF);
			else if (le32_to_cpu(ctx->common_cfg.device_feature_select) == 1)
				val = (uint32_t)((features >> 32) & 0xFFFFFFFF);

			return val; /* Guest expects native byte order for MMIO read return value in our bus */
		}

		if (offset == offsetof(struct virtio_pci_common_cfg, guest_feature)) {
			uint32_t selector = le32_to_cpu(ctx->common_cfg.guest_feature_select);
			return selector < 2 ? (uint32_t)(vdev->driver_features >> (selector * 32)) : 0;
		}

		if (offset >= offsetof(struct virtio_pci_common_cfg, queue_size)) {
			uint16_t q = le16_to_cpu(ctx->common_cfg.queue_select);
			size_t off = offset - offsetof(struct virtio_pci_common_cfg, queue_size);
			if (q >= vdev->nr_vqs || off + size > sizeof(ctx->queues[q]))
				return 0;
			for (unsigned int i = 0; i < size; i++)
				val |= (uint64_t)((uint8_t *)&ctx->queues[q])[off + i] << (8 * i);
			return val;
		}

		if (offset + size > sizeof(ctx->common_cfg))
			return 0;
		switch (size) {
		case 1:
			val = *((uint8_t *)&ctx->common_cfg + offset);
			break;
		case 2: {
			le16_t v16;
			memcpy(&v16, (uint8_t *)&ctx->common_cfg + offset, 2);
			val = le16_to_cpu(v16);
			break;
		}
		case 4: {
			le32_t v32;
			memcpy(&v32, (uint8_t *)&ctx->common_cfg + offset, 4);
			val = le32_to_cpu(v32);
			break;
		}
		}
		return val;
	}

	if (offset == 0x200) { /* ISR Config Region */
		val = ctx->isr_status;
		ctx->isr_status = 0;
		pci_device_set_irq_locked(&ctx->pci_dev, 0); /* Acknowledge */
		return val;
	}

	if (offset >= 0x300) { /* Device Config Region */
		if (vdev->device_ops->read_config)
			return vdev->device_ops->read_config(vdev, offset - 0x300, size);
	}

	return 0;
}

static void virtio_pci_bar0_write(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size)
{
	struct device *dev = region->dev;
	struct virtio_pci_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint16_t q_sel;

	if (offset < sizeof(struct virtio_pci_common_cfg)) {
		q_sel = le16_to_cpu(ctx->common_cfg.queue_select);
		if (offset >= offsetof(struct virtio_pci_common_cfg, queue_size)) {
			struct virtio_pci_queue_cfg *q;
			size_t off = offset - offsetof(struct virtio_pci_common_cfg, queue_size);
			if (q_sel >= vdev->nr_vqs || off + size > sizeof(*q))
				return;
			/* Queue control fields are 16-bit; addresses allow 32/64-bit writes. */
			if ((off < 8 && (size != 2 || off % 2)) || (off >= 8 && !((size == 4 && off % 4 == 0) || (size == 8 && off % 8 == 0))))
				return;
			q = &ctx->queues[q_sel];
			if (le16_to_cpu(q->enable))
				return;
			if (off == offsetof(struct virtio_pci_queue_cfg, notify_off) || off == offsetof(struct virtio_pci_queue_cfg, msix_vector))
				return;
			if (off == offsetof(struct virtio_pci_queue_cfg, size) && (!val || (val & (val - 1)) || val > virtqueue_get_max_size(vdev->vqs[q_sel])))
				return;
			if (off == offsetof(struct virtio_pci_queue_cfg, enable)) {
				if (val != 1)
					return;
				if (virtqueue_set_size(vdev->vqs[q_sel], le16_to_cpu(q->size)) ||
				    virtqueue_set_addrs(vdev->vqs[q_sel], TO_GPA(((uint64_t)le32_to_cpu(q->desc_hi) << 32) | le32_to_cpu(q->desc_lo)),
							TO_GPA(((uint64_t)le32_to_cpu(q->avail_hi) << 32) | le32_to_cpu(q->avail_lo)),
							TO_GPA(((uint64_t)le32_to_cpu(q->used_hi) << 32) | le32_to_cpu(q->used_lo))))
					return;
			}
			for (unsigned int i = 0; i < size; i++)
				((uint8_t *)q)[off + i] = val >> (8 * i);
			return;
		}
		if (!((size == 4 && (offset == 0 || offset == 8 || offset == 12)) || (size == 1 && offset == 20) || (size == 2 && offset == 22)))
			return;
		if (offset + size > sizeof(ctx->common_cfg))
			return;
		if (offset == offsetof(struct virtio_pci_common_cfg, guest_feature)) {
			virtio_device_set_features_locked(vdev, le32_to_cpu(ctx->common_cfg.guest_feature_select), (uint32_t)val);
			return;
		}
		switch (size) {
		case 1:
			virtio_device_set_status_locked(vdev, val);
			break;
		case 2: {
			le16_t v16 = cpu_to_le16((uint16_t)val);
			memcpy((uint8_t *)&ctx->common_cfg + offset, &v16, 2);
			break;
		}
		case 4: {
			le32_t v32 = cpu_to_le32((uint32_t)val);
			memcpy((uint8_t *)&ctx->common_cfg + offset, &v32, 4);
			break;
		}
		}

		q_sel = le16_to_cpu(ctx->common_cfg.queue_select);

		if (offset == offsetof(struct virtio_pci_common_cfg, device_status)) {
			if (!val) {
				memset(&ctx->common_cfg, 0, sizeof(ctx->common_cfg));
				ctx->common_cfg.num_queues = cpu_to_le16(vdev->nr_vqs);
				ctx->common_cfg.msix_config = cpu_to_le16(0xffff);
				ctx->isr_status = 0;
				pci_device_set_irq_locked(&ctx->pci_dev, 0);
				for (unsigned int i = 0; i < vdev->nr_vqs; i++) {
					memset(&ctx->queues[i], 0, sizeof(ctx->queues[i]));
					ctx->queues[i].size = cpu_to_le16(virtqueue_get_max_size(vdev->vqs[i]));
					ctx->queues[i].msix_vector = cpu_to_le16(0xffff);
				}
			}
		}
		return;
	}

	if (offset == 0x100) { /* Notify Config Region */
		q_sel = (uint16_t)val;
		if (q_sel < vdev->nr_vqs && le16_to_cpu(ctx->queues[q_sel].enable) && virtio_device_ready_locked(vdev))
			vdev->device_ops->notify_queue(vdev, q_sel);
		return;
	}

	if (offset >= 0x300) { /* Device Config Region */
		if (vdev->device_ops->write_config)
			vdev->device_ops->write_config(vdev, offset - 0x300, (uint32_t)val, size);
	}
}

static void virtio_pci_stop(struct device *dev)
{
	struct virtio_pci_ctx *ctx = dev->priv;
	virtio_device_stop_locked(ctx->vdev);
	pci_device_set_irq_locked(&ctx->pci_dev, 0);
}

static const struct device_ops virtio_pci_bar_ops = {
	.stop = virtio_pci_stop,
	.read = virtio_pci_bar0_read,
	.write = virtio_pci_bar0_write,
};

/**
 * virtio_pci_build_config_space - construct the standard PCI capability list
 * @ctx: the virtio pci context
 */
static void virtio_pci_build_config_space(struct virtio_pci_ctx *ctx)
{
	uint8_t *cfg = ctx->pci_dev.config_space;
	le16_t v16;
	struct virtio_pci_cap cap = { 0 };
	struct virtio_pci_notify_cap notif = { 0 };
	uint8_t class_code = 0x00;
	uint8_t subclass = 0x00;

	/*
	 * Map Virtio Device IDs to standard PCI Class Codes.
	 * Virtio ID 1 = Network Card, Virtio ID 2 = Block Device.
	 */
	switch (ctx->vdev->device_id) {
	case 1: /* VIRTIO_ID_NET */
		class_code = 0x02; /* Network Controller */
		subclass = 0x00; /* Ethernet Controller */
		break;
	case 2: /* VIRTIO_ID_BLOCK */
		class_code = 0x01; /* Mass Storage Controller */
		subclass = 0x80; /* Other Mass Storage */
		break;
	default:
		class_code = 0xFF; /* Unclassified Device */
		subclass = 0xFF;
		break;
	}

	/* PCI Header Type 0 */
	v16 = cpu_to_le16(0x1AF4);
	memcpy(&cfg[0x00], &v16, 2); /* Vendor: Red Hat */

	/* Virtio 1.0 Spec: Modern PCI Device ID is 0x1040 + Virtio Device ID */
	v16 = cpu_to_le16(0x1040 + ctx->vdev->device_id);
	memcpy(&cfg[0x02], &v16, 2);

	v16 = cpu_to_le16(0x0002);
	memcpy(&cfg[0x04], &v16, 2); /* Command: Memory Space Enable */

	v16 = cpu_to_le16(0x0010);
	memcpy(&cfg[0x06], &v16, 2); /* Status: Capabilities List */

	cfg[0x08] = 0x01; /* Revision ID: 1 (Virtio 1.0 Non-transitional) */
	cfg[0x09] = 0x00; /* Programming Interface */
	cfg[0x0A] = subclass; /* Subclass Code */
	cfg[0x0B] = class_code; /* Base Class Code */

	/*
	 * Virtio 1.0 Spec 4.1.2.1:
	 * Non-transitional devices MUST have a PCI Subsystem Device ID matching
	 * the Virtio Device ID.
	 */
	v16 = cpu_to_le16(0x1AF4);
	memcpy(&cfg[0x2C], &v16, 2); /* Subsystem Vendor ID: Red Hat */
	v16 = cpu_to_le16((uint16_t)ctx->vdev->device_id);
	memcpy(&cfg[0x2E], &v16, 2); /* Subsystem Device ID */

	cfg[0x34] = 0x40; /* Capabilities Pointer */

	/* Capability 1: Common Configuration */
	cap.cap_vndr = 0x09; /* PCI_CAP_ID_VNDR */
	cap.cap_next = 0x50;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
	cap.bar = 0;
	cap.offset = cpu_to_le32(0x000);
	cap.length = cpu_to_le32(0x100);
	memcpy(&cfg[0x40], &cap, sizeof(cap));

	/* Capability 2: Notify Configuration */
	notif.cap.cap_vndr = 0x09;
	notif.cap.cap_next = 0x68;
	notif.cap.cap_len = sizeof(notif);
	notif.cap.cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
	notif.cap.bar = 0;
	notif.cap.offset = cpu_to_le32(0x100);
	notif.cap.length = cpu_to_le32(0x100);
	notif.notify_off_multiplier = cpu_to_le32(0);
	memcpy(&cfg[0x50], &notif, sizeof(notif));

	/* Capability 3: ISR Configuration */
	cap.cap_next = 0x78;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
	cap.offset = cpu_to_le32(0x200);
	cap.length = cpu_to_le32(1); /* ISR is 1 byte per Virtio 1.0 Spec */
	memcpy(&cfg[0x68], &cap, sizeof(cap));

	/* Capability 4: Device Specific Configuration */
	cap.cap_next = 0x00;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
	cap.offset = cpu_to_le32(0x300);
	cap.length = cpu_to_le32(0x100);
	memcpy(&cfg[0x78], &cap, sizeof(cap));
}

static int virtio_pci_realize(struct device *dev, void *pdata)
{
	struct virtio_pci_pdata *plat = pdata;
	struct virtio_pci_ctx *ctx;
	struct virtio_device *vdev;
	int ret;

	if (WARN_ON(!plat || !plat->pci_bus || !plat->vdev))
		return -VM_EINVAL;

	ctx = res_zalloc(&dev->resources, sizeof(*ctx));
	if (!ctx)
		return -VM_ENOMEM;

	ret = virtio_device_adopt_locked(dev, plat->vdev, &virtio_pci_transport, ctx);
	if (ret < 0)
		return ret;
	vdev = plat->vdev;

	ctx->vdev = vdev;
	ctx->pci_dev.owner = dev;
	ctx->pci_dev.priv = ctx;
	ctx->pci_dev.devfn = plat->devfn;
	ctx->pci_dev.interrupt_pin = plat->interrupt_pin;

	dev->ops = &virtio_pci_bar_ops;
	dev->priv = ctx;

	/* Realize the VirtIO device model and its queues. */
	ret = virtio_device_realize_locked(vdev);
	if (ret < 0)
		return ret;

	/* MUST set this AFTER realize() assigns vdev->nr_vqs */
	ctx->common_cfg.num_queues = cpu_to_le16(vdev->nr_vqs);
	ctx->common_cfg.msix_config = cpu_to_le16(0xffff);
	for (unsigned int i = 0; i < vdev->nr_vqs; i++) {
		ctx->queues[i].size = cpu_to_le16(virtqueue_get_max_size(vdev->vqs[i]));
		ctx->queues[i].msix_vector = cpu_to_le16(0xffff);
	}

	virtio_pci_build_config_space(ctx);

	/* Register configuration space on the abstract PCI bus */
	ret = pci_device_register_locked(plat->pci_bus, &ctx->pci_dev);
	if (ret < 0)
		return ret;

	/* Guest-visible 4 KiB BAR covers the capability ranges through 0x3ff. */
	ret = pci_register_bar_locked(&ctx->pci_dev, 0, 0x1000, plat->bar0_base);
	if (ret < 0)
		return ret;

	pr_info("virtio-pci transport attached at devfn %u for device %u (BAR0: 0x%llx)\n", PCI_DEVFN_VAL(ctx->pci_dev.devfn), vdev->device_id,
		(unsigned long long)GPA_VAL(ctx->pci_dev.bars[0].region->base));
	return 0;
}

static const struct device_desc virtio_pci_desc = {
	.name = "virtio-pci",
	.realize = virtio_pci_realize,
};

static void __attribute__((constructor)) register_virtio_pci_desc(void)
{
	device_register(&virtio_pci_desc);
}
