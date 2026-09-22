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

static atomic_uint calls;
static atomic_bool callback_active, release_callback, destroy_started, destroyed;
static struct host_posix_service *service;
static int input;
static void failed(void *p, int error)
{
	(void)p;
	(void)error;
	CHECK(0);
}
static void ready(struct host_posix_service *s, uint32_t events, void *p)
{
	(void)p;
	CHECK(events & HOST_POSIX_READ);
	char c;
	CHECK(read(input, &c, 1) == 1);
	host_posix_service_update_locked(s, 0, 0);
	atomic_fetch_add(&calls, 1);
	atomic_store(&callback_active, true);
	while (!atomic_load(&release_callback))
		sched_yield();
}
static void *destroy_service(void *p)
{
	(void)p;
	atomic_store(&destroy_started, true);
	host_posix_service_destroy(service);
	atomic_store(&destroyed, true);
	return NULL;
}
int main(void)
{
	alarm(5);
	CHECK(!log_init());
	int fds[2];
	CHECK(!pipe(fds));
	input = fds[0];
	struct host_posix_source source = { input, HOST_POSIX_READ, ready, NULL };
	service = host_posix_service_create(&source, 1, failed, NULL);
	CHECK(!IS_ERR(service));
	CHECK(write(fds[1], "a", 1) == 1);
	while (!atomic_load(&callback_active))
		sched_yield();
	pthread_t t;
	CHECK(!pthread_create(&t, NULL, destroy_service, NULL));
	while (!atomic_load(&destroy_started))
		sched_yield();
	CHECK(!atomic_load(&destroyed));
	atomic_store(&release_callback, true);
	CHECK(!pthread_join(t, NULL));
	CHECK(atomic_load(&calls) == 1 && atomic_load(&destroyed));
	/* Reuse the descriptor only after close has retired all native callbacks. */
	CHECK(close(input) == 0);
	int replacement[2];
	CHECK(!pipe(replacement));
	CHECK(replacement[0] == input);
	close(replacement[0]);
	close(replacement[1]);
	close(fds[1]);
	log_destroy();
	return 0;
}
