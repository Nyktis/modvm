/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/io/ctx.h>
#include <modvm/io/receiver.h>
#include <modvm/errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "fixture.h"
#include <stdlib.h>
#include <string.h>
#include <modvm/core/vm.h>
#include <modvm/io/net.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>
#include "../src/hw/virtio/virtqueue.h"
#include "../src/hw/virtio/virtio_net_reg.h"

#define CHECK(expr)                                                        \
	do {                                                               \
		if (!(expr)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
			exit(1);                                           \
		}                                                          \
	} while (0)

static unsigned int registrations, interrupts, sent;
static ptrdiff_t send_frame(struct net_backend *net, const uint8_t *buf, size_t len)
{
	(void)net;
	CHECK(len == 5000 && buf[0] == 0x5a && buf[len - 1] == 0x5a);
	sent++;
	return -VM_EAGAIN;
}

/* Like linux-tap, the backend receives an owned delivery binding. */
static int bind_rx(struct net_backend *net, struct io_receiver *binding)
{
	CHECK(binding && atomic_load(&net->binding));
	registrations++;
	return 0;
}

static void raise_irq(void *data)
{
	(void)data;
	interrupts++;
}

static void unbind(struct net_backend *net)
{
	CHECK(atomic_load(&net->binding));
	registrations++;
}

static int mac_error;
static int get_mac(struct net_backend *net, uint8_t mac[6])
{
	(void)net;
	if (mac_error)
		return mac_error;
	for (unsigned i = 0; i < 6; i++)
		mac[i] = i + 2;
	return 0;
}

static atomic_bool consumed;
static void received(void *data)
{
	(void)data;
	atomic_store(&consumed, true);
}
static void *run_io(void *data)
{
	CHECK(!io_ctx_run(data));
	return NULL;
}
static void inject(struct net_backend *backend, const uint8_t *frame, size_t len)
{
	atomic_store(&consumed, false);
	CHECK(!io_receiver_submit(atomic_load(&backend->binding), frame, len, received, NULL));
	while (!atomic_load(&consumed))
		sched_yield();
}

int main(void)
{
	alarm(5);
	const struct net_ops backend_ops = { .get_mac = get_mac, .unbind = unbind, .bind = bind_rx, .write = send_frame };
	const struct virtio_transport_ops transport = { .notify_queue = raise_irq, .notify_config = raise_irq };
	struct net_backend backend = { .ops = &backend_ops };
	struct vm_ctx vm = { 0 };
	res_pool_init(&vm.resources);
	CHECK(test_io_init(&vm) == 0);
	pthread_t runner;
	CHECK(!pthread_create(&runner, NULL, run_io, vm.io_ctx));
	struct device parent = { .ctx = &vm };
	res_pool_init(&parent.resources);
	struct mem_region ram = { .gpa = TO_GPA(0), .size = 0x20000 };
	struct virtio_device *vdev;
	struct vring_desc *desc;
	uint8_t *mem;
	const uint8_t frame[60] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x52, 0x54, 0, 0x12, 0x34, 0x56, 0x08, 0x06 };
	le16_t avail_idx = cpu_to_le16(1), used_idx;
	le32_t used_len;
	size_t hdr_len = sizeof(struct virtio_net_hdr_v1);

	log_init();
	mem = calloc(1, ram.size);
	CHECK(mem);
	ram.hva = mem;
	INIT_LIST_HEAD(&vm.mem_space.regions);
	list_add_tail(&ram.node, &vm.mem_space.regions);
	vdev = virtio_net_create(&vm, &backend);
	CHECK(vdev);
	CHECK(virtio_device_adopt_locked(&parent, vdev, &transport, NULL) == 0);
	CHECK(virtio_device_realize_locked(vdev) == 0);
	CHECK(registrations == 1 && atomic_load(&backend.binding));
	CHECK(virtqueue_set_addrs(vdev->vqs[0], TO_GPA(0x1000), TO_GPA(0x2000), TO_GPA(0x3000)) == 0);
	desc = (struct vring_desc *)(mem + 0x1000);
	desc[0] = (struct vring_desc){ .flags = cpu_to_le16(VRING_DESC_F_WRITE | VRING_DESC_F_NEXT), .next = cpu_to_le16(1) };
	desc++;
	desc->addr = cpu_to_le64(0x4000);
	desc->len = cpu_to_le32(2048);
	desc->flags = cpu_to_le16(VRING_DESC_F_WRITE);
	memcpy(mem + 0x2002, &avail_idx, sizeof(avail_idx));
	inject(&backend, frame, sizeof(frame));
	CHECK(interrupts == 0 && mem[0x4000 + hdr_len] == 0);
	vdev->driver_features = VIRTIO_F_VERSION_1 | (1ULL << 63);
	virtio_device_set_status_locked(vdev, 15);
	CHECK(!(vdev->status & VIRTIO_STATUS_FEATURES_OK) && !virtio_device_ready_locked(vdev));
	vdev->driver_features = VIRTIO_F_VERSION_1;
	virtio_device_set_status_locked(vdev, 15);
	CHECK(virtio_device_ready_locked(vdev));
	inject(&backend, frame, sizeof(frame));
	CHECK(memcmp(mem + 0x4000 + hdr_len, frame, sizeof(frame)) == 0);
	memcpy(&used_idx, mem + 0x3002, sizeof(used_idx));
	memcpy(&used_len, mem + 0x3008, sizeof(used_len));
	CHECK(le16_to_cpu(used_idx) == 1);
	CHECK(le32_to_cpu(used_len) == hdr_len + sizeof(frame));
	CHECK(interrupts == 1);
	/* No guest buffer left: the next packet must not complete another buffer. */
	inject(&backend, frame, sizeof(frame));
	CHECK(interrupts == 1);
	/* An undersized RX buffer must remain untouched and return no frame. */
	memset(mem + 0x4000, 0x7b, 64);
	desc->len = cpu_to_le32(16);
	avail_idx = cpu_to_le16(2);
	memcpy(mem + 0x2002, &avail_idx, 2);
	inject(&backend, frame, sizeof(frame));
	memcpy(&used_len, mem + 0x3010, 4);
	CHECK(le32_to_cpu(used_len) == 0 && mem[0x4000] == 0x7b);
	CHECK(virtqueue_set_addrs(vdev->vqs[1], TO_GPA(0x5000), TO_GPA(0x6000), TO_GPA(0x7000)) == 0);
	struct vring_desc *tx = (void *)(mem + 0x5000);
	tx->addr = cpu_to_le64(0x8000);
	tx->len = cpu_to_le32(hdr_len + 5000);
	memset(mem + 0x8000 + hdr_len, 0x5a, 5000);
	avail_idx = cpu_to_le16(1);
	memcpy(mem + 0x6002, &avail_idx, 2);
	vdev->device_ops->notify_queue(vdev, 1);
	CHECK(sent == 1);
	memcpy(&used_idx, mem + 0x7002, 2);
	CHECK(le16_to_cpu(used_idx) == 1);
	/* An oversized frame must never reach the backend as a prefix. */
	tx->len = cpu_to_le32(hdr_len + 65551);
	avail_idx = cpu_to_le16(2);
	memcpy(mem + 0x6002, &avail_idx, 2);
	vdev->device_ops->notify_queue(vdev, 1);
	CHECK(sent == 1);
	io_receiver_notify(atomic_load(&backend.binding), -VM_ENODEV);
	CHECK(!pthread_join(runner, NULL));
	CHECK(atomic_load(&vm.run_error) == -VM_ENODEV);
	CHECK(virtio_device_destroy(vdev) == -VM_EBUSY);
	res_release_all(&parent.resources);
	CHECK(registrations == 2 && !atomic_load(&backend.binding));
	mac_error = -VM_EIO;
	vdev = virtio_net_create(&vm, &backend);
	CHECK(vdev);
	CHECK(virtio_device_adopt_locked(&parent, vdev, &transport, NULL) == 0);
	CHECK(virtio_device_realize_locked(vdev) == -VM_EIO);
	CHECK(registrations == 2 && !atomic_load(&backend.binding));
	CHECK(virtio_device_destroy(vdev) == -VM_EBUSY);
	res_release_all(&parent.resources);
	res_release_all(&vm.resources);
	free(mem);
	log_destroy();
	puts("virtio-net backend RX, guest delivery and callback teardown passed");
	return 0;
}
