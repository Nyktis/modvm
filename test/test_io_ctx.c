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
static int calls, released;
static void release(void *p)
{
	(void)p;
	released++;
}
static void run(void *p)
{
	(void)p;
	calls++;
	io_ctx_stop(io);
}
int main(void)
{
	CHECK(log_init() == 0);
	io = io_ctx_create(NULL, NULL);
	CHECK(!IS_ERR(io));
	struct host_mutex *lock = io_ctx_get_device_lock(io);
	host_mutex_lock(lock);
	struct io_binding *old = io_binding_create_locked(io);
	CHECK(!IS_ERR(old));
	CHECK(io_binding_post(old, run, NULL, release, IO_DATA) == 0);
	io_binding_close_locked(old);
	CHECK(calls == 0 && released == 1);
	CHECK(io_binding_post(old, run, NULL, release, IO_DATA) == -VM_ESHUTDOWN);
	io_binding_put(old);
	struct io_binding *next = io_binding_create_locked(io);
	CHECK(!IS_ERR(next));
	CHECK(io_binding_post(next, run, NULL, release, IO_DATA) == 0);
	host_mutex_unlock(lock);
	CHECK(io_ctx_run(io) == 0);
	CHECK(calls == 1 && released == 3);
	host_mutex_lock(lock);
	CHECK(PTR_ERR(io_binding_create_locked(io)) == -VM_ESHUTDOWN);
	CHECK(io_binding_post(next, run, NULL, release, IO_DATA) == -VM_ESHUTDOWN);
	io_binding_close_locked(next);
	io_binding_put(next);
	host_mutex_unlock(lock);
	io_ctx_destroy(io);
	log_destroy();
	return 0;
}
