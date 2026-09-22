/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/host/mutex.h>
#include <modvm/io/ctx.h>
#include <modvm/io/receiver.h>
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

#include <modvm/io/net.h>
static struct net_backend net;
static struct io_ctx *ios[2];
static atomic_bool start;
static int results[2], fail_bind;
static unsigned received;
static int bind_native(struct net_backend *n, struct io_receiver *binding)
{
	(void)n;
	CHECK(binding);
	return fail_bind ? -VM_ENOSPC : 0;
}
static void unbind_native(struct net_backend *n)
{
	(void)n;
}
static void receive(void *data, const uint8_t *buf, size_t len)
{
	CHECK(len == 1 && *buf == 'n');
	received++;
	net_unbind_locked(&net);
	io_ctx_stop(data);
}
static void *binder(void *data)
{
	size_t i = (size_t)data;
	while (!atomic_load(&start))
		sched_yield();
	host_mutex_lock(io_ctx_get_device_lock(ios[i]));
	results[i] = net_bind_locked(&net, ios[i], receive, NULL, ios[i]);
	host_mutex_unlock(io_ctx_get_device_lock(ios[i]));
	return NULL;
}
int main(void)
{
	alarm(5);
	CHECK(!log_init());
	const struct net_ops ops = { .bind = bind_native, .unbind = unbind_native };
	net.ops = &ops;
	for (int i = 0; i < 2; i++) {
		ios[i] = io_ctx_create(NULL, NULL);
		CHECK(!IS_ERR(ios[i]));
	}
	pthread_t a, b;
	CHECK(!pthread_create(&a, NULL, binder, (void *)0));
	CHECK(!pthread_create(&b, NULL, binder, (void *)1));
	atomic_store(&start, true);
	CHECK(!pthread_join(a, NULL));
	CHECK(!pthread_join(b, NULL));
	CHECK((results[0] == 0 && results[1] == -VM_EBUSY) || (results[1] == 0 && results[0] == -VM_EBUSY));
	int winner = results[0] == 0 ? 0 : 1, other = 1 - winner;
	host_mutex_lock(io_ctx_get_device_lock(ios[winner]));
	struct io_receiver *old = atomic_load(&net.binding);
	CHECK(!io_receiver_submit(old, (const uint8_t *)"o", 1, NULL, NULL));
	net_unbind_locked(&net);
	host_mutex_unlock(io_ctx_get_device_lock(ios[winner]));
	host_mutex_lock(io_ctx_get_device_lock(ios[other]));
	fail_bind = 1;
	CHECK(net_bind_locked(&net, ios[other], receive, NULL, ios[other]) == -VM_ENOSPC);
	CHECK(!atomic_load(&net.binding));
	fail_bind = 0;
	CHECK(!net_bind_locked(&net, ios[other], receive, NULL, ios[other]));
	CHECK(!io_receiver_submit(atomic_load(&net.binding), (const uint8_t *)"n", 1, NULL, NULL));
	host_mutex_unlock(io_ctx_get_device_lock(ios[other]));
	CHECK(!io_ctx_run(ios[other]));
	CHECK(received == 1);
	io_ctx_destroy(ios[0]);
	io_ctx_destroy(ios[1]);
	log_destroy();
	return 0;
}
