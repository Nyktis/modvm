/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>

#include <modvm/core/device.h>
#include <modvm/core/vm.h>
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>
#include <modvm/util/log.h>
#include <modvm/util/res_pool.h>

#undef pr_fmt
#define pr_fmt(fmt) "device: " fmt

#define MAX_DEVICE_DESCS 64

static const struct device_desc *device_descs[MAX_DEVICE_DESCS];
static unsigned int nr_device_descs;

/**
 * device_register - register a device implementation description
 * @desc: the device implementation description to register
 */
void device_register(const struct device_desc *desc)
{
	if (!desc || !desc->name || !*desc->name)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_device_descs; i++) {
		if (!strcmp(device_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_device_descs == MAX_DEVICE_DESCS)
		panic(pr_fmt("registry full\n"));

	device_descs[nr_device_descs++] = desc;
}

/**
 * device_alloc_locked - allocate a device before realization
 * @ctx: the parent machine context
 * @name: the string identifier of the requested device class
 *
 * Scans the static registry for the specified device implementation and
 * initializes the base structure, including its private resource pool.
 *
 * Return: pointer to the initialized device shell, or NULL on failure.
 */
struct device *device_alloc_locked(struct vm_ctx *ctx, const char *name)
{
	struct device *dev;
	const struct device_desc *desc = NULL;

	if (WARN_ON(!ctx || !name))
		return NULL;

	for (unsigned int i = 0; i < nr_device_descs; i++) {
		if (!strcmp(device_descs[i]->name, name)) {
			desc = device_descs[i];
			break;
		}
	}

	if (!desc) {
		pr_err("device class '%s' not found in registry\n", name);
		return NULL;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->desc = desc;
	dev->ctx = ctx;

	INIT_LIST_HEAD(&dev->node);
	res_pool_init(&dev->resources);

	list_add_tail(&dev->node, &ctx->devices);
	return dev;
}

static void device_destroy_children(struct device *dev)
{
	/* Children must release their references before their parent disappears. */
	for (;;) {
		struct device *child, *found = NULL;
		list_for_each_entry(child, &dev->ctx->devices, node)
		{
			if (child->parent == dev) {
				found = child;
				break;
			}
		}
		if (!found)
			break;
		device_destroy_locked(found);
	}
}

/**
 * device_realize_locked - realize an allocated device
 * @dev: the device shell to instantiate
 * @pdata: hardware routing data from the motherboard (platform data)
 *
 * Invokes the specific initialization routine defined by the device class.
 * The VM owns the shell from allocation, including failed initialization.
 *
 * Return: 0 on success, or a negative error code.
 */
int device_realize_locked(struct device *dev, void *pdata)
{
	int ret;

	if (WARN_ON(!dev || !dev->desc || !dev->ctx))
		return -VM_EINVAL;

	if (dev->state != DEVICE_NEW) {
		return -VM_EALREADY;
	}
	dev->state = DEVICE_INITIALIZING;
	if (dev->desc->realize) {
		ret = dev->desc->realize(dev, pdata);
		if (ret < 0) {
			dev->state = DEVICE_FAILED;
			device_destroy_children(dev);
			if (dev->ops && dev->ops->stop)
				dev->ops->stop(dev);
			res_release_all(&dev->resources);
			dev->ops = NULL;
			dev->priv = NULL;
			dev->parent = NULL;
			return ret;
		}
	}

	dev->state = DEVICE_ACTIVE;
	return 0;
}

/**
 * device_destroy_locked - drop the device and release all associated resources
 * @dev: the device to destroy
 *
 * Releases the device resource pool which safely unwinds all allocations,
 * interrupts, and bus mappings requested by this device.
 */
void device_destroy_locked(struct device *dev)
{
	if (WARN_ON(!dev))
		return;

	device_destroy_children(dev);
	dev->state = DEVICE_DESTROYING;
	list_del(&dev->node);
	if (dev->ops && dev->ops->stop)
		dev->ops->stop(dev);

	res_release_all(&dev->resources);
	free(dev);
}

struct device *device_alloc(struct vm_ctx *ctx, const char *name)
{
	if (!ctx)
		return NULL;
	host_mutex_lock(ctx->io_map.io_lock);
	struct device *dev = device_alloc_locked(ctx, name);
	host_mutex_unlock(ctx->io_map.io_lock);
	return dev;
}

int device_realize(struct device *dev, void *pdata)
{
	if (!dev || !dev->ctx)
		return -VM_EINVAL;
	host_mutex_lock(dev->ctx->io_map.io_lock);
	int ret = device_realize_locked(dev, pdata);
	host_mutex_unlock(dev->ctx->io_map.io_lock);
	return ret;
}

void device_destroy(struct device *dev)
{
	if (!dev)
		return;
	struct host_mutex *lock = dev->ctx->io_map.io_lock;
	host_mutex_lock(lock);
	device_destroy_locked(dev);
	host_mutex_unlock(lock);
}
