/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/errno.h>
#include <modvm/io/char.h>
#include <modvm/io/receiver.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#include <modvm/util/res_pool.h>

#undef pr_fmt
#define pr_fmt(fmt) "char: " fmt

#define MAX_CHAR_DESCS 16

static const struct char_desc *char_descs[MAX_CHAR_DESCS];
static unsigned int nr_char_descs;

/**
 * char_register - register a character backend description
 * @desc: the backend factory description to register
 */
void char_register(const struct char_desc *desc)
{
	if (!desc || !desc->name || !*desc->name || !desc->create)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_char_descs; i++) {
		if (!strcmp(char_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_char_descs == MAX_CHAR_DESCS)
		panic(pr_fmt("registry full\n"));

	char_descs[nr_char_descs++] = desc;
}

static void backend_cleanup(void *data)
{
	struct char_backend *backend = data;

	backend->ops->destroy(backend);
}

struct char_backend *char_create(struct res_pool *owner, const char *name, const char *opts)
{
	struct char_backend *backend;
	int ret;

	if (!owner || !name)
		return ERR_PTR(-VM_EINVAL);

	for (unsigned int i = 0; i < nr_char_descs; i++) {
		const struct char_desc *desc = char_descs[i];

		if (strcmp(desc->name, name))
			continue;

		backend = desc->create(opts);
		if (IS_ERR(backend))
			return backend;

		/* A successful internal constructor must return a usable, destructible object. */
		BUG_ON(!backend || !backend->ops || !backend->ops->write || !backend->ops->bind || !backend->ops->unbind || !backend->ops->destroy);
		atomic_init(&backend->binding, NULL);

		ret = res_add_action_or_reset(owner, backend_cleanup, backend);
		if (ret < 0)
			return ERR_PTR(ret);
		return backend;
	}

	pr_err("driver '%s' not found\n", name);
	return ERR_PTR(-VM_ENOENT);
}

int char_bind_locked(struct char_backend *dev, struct io_ctx *io, size_t rx_limit, char_rx_cb_t cb, void (*notify)(void *, int), void *data)
{
	struct io_receiver *b, *expected = NULL;
	int ret;

	if (!dev || !io || !cb || !rx_limit)
		return -VM_EINVAL;

	b = io_receiver_create_locked(io, rx_limit, cb, notify, data);
	if (IS_ERR(b))
		return PTR_ERR(b);

	if (!atomic_compare_exchange_strong(&dev->binding, &expected, b)) {
		ret = -VM_EBUSY;
		goto fail;
	}

	ret = dev->ops->bind(dev, b, rx_limit);
	if (!ret)
		return 0;

	atomic_store(&dev->binding, NULL);

fail:
	io_receiver_close_locked(b);
	io_receiver_destroy(b);
	return ret;
}

void char_unbind_locked(struct char_backend *dev)
{
	struct io_receiver *b = dev ? atomic_load(&dev->binding) : NULL;

	if (!b)
		return;

	io_receiver_close_locked(b);

	dev->ops->unbind(dev);
	atomic_store(&dev->binding, NULL);
	io_receiver_destroy(b);
}

int char_pause_rx_locked(struct char_backend *dev)
{
	struct io_receiver *b = atomic_load(&dev->binding);

	if (!b)
		return -VM_ENOTCONN;

	io_receiver_set_enabled_locked(b, false);
	return 0;
}

int char_resume_rx_locked(struct char_backend *dev)
{
	struct io_receiver *b = atomic_load(&dev->binding);

	if (!b)
		return -VM_ENOTCONN;

	io_receiver_set_enabled_locked(b, true);
	return 0;
}
