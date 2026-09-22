/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IO_RECEIVER_H
#define MODVM_IO_RECEIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct io_ctx;

struct io_receiver;

/* The bytes are borrowed until return. The receiver accepts the whole batch. */
typedef void (*io_receive_fn)(void *data, const uint8_t *bytes, size_t length);

/* Create while holding the execution context's lock. The receiver owns its
 * delivery binding and borrows callback data. limit bounds each copied batch.
 * notify is optional: zero requests exit, a negative project error reports failure.
 * Returns an owned receiver or ERR_PTR(-VM_E*).
 */
struct io_receiver *io_receiver_create_locked(struct io_ctx *io, size_t limit, io_receive_fn receive, void (*notify)(void *, int), void *data);

/* Cancel queued deliveries under the device lock; may be called by a callback.
 * Stop/join all native producers after close and before destroy. An active
 * callback may finish, but cannot rearm its producer after close.
 */
void io_receiver_close_locked(struct io_receiver *receiver);
void io_receiver_destroy(struct io_receiver *receiver);
void io_receiver_set_enabled_locked(struct io_receiver *receiver, bool enabled);

/* Native producer entry points. Bytes are copied before return. consumed runs
 * under the device lock after delivery, only if the binding is still open.
 * Producers must bound in-flight deliveries and outlive their consumed callbacks.
 * Notifications remain runnable while data delivery is paused.
 */
int io_receiver_submit(struct io_receiver *receiver, const uint8_t *buf, size_t len, void (*consumed)(void *), void *data);
void io_receiver_notify(struct io_receiver *receiver, int error);

#endif /* MODVM_IO_RECEIVER_H */
