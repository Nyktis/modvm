/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <modvm/core/memory.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/compiler.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/types.h>

#include "virtqueue.h"

struct vring_avail {
	le16_t flags;
	le16_t idx;
	le16_t ring[];
} __packed;

struct vring_used_elem {
	le32_t id;
	le32_t len;
} __packed;

struct vring_used {
	le16_t flags;
	le16_t idx;
	struct vring_used_elem ring[];
} __packed;

/**
 * struct virtqueue - internal representation of a virtio ring
 * @mem: reference to the virtual machine physical memory space
 * @queue_size: current guest-selected ring size
 * @max_size: device-advertised maximum descriptor count
 * @broken: chain validation failed; requires reset before reuse
 * @enabled: ring mappings are enabled; device readiness is checked separately
 * @last_avail_idx: host-side cached index of the next available buffer
 * @last_used_idx: host-side cached index of the next used buffer
 * @desc_table: mapped host virtual address of the descriptor table
 * @avail_ring: mapped host virtual address of the available ring
 * @used_ring: mapped host virtual address of the used ring
 * @bufs: one descriptor snapshot per slot; storage borrowed by the current chain
 */
struct virtqueue {
	struct mem_space *mem;
	uint16_t queue_size;
	uint16_t max_size;
	bool broken;
	bool enabled;
	uint16_t last_avail_idx;
	uint16_t last_used_idx;

	struct vring_desc *desc_table;
	struct vring_avail *avail_ring;
	struct vring_used *used_ring;
	struct virtqueue_buf bufs[];
};

/**
 * virtqueue_create - allocate and initialize a virtqueue object
 * @mem: memory space for translating buffer addresses
 * @queue_size: hardware-defined maximum depth of the queue
 *
 * Return: allocated virtqueue, or NULL on failure.
 */
struct virtqueue *virtqueue_create(struct mem_space *mem, uint16_t queue_size)
{
	struct virtqueue *vq;

	if (WARN_ON(!mem || queue_size == 0))
		return NULL;

	if (WARN_ON((queue_size & (queue_size - 1)) != 0))
		return NULL;

	vq = calloc(1, sizeof(*vq) + queue_size * sizeof(vq->bufs[0]));
	if (!vq)
		return NULL;

	vq->mem = mem;
	vq->queue_size = queue_size;
	vq->max_size = queue_size;
	vq->last_avail_idx = 0;
	vq->last_used_idx = 0;

	return vq;
}

/**
 * virtqueue_get_max_size - retrieve the configured maximum depth of the queue
 * @vq: the virtqueue instance
 *
 * Return: number of descriptors the queue can hold, or 0 if invalid.
 */
uint16_t virtqueue_get_max_size(struct virtqueue *vq)
{
	if (WARN_ON(!vq))
		return 0;

	return vq->max_size;
}

int virtqueue_set_size(struct virtqueue *vq, uint16_t size)
{
	if (vq->enabled || !size || (size & (size - 1)) || size > vq->max_size)
		return -VM_EINVAL;
	vq->queue_size = size;
	return 0;
}

void virtqueue_reset(struct virtqueue *vq)
{
	vq->enabled = false;
	vq->broken = false;
	vq->queue_size = vq->max_size;
	vq->last_avail_idx = vq->last_used_idx = 0;
	vq->desc_table = NULL;
	vq->avail_ring = NULL;
	vq->used_ring = NULL;
}

/**
 * virtqueue_set_addrs - bind guest physical rings to the host structures
 * @vq: the virtqueue instance
 * @desc_gpa: guest physical address of the descriptor table
 * @avail_gpa: guest physical address of the available ring
 * @used_gpa: guest physical address of the used ring
 *
 * Return: 0 on success, or a negative error code.
 */
int virtqueue_set_addrs(struct virtqueue *vq, gpa_t desc_gpa, gpa_t avail_gpa, gpa_t used_gpa)
{
	if (WARN_ON(!vq))
		return -VM_EINVAL;

	if ((GPA_VAL(desc_gpa) & 15) || (GPA_VAL(avail_gpa) & 1) || (GPA_VAL(used_gpa) & 3))
		return -VM_EINVAL;
	void *desc = mem_map_range(vq->mem, desc_gpa, 16u * vq->queue_size, false);
	void *avail = mem_map_range(vq->mem, avail_gpa, 6u + 2u * vq->queue_size, false);
	void *used = mem_map_range(vq->mem, used_gpa, 6u + 8u * vq->queue_size, true);
	if (!desc || !avail || !used)
		return -VM_EFAULT;
	vq->desc_table = desc;
	vq->avail_ring = avail;
	vq->used_ring = used;
	vq->enabled = true;

	return 0;
}

void virtqueue_disable(struct virtqueue *vq)
{
	vq->enabled = false;
}

int virtqueue_pop(struct virtqueue *vq, struct virtqueue_chain *chain)
{
	if (!vq || !chain)
		return -VM_EINVAL;
	*chain = (struct virtqueue_chain){ .mem = vq->mem, .bufs = vq->bufs };
	if (!vq->enabled)
		return 0;
	if (vq->broken)
		return -VM_EIO;
	uint16_t available = le16_to_cpu(vq->avail_ring->idx);
	atomic_thread_fence(memory_order_acquire);
	if (available == vq->last_avail_idx)
		return 0;
	int error = -VM_EFAULT;
	if ((uint16_t)(available - vq->last_avail_idx) > vq->queue_size)
		goto fail;
	uint16_t index = le16_to_cpu(vq->avail_ring->ring[vq->last_avail_idx % vq->queue_size]);
	chain->head = index;
	bool writable = false;
	for (;;) {
		if (index >= vq->queue_size)
			goto fail;
		if (chain->nr_bufs == vq->queue_size) {
			error = -VM_ELOOP;
			goto fail;
		}
		struct vring_desc desc;
		memcpy(&desc, &vq->desc_table[index], sizeof(desc));
		uint16_t flags = le16_to_cpu(desc.flags);
		bool write = !!(flags & VRING_DESC_F_WRITE);
		if ((flags & ~(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE)) || (writable && !write)) {
			error = -VM_EINVAL;
			goto fail;
		}
		writable |= write;
		gpa_t gpa = TO_GPA(le64_to_cpu(desc.addr));
		size_t length = le32_to_cpu(desc.len);
		size_t *total = write ? &chain->writable : &chain->readable;
		if (length > UINT64_MAX - GPA_VAL(gpa) || length > SIZE_MAX - *total)
			goto fail;
		*total += length;
		vq->bufs[chain->nr_bufs++] = (struct virtqueue_buf){ .gpa = gpa, .len = length, .is_write = write };
		while (length) {
			size_t chunk;
			if (!mem_map_chunk(vq->mem, gpa, length, write, &chunk) || !chunk)
				goto fail;
			gpa = gpa_add(gpa, chunk);
			length -= chunk;
		}
		if (!(flags & VRING_DESC_F_NEXT))
			break;
		index = le16_to_cpu(desc.next);
	}
	vq->last_avail_idx++;
	return 1;
fail:
	vq->broken = true;
	return error;
}

void *virtqueue_map(const struct virtqueue_chain *chain, bool write, size_t offset, size_t *length)
{
	for (unsigned i = 0; i < chain->nr_bufs; i++) {
		const struct virtqueue_buf *buf = &chain->bufs[i];
		if (buf->is_write != write)
			continue;
		if (offset >= buf->len) {
			offset -= buf->len;
			continue;
		}
		return mem_map_chunk(chain->mem, gpa_add(buf->gpa, offset), buf->len - offset, write, length);
	}
	*length = 0;
	return NULL;
}

static int virtqueue_copy(const struct virtqueue_chain *chain, bool write, size_t offset, void *data, size_t size)
{
	size_t capacity = write ? chain->writable : chain->readable;
	if (offset > capacity || size > capacity - offset)
		return -VM_EINVAL;
	uint8_t *bytes = data;
	while (size) {
		size_t chunk;
		void *hva = virtqueue_map(chain, write, offset, &chunk);
		if (!hva || !chunk)
			return -VM_EFAULT;
		if (chunk > size)
			chunk = size;
		if (write)
			memcpy(hva, bytes, chunk);
		else
			memcpy(bytes, hva, chunk);
		bytes += chunk;
		offset += chunk;
		size -= chunk;
	}
	return 0;
}

int virtqueue_read(const struct virtqueue_chain *chain, size_t offset, void *data, size_t size)
{
	return virtqueue_copy(chain, false, offset, data, size);
}

int virtqueue_write(const struct virtqueue_chain *chain, size_t offset, const void *data, size_t size)
{
	return virtqueue_copy(chain, true, offset, (void *)data, size);
}

/**
 * virtqueue_push - return a processed buffer chain to the guest
 * @vq: the virtqueue instance
 * @desc_idx: the head index of the completed descriptor chain
 * @len: total bytes written to the device-writable buffers
 */
void virtqueue_push(struct virtqueue *vq, uint16_t desc_idx, uint32_t len)
{
	struct vring_used_elem *used_elem;
	uint16_t ring_idx;

	if (unlikely(!vq || !vq->enabled || !vq->used_ring))
		return;

	ring_idx = vq->last_used_idx % vq->queue_size;
	used_elem = &vq->used_ring->ring[ring_idx];

	used_elem->id = cpu_to_le32(desc_idx);
	used_elem->len = cpu_to_le32(len);

	atomic_thread_fence(memory_order_release);

	vq->last_used_idx++;
	vq->used_ring->idx = cpu_to_le16(vq->last_used_idx);
}

/**
 * virtqueue_destroy - release virtqueue memory
 * @vq: the virtqueue to destroy
 */
void virtqueue_destroy(struct virtqueue *vq)
{
	if (WARN_ON(!vq))
		return;

	free(vq);
}
