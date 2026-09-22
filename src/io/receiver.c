/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>

#include <modvm/io/ctx.h>
#include <modvm/io/receiver.h>
#include <modvm/util/err.h>

#include "binding.h"

struct io_receiver {
	struct io_ctx *io_ctx;
	struct io_binding *scope;
	io_receive_fn receive;
	void (*notify)(void *, int);
	void *data;
	size_t limit;
};

struct io_delivery {
	struct io_binding *scope;
	io_receive_fn receive;
	void (*notify)(void *, int);
	void *data;
	void (*consumed)(void *);
	void *producer;
	int error;
	size_t len;
	uint8_t bytes[];
};

static void deliver(void *data)
{
	struct io_delivery *d = data;

	if (d->receive)
		d->receive(d->data, d->bytes, d->len);
	else if (d->notify)
		d->notify(d->data, d->error);
	if (d->consumed && io_binding_is_open(d->scope))
		d->consumed(d->producer);
}

struct io_receiver *io_receiver_create_locked(struct io_ctx *io, size_t limit, io_receive_fn receive, void (*notify)(void *, int), void *data)
{
	struct io_receiver *r;

	if (!io || !limit || !receive)
		return ERR_PTR(-VM_EINVAL);

	r = calloc(1, sizeof(*r));
	if (!r)
		return ERR_PTR(-VM_ENOMEM);

	r->scope = io_binding_create_locked(io);
	if (IS_ERR(r->scope)) {
		int ret = PTR_ERR(r->scope);

		free(r);
		return ERR_PTR(ret);
	}

	r->io_ctx = io;
	r->limit = limit;
	r->receive = receive;
	r->notify = notify;
	r->data = data;
	return r;
}

void io_receiver_close_locked(struct io_receiver *r)
{
	io_binding_close_locked(r->scope);
}

void io_receiver_destroy(struct io_receiver *r)
{
	io_binding_put(r->scope);
	free(r);
}

void io_receiver_set_enabled_locked(struct io_receiver *r, bool enabled)
{
	io_binding_set_enabled_locked(r->scope, enabled);
}

int io_receiver_submit(struct io_receiver *b, const uint8_t *buf, size_t len, void (*consumed)(void *), void *data)
{
	struct io_delivery *d;
	int ret;

	if (!buf || !len || len > b->limit || len > SIZE_MAX - sizeof(struct io_delivery))
		return -VM_EINVAL;

	d = malloc(sizeof(*d) + len);
	if (!d) {
		io_ctx_fail(b->io_ctx, -VM_ENOMEM);
		return -VM_ENOMEM;
	}

	*d = (struct io_delivery){ .scope = b->scope, .receive = b->receive, .data = b->data, .consumed = consumed, .producer = data, .len = len };
	memcpy(d->bytes, buf, len);

	ret = io_binding_post(b->scope, deliver, d, free, IO_DATA);
	if (ret < 0 && ret != -VM_ESHUTDOWN)
		io_ctx_fail(b->io_ctx, ret);
	return ret;
}

void io_receiver_notify(struct io_receiver *b, int error)
{
	struct io_delivery *d = calloc(1, sizeof(*d));
	int ret;

	if (!d) {
		io_ctx_fail(b->io_ctx, error < 0 ? error : -VM_ENOMEM);
		return;
	}

	d->scope = b->scope;
	d->notify = b->notify;
	d->data = b->data;
	d->error = error;

	ret = io_binding_post(b->scope, deliver, d, free, IO_CONTROL);
	if (ret < 0 && ret != -VM_ESHUTDOWN)
		io_ctx_fail(b->io_ctx, error < 0 ? error : ret);
}
