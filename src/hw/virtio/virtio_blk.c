/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>

#include <modvm/io/block.h>
#include <modvm/core/vm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_blk.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#include "virtqueue.h"
#include "virtio_blk_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_blk: " fmt

#define VIRTIO_ID_BLOCK 2
#define VIRTIO_BLK_QUEUE_SIZE 128
#define SECTOR_SIZE 512

/**
 * struct virtio_blk_ctx - state container for a virtio block instance
 * @backend: host storage backend performing actual data manipulation
 * @config: cached standard configuration space
 */
struct virtio_blk_ctx {
	struct block_backend *backend;
	struct virtio_blk_config config;
};

/**
 * virtio_blk_realize - initialize internal device state and allocate virtqueues
 * @vdev: the base virtio device pointer
 *
 * Return: 0 on success, or a negative error code.
 */
static int virtio_blk_realize(struct virtio_device *vdev)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	uint64_t capacity_bytes;

	int ret = virtio_device_add_queue_locked(vdev, VIRTIO_BLK_QUEUE_SIZE);
	if (ret < 0)
		return ret;

	/* Populate standard block geometry and capacity into config space */
	capacity_bytes = ctx->backend->ops->get_capacity(ctx->backend);

	memset(&ctx->config, 0, sizeof(ctx->config));
	ctx->config.capacity = cpu_to_le64(capacity_bytes / SECTOR_SIZE);
	ctx->config.blk_size = cpu_to_le32(SECTOR_SIZE);
	ctx->config.size_max = cpu_to_le32(65536);
	ctx->config.seg_max = cpu_to_le32(128 - 2); /* Account for header and status descriptors */

	/* Synthetic standard geometry */
	ctx->config.geometry.cylinders = cpu_to_le16((uint16_t)((capacity_bytes / SECTOR_SIZE) / (16 * 63)));
	ctx->config.geometry.heads = 16;
	ctx->config.geometry.sectors = 63;

	pr_info("virtio-blk realized with capacity %llu sectors\n", (unsigned long long)(capacity_bytes / SECTOR_SIZE));

	return 0;
}

static uint64_t virtio_blk_get_features(struct virtio_device *vdev)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	uint64_t features = (1ULL << VIRTIO_BLK_F_BLK_SIZE) | (1ULL << VIRTIO_BLK_F_GEOMETRY);
	if (ctx->backend->ops->flush)
		features |= 1ULL << VIRTIO_BLK_F_FLUSH;
	if (ctx->backend->readonly)
		features |= 1ULL << VIRTIO_BLK_F_RO;
	return features;
}

static uint64_t virtio_blk_read_config(struct virtio_device *vdev, uint64_t offset, uint8_t size)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	uint64_t val = 0;

	if (!size || size > sizeof(val) || offset > sizeof(ctx->config) || size > sizeof(ctx->config) - offset)
		return 0;

	memcpy(&val, (uint8_t *)&ctx->config + offset, size);
	return val;
}

/**
 * virtio_blk_notify_queue - process I/O requests submitted by the guest
 * @vdev: the base virtio device pointer
 * @queue_idx: index of the virtqueue that received a doorbell kick
 *
 * Pops descriptor chains,
 * validates layout, performs host I/O, pushes status, and injects interrupts.
 */
static void virtio_blk_notify_queue(struct virtio_device *vdev, uint16_t queue_idx)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	struct block_backend *backend = ctx->backend;
	if (!virtio_device_ready_locked(vdev) || queue_idx >= vdev->nr_vqs)
		return;
	struct virtqueue *vq = vdev->vqs[queue_idx];
	struct virtqueue_chain chain;
	bool need_irq = false;
	int ret;
	while ((ret = virtqueue_pop(vq, &chain)) > 0) {
		struct virtio_blk_outhdr hdr;
		uint8_t status = VIRTIO_BLK_S_IOERR;
		uint32_t written = 0;
		if (!chain.writable) {
			virtio_device_fail_locked(vdev);
			break;
		}
		if (virtqueue_read(&chain, 0, &hdr, sizeof(hdr)))
			goto complete;
		uint32_t type = le32_to_cpu(hdr.type);
		bool input = type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_GET_ID;
		size_t total = input ? chain.writable - 1 : chain.readable - sizeof(hdr);
		if ((input && chain.readable != sizeof(hdr)) || (!input && chain.writable != 1) || total > UINT32_MAX - 1)
			goto complete;
		switch (type) {
		case VIRTIO_BLK_T_IN:
		case VIRTIO_BLK_T_OUT: {
			if (le64_to_cpu(hdr.sector) > UINT64_MAX / SECTOR_SIZE || total % SECTOR_SIZE)
				goto complete;
			uint64_t offset = le64_to_cpu(hdr.sector) * SECTOR_SIZE;
			uint64_t capacity = backend->ops->get_capacity(backend);
			if (offset > capacity || total > capacity - offset || (!input && backend->readonly))
				goto complete;
			for (size_t done = 0; done < total;) {
				size_t chunk;
				void *data = virtqueue_map(&chain, input, done + (input ? 0 : sizeof(hdr)), &chunk);
				if (!data || !chunk)
					goto complete;
				if (chunk > total - done)
					chunk = total - done;
				ptrdiff_t n = input ? backend->ops->read(backend, data, chunk, offset + done) : backend->ops->write(backend, data, chunk, offset + done);
				if (n > 0 && input)
					written += n;
				if (n != (ptrdiff_t)chunk)
					goto complete;
				done += chunk;
			}
			status = VIRTIO_BLK_S_OK;
			break;
		}
		case VIRTIO_BLK_T_FLUSH:
			if (total)
				goto complete;
			status = !backend->ops->flush ? VIRTIO_BLK_S_UNSUPP : backend->ops->flush(backend) < 0 ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;
			break;
		case VIRTIO_BLK_T_GET_ID: {
			const char id[20] = "MODVM-DISK";
			if (total < sizeof(id) || virtqueue_write(&chain, 0, id, sizeof(id)))
				goto complete;
			written = sizeof(id);
			status = VIRTIO_BLK_S_OK;
			break;
		}
		default:
			status = VIRTIO_BLK_S_UNSUPP;
		}
complete:
		if (virtqueue_write(&chain, chain.writable - 1, &status, 1)) {
			virtio_device_fail_locked(vdev);
			break;
		}
		virtqueue_push(vq, chain.head, written + 1);
		need_irq = true;
	}
	if (ret < 0)
		virtio_device_fail_locked(vdev);
	if (need_irq)
		vdev->transport_ops->notify_queue(vdev->transport_data);
}

static const struct virtio_device_ops virtio_blk_ops = {
	.realize = virtio_blk_realize,
	.get_features = virtio_blk_get_features,
	.read_config = virtio_blk_read_config,
	.notify_queue = virtio_blk_notify_queue,
};

/**
 * virtio_blk_create - allocate a virtio block front-end device
 * @ctx: owning VM context
 * @backend: host block storage abstraction to pair with
 *
 * Return: VM-owned device awaiting transport attachment and realization,
 * or NULL on failure. The backend is borrowed and must outlive the device.
 */
struct virtio_device *virtio_blk_create(struct vm_ctx *ctx, struct block_backend *backend)
{
	struct virtio_blk_ctx *blk_ctx;

	if (WARN_ON(!ctx || !backend))
		return NULL;

	struct virtio_device *vdev = virtio_device_alloc(ctx, VIRTIO_ID_BLOCK, &virtio_blk_ops, sizeof(*blk_ctx));
	if (!vdev)
		return NULL;
	blk_ctx = vdev->priv;
	blk_ctx->backend = backend;
	return vdev;
}
