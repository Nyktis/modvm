/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/io/ctx.h>
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include "fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <modvm/io/char.h>
#include <modvm/core/io_map.h>
#include <modvm/core/device.h>
#include <modvm/core/irq.h>
#include <modvm/hw/char/serial.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)

struct observer {
	struct char_backend *dev;
	struct io_ctx *loop;
	unsigned received, calls;
	int error;
};

static void received(void *data, const uint8_t *buf, size_t len)
{
	struct observer *o = data;
	CHECK(len > 0 && len <= 3);
	for (size_t i = 0; i < len; i++)
		CHECK(buf[i] == 'a' + o->received++);
	CHECK(char_pause_rx_locked(o->dev) == 0);
	CHECK(char_resume_rx_locked(o->dev) == 0);
	if (o->received == 6) {
		char_unbind_locked(o->dev);
		io_ctx_stop(o->loop);
	}
}

static void notified(void *data, int error)
{
	struct observer *o = data;
	o->calls++;
	o->error = error;
	/* Self-unbinding must not leave a pending callback touching cleared state. */
	char_unbind_locked(o->dev);
	io_ctx_stop(o->loop);
}

static void test_stdio(void)
{
	int saved_in = dup(0), saved_out = dup(1);
	CHECK(saved_in >= 0 && saved_out >= 0);
	for (int mode = 0; mode < 4; mode++) {
		int input[2], output[2];
		CHECK(pipe(input) == 0 && pipe(output) == 0);
		CHECK(dup2(input[0], 0) == 0 && dup2(output[1], 1) == 1);
		struct vm_ctx vm = { 0 };
		struct res_pool backends;
		res_pool_init(&backends);
		res_pool_init(&vm.resources);
		CHECK(test_io_init(&vm) == 0);
		struct char_backend *dev = char_create(&backends, "posix-stdio", NULL);
		CHECK(!IS_ERR(dev));
		struct observer o = { .dev = dev, .loop = vm.io_ctx };
		host_mutex_lock(vm.io_map.io_lock);
		CHECK(char_bind_locked(dev, vm.io_ctx, 0, received, notified, &o) == -VM_EINVAL);
		CHECK(char_bind_locked(dev, vm.io_ctx, 3, received, notified, &o) == 0);
		if (mode == 0) {
			CHECK(close(output[0]) == 0);
			output[0] = -1;
			CHECK(dev->ops->write(dev, (const uint8_t *)"x", 1) == -VM_EPIPE);
			CHECK(o.calls == 0);
		} else if (mode == 1) {
			uint8_t *buf = calloc(1, 512 * 1024);
			CHECK(buf && dev->ops->write(dev, buf, 512 * 1024) == 0);
			free(buf);
			CHECK(close(output[0]) == 0);
			output[0] = -1;
		} else if (mode == 2) {
			CHECK(write(input[1], "abcdef", 6) == 6);
		} else {
			CHECK(char_pause_rx_locked(dev) == 0);
			CHECK(char_resume_rx_locked(dev) == 0);
			CHECK(o.calls == 0);
		}
		host_mutex_unlock(vm.io_map.io_lock);
		if (mode == 1 || mode == 2) {
			alarm(3);
			CHECK(io_ctx_run(vm.io_ctx) == 0);
			alarm(0);
			CHECK(!atomic_load(&dev->binding));
			CHECK(mode == 1 ? o.calls == 1 && o.error == -VM_EPIPE : o.calls == 0 && o.received == 6);
		}
		host_mutex_lock(vm.io_map.io_lock);
		char_unbind_locked(dev);
		CHECK(char_pause_rx_locked(dev) == -VM_ENOTCONN);
		CHECK(char_resume_rx_locked(dev) == -VM_ENOTCONN);
		host_mutex_unlock(vm.io_map.io_lock);
		res_release_all(&backends);
		res_release_all(&vm.resources);
		CHECK(dup2(saved_in, 0) == 0 && dup2(saved_out, 1) == 1);
		close(input[0]);
		close(input[1]);
		close(output[1]);
		if (output[0] >= 0)
			close(output[0]);
	}
	close(saved_in);
	close(saved_out);
}

static void irq(void *data, int level)
{
	(void)data;
	(void)level;
}

static int failure_mode;
static int bind_console(struct char_backend *dev, struct io_receiver *binding, size_t limit)
{
	(void)dev;
	(void)binding;
	(void)limit;
	return 0;
}
static void unbind_console(struct char_backend *dev)
{
	(void)dev;
}
static void destroy_console(struct char_backend *dev)
{
	(void)dev;
}
static int write_console(struct char_backend *dev, const uint8_t *buf, size_t len)
{
	(void)dev;
	(void)buf;
	(void)len;
	return -VM_EIO; /* No notify: the frontend must propagate the return value. */
}
static void test_uart(void)
{
	const struct char_ops ops = { .write = write_console, .bind = bind_console, .unbind = unbind_console, .destroy = destroy_console };
	for (failure_mode = 0; failure_mode < 1; failure_mode++) {
		struct vm_ctx vm = { 0 };
		INIT_LIST_HEAD(&vm.devices);
		INIT_LIST_HEAD(&vm.io_map.pio_regions);
		INIT_LIST_HEAD(&vm.io_map.mmio_regions);
		INIT_LIST_HEAD(&vm.mem_space.regions);
		res_pool_init(&vm.resources);
		CHECK(test_io_init(&vm) == 0);
		struct char_backend console = { .ops = &ops };
		struct serial_pdata pdata = { .io_space = IO_PIO, .base = TO_GPA(0x3f8), .console = &console, .io_ctx = vm.io_ctx };
		struct device *uart = device_alloc(&vm, "uart-16550a");
		CHECK(uart);
		pdata.irq = irq_alloc(&uart->resources, irq, NULL);
		CHECK(pdata.irq && device_realize(uart, &pdata) == 0);
		if (failure_mode == 0) {
			io_map_write(&vm.io_map, IO_PIO, TO_GPA(0x3f8), 'x', 1);
			CHECK(atomic_load(&vm.run_error) == -VM_EIO);
		}
		vm_destroy(&vm);
	}
}

int main(void)
{
	CHECK(log_init() == 0);
	test_stdio();
	test_uart();
	log_destroy();
	puts("character synchronous errors, asynchronous self-unbind, bounded RX and UART synchronous failure passed");
	return 0;
}
