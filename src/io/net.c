/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/errno.h>
#include <modvm/io/net.h>
#include <modvm/io/receiver.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#include <modvm/util/res_pool.h>

#undef pr_fmt
#define pr_fmt(fmt) "net: " fmt

#define MAX_NET_DESCS 16

static const struct net_desc *net_descs[MAX_NET_DESCS];
static unsigned int nr_net_descs;

/**
 * net_register - register a net backend description
 * @desc: the backend factory description to register
 */
void net_register(const struct net_desc *desc)
{
	if (!desc || !desc->name || !*desc->name || !desc->create)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_net_descs; i++) {
		if (!strcmp(net_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_net_descs == MAX_NET_DESCS)
		panic(pr_fmt("registry full\n"));

	net_descs[nr_net_descs++] = desc;
}

static void backend_cleanup(void *data)
{
	struct net_backend *backend = data;

	backend->ops->destroy(backend);
}

struct net_backend *net_create(struct res_pool *owner, const char *name, const char *opts)
{
	struct net_backend *backend;
	int ret;

	if (!owner || !name)
		return ERR_PTR(-VM_EINVAL);

	for (unsigned int i = 0; i < nr_net_descs; i++) {
		const struct net_desc *desc = net_descs[i];

		if (strcmp(desc->name, name))
			continue;

		backend = desc->create(opts);
		if (IS_ERR(backend))
			return backend;

		/* A successful internal constructor must return a usable, destructible object. */
		BUG_ON(!backend || !backend->ops || !backend->ops->write || !backend->ops->bind || !backend->ops->unbind || !backend->ops->get_mac || !backend->ops->destroy);
		atomic_init(&backend->binding, NULL);

		ret = res_add_action_or_reset(owner, backend_cleanup, backend);
		if (ret < 0)
			return ERR_PTR(ret);
		return backend;
	}

	pr_err("driver '%s' not found\n", name);
	return ERR_PTR(-VM_ENOENT);
}

int net_bind_locked(struct net_backend *net, struct io_ctx *io, net_rx_cb_t cb, void (*notify)(void *, int), void *data)
{
	struct io_receiver *b, *expected = NULL;
	int ret;

	if (!net || !io || !cb)
		return -VM_EINVAL;

	b = io_receiver_create_locked(io, NET_MAX_FRAME_SIZE, cb, notify, data);
	if (IS_ERR(b))
		return PTR_ERR(b);

	if (!atomic_compare_exchange_strong(&net->binding, &expected, b)) {
		ret = -VM_EBUSY;
		goto fail;
	}

	ret = net->ops->bind(net, b);
	if (!ret)
		return 0;

	atomic_store(&net->binding, NULL);

fail:
	io_receiver_close_locked(b);
	io_receiver_destroy(b);
	return ret;
}

void net_unbind_locked(struct net_backend *net)
{
	struct io_receiver *b = net ? atomic_load(&net->binding) : NULL;

	if (!b)
		return;

	io_receiver_close_locked(b);

	net->ops->unbind(net);
	atomic_store(&net->binding, NULL);
	io_receiver_destroy(b);
}
