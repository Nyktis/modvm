/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/host/mutex.h>
#include "../src/io/binding.h"
#include <modvm/io/ctx.h>
#include <modvm/errno.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			abort();                                        \
		}                                                       \
	} while (0)

static struct io_ctx *io;
static struct io_binding *a, *b, *replacement;
static unsigned released, calls;
static void release(void *p)
{
	(void)p;
	released++;
}
static void unexpected(void *p)
{
	(void)p;
	CHECK(0);
}
static void finish(void *p)
{
	(void)p;
	calls++;
	io_ctx_stop(io);
}
static void replace(void *p)
{
	(void)p;
	calls++;
	io_binding_close_locked(a);
	io_binding_put(a);
	io_binding_close_locked(b);
	io_binding_put(b);
	replacement = io_binding_create_locked(io);
	CHECK(!IS_ERR(replacement));
	CHECK(io_binding_post(replacement, finish, NULL, release, IO_DATA) == 0);
}
int main(void)
{
	alarm(5);
	CHECK(log_init() == 0);
	io = io_ctx_create(NULL, NULL);
	CHECK(!IS_ERR(io));
	host_mutex_lock(io_ctx_get_device_lock(io));
	a = io_binding_create_locked(io);
	b = io_binding_create_locked(io);
	CHECK(!IS_ERR(a) && !IS_ERR(b));
	CHECK(!io_binding_post(a, replace, NULL, release, IO_DATA));
	CHECK(!io_binding_post(b, unexpected, NULL, release, IO_DATA));
	host_mutex_unlock(io_ctx_get_device_lock(io));
	CHECK(!io_ctx_run(io));
	CHECK(calls == 2 && released == 3);
	host_mutex_lock(io_ctx_get_device_lock(io));
	io_binding_close_locked(replacement);
	io_binding_put(replacement);
	host_mutex_unlock(io_ctx_get_device_lock(io));
	io_ctx_destroy(io);
	log_destroy();
	return 0;
}
