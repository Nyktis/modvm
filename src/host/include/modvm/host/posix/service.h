/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_POSIX_SERVICE_H
#define MODVM_HOST_POSIX_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <modvm/util/compiler.h>

struct __ctx_lock_type(host_service) host_posix_service;

enum {
	HOST_POSIX_READ = 1,
	HOST_POSIX_WRITE = 2,
	HOST_POSIX_ERROR = 4,
};

/* Sources are fixed for one service lifetime. Disabled sources are not polled.
 * Callbacks run under the service lock, never under a VM/device lock.
 * A callback may update sources but must not destroy its own service.
 */
struct host_posix_source {
	int fd;
	uint32_t events;
	void (*ready)(struct host_posix_service *, uint32_t, void *);
	void *data;
};

/* Copies sources, borrows their fds and callback contexts; starts a worker.
 * fatal reports a wait failure once on the worker, under the service lock.
 */
struct host_posix_service *host_posix_service_create(const struct host_posix_source *sources, size_t count, void (*fatal)(void *, int), void *data);

void host_posix_service_lock(struct host_posix_service *service) __acquires(service);
void host_posix_service_unlock(struct host_posix_service *service) __releases(service);

void host_posix_service_update_locked(struct host_posix_service *service, size_t source, uint32_t events);

/* Stops and joins the worker. No lock held by the caller may be needed by callbacks.
 * After return, no native callback or borrowed fd/context access remains.
 */
void host_posix_service_destroy(struct host_posix_service *service);

#endif /* MODVM_HOST_POSIX_SERVICE_H */
