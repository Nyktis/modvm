/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>

#include <modvm/io/net.h>
#include <modvm/core/vm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#include "virtqueue.h"
#include "virtio_net_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_net: " fmt

#define VIRTIO_ID_NET 1
#define VIRTIO_NET_QUEUE_SIZE 256
#define VIRTIO_NET_CTRL_QUEUE_SIZE 64

struct virtio_net_ctx {
	struct virtio_device *vdev;
	struct net_backend *backend;
	struct virtio_net_config config;
	bool bound;
};

static void virtio_net_rx_cb(void *data, const uint8_t *buf, size_t len);

static void virtio_net_error_cb(void *data, int error)
{
	struct virtio_net_ctx *ctx = data;
	vm_report_error(ctx->vdev->parent_dev->ctx, error);
}

/**
 * virtio_net_realize - initialize internal device state and allocate virtqueues
 * @vdev: the base virtio device pointer
 *
 * Return: 0 on success, or a negative error code.
 */
static int virtio_net_realize(struct virtio_device *vdev)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct vm_ctx *mctx = vdev->parent_dev->ctx;

	const uint16_t sizes[] = { VIRTIO_NET_QUEUE_SIZE, VIRTIO_NET_QUEUE_SIZE, VIRTIO_NET_CTRL_QUEUE_SIZE };
	for (unsigned i = 0; i < 3; i++) {
		int ret = virtio_device_add_queue_locked(vdev, sizes[i]);
		if (ret < 0)
			return ret;
	}

	memset(&ctx->config, 0, sizeof(ctx->config));
	int ret = ctx->backend->ops->get_mac(ctx->backend, ctx->config.mac);
	if (ret < 0)
		return ret;

	/* Enforce strict little-endian assignment for configuration space */
	ctx->config.status = cpu_to_le16(VIRTIO_NET_S_LINK_UP);

	ret = net_bind_locked(ctx->backend, mctx->io_ctx, virtio_net_rx_cb, virtio_net_error_cb, ctx);
	if (ret < 0)
		return ret;
	ctx->bound = true;

	pr_info("virtio-net realized with mac %02x:%02x:%02x:%02x:%02x:%02x\n", ctx->config.mac[0], ctx->config.mac[1], ctx->config.mac[2], ctx->config.mac[3], ctx->config.mac[4],
		ctx->config.mac[5]);

	return 0;
}

/**
 * virtio_net_stop - detach the receive callback before resource teardown
 * @vdev: the base virtio device pointer
 */
static void virtio_net_stop(struct virtio_device *vdev)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	if (ctx->bound)
		net_unbind_locked(ctx->backend);
	ctx->bound = false;
}

static uint64_t virtio_net_get_features(struct virtio_device *vdev)
{
	(void)vdev;
	return (1ULL << VIRTIO_NET_F_MAC) | (1ULL << VIRTIO_NET_F_STATUS) | (1ULL << VIRTIO_NET_F_CTRL_VQ) | (1ULL << VIRTIO_NET_F_CTRL_MAC_ADDR);
}

static uint64_t virtio_net_read_config(struct virtio_device *vdev, uint64_t offset, uint8_t size)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	uint64_t val = 0;

	if (!size || size > sizeof(val) || offset > sizeof(ctx->config) || size > sizeof(ctx->config) - offset)
		return 0;

	memcpy(&val, (uint8_t *)&ctx->config + offset, size);
	return val;
}

/**
 * virtio_net_handle_ctrl - process guest control-queue commands
 * @vdev: the base virtio device pointer
 * @vq: the control virtqueue instance
 */
static void virtio_net_handle_ctrl(struct virtio_device *vdev, struct virtqueue *vq)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct virtqueue_chain chain;
	bool need_irq = false;
	int ret;
	while ((ret = virtqueue_pop(vq, &chain)) > 0) {
		struct virtio_net_ctrl_hdr ctrl;
		uint8_t status = VIRTIO_NET_ERR;
		if (!chain.writable) {
			virtio_device_fail_locked(vdev);
			break;
		}
		if ((vdev->driver_features & (1ULL << VIRTIO_NET_F_CTRL_MAC_ADDR)) && !virtqueue_read(&chain, 0, &ctrl, sizeof(ctrl)) && ctrl.class == VIRTIO_NET_CTRL_MAC &&
		    ctrl.cmd == VIRTIO_NET_CTRL_MAC_ADDR_SET && chain.readable == sizeof(ctrl) + sizeof(ctx->config.mac) &&
		    !virtqueue_read(&chain, sizeof(ctrl), ctx->config.mac, sizeof(ctx->config.mac)))
			status = VIRTIO_NET_OK;
		if (virtqueue_write(&chain, chain.writable - 1, &status, 1)) {
			virtio_device_fail_locked(vdev);
			break;
		}
		virtqueue_push(vq, chain.head, 1);
		need_irq = true;
	}
	if (ret < 0)
		virtio_device_fail_locked(vdev);
	if (need_irq)
		vdev->transport_ops->notify_queue(vdev->transport_data);
}

static void virtio_net_notify_queue(struct virtio_device *vdev, uint16_t queue_idx)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	if (!virtio_device_ready_locked(vdev) || queue_idx >= vdev->nr_vqs || queue_idx == 0)
		return;
	struct virtqueue *vq = vdev->vqs[queue_idx];
	if (queue_idx == 2) {
		if (!(vdev->driver_features & (1ULL << VIRTIO_NET_F_CTRL_VQ)))
			return;
		virtio_net_handle_ctrl(vdev, vq);
		return;
	}
	struct virtqueue_chain chain;
	bool need_irq = false;
	int ret;
	while ((ret = virtqueue_pop(vq, &chain)) > 0) {
		uint8_t frame[NET_MAX_FRAME_SIZE];
		size_t header = sizeof(struct virtio_net_hdr_v1);
		if (!chain.writable && chain.readable > header && chain.readable - header <= sizeof(frame) && !virtqueue_read(&chain, header, frame, chain.readable - header)) {
			size_t length = chain.readable - header;
			ptrdiff_t n = ctx->backend->ops->write(ctx->backend, frame, length);
			if (n != (ptrdiff_t)length)
				pr_warn_once("host network rejected a complete TX frame (%zd)\n", n);
		}
		virtqueue_push(vq, chain.head, 0);
		need_irq = true;
	}
	if (ret < 0)
		virtio_device_fail_locked(vdev);
	if (need_irq)
		vdev->transport_ops->notify_queue(vdev->transport_data);
}

static void virtio_net_rx_cb(void *data, const uint8_t *buf, size_t len)
{
	struct virtio_net_ctx *ctx = data;
	struct virtio_device *vdev = ctx->vdev;
	if (!virtio_device_ready_locked(vdev))
		return;
	struct virtqueue *vq = vdev->vqs[0];
	struct virtqueue_chain chain;
	int ret = virtqueue_pop(vq, &chain);
	if (ret <= 0) {
		if (ret < 0)
			virtio_device_fail_locked(vdev);
		return;
	}
	struct virtio_net_hdr_v1 header = { .num_buffers = cpu_to_le16(1) };
	uint32_t written = 0;
	if (!chain.readable && len <= UINT32_MAX - sizeof(header) && chain.writable >= sizeof(header) + len && !virtqueue_write(&chain, 0, &header, sizeof(header)) &&
	    !virtqueue_write(&chain, sizeof(header), buf, len))
		written = sizeof(header) + len;
	virtqueue_push(vq, chain.head, written);
	vdev->transport_ops->notify_queue(vdev->transport_data);
}

static const struct virtio_device_ops virtio_net_ops = {
	.realize = virtio_net_realize,
	.stop = virtio_net_stop,
	.get_features = virtio_net_get_features,
	.read_config = virtio_net_read_config,
	.notify_queue = virtio_net_notify_queue,
};

/**
 * virtio_net_create - allocate a virtio network front-end device
 * @ctx: owning VM context
 * @backend: host network abstraction to pair with
 *
 * Return: VM-owned device awaiting transport attachment and realization,
 * or NULL on failure. The backend is borrowed and must outlive the device.
 */
struct virtio_device *virtio_net_create(struct vm_ctx *ctx, struct net_backend *backend)
{
	struct virtio_net_ctx *net_ctx;

	if (WARN_ON(!ctx || !backend))
		return NULL;

	struct virtio_device *vdev = virtio_device_alloc(ctx, VIRTIO_ID_NET, &virtio_net_ops, sizeof(*net_ctx));
	if (!vdev)
		return NULL;
	net_ctx = vdev->priv;
	net_ctx->backend = backend;
	net_ctx->vdev = vdev;
	return vdev;
}
