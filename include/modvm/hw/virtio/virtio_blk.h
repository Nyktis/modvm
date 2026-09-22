/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_BLK_H
#define MODVM_HW_VIRTIO_VIRTIO_BLK_H

struct vm_ctx;
struct block_backend;
struct virtio_device;

struct virtio_device *virtio_blk_create(struct vm_ctx *ctx, struct block_backend *backend);

#endif /* MODVM_HW_VIRTIO_VIRTIO_BLK_H */
