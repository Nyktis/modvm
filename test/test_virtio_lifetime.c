/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <modvm/core/vm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int fail_realize, stops, releases;
static void release(void *data)
{
	(void)data;
	CHECK(stops == 1);
	releases++;
}
static int realize(struct virtio_device *vdev)
{
	CHECK(vdev->state == VIRTIO_DEVICE_INITIALIZING && !virtio_device_ready_locked(vdev));
	CHECK(virtio_device_add_queue_locked(vdev, 8) == 0);
	CHECK(res_add_action(&vdev->resources, release, NULL) == 0);
	return fail_realize ? -VM_EIO : 0;
}
static void stop(struct virtio_device *vdev)
{
	CHECK(vdev->state == VIRTIO_DEVICE_STOPPED && !virtio_device_ready_locked(vdev));
	stops++;
}
static uint64_t features(struct virtio_device *vdev)
{
	(void)vdev;
	return 2;
}
static void notify(struct virtio_device *vdev, uint16_t q)
{
	(void)vdev;
	(void)q;
}
static void irq(void *data)
{
	(void)data;
}
int main(void)
{
	CHECK(log_init() == 0);
	const struct virtio_device_ops ops = { .realize = realize, .stop = stop, .get_features = features, .notify_queue = notify };
	const struct virtio_transport_ops transport = { .notify_queue = irq, .notify_config = irq };
	const struct virtio_transport_ops incomplete = { .notify_queue = irq };
	for (fail_realize = 0; fail_realize < 2; fail_realize++) {
		stops = releases = 0;
		struct vm_ctx vm = { 0 }, other = { 0 };
		res_pool_init(&vm.resources);
		res_pool_init(&other.resources);
		struct device parent = { .ctx = &vm }, foreign = { .ctx = &other };
		res_pool_init(&parent.resources);
		res_pool_init(&foreign.resources);
		struct virtio_device *vdev = virtio_device_alloc(&vm, 2, &ops, 1);
		CHECK(vdev && vdev->mem == &vm.mem_space);
		CHECK(virtio_device_realize_locked(vdev) == -VM_EINVAL);
		CHECK(virtio_device_add_queue_locked(vdev, 8) == -VM_EINVAL);
		CHECK(virtio_device_adopt_locked(&parent, vdev, &incomplete, NULL) == -VM_EINVAL);
		CHECK(virtio_device_adopt_locked(&foreign, vdev, &transport, NULL) == -VM_EINVAL);
		CHECK(!vdev->parent_dev && vdev->owner == &vm.resources && !vdev->transport_ops);
		CHECK(virtio_device_adopt_locked(&parent, vdev, &transport, NULL) == 0);
		CHECK(virtio_device_destroy(vdev) == -VM_EBUSY);
		CHECK(vdev->owner == &parent.resources && vdev->parent_dev == &parent);
		CHECK(virtio_device_realize_locked(vdev) == (fail_realize ? -VM_EIO : 0));
		if (!fail_realize) {
			CHECK(vdev->state == VIRTIO_DEVICE_ACTIVE);
			CHECK(virtio_device_add_queue_locked(vdev, 8) == -VM_EINVAL);
			virtio_device_set_features_locked(vdev, 0, 2);
			virtio_device_set_features_locked(vdev, 1, 1);
			virtio_device_set_features_locked(vdev, UINT32_MAX, UINT32_MAX);
			CHECK(vdev->driver_features == (VIRTIO_F_VERSION_1 | 2));
			virtio_device_set_status_locked(vdev, 15);
			CHECK(virtio_device_ready_locked(vdev));
			virtio_device_set_features_locked(vdev, 0, 0);
			virtio_device_set_features_locked(vdev, 1, 0);
			CHECK(vdev->driver_features == (VIRTIO_F_VERSION_1 | 2));
			virtio_device_set_status_locked(vdev, 0);
			CHECK(!vdev->driver_features);
			virtio_device_set_features_locked(vdev, 0, 2);
			CHECK(vdev->driver_features == 2);
		} else
			CHECK(vdev->state == VIRTIO_DEVICE_STOPPED && stops == 1);
		res_release_all(&parent.resources);
		res_release_all(&vm.resources);
		CHECK(stops == 1 && releases == 1);
		vdev = virtio_device_alloc(&vm, 2, &ops, 1);
		CHECK(vdev && virtio_device_destroy(vdev) == 0 && list_empty(&vm.resources.resources));
	}
	log_destroy();
	puts("VirtIO initialization, transport ownership, memory identity and feature negotiation passed");
	return 0;
}
