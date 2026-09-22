/* SPDX-License-Identifier: GPL-2.0 */
#include "fixture.h"
#include <modvm/core/io_map.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>
#include "../src/hw/virtio/virtqueue.h"
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static unsigned notifications;
static int realize(struct virtio_device *vdev)
{
	return virtio_device_add_queue_locked(vdev, 256);
}

static uint64_t features(struct virtio_device *vdev)
{
	(void)vdev;
	return 0;
}

static void notify(struct virtio_device *vdev, uint16_t queue)
{
	(void)vdev;
	CHECK(queue == 0);
	notifications++;
}

static void irq(void *data, int level)
{
	(void)data;
	(void)level;
}

int main(void)
{
	log_init();
	struct vm_ctx vm = { 0 };
	res_pool_init(&vm.resources);
	INIT_LIST_HEAD(&vm.devices);
	INIT_LIST_HEAD(&vm.io_map.mmio_regions);
	INIT_LIST_HEAD(&vm.io_map.pio_regions);
	INIT_LIST_HEAD(&vm.mem_space.regions);
	CHECK(test_io_init(&vm) == 0);
	struct mem_region regions[2] = {
		{ .gpa = TO_GPA(0), .size = 0x10000 },
		{ .gpa = TO_GPA(0x10000), .size = 0x10000 },
	};
	for (unsigned i = 0; i < 2; i++) {
		regions[i].hva = calloc(1, regions[i].size);
		CHECK(regions[i].hva);
		list_add_tail(&regions[i].node, &vm.mem_space.regions);
	}
	uint8_t *low = regions[0].hva, *high = regions[1].hva;
	const struct virtio_device_ops ops = { .realize = realize, .get_features = features, .notify_queue = notify };
	struct virtio_device *vdev = virtio_device_alloc(&vm, 2, &ops, 1);
	CHECK(vdev);
	struct irq *line = irq_alloc(&vm.resources, irq, NULL);
	CHECK(line);
	struct virtio_mmio_pdata pdata = { .base = TO_GPA(0x200000), .irq = line, .vdev = vdev };
	struct device *device = device_alloc(&vm, "virtio-mmio");
	CHECK(!IS_ERR(device) && device_realize(device, &pdata) == 0);
#define WRITE(off, val) io_map_write(&vm.io_map, IO_MMIO, TO_GPA(0x200000 + (off)), (val), 4)
#define READ(off) io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x200000 + (off)), 4)
	WRITE(0x38, 256);
	WRITE(0x80, 0x1000);
	WRITE(0x90, 0x3000);
	WRITE(0xa0, 0x4000);
	WRITE(0x44, 1);
	CHECK(READ(0x44) == 1);
	WRITE(0x50, 0);
	CHECK(notifications == 0);
	WRITE(0x70, 15);
	CHECK(!(READ(0x70) & VIRTIO_STATUS_FEATURES_OK));
	WRITE(0x24, 1);
	WRITE(0x20, 1);
	WRITE(0x70, 15);
	CHECK(READ(0x70) == 15);
	WRITE(0x20, 0);
	CHECK(vdev->driver_features == VIRTIO_F_VERSION_1);
	WRITE(0x50, 0);
	CHECK(notifications == 1);
	struct vring_desc *desc = (void *)(low + 0x1000);
	/* 40 descriptors, including zero length and a descriptor crossing two
	 * separately allocated host regions. No artificial host-fragment limit. */
	for (unsigned i = 0; i < 40; i++)
		desc[i] = (struct vring_desc){
			.addr = cpu_to_le64(0xfff8), .len = cpu_to_le32(i ? 16 : 0), .flags = cpu_to_le16(i == 39 ? 0 : VRING_DESC_F_NEXT), .next = cpu_to_le16(i + 1)
		};
	memset(low + 0xfff8, 0x31, 8);
	memset(high, 0x32, 8);
	le16_t index = cpu_to_le16(1);
	memcpy(low + 0x3002, &index, 2);
	WRITE(0x44, 0);
	CHECK(READ(0x44) == 0);
	struct virtqueue_chain chain;
	CHECK(virtqueue_pop(vdev->vqs[0], &chain) == 0);
	WRITE(0x50, 0);
	CHECK(notifications == 1);
	WRITE(0x44, 1);
	CHECK(READ(0x44) == 1);
	CHECK(virtqueue_pop(vdev->vqs[0], &chain) == 1 && chain.nr_bufs == 40);
	uint8_t data[16];
	CHECK(virtqueue_read(&chain, 0, data, sizeof(data)) == 0);
	CHECK(data[0] == 0x31 && data[7] == 0x31 && data[8] == 0x32 && data[15] == 0x32);
	virtqueue_push(vdev->vqs[0], chain.head, 0);
	WRITE(0x44, 0);
	WRITE(0x44, 1);
	CHECK(virtqueue_pop(vdev->vqs[0], &chain) == 0); /* Disable did not rewind. */
	virtio_device_fail_locked(vdev);
	WRITE(0x50, 0);
	CHECK(notifications == 1 && (READ(0x70) & 64));
	WRITE(0x70, 0);
	CHECK(READ(0x70) == 0 && READ(0x44) == 0 && !vdev->driver_features);
	device_destroy(vdev->parent_dev);
	res_release_all(&vm.resources);
	free(low);
	free(high);
	log_destroy();
	puts("VirtIO negotiation, queue disable/resume and fragmented descriptor streams passed");
	return 0;
}
