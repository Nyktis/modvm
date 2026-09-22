/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTIL_RES_POOL_H
#define MODVM_UTIL_RES_POOL_H

#include <stdbool.h>
#include <modvm/util/stddef.h>
#include <modvm/util/list.h>

/**
 * struct res_pool - resource pool with reverse-order cleanup
 * @resources: list head tracking all allocated resource nodes
 * @releasing: rejects acquisitions and transfers during teardown
 *
 * Embed this structure into domain-specific objects to grant them
 * automated resource tracking and deterministic teardown capabilities.
 */
struct res_pool {
	struct list_head resources;
	bool releasing;
};

typedef void (*res_cleanup_fn)(void *data);
/* Pools have one owner; allocation and teardown require external serialization.
 * Cleanup callbacks must not add resources to the pool being destroyed.
 * zalloc supports fundamental alignment up to max_align_t, not over-aligned types.
 */
void *res_zalloc(struct res_pool *pool, size_t size);
int res_add_action(struct res_pool *pool, res_cleanup_fn fn, void *data);
/* On failure, immediately cleans up, except EEXIST: the existing action already owns data. */
int res_add_action_or_reset(struct res_pool *pool, res_cleanup_fn fn, void *data);
/* Transfer an existing cleanup obligation without allocating. cancel removes it
 * without running it; these operations are intended for explicit ownership moves. */
int res_transfer_action(struct res_pool *from, struct res_pool *to, res_cleanup_fn fn, void *data);
int res_cancel_action(struct res_pool *pool, res_cleanup_fn fn, void *data);
void res_pool_init(struct res_pool *pool);
void res_release_all(struct res_pool *pool);

#endif /* MODVM_UTIL_RES_POOL_H */
