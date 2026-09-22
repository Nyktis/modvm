/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_NET_H
#define MODVM_IO_NET_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

struct res_pool;
struct io_receiver;
struct io_ctx;

/* Maximum Ethernet frame bytes accepted by the network interface, excluding FCS. */
#define NET_MAX_FRAME_SIZE 65550

/* Synchronous transfer lengths must not exceed PTRDIFF_MAX. */
struct net_backend;

/**
 * typedef net_rx_cb_t - callback for incoming network frames
 * @data: pointer to the frontend device context closure
 * @buf: borrowed Ethernet frame, valid only until the callback returns
 * @len: size of the ethernet frame in bytes
 */
typedef void (*net_rx_cb_t)(void *data, const uint8_t *buf, size_t len);

/**
 * struct net_ops - operations for host network backends
 * @write: inject an ethernet frame from the guest into the host network
 * @bind: start native I/O for the binding; failure must stop all native users
 * @unbind: stop and join all native users of the binding
 * @get_mac: retrieve the hardware MAC address assigned to this backend
 * @destroy: free the instance and its private resources; also usable for
 *           constructor rollback after enough state exists for cleanup
 *
 * Runtime I/O and binding operations run under the VM I/O lock. Destruction
 * requires unbinding and stopping all users; constructor rollback has no
 * concurrent users.
 *
 * All callbacks are mandatory.
 */
struct net_ops {
	ptrdiff_t (*write)(struct net_backend *net, const uint8_t *buf, size_t len);
	int (*bind)(struct net_backend *net, struct io_receiver *binding);
	void (*unbind)(struct net_backend *net);
	int (*get_mac)(struct net_backend *net, uint8_t mac_out[6]);
	void (*destroy)(struct net_backend *net);
};

/**
 * struct net_backend - host network backend instance
 * @name: backend label for diagnostics (e.g., "linux-tap"); need not be unique
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., file descriptors)
 * @binding: exclusively owned delivery binding; NULL when unbound
 */
struct net_backend {
	const char *name;
	const struct net_ops *ops;
	void *priv;

	_Atomic(struct io_receiver *) binding;
};

/**
 * struct net_desc - network backend driver description
 * @name: name unique within the network driver registry, used by net_create()
 * @create: create an instance from opts, or return ERR_PTR(-VM_E*) on failure
 *
 * Before returning an instance, create must set its ops and initialize its
 * private state. All required callbacks, including destroy, must be present.
 * Network reception must remain unbound until bind is called.
 * On failure, create must free any resources it allocated.
 *
 * net_create() registers the returned instance with the caller's resource
 * pool. If registration fails, it calls the instance's destroy callback.
 */
struct net_desc {
	const char *name;
	struct net_backend *(*create)(const char *opts);
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void net_register(const struct net_desc *desc);

/* Returns an instance managed by owner, or ERR_PTR(-VM_E*).
 * Stop all users before releasing owner; do not destroy the instance separately.
 * If cleanup registration fails, destroys the instance before returning the error.
 */
struct net_backend *net_create(struct res_pool *owner, const char *name, const char *opts);

/* Binding operations and frontend callbacks require the execution context's lock.
 * Callbacks may unbind, but destruction/rebinding must wait until return.
 * Native producers never invoke frontend callbacks directly.
 */
int net_bind_locked(struct net_backend *net, struct io_ctx *io, net_rx_cb_t cb, void (*notify)(void *, int), void *data);
void net_unbind_locked(struct net_backend *net);

#endif /* MODVM_IO_NET_H */
