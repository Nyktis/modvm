/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/io/receiver.h>
#include <modvm/host/mutex.h>
#include <modvm/io/ctx.h>
#include <modvm/errno.h>
#include <modvm/util/err.h>
#include <modvm/util/log.h>
#include <modvm/host/posix/service.h>
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

#include <modvm/io/char.h>
#include <errno.h>
static int calloc_at, malloc_at, thread_fail;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size)
{
	if (calloc_at && !--calloc_at)
		return NULL;
	return __real_calloc(n, size);
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n)
{
	if (malloc_at && !--malloc_at)
		return NULL;
	return __real_malloc(n);
}
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *d)
{
	return thread_fail ? EAGAIN : __real_pthread_create(t, a, f, d);
}
static void fatal(void *data, int error)
{
	(void)data;
	(void)error;
	CHECK(0);
}
static void ready(struct host_posix_service *s, uint32_t e, void *data)
{
	(void)s;
	(void)e;
	(void)data;
	CHECK(0);
}
static int bind_native(struct char_backend *d, struct io_receiver *b, size_t limit)
{
	(void)d;
	(void)b;
	(void)limit;
	return 0;
}
static void unbind_native(struct char_backend *d)
{
	(void)d;
}
static void receive(void *data, const uint8_t *buf, size_t len)
{
	(void)data;
	(void)buf;
	(void)len;
	CHECK(0);
}
int main(void)
{
	alarm(5);
	CHECK(!log_init());
	for (int n = 1; n <= 5; n++) {
		calloc_at = n;
		struct io_ctx *io = io_ctx_create(NULL, NULL);
		calloc_at = 0;
		if (IS_ERR(io))
			CHECK(PTR_ERR(io) == -VM_ENOMEM);
		else
			io_ctx_destroy(io);
	}
	int fds[2];
	CHECK(!pipe(fds));
	struct host_posix_source source = { fds[0], 0, ready, NULL };
	for (int n = 1; n <= 5; n++) {
		calloc_at = n;
		struct host_posix_service *s = host_posix_service_create(&source, 1, fatal, NULL);
		calloc_at = 0;
		if (IS_ERR(s))
			CHECK(PTR_ERR(s) == -VM_ENOMEM);
		else
			host_posix_service_destroy(s);
	}
	thread_fail = 1;
	CHECK(PTR_ERR(host_posix_service_create(&source, 1, fatal, NULL)) == -VM_EAGAIN);
	thread_fail = 0;
	close(fds[0]);
	close(fds[1]);
	const struct char_ops ops = { .bind = bind_native, .unbind = unbind_native };
	for (int n = 1; n <= 2; n++) {
		struct io_ctx *io = io_ctx_create(NULL, NULL);
		CHECK(!IS_ERR(io));
		struct char_backend dev = { .ops = &ops };
		host_mutex_lock(io_ctx_get_device_lock(io));
		CHECK(!char_bind_locked(&dev, io, 1, receive, NULL, NULL));
		host_mutex_unlock(io_ctx_get_device_lock(io));
		malloc_at = n;
		CHECK(io_receiver_submit(atomic_load(&dev.binding), (const uint8_t *)"x", 1, NULL, NULL) == -VM_ENOMEM);
		malloc_at = 0;
		CHECK(io_ctx_run(io) == -VM_ENOMEM);
		host_mutex_lock(io_ctx_get_device_lock(io));
		char_unbind_locked(&dev);
		host_mutex_unlock(io_ctx_get_device_lock(io));
		io_ctx_destroy(io);
	}
	log_destroy();
	return 0;
}
