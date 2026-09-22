/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_BLOCK_H
#define MODVM_IO_BLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct res_pool;

/* Synchronous transfer lengths must not exceed PTRDIFF_MAX. */
struct block_backend;

/**
 * struct block_ops - operations for host block storage backends
 * @read: read at a byte offset; return bytes transferred or negative project error
 * @write: write at a byte offset; return bytes transferred or negative project error
 * @flush: optional durability operation; return 0 on success or negative project error
 *         NULL means flush is unsupported
 * @get_capacity: retrieve the total size of the storage medium in bytes
 * @destroy: free the instance and its private resources; also usable for
 *           constructor rollback after enough state exists for cleanup
 *
 * All callbacks except flush are mandatory.
 */
/* Transfers are synchronous; buffers are borrowed only for the call. Short
 * transfers are allowed. Errors may follow partial I/O; do not blindly retry.
 * Callers serialize operations and stop all users before destruction. */
struct block_ops {
	ptrdiff_t (*read)(struct block_backend *blk, void *buf, size_t count, uint64_t offset);
	ptrdiff_t (*write)(struct block_backend *blk, const void *buf, size_t count, uint64_t offset);
	int (*flush)(struct block_backend *blk);
	uint64_t (*get_capacity)(struct block_backend *blk);
	void (*destroy)(struct block_backend *blk);
};

/**
 * struct block_backend - host block storage backend instance
 * @name: backend label for diagnostics; need not be unique
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., file descriptors)
 * @readonly: true when guest writes must be rejected
 */
struct block_backend {
	const char *name;
	const struct block_ops *ops;
	void *priv;
	bool readonly;
};

/**
 * struct block_desc - block backend driver description
 * @name: name unique within the block driver registry, used by block_create()
 * @create: create an instance from opts, or return ERR_PTR(-VM_E*) on failure
 *
 * Before returning an instance, create must set its ops and initialize its
 * private state. All required callbacks, including destroy, must be present.
 * On failure, create must free any resources it allocated.
 *
 * block_create() registers the returned instance with the caller's resource
 * pool. If registration fails, it calls the instance's destroy callback.
 */
struct block_desc {
	const char *name;
	struct block_backend *(*create)(const char *opts);
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void block_register(const struct block_desc *desc);

/* Returns an instance managed by owner, or ERR_PTR(-VM_E*).
 * Stop all users before releasing owner; do not destroy the instance separately.
 * If cleanup registration fails, destroys the instance before returning the error.
 */
struct block_backend *block_create(struct res_pool *owner, const char *name, const char *opts);

#endif /* MODVM_IO_BLOCK_H */
