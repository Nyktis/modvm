/* SPDX-License-Identifier: GPL-2.0 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include <modvm/errno.h>
#include <modvm/host/condition.h>
#include <modvm/host/mutex.h>
#include <modvm/io/ctx.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/list.h>

#include "binding.h"

struct io_ctx {
	struct host_mutex *device_lock;
	struct host_mutex *queue_lock;
	struct host_condition *ready;
	struct list_head work;
	bool stopped;
	int error;
	unsigned bindings;
	void (*fatal)(void *, int);
	void *data;
};

struct io_binding {
	struct io_ctx *io_ctx;
	atomic_uint refs;
	atomic_bool open;
	bool enabled;
};

struct io_work {
	struct list_head node;
	struct io_binding *binding;
	enum io_work_kind kind;
	void (*run)(void *);
	void (*release)(void *);
	void *data;
};

struct io_ctx *io_ctx_create(void (*fatal)(void *, int), void *data)
{
	struct io_ctx *io = calloc(1, sizeof(*io));

	if (!io)
		return ERR_PTR(-VM_ENOMEM);
	INIT_LIST_HEAD(&io->work);
	io->fatal = fatal;
	io->data = data;
	io->device_lock = host_mutex_create();
	if (IS_ERR(io->device_lock)) {
		int ret = PTR_ERR(io->device_lock);

		free(io);
		return ERR_PTR(ret);
	}
	io->queue_lock = host_mutex_create();
	if (IS_ERR(io->queue_lock)) {
		int ret = PTR_ERR(io->queue_lock);

		host_mutex_destroy(io->device_lock);
		free(io);
		return ERR_PTR(ret);
	}
	io->ready = host_condition_create();
	if (IS_ERR(io->ready)) {
		int ret = PTR_ERR(io->ready);

		host_mutex_destroy(io->queue_lock);
		host_mutex_destroy(io->device_lock);
		free(io);
		return ERR_PTR(ret);
	}

	return io;
}

struct host_mutex *io_ctx_get_device_lock(struct io_ctx *io)
{
	return io->device_lock;
}

struct io_binding *io_binding_create_locked(struct io_ctx *io)
{
	if (!io)
		return ERR_PTR(-VM_EINVAL);
	struct io_binding *b = calloc(1, sizeof(*b));

	if (!b)
		return ERR_PTR(-VM_ENOMEM);
	host_mutex_lock(io->queue_lock);
	if (io->stopped) {
		host_mutex_unlock(io->queue_lock);
		free(b);
		return ERR_PTR(-VM_ESHUTDOWN);
	}
	io->bindings++;
	host_mutex_unlock(io->queue_lock);
	b->io_ctx = io;
	b->enabled = true;
	atomic_init(&b->refs, 1);
	atomic_init(&b->open, true);
	return b;
}

void io_binding_put(struct io_binding *b)
{
	if (atomic_fetch_sub(&b->refs, 1) == 1) {
		BUG_ON(atomic_load(&b->open));
		host_mutex_lock(b->io_ctx->queue_lock);
		b->io_ctx->bindings--;
		host_mutex_unlock(b->io_ctx->queue_lock);
		free(b);
	}
}

bool io_binding_is_open(struct io_binding *b)
{
	return atomic_load(&b->open);
}

void io_binding_close_locked(struct io_binding *b)
{
	struct io_ctx *io = b->io_ctx;

	LIST_HEAD(cancelled);
	host_mutex_lock(io->queue_lock);
	atomic_store(&b->open, false);
	struct io_work *w, *next;
	list_for_each_entry_safe(w, next, &io->work, node)
	{
		if (w->binding == b) {
			list_del(&w->node);
			list_add_tail(&w->node, &cancelled);
		}
	}
	host_mutex_unlock(io->queue_lock);
	list_for_each_entry_safe(w, next, &cancelled, node)
	{
		list_del(&w->node);
		w->release(w->data);
		io_binding_put(b);
		free(w);
	}
}

int io_binding_post(struct io_binding *b, void (*run)(void *), void *data, void (*release)(void *), enum io_work_kind kind)
{
	struct io_work *w = malloc(sizeof(*w));

	if (!w) {
		release(data);
		return -VM_ENOMEM;
	}
	struct io_ctx *io = b->io_ctx;

	host_mutex_lock(io->queue_lock);
	if (io->stopped || !atomic_load(&b->open)) {
		host_mutex_unlock(io->queue_lock);
		free(w);
		release(data);
		return -VM_ESHUTDOWN;
	}
	atomic_fetch_add(&b->refs, 1);
	*w = (struct io_work){ .binding = b, .kind = kind, .run = run, .release = release, .data = data };
	list_add_tail(&w->node, &io->work);
	host_condition_signal(io->ready);
	host_mutex_unlock(io->queue_lock);
	return 0;
}

int io_ctx_run(struct io_ctx *io)
{
	for (;;) {
		host_mutex_lock(io->queue_lock);
		for (;;) {
			bool available = false;
			struct io_work *pending;
			list_for_each_entry(pending, &io->work, node)
			{
				if (pending->binding->enabled || pending->kind == IO_CONTROL) {
					available = true;
					break;
				}
			}
			if (available || io->stopped)
				break;
			host_condition_wait(io->ready, io->queue_lock);
		}
		bool stopped = io->stopped;
		int error = io->error;

		host_mutex_unlock(io->queue_lock);
		if (stopped)
			return error;
		/* Acquire the device lock before taking a job; close can then cancel all
		 * queued work without waiting for a runner blocked on that same lock. */
		host_mutex_lock(io->device_lock);
		host_mutex_lock(io->queue_lock);
		struct io_work *w = NULL;

		if (!io->stopped) {
			struct io_work *pending;
			list_for_each_entry(pending, &io->work, node)
			{
				if (pending->binding->enabled || pending->kind == IO_CONTROL) {
					w = pending;
					list_del(&w->node);
					break;
				}
			}
		}
		host_mutex_unlock(io->queue_lock);
		if (w) {
			if (atomic_load(&w->binding->open))
				w->run(w->data);
			w->release(w->data);
			/* The owner still holds a reference if the callback left it open. */
			io_binding_put(w->binding);
			free(w);
		}
		host_mutex_unlock(io->device_lock);
	}
}

void io_ctx_stop(struct io_ctx *io)
{
	if (!io)
		return;
	host_mutex_lock(io->queue_lock);
	io->stopped = true;
	host_condition_signal(io->ready);
	host_mutex_unlock(io->queue_lock);
}

void io_ctx_destroy(struct io_ctx *io)
{
	if (!io)
		return;
	BUG_ON(io->bindings || !list_empty(&io->work));
	host_condition_destroy(io->ready);
	host_mutex_destroy(io->queue_lock);
	host_mutex_destroy(io->device_lock);
	free(io);
}

void io_binding_set_enabled_locked(struct io_binding *b, bool enabled)
{
	host_mutex_lock(b->io_ctx->queue_lock);
	b->enabled = enabled;
	host_condition_signal(b->io_ctx->ready);
	host_mutex_unlock(b->io_ctx->queue_lock);
}

void io_ctx_fail(struct io_ctx *io, int error)
{
	host_mutex_lock(io->queue_lock);
	bool report = error < 0 && !io->error;

	if (report)
		io->error = error;
	io->stopped = true;
	host_condition_signal(io->ready);
	host_mutex_unlock(io->queue_lock);
	if (report && io->fatal)
		io->fatal(io->data, error);
}
