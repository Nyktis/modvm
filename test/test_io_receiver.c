/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/host/mutex.h>
#include <modvm/io/ctx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modvm/io/receiver.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			abort();                                        \
		}                                                       \
	} while (0)

struct observer {
	struct io_ctx *io_ctx;
	struct io_receiver *receiver;
	unsigned received, notified;
};

static void consumed(void *data)
{
	(void)data;
	/* Closing inside receive must prevent rearming native input. */
	CHECK(0);
}

static void receive(void *data, const uint8_t *bytes, size_t length)
{
	struct observer *o = data;
	CHECK(length == 3 && !memcmp(bytes, "abc", 3));
	o->received++;
	io_receiver_close_locked(o->receiver);
	io_receiver_destroy(o->receiver);
	o->receiver = NULL;
	io_ctx_stop(o->io_ctx);
}

static void notify(void *data, int error)
{
	struct observer *o = data;
	CHECK(error == -VM_EIO && !o->received);
	o->notified++;
	io_receiver_set_enabled_locked(o->receiver, true);
}

int main(void)
{
	CHECK(!log_init());
	for (unsigned mode = 0; mode < 3; mode++) {
		struct observer o = { .io_ctx = io_ctx_create(NULL, NULL) };
		CHECK(!IS_ERR(o.io_ctx));
		struct host_mutex *lock = io_ctx_get_device_lock(o.io_ctx);
		host_mutex_lock(lock);
		CHECK(PTR_ERR(io_receiver_create_locked(o.io_ctx, 0, receive, notify, &o)) == -VM_EINVAL);
		o.receiver = io_receiver_create_locked(o.io_ctx, 3, receive, notify, &o);
		CHECK(!IS_ERR(o.receiver));
		uint8_t bytes[] = "abcd";
		CHECK(io_receiver_submit(o.receiver, bytes, 4, consumed, NULL) == -VM_EINVAL);
		CHECK(!io_receiver_submit(o.receiver, bytes, 3, consumed, NULL));
		/* The producer may reuse its buffer as soon as submit returns. */
		memset(bytes, 0, sizeof(bytes));
		if (mode == 1)
			io_receiver_set_enabled_locked(o.receiver, false);
		io_receiver_notify(o.receiver, -VM_EIO);
		if (mode == 2) {
			io_receiver_close_locked(o.receiver);
			CHECK(io_receiver_submit(o.receiver, bytes, 3, consumed, NULL) == -VM_ESHUTDOWN);
			io_receiver_notify(o.receiver, -VM_EIO);
			io_receiver_destroy(o.receiver);
			io_ctx_stop(o.io_ctx);
		}
		host_mutex_unlock(lock);
		CHECK(!io_ctx_run(o.io_ctx));
		CHECK(o.received == (mode != 2) && o.notified == (mode == 1));
		io_ctx_destroy(o.io_ctx);
	}
	log_destroy();
	return 0;
}
