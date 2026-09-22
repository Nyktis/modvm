/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/errno.h>
#include <modvm/host/posix/service.h>
#include <modvm/host/mutex.h>
#include "fixture.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modvm/io/char.h>
#include <modvm/host/posix/io.h>
#include <modvm/util/log.h>
#include <modvm/util/once_lite.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int paused_fd = -1;
static unsigned polls;
int __real_poll(struct pollfd *, nfds_t, int);
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
	if (paused_fd < 0)
		return __real_poll(fds, count, timeout);
	for (nfds_t i = 0; i < count; i++)
		CHECK(fds[i].fd != paused_fd);
	polls++;
	errno = EIO;
	return -1;
}

int __wrap___poll_chk(struct pollfd *fds, nfds_t count, int timeout, size_t size)
{
	(void)size;
	return __wrap_poll(fds, count, timeout);
}

static void unexpected(struct host_posix_service *service, uint32_t events, void *data)
{
	(void)service;
	(void)events;
	(void)data;
	CHECK(0);
}

static void receive(void *data, const uint8_t *buf, size_t len)
{
	(void)data;
	(void)buf;
	(void)len;
	CHECK(0);
}

static atomic_int native_error;
static void native_failed(void *data, int error)
{
	(void)data;
	atomic_store(&native_error, error);
}

static atomic_uint once_count;
static void *once_worker(void *data)
{
	(void)data;
	for (unsigned i = 0; i < 1000; i++)
		DO_ONCE_LITE(atomic_fetch_add, &once_count, 1);
	return NULL;
}

int main(void)
{
	CHECK(log_init() == 0);
	pthread_t workers[4];
	for (unsigned i = 0; i < 4; i++)
		CHECK(pthread_create(&workers[i], NULL, once_worker, NULL) == 0);
	for (unsigned i = 0; i < 4; i++)
		CHECK(pthread_join(workers[i], NULL) == 0);
	CHECK(atomic_load(&once_count) == 1);
	struct sigaction action = { .sa_handler = SIG_DFL }, old_action;
	sigemptyset(&action.sa_mask);
	CHECK(sigaction(SIGPIPE, &action, &old_action) == 0);
	int pipefd[2];
	CHECK(pipe(pipefd) == 0 && close(pipefd[0]) == 0);
	CHECK(host_posix_write_nosigpipe(pipefd[1], "x", 1) == -VM_EPIPE);
	sigset_t mask, old_mask, pending;
	sigemptyset(&mask);
	sigaddset(&mask, SIGPIPE);
	CHECK(pthread_sigmask(SIG_BLOCK, &mask, &old_mask) == 0);
	CHECK(raise(SIGPIPE) == 0);
	CHECK(host_posix_write_nosigpipe(pipefd[1], "x", 1) == -VM_EPIPE);
	CHECK(sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1);
	int signal;
	CHECK(sigwait(&mask, &signal) == 0 && signal == SIGPIPE);
	CHECK(pthread_sigmask(SIG_SETMASK, &old_mask, NULL) == 0);
	close(pipefd[1]);

	int saved_in = dup(0), saved_out = dup(1), saved_err = dup(2), input[2], output[2];
	CHECK(saved_in >= 0 && saved_out >= 0 && saved_err >= 0);
	CHECK(pipe(input) == 0 && pipe(output) == 0);
	CHECK(dup2(input[0], 0) == 0 && dup2(output[1], 1) == 1);
	int original_flags = fcntl(0, F_GETFL);
	struct res_pool pool;
	res_pool_init(&pool);
	struct char_backend *console = char_create(&pool, "posix-stdio", NULL);
	CHECK(!IS_ERR(console));
	CHECK(PTR_ERR(char_create(&pool, "posix-stdio", NULL)) == -VM_EBUSY);
	CHECK(fcntl(0, F_GETFL) & O_NONBLOCK);
	struct vm_ctx console_vm = { 0 };
	res_pool_init(&console_vm.resources);
	CHECK(test_io_init(&console_vm) == 0);
	host_mutex_lock(console_vm.io_map.io_lock);
	CHECK(char_bind_locked(console, console_vm.io_ctx, 64, receive, NULL, NULL) == 0);
	close(output[0]);
	CHECK(console->ops->write(console, (const uint8_t *)"x", 1) == -VM_EPIPE);
	char_unbind_locked(console);
	host_mutex_unlock(console_vm.io_map.io_lock);
	res_release_all(&pool);
	res_release_all(&console_vm.resources);
	CHECK(fcntl(0, F_GETFL) == original_flags);
	CHECK(dup2(saved_in, 0) == 0 && dup2(saved_out, 1) == 1);
	close(input[0]);
	close(input[1]);
	close(output[1]);

	/* A paused hung-up descriptor must not be submitted to poll. Its error
	 * must also survive a logger that cannot write to stderr. */
	struct vm_ctx vm = { 0 };
	res_pool_init(&vm.resources);
	CHECK(test_io_init(&vm) == 0);
	CHECK(pipe(pipefd) == 0);
	paused_fd = pipefd[0];
	close(pipefd[1]);
	int broken[2];
	CHECK(pipe(broken) == 0 && close(broken[0]) == 0 && dup2(broken[1], 2) == 2);
	struct host_posix_source source = { paused_fd, 0, unexpected, NULL };
	struct host_posix_service *service = host_posix_service_create(&source, 1, native_failed, NULL);
	CHECK(!IS_ERR(service));
	while (!atomic_load(&native_error))
		sched_yield();
	host_posix_service_destroy(service);
	int result = atomic_load(&native_error);
	errno = ENOSPC;
	int logged = printk(LOG_ERR, "closed output\n");
	int preserved = errno;
	CHECK(dup2(saved_err, 2) == 2);
	CHECK(result == -VM_EIO && polls == 1 && logged == -VM_EPIPE && preserved == ENOSPC);
	res_release_all(&vm.resources);
	close(pipefd[0]);
	close(broken[1]);
	close(saved_in);
	close(saved_out);
	close(saved_err);
	CHECK(sigaction(SIGPIPE, &old_action, NULL) == 0);
	log_destroy();
	puts("paused poll, errno, exclusive stdio and scoped SIGPIPE checks passed");
	return 0;
}
