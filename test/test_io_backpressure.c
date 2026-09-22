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
static atomic_uint delivered, retired;
static void dispose(void *p)
{
	(void)p;
	atomic_fetch_add(&retired, 1);
}
static void control(void *p)
{
	(void)p;
	CHECK(!atomic_load(&delivered));
	io_binding_set_enabled_locked(binding, 1);
}
static void receive(void *p)
{
	(void)p;
	atomic_fetch_add(&delivered, 1);
	io_ctx_stop(io);
}
static void *run(void *p)
{
	(void)p;
	CHECK(!io_ctx_run(io));
	return NULL;
}
int main(void)
{
	alarm(5);
	CHECK(!log_init());
	io = io_ctx_create(NULL, NULL);
	CHECK(!IS_ERR(io));
	host_mutex_lock(io_ctx_get_device_lock(io));
	binding = io_binding_create_locked(io);
	CHECK(!IS_ERR(binding));
	io_binding_set_enabled_locked(binding, 0);
	CHECK(!io_binding_post(binding, receive, NULL, dispose, IO_DATA));
	host_mutex_unlock(io_ctx_get_device_lock(io));
	pthread_t t;
	CHECK(!pthread_create(&t, NULL, run, NULL));
	host_mutex_lock(io_ctx_get_device_lock(io));
	CHECK(!atomic_load(&delivered));
	CHECK(!io_binding_post(binding, control, NULL, dispose, IO_CONTROL));
	host_mutex_unlock(io_ctx_get_device_lock(io));
	CHECK(!pthread_join(t, NULL));
	CHECK(atomic_load(&delivered) == 1 && atomic_load(&retired) == 2);
	host_mutex_lock(io_ctx_get_device_lock(io));
	io_binding_close_locked(binding);
	io_binding_put(binding);
	host_mutex_unlock(io_ctx_get_device_lock(io));
	io_ctx_destroy(io);
	log_destroy();
	return 0;
}
