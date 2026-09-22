/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/io/ctx.h>
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include "fixture.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <modvm/io/net.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int source = -1, tap_fd = -1, read_error;
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
	if (!strcmp(path, "/dev/net/tun")) {
		tap_fd = dup(source);
		return tap_fd;
	}
	if (flags & O_CREAT) {
		va_list args;
		va_start(args, flags);
		mode_t mode = va_arg(args, mode_t);
		va_end(args);
		return __real_open(path, flags, mode);
	}
	return __real_open(path, flags);
}
int __real_ioctl(int, unsigned long, ...);
int __wrap_ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	va_start(args, request);
	void *arg = va_arg(args, void *);
	va_end(args);
	if (fd == tap_fd && request == TUNSETIFF)
		return 0;
	return __real_ioctl(fd, request, arg);
}
ssize_t __real_read(int, void *, size_t);
ssize_t __wrap_read(int fd, void *buf, size_t size)
{
	if (fd == tap_fd && read_error) {
		errno = read_error;
		return -1;
	}
	return __real_read(fd, buf, size);
}
ssize_t __wrap___read_chk(int fd, void *buf, size_t size, size_t capacity)
{
	CHECK(size <= capacity);
	return __wrap_read(fd, buf, size);
}

struct receiver {
	struct net_backend *net;
	struct io_ctx *loop;
	int calls, error;
};
static void receive(void *data, const uint8_t *buf, size_t len)
{
	(void)data;
	(void)buf;
	(void)len;
	CHECK(0);
}
static void failed(void *data, int error)
{
	struct receiver *r = data;
	r->calls++;
	r->error = error;
	net_unbind_locked(r->net);
	io_ctx_stop(r->loop);
}
int main(void)
{
	CHECK(log_init() == 0);
	int pair[2];
	CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
	source = pair[0];
	struct vm_ctx vm = { 0 };
	struct res_pool backends;
	res_pool_init(&vm.resources);
	res_pool_init(&backends);
	CHECK(test_io_init(&vm) == 0);
	struct net_backend *net = net_create(&backends, "linux-tap", "mac=02:00:00:00:00:01");
	CHECK(!IS_ERR(net));
	struct receiver receiver = { .net = net, .loop = vm.io_ctx };
	host_mutex_lock(vm.io_map.io_lock);
	CHECK(net_bind_locked(net, vm.io_ctx, receive, failed, &receiver) == 0);
	host_mutex_unlock(vm.io_map.io_lock);
	read_error = ENODEV;
	CHECK(write(pair[1], "x", 1) == 1);
	alarm(3);
	CHECK(io_ctx_run(vm.io_ctx) == 0);
	alarm(0);
	CHECK(receiver.calls == 1 && receiver.error == -VM_ENODEV && !atomic_load(&net->binding));

	res_release_all(&backends);
	res_release_all(&vm.resources);
	close(pair[0]);
	close(pair[1]);
	log_destroy();
	puts("TAP receive errors notify once and permit callback unbinding");
	return 0;
}
