/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_NET_H
#define MODVM_HW_VIRTIO_VIRTIO_NET_H

struct vm_ctx;
struct net_backend;
struct virtio_device;

struct virtio_device *virtio_net_create(struct vm_ctx *ctx, struct net_backend *backend);

#endif /* MODVM_HW_VIRTIO_VIRTIO_NET_H */
