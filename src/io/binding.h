/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_BINDING_H
#define MODVM_IO_BINDING_H

#include <modvm/io/ctx.h>

struct io_binding;

enum io_work_kind {
	IO_DATA,
	IO_CONTROL,
};

/* Create/close/set_enabled and delivery callbacks require the context's device lock.
 * close cancels pending deliveries, and may be called from a delivery callback.
 * It does not wait for native producers: stop/join those before dropping the
 * owner's reference. An active callback keeps its binding alive.
 */
struct io_binding *io_binding_create_locked(struct io_ctx *io);
void io_binding_close_locked(struct io_binding *binding);

/* Drop one reference; the final reference frees the closed binding. The owner
 * must close and stop all producers before put; queued/running work holds its
 * own references, released after cancellation/completion even while open. */
void io_binding_put(struct io_binding *binding);

void io_binding_set_enabled_locked(struct io_binding *binding, bool enabled);
bool io_binding_is_open(struct io_binding *binding);

/* Thread-safe. Takes ownership of data on every return path. release must not
 * touch device state; it runs after delivery or cancellation, possibly on a producer.
 * Control work (exit/errors) remains runnable while data delivery is paused.
 * run executes only while the binding is open, under the device lock.
 */
int io_binding_post(struct io_binding *binding, void (*run)(void *), void *data, void (*release)(void *), enum io_work_kind kind);

#endif /* MODVM_IO_BINDING_H */
