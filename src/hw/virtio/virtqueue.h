/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTQUEUE_H
#define MODVM_HW_VIRTIO_VIRTQUEUE_H

#include <modvm/util/types.h>

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct mem_space;
struct virtqueue;

/**
 * struct vring_desc - Virtio ring descriptor
 * @addr: guest physical address of the buffer
 * @len: length of the buffer
 * @flags: descriptor flags (e.g., next, write-only)
 * @next: index of the next descriptor in the chain
 */
struct vring_desc {
	le64_t addr;
	le32_t len;
	le16_t flags;
	le16_t next;
} __packed;

/* One snapshot per guest descriptor, independent of host mapping boundaries. */
struct virtqueue_buf {
	gpa_t gpa;
	uint32_t len;
	bool is_write;
};

/* Borrowed until the next pop/reset on this queue. Access requires the VM I/O lock. */
struct virtqueue_chain {
	struct mem_space *mem;
	const struct virtqueue_buf *bufs;
	unsigned nr_bufs;
	uint16_t head;
	size_t readable, writable;
};

/* Map one host-contiguous part of a directional byte stream. */
void *virtqueue_map(const struct virtqueue_chain *chain, bool write, size_t offset, size_t *length);
int virtqueue_read(const struct virtqueue_chain *chain, size_t offset, void *data, size_t size);
int virtqueue_write(const struct virtqueue_chain *chain, size_t offset, const void *data, size_t size);

struct virtqueue *virtqueue_create(struct mem_space *mem, uint16_t queue_size);
void virtqueue_destroy(struct virtqueue *vq);

uint16_t virtqueue_get_max_size(struct virtqueue *vq);

int virtqueue_set_size(struct virtqueue *vq, uint16_t size);
void virtqueue_reset(struct virtqueue *vq);

int virtqueue_set_addrs(struct virtqueue *vq, gpa_t desc_gpa, gpa_t avail_gpa, gpa_t used_gpa);
int virtqueue_pop(struct virtqueue *vq, struct virtqueue_chain *chain);
void virtqueue_disable(struct virtqueue *vq);
void virtqueue_push(struct virtqueue *vq, uint16_t desc_idx, uint32_t len);

#endif /* MODVM_HW_VIRTIO_VIRTQUEUE_H */
