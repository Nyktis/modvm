/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_CTX_H
#define MODVM_IO_CTX_H

#include <stdbool.h>

struct host_mutex;
struct io_ctx;

/* The context owns device serialization and queued deliveries. It has one runner.
 * Stop is irreversible. Close every binding and join the runner before destroy.
 */
/* fatal is optional and may run on a producer thread; it must be thread-safe. */
struct io_ctx *io_ctx_create(void (*fatal)(void *, int), void *data);

/* Returns the borrowed device mutex without acquiring it. */
struct host_mutex *io_ctx_get_device_lock(struct io_ctx *io);
int io_ctx_run(struct io_ctx *io);
void io_ctx_fail(struct io_ctx *io, int error);
void io_ctx_stop(struct io_ctx *io);
void io_ctx_destroy(struct io_ctx *io);

#endif /* MODVM_IO_CTX_H */
