/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/io/ctx.h>
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modvm/io/char.h>
#include <modvm/util/err.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
struct observer {
	struct io_ctx *loop;
	int error, calls;
};
static void receive(void *data, const uint8_t *buf, size_t len)
{
	(void)data;
	(void)buf;
	(void)len;
	CHECK(0);
}

static void notify(void *data, int error)
{
	struct observer *observer = data;
	observer->error = error;
	observer->calls++;
	io_ctx_stop(observer->loop);
}

int main(void)
{
	log_init();
	int saved = dup(0);
	CHECK(saved >= 0);
	for (int mode = 0; mode < 2; mode++) {
		int input[2];
		CHECK(pipe(input) == 0 && dup2(input[0], 0) == 0);
		struct res_pool resources, backends;
		res_pool_init(&resources);
		res_pool_init(&backends);
		/* Standalone execution context: no VM exists to recover with container_of. */
		struct io_ctx *loop = io_ctx_create(NULL, NULL);
		CHECK(!IS_ERR(loop));
		struct host_mutex *lock = io_ctx_get_device_lock(loop);
		struct observer observer = { .loop = loop };
		struct char_backend *dev = char_create(&backends, "posix-stdio", NULL);
		CHECK(!IS_ERR(dev));
		host_mutex_lock(lock);
		CHECK(char_bind_locked(dev, loop, 64, receive, notify, &observer) == 0);
		CHECK(char_bind_locked(dev, loop, 64, receive, notify, &observer) == -VM_EBUSY);
		host_mutex_unlock(lock);
		if (mode == 0)
			CHECK(write(input[1], "\001x", 2) == 2);
		else
			CHECK(close(0) == 0);
		alarm(3);
		CHECK(io_ctx_run(loop) == 0);
		alarm(0);
		CHECK(observer.calls == 1 && observer.error == (mode == 0 ? 0 : -VM_EIO));
		host_mutex_lock(lock);
		char_unbind_locked(dev);
		host_mutex_unlock(lock);
		res_release_all(&backends);
		res_release_all(&resources);
		io_ctx_destroy(loop);
		CHECK(dup2(saved, 0) == 0);
		close(input[0]);
		close(input[1]);
	}
	close(saved);
	log_destroy();
	puts("standalone console exit and fatal I/O notifications passed");
	return 0;
}
