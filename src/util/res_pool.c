/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/bug.h>

struct res_node {
	struct list_head node;
	struct res_pool *pool;
	res_cleanup_fn action;
	void *action_data;
	_Alignas(max_align_t) unsigned char data[];
};

void res_pool_init(struct res_pool *pool)
{
	INIT_LIST_HEAD(&pool->resources);
	pool->releasing = false;
}

void *res_zalloc(struct res_pool *pool, size_t size)
{
	if (!pool || pool->releasing || !size || size > SIZE_MAX - sizeof(struct res_node))
		return NULL;
	struct res_node *node = calloc(1, sizeof(*node) + size);
	if (!node)
		return NULL;
	node->pool = pool;
	list_add_tail(&node->node, &pool->resources);
	return node->data;
}

int res_add_action(struct res_pool *pool, res_cleanup_fn fn, void *data)
{
	if (!pool || !fn)
		return -VM_EINVAL;
	if (pool->releasing)
		return -VM_ESHUTDOWN;
	struct list_head *pos;
	list_for_each(pos, &pool->resources)
	{
		struct res_node *node = list_entry(pos, struct res_node, node);
		if (node->action == fn && node->action_data == data)
			return -VM_EEXIST;
	}
	struct res_node *node = calloc(1, sizeof(*node));
	if (!node)
		return -VM_ENOMEM;
	node->pool = pool;
	node->action = fn;
	node->action_data = data;
	list_add_tail(&node->node, &pool->resources);
	return 0;
}

int res_add_action_or_reset(struct res_pool *pool, res_cleanup_fn fn, void *data)
{
	int ret = res_add_action(pool, fn, data);
	/* An existing obligation already owns data; do not destroy it twice. */
	if (ret < 0 && ret != -VM_EEXIST && fn)
		fn(data);
	return ret;
}

int res_transfer_action(struct res_pool *from, struct res_pool *to, res_cleanup_fn fn, void *data)
{
	if (!from || !to || !fn)
		return -VM_EINVAL;
	if (from->releasing || to->releasing)
		return -VM_ESHUTDOWN;
	struct res_node *found = NULL;
	struct list_head *pos;
	list_for_each(pos, &from->resources)
	{
		struct res_node *node = list_entry(pos, struct res_node, node);
		if (node->action == fn && node->action_data == data) {
			found = node;
			break;
		}
	}
	if (!found)
		return -VM_ENOENT;
	if (WARN_ON(found->pool != from))
		return -VM_EINVAL;
	if (from == to)
		return 0;
	list_for_each(pos, &to->resources)
	{
		struct res_node *node = list_entry(pos, struct res_node, node);
		if (node->action == fn && node->action_data == data)
			return -VM_EEXIST;
	}
	list_del(&found->node);
	found->pool = to;
	list_add_tail(&found->node, &to->resources);
	return 0;
}

int res_cancel_action(struct res_pool *pool, res_cleanup_fn fn, void *data)
{
	if (!pool || !fn)
		return -VM_EINVAL;
	struct list_head *pos;
	list_for_each(pos, &pool->resources)
	{
		struct res_node *node = list_entry(pos, struct res_node, node);
		if (node->action != fn || node->action_data != data)
			continue;
		if (WARN_ON(node->pool != pool))
			return -VM_EINVAL;
		list_del(&node->node);
		node->pool = NULL;
		free(node);
		return 0;
	}
	return -VM_ENOENT;
}

void res_release_all(struct res_pool *pool)
{
	if (!pool || pool->releasing)
		return;
	pool->releasing = true;
	while (!list_empty(&pool->resources)) {
		struct res_node *node = list_last_entry(&pool->resources, struct res_node, node);
		BUG_ON(node->pool != pool);
		list_del(&node->node);
		node->pool = NULL;
		if (node->action)
			node->action(node->action_data);
		free(node);
	}
	pool->releasing = false;
}
