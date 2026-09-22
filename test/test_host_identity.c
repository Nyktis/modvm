/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
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

static atomic_uint first, second;
static atomic_bool rearmed;
static int fd_a, fd_b;
static void fatal(void *data, int error)
{
	(void)data;
	(void)error;
	CHECK(0);
}
static void ready_a(struct host_posix_service *s, uint32_t events, void *data)
{
	(void)data;
	CHECK(events & HOST_POSIX_READ);
	char c;
	CHECK(read(fd_a, &c, 1) == 1);
	host_posix_service_update_locked(s, 0, 0);
	host_posix_service_update_locked(s, 1, 0);
	atomic_fetch_add(&first, 1);
}
static void ready_b(struct host_posix_service *s, uint32_t events, void *data)
{
	(void)data;
	CHECK(atomic_load(&rearmed));
	CHECK(events & HOST_POSIX_READ);
	char c;
	CHECK(read(fd_b, &c, 1) == 1);
	host_posix_service_update_locked(s, 1, 0);
	atomic_fetch_add(&second, 1);
}
int main(void)
{
	alarm(5);
	CHECK(!log_init());
	int a[2], b[2];
	CHECK(!pipe(a) && !pipe(b));
	fd_a = a[0];
	fd_b = b[0];
	CHECK(write(a[1], "a", 1) == 1 && write(b[1], "b", 1) == 1);
	struct host_posix_source sources[] = { { fd_a, HOST_POSIX_READ, ready_a, NULL }, { fd_b, HOST_POSIX_READ, ready_b, NULL } };
	struct host_posix_service *s = host_posix_service_create(sources, 2, fatal, NULL);
	CHECK(!IS_ERR(s));
	while (!atomic_load(&first))
		sched_yield();
	host_posix_service_lock(s);
	CHECK(!atomic_load(&second));
	atomic_store(&rearmed, true);
	host_posix_service_update_locked(s, 1, HOST_POSIX_READ);
	host_posix_service_unlock(s);
	while (!atomic_load(&second))
		sched_yield();
	host_posix_service_destroy(s);
	CHECK(atomic_load(&first) == 1 && atomic_load(&second) == 1);
	close(a[0]);
	close(a[1]);
	close(b[0]);
	close(b[1]);
	log_destroy();
	return 0;
}
