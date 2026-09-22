/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/errno.h>
#include <modvm/io/block.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#include <modvm/util/res_pool.h>

#undef pr_fmt
#define pr_fmt(fmt) "block: " fmt

#define MAX_BLOCK_DESCS 16

static const struct block_desc *block_descs[MAX_BLOCK_DESCS];
static unsigned int nr_block_descs;

/**
 * block_register - register a block backend description
 * @desc: the backend factory description to register
 */
void block_register(const struct block_desc *desc)
{
	if (!desc || !desc->name || !*desc->name || !desc->create)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_block_descs; i++) {
		if (!strcmp(block_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_block_descs == MAX_BLOCK_DESCS)
		panic(pr_fmt("registry full\n"));

	block_descs[nr_block_descs++] = desc;
}

static void backend_cleanup(void *data)
{
	struct block_backend *backend = data;

	backend->ops->destroy(backend);
}

struct block_backend *block_create(struct res_pool *owner, const char *name, const char *opts)
{
	struct block_backend *backend;
	int ret;

	if (!owner || !name)
		return ERR_PTR(-VM_EINVAL);

	for (unsigned int i = 0; i < nr_block_descs; i++) {
		const struct block_desc *desc = block_descs[i];

		if (strcmp(desc->name, name))
			continue;

		backend = desc->create(opts);
		if (IS_ERR(backend))
			return backend;

		/* A successful internal constructor must return a usable, destructible object. */
		BUG_ON(!backend || !backend->ops || !backend->ops->read || !backend->ops->write || !backend->ops->get_capacity || !backend->ops->destroy);

		ret = res_add_action_or_reset(owner, backend_cleanup, backend);
		if (ret < 0)
			return ERR_PTR(ret);
		return backend;
	}

	pr_err("driver '%s' not found\n", name);
	return ERR_PTR(-VM_ENOENT);
}
