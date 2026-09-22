/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/errno.h>
#include "fixture.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <modvm/core/vm.h>
#include <modvm/util/res_pool.h>
#include <modvm/io/net.h>
#include <modvm/io/char.h>
#include <modvm/io/block.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int fail_at, stopped, released, alive, child_released;
static void child_cleanup(void *p)
{
	(void)p;
	CHECK(alive && !stopped);
	child_released++;
}

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t s)
{
	if (fail_at && !--fail_at)
		return NULL;
	return __real_calloc(n, s);
}

static void cleanup(void *p)
{
	(void)p;
	CHECK(stopped);
	released++;
	alive = 0;
}

static void stop(struct device *dev)
{
	(void)dev;
	CHECK(alive);
	stopped++;
}

static const struct device_ops ops = { .stop = stop };
static int realize(struct device *dev, void *data)
{
	(void)data;
	dev->ops = &ops;
	alive = 1;
	int ret = res_add_action(&dev->resources, cleanup, dev);
	CHECK(ret == 0);
	struct device *child = device_alloc_locked(dev->ctx, "ownership-test");
	CHECK(child);
	child->parent = dev;
	CHECK(res_add_action(&child->resources, child_cleanup, child) == 0);
	return -VM_EIO;
}

static int bind(struct net_backend *net, struct io_receiver *binding)
{
	(void)binding;
	(void)net;
	return 0;
}

static void irq(void *p, int level)
{
	(void)p;
	(void)level;
}

static int mac(struct net_backend *n, uint8_t out[6])
{
	(void)n;
	memset(out, 2, 6);
	return 0;
}

static unsigned factory_calls, factory_destroyed[2];
static ptrdiff_t factory_read(struct block_backend *blk, void *buf, size_t count, uint64_t offset)
{
	(void)blk;
	(void)buf;
	(void)count;
	(void)offset;
	return -VM_ENOTSUP;
}

static ptrdiff_t factory_write(struct block_backend *blk, const void *buf, size_t count, uint64_t offset)
{
	(void)blk;
	(void)buf;
	(void)count;
	(void)offset;
	return -VM_EROFS;
}

static uint64_t factory_capacity(struct block_backend *blk)
{
	(void)blk;
	return 0;
}

static void factory_destroy(struct block_backend *blk)
{
	(*(unsigned *)blk->priv)++;
	free(blk);
}

static int factory_flush(struct block_backend *blk)
{
	(void)blk;
	return 0;
}

static const struct block_ops factory_ops[2] = {
	{ .read = factory_read, .write = factory_write, .get_capacity = factory_capacity, .destroy = factory_destroy },
	{ .read = factory_read, .write = factory_write, .flush = factory_flush, .get_capacity = factory_capacity, .destroy = factory_destroy },
};

static struct block_backend *factory_create(const char *opts)
{
	factory_calls++;
	if (!strcmp(opts, "fail"))
		return ERR_PTR(-VM_EIO);
	unsigned mode = !strcmp(opts, "flush");
	struct block_backend *blk = calloc(1, sizeof(*blk));
	if (!blk)
		return ERR_PTR(-VM_ENOMEM);
	*blk = (struct block_backend){ .name = "factory-test", .ops = &factory_ops[mode], .priv = &factory_destroyed[mode], .readonly = true };
	return blk;
}

static void setup(struct vm_ctx *vm)
{
	memset(vm, 0, sizeof(*vm));
	res_pool_init(&vm->resources);
	INIT_LIST_HEAD(&vm->devices);
	INIT_LIST_HEAD(&vm->io_map.mmio_regions);
	INIT_LIST_HEAD(&vm->io_map.pio_regions);
	INIT_LIST_HEAD(&vm->mem_space.regions);
}

static int queue_realize(struct virtio_device *vdev)
{
	return virtio_device_add_queue_locked(vdev, 8);
}

static uint64_t features(struct virtio_device *vdev)
{
	(void)vdev;
	return 0;
}

static void notify(struct virtio_device *vdev, uint16_t index)
{
	(void)vdev;
	(void)index;
}

static void unbind(struct net_backend *net)
{
	(void)net;
}

static void transport_irq(void *data)
{
	(void)data;
}
static const struct virtio_transport_ops transport = { .notify_queue = transport_irq, .notify_config = transport_irq };

int main(void)
{
	log_init();
	const struct device_desc desc = { .name = "ownership-test", .realize = realize };
	device_register(&desc);
	struct vm_ctx vm;
	setup(&vm);
	CHECK(test_io_init(&vm) == 0);
	struct device *dev = device_alloc(&vm, desc.name);
	CHECK(dev);
	CHECK(device_realize(dev, NULL) == -VM_EIO);
	CHECK(stopped == 1 && released == 1 && !alive && child_released == 1);
	CHECK(dev->state == DEVICE_FAILED && list_empty(&dev->resources.resources));
	CHECK(device_realize(dev, NULL) == -VM_EALREADY);
	/* No explicit destroy needed for either failed or never-added shells. */
	CHECK(device_alloc(&vm, desc.name));
	vm_destroy(&vm);
	CHECK(stopped == 1 && released == 1 && list_empty(&vm.devices));
	const struct block_desc factory = { .name = "factory-test", .create = factory_create };
	block_register(&factory);
	/* The constructor chooses each instance's ops; registration owns no copy. */
	struct block_backend *plain = block_create(&vm.resources, factory.name, "plain");
	struct block_backend *durable = block_create(&vm.resources, factory.name, "flush");
	CHECK(!IS_ERR(plain) && !IS_ERR(durable));
	CHECK(plain->ops == &factory_ops[0] && durable->ops == &factory_ops[1]);
	CHECK(!plain->ops->flush && durable->ops->flush(durable) == 0 && factory_calls == 2);
	res_release_all(&vm.resources);
	CHECK(factory_destroyed[0] == 1 && factory_destroyed[1] == 1);
	/* Constructor errors are returned unchanged, without adopting anything. */
	CHECK(PTR_ERR(block_create(&vm.resources, factory.name, "fail")) == -VM_EIO);
	CHECK(list_empty(&vm.resources.resources) && factory_destroyed[0] == 1 && factory_destroyed[1] == 1);
	/* Failed pool adoption destroys the successfully constructed instance once. */
	fail_at = 2;
	CHECK(PTR_ERR(block_create(&vm.resources, factory.name, "flush")) == -VM_ENOMEM);
	fail_at = 0;
	CHECK(factory_destroyed[1] == 2 && list_empty(&vm.resources.resources));
	char image[] = "/tmp/modvm-factory-XXXXXX";
	int fd = mkstemp(image);
	CHECK(fd >= 0);
	close(fd);
	char opts[256];
	snprintf(opts, sizeof(opts), "path=%s", image);
	/* Both concrete allocations and the ownership record must propagate ENOMEM. */
	for (int n = 1; n <= 3; n++) {
		fail_at = n;
		CHECK(PTR_ERR(block_create(&vm.resources, "linux-file", opts)) == -VM_ENOMEM);
		fail_at = 0;
	}
	CHECK(list_empty(&vm.resources.resources));
	CHECK(unlink(image) == 0);
	/* Every failure inside frontend creation and transport realization is owned. */
	for (int n = 1; n < 14; n++) {
		setup(&vm);
		CHECK(test_io_init(&vm) == 0);
		const struct net_ops netops = { .unbind = unbind, .bind = bind, .get_mac = mac };
		struct net_backend net = { .ops = &netops };
		dev = device_alloc(&vm, "virtio-mmio");
		CHECK(dev);
		struct virtio_mmio_pdata p = { .base = TO_GPA(0x100000) };
		p.irq = irq_alloc(&dev->resources, irq, NULL);
		CHECK(p.irq);
		fail_at = n;
		p.vdev = virtio_net_create(&vm, &net);
		if (p.vdev) {
			int ret = device_realize(dev, &p);
			CHECK(ret == 0 || ret == -VM_ENOMEM);
		}
		fail_at = 0;
		vm_destroy(&vm);
		CHECK(!atomic_load(&net.binding));
	}
	/* An unattached frontend is also reclaimed by the VM owner scope. */
	setup(&vm);
	struct net_ops netops = { .unbind = unbind, .bind = bind, .get_mac = mac };
	struct net_backend net = { .ops = &netops };
	CHECK(virtio_net_create(&vm, &net));
	vm_destroy(&vm);
	const struct device_desc fresh = { .name = "after-duplicates" };
	device_register(&fresh);
	setup(&vm);
	CHECK(device_alloc(&vm, fresh.name));
	/* Invalid mandatory ops fail before publishing callback state. */
	struct net_backend invalid_net = { 0 };
	struct char_backend invalid_char = { 0 };
	CHECK(net_bind_locked(NULL, NULL, NULL, NULL, NULL) == -VM_EINVAL);
	CHECK(net_bind_locked(&invalid_net, NULL, NULL, NULL, NULL) == -VM_EINVAL);
	CHECK(char_bind_locked(&invalid_char, NULL, 64, NULL, NULL, NULL) == -VM_EINVAL);
	/* stop preserves owned storage; destroy reclaims it, with no restart. */
	const struct virtio_device_ops queue_ops = { .realize = queue_realize, .get_features = features, .notify_queue = notify };
	struct virtio_device *vdev = virtio_device_alloc(&vm, 2, &queue_ops, 1);
	CHECK(vdev);
	struct device *parent = device_alloc(&vm, fresh.name);
	CHECK(parent && virtio_device_adopt_locked(parent, vdev, &transport, NULL) == 0);
	CHECK(virtio_device_realize_locked(vdev) == 0 && vdev->nr_vqs == 1);
	struct virtqueue *queue = vdev->vqs[0];
	virtio_device_stop_locked(vdev);
	virtio_device_stop_locked(vdev);
	CHECK(vdev->nr_vqs == 1 && vdev->vqs[0] == queue);
	CHECK(virtio_device_realize_locked(vdev) == -VM_EINVAL);
	CHECK(virtio_device_add_queue_locked(vdev, 8) == -VM_EINVAL);
	vm_destroy(&vm);
	log_destroy();
	puts("automatic ownership, realization rollback and required ops validation passed");
	return 0;
}
