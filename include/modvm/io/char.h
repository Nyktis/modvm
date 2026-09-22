/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_CHAR_H
#define MODVM_IO_CHAR_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

struct res_pool;
struct io_receiver;
struct io_ctx;
struct char_backend;

/**
 * typedef char_rx_cb_t - callback for incoming data stream
 * @data: pointer to the frontend device context closure
 * @buf: borrowed bytes, valid only until the callback returns
 * @len: number of bytes, at most the binding rx_limit; the receiver accepts all bytes
 */
typedef void (*char_rx_cb_t)(void *data, const uint8_t *buf, size_t len);

/**
 * struct char_ops - operations for character device backends
 * @write: accept all bytes (possibly buffered), returning 0; negative project error may
 *         follow partial output and must not be retried blindly. No synchronous notify.
 * @bind: start native I/O for the binding; failure must stop all native users
 * @unbind: stop and join native I/O, then discard buffered output
 * @destroy: free the instance and its private resources; also usable for
 *           constructor rollback after enough state exists for cleanup
 *
 * Runtime I/O and binding operations run under the VM I/O lock. Destruction
 * requires unbinding and stopping all users; constructor rollback has no
 * concurrent users.
 *
 * All callbacks are mandatory.
 */
struct char_ops {
	int (*write)(struct char_backend *dev, const uint8_t *buf, size_t len);
	int (*bind)(struct char_backend *dev, struct io_receiver *binding, size_t rx_limit);
	void (*unbind)(struct char_backend *dev);
	void (*destroy)(struct char_backend *dev);
};

/**
 * struct char_backend - host character I/O backend instance
 * @name: backend label for diagnostics; need not be unique
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., termios states)
 * @binding: exclusively owned delivery binding; NULL when unbound
 */
struct char_backend {
	const char *name;
	const struct char_ops *ops;
	void *priv;

	_Atomic(struct io_receiver *) binding;
};

/**
 * struct char_desc - character backend driver description
 * @name: name unique within the character driver registry, used by char_create()
 * @create: create an instance from opts, or return ERR_PTR(-VM_E*) on failure
 *
 * Before returning an instance, create must set its ops and initialize its
 * private state. All required callbacks, including destroy, must be present.
 * Console input must remain unbound until bind is called.
 * On failure, create must free any resources it allocated.
 *
 * char_create() registers the returned instance with the caller's resource
 * pool. If registration fails, it calls the instance's destroy callback.
 */
struct char_desc {
	const char *name;
	struct char_backend *(*create)(const char *opts);
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void char_register(const struct char_desc *desc);

/* Returns an instance managed by owner, or ERR_PTR(-VM_E*).
 * Stop all users before releasing owner; do not destroy the instance separately.
 * If cleanup registration fails, destroys the instance before returning the error.
 */
struct char_backend *char_create(struct res_pool *owner, const char *name, const char *opts);

/* Binding operations and frontend callbacks require the execution context's lock.
 * Callbacks may unbind or pause, but destruction/rebinding must wait until return.
 * Native producers never invoke frontend callbacks directly.
 */
int char_bind_locked(struct char_backend *dev, struct io_ctx *io, size_t rx_limit, char_rx_cb_t cb, void (*notify)(void *, int), void *data);
void char_unbind_locked(struct char_backend *dev);
int char_pause_rx_locked(struct char_backend *dev);
int char_resume_rx_locked(struct char_backend *dev);

#endif /* MODVM_IO_CHAR_H */
