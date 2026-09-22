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
static struct io_binding *binding;
static atomic_bool entered, closing, closed;
static atomic_uint releases;
static void release(void *p)
{
	(void)p;
	atomic_fetch_add(&releases, 1);
}
static void delivered(void *p)
{
	(void)p;
	atomic_store(&entered, true);
	while (!atomic_load(&closing))
		sched_yield();
	CHECK(!atomic_load(&closed));
}
static void *runner(void *p)
{
	(void)p;
	CHECK(io_ctx_run(io) == 0);
	return NULL;
}
static void *closer(void *p)
{
	(void)p;
	while (!atomic_load(&entered))
		sched_yield();
	atomic_store(&closing, true);
	host_mutex_lock(io_ctx_get_device_lock(io));
	io_binding_close_locked(binding);
	atomic_store(&closed, true);
	/* A native completion arriving after callback cancellation still retires. */
	CHECK(io_binding_post(binding, delivered, NULL, release, IO_DATA) == -VM_ESHUTDOWN);
	io_binding_put(binding);
	host_mutex_unlock(io_ctx_get_device_lock(io));
	io_ctx_stop(io);
	return NULL;
}
int main(void)
{
	alarm(5);
	CHECK(log_init() == 0);
	io = io_ctx_create(NULL, NULL);
	CHECK(!IS_ERR(io));
	host_mutex_lock(io_ctx_get_device_lock(io));
	binding = io_binding_create_locked(io);
	CHECK(!IS_ERR(binding));
	CHECK(io_binding_post(binding, delivered, NULL, release, IO_DATA) == 0);
	host_mutex_unlock(io_ctx_get_device_lock(io));
	pthread_t a, b;
	CHECK(!pthread_create(&a, NULL, runner, NULL));
	CHECK(!pthread_create(&b, NULL, closer, NULL));
	CHECK(!pthread_join(a, NULL));
	CHECK(!pthread_join(b, NULL));
	CHECK(atomic_load(&releases) == 2 && atomic_load(&closed));
	io_ctx_destroy(io);
	log_destroy();
	return 0;
}
