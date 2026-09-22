/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/host/mutex.h>
#include "fixture.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modvm/core/vm.h>
#include <modvm/io/char.h>
#include <modvm/io/ctx.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
#define N (512 * 1024)
struct reader {
	int fd;
	struct vm_ctx *vm;
};
static void *read_output(void *p)
{
	struct reader *r = p;
	size_t total = 0;
	unsigned char buf[4096];
	while (total < N) {
		ssize_t n = read(r->fd, buf, sizeof(buf));
		CHECK(n > 0);
		for (ssize_t i = 0; i < n; i++)
			CHECK(buf[i] == (unsigned char)(total + i));
		total += n;
	}
	io_ctx_stop(r->vm->io_ctx);
	return NULL;
}

static void receive(void *p, const uint8_t *b, size_t n)
{
	(void)p;
	(void)b;
	(void)n;
}

int main(void)
{
	struct res_pool backends;
	res_pool_init(&backends);
	struct vm_ctx vm = { 0 };
	int in[2], out[2], saved_in = dup(0), saved_out = dup(1);
	pthread_t thread;
	CHECK(saved_in >= 0 && saved_out >= 0 && pipe(in) == 0 && pipe(out) == 0);
	CHECK(dup2(in[0], 0) == 0 && dup2(out[1], 1) == 1);
	int before_in = fcntl(0, F_GETFL), before_out = fcntl(1, F_GETFL);
	log_init();
	res_pool_init(&vm.resources);
	CHECK(test_io_init(&vm) == 0);
	struct char_backend *dev = char_create(&backends, "posix-stdio", NULL);
	CHECK(!IS_ERR(dev));
	host_mutex_lock(vm.io_map.io_lock);
	CHECK(char_bind_locked(dev, vm.io_ctx, 64, receive, NULL, NULL) == 0);
	host_mutex_unlock(vm.io_map.io_lock);
	uint8_t *payload = malloc(N);
	CHECK(payload);
	for (size_t i = 0; i < N; i++)
		payload[i] = i;
	/* No reader yet: force partial write and EAGAIN. */
	host_mutex_lock(vm.io_map.io_lock);
	CHECK(dev->ops->write(dev, payload, N) == 0);
	host_mutex_unlock(vm.io_map.io_lock);
	struct reader r = { out[0], &vm };
	alarm(5);
	CHECK(pthread_create(&thread, NULL, read_output, &r) == 0);
	CHECK(io_ctx_run(vm.io_ctx) == 0);
	CHECK(pthread_join(thread, NULL) == 0);
	alarm(0);
	host_mutex_lock(vm.io_map.io_lock);
	char_unbind_locked(dev);
	res_release_all(&backends);
	host_mutex_unlock(vm.io_map.io_lock);
	CHECK(fcntl(0, F_GETFL) == before_in && fcntl(1, F_GETFL) == before_out);
	res_release_all(&vm.resources);
	log_destroy();
	free(payload);
	CHECK(dup2(saved_in, 0) == 0 && dup2(saved_out, 1) == 1);
	close(saved_in);
	close(saved_out);
	close(in[0]);
	close(in[1]);
	close(out[0]);
	close(out[1]);
	puts("stdio preserves partial/EAGAIN output and restores shared descriptor flags");
	return 0;
}
