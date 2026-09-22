/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/io/receiver.h>
#include <modvm/host/error.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <string.h>

#include <modvm/io/char.h>
#include <modvm/host/posix/service.h>
#include <modvm/host/posix/io.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_stdio: " fmt

#define STDIO_TX_BUF_SIZE (1024 * 1024)
#define ESCAPE_CHAR 0x01 /* Ctrl+a */

static atomic_bool stdio_owned;

struct char_posix_ctx {
	struct termios orig_termios;
	bool is_saved;
	int input_flags, output_flags;
	struct host_posix_service *service;
	struct io_receiver *binding;
	size_t rx_limit;
	uint8_t tx_buf[STDIO_TX_BUF_SIZE];
	size_t tx_len;
	bool escape_pending;
	bool rx_closed;
};

static void stdio_fail(struct char_posix_ctx *ctx, int error)
{
	pr_err("console I/O failed: %d\n", error);
	io_receiver_notify(ctx->binding, error);
}

static int stdio_flush(struct char_posix_ctx *ctx)
{
	while (ctx->tx_len) {
		ssize_t n = host_posix_write_nosigpipe(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len);
		if (n < 0) {
			if (n == -VM_EAGAIN)
				return 0;
			return n;
		}
		if (!n)
			return -VM_EIO;
		ctx->tx_len -= n;
		memmove(ctx->tx_buf, ctx->tx_buf + n, ctx->tx_len);
	}
	return 0;
}

static void stdio_tx_ready(struct host_posix_service *service, uint32_t events, void *data)
{
	struct char_posix_ctx *ctx = data;
	(void)events;
	int ret = stdio_flush(ctx);
	if (!ctx->tx_len || ret < 0)
		host_posix_service_update_locked(service, 1, 0);
	if (ret < 0)
		stdio_fail(ctx, ret);
}

static int char_posix_write(struct char_backend *dev, const uint8_t *buf, size_t len)
{
	struct char_posix_ctx *ctx = dev->priv;
	if (!atomic_load(&dev->binding))
		return -VM_ENOTCONN;
	host_posix_service_lock(ctx->service);
	int ret;
	if (len > sizeof(ctx->tx_buf) - ctx->tx_len) {
		ret = -VM_ENOBUFS;
	} else {
		memcpy(ctx->tx_buf + ctx->tx_len, buf, len);
		ctx->tx_len += len;
		ret = stdio_flush(ctx);
		host_posix_service_update_locked(ctx->service, 1, !ret && ctx->tx_len ? HOST_POSIX_WRITE : 0);
	}
	host_posix_service_unlock(ctx->service);
	return ret;
}

static void stdio_consumed(void *data)
{
	struct char_posix_ctx *ctx = data;
	host_posix_service_lock(ctx->service);
	if (!ctx->rx_closed)
		host_posix_service_update_locked(ctx->service, 0, HOST_POSIX_READ);
	host_posix_service_unlock(ctx->service);
}

/**
 * char_posix_rx_cb - handle console input readiness and errors
 * @events: HOST_POSIX_* readiness and error flags
 * @data: opaque closure pointing to the character device instance
 */
static void char_posix_rx_cb(struct host_posix_service *service, uint32_t events, void *data)
{
	struct char_backend *dev = data;
	struct char_posix_ctx *ctx = dev->priv;
	uint8_t rx_buf[64];
	uint8_t filtered_buf[64];
	size_t filtered_len = 0;
	ssize_t ret;
	ssize_t i;

	if (events & HOST_POSIX_ERROR) {
		ctx->rx_closed = true;
		host_posix_service_update_locked(service, 0, 0);
		stdio_fail(ctx, -VM_EIO);
		return;
	}
	if (unlikely(!(events & HOST_POSIX_READ)))
		return;

	do {
		ret = read(STDIN_FILENO, rx_buf, ctx->rx_limit < sizeof(rx_buf) ? ctx->rx_limit : sizeof(rx_buf));
	} while (ret < 0 && errno == EINTR);
	if (unlikely(ret <= 0)) {
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		int error = ret < 0 ? -host_error_from_errno(errno) : 0;
		ctx->rx_closed = true;
		host_posix_service_update_locked(service, 0, 0);
		if (error)
			stdio_fail(ctx, error);
		return;
	}

	for (i = 0; i < ret; i++) {
		if (unlikely(ctx->escape_pending)) {
			ctx->escape_pending = false;

			if (rx_buf[i] == 'x' || rx_buf[i] == 'X') {
				pr_info("caught Ctrl+a x, requesting shutdown...\n");
				host_posix_service_update_locked(service, 0, 0);
				io_receiver_notify(ctx->binding, 0);
				return;
			} else if (rx_buf[i] == ESCAPE_CHAR) {
				filtered_buf[filtered_len++] = ESCAPE_CHAR;
			} else {
				pr_info("Ctrl+a %c is not supported (only 'x' to exit)\n", (rx_buf[i] >= 32 && rx_buf[i] <= 126) ? rx_buf[i] : '?');
			}
		} else if (unlikely(rx_buf[i] == ESCAPE_CHAR)) {
			ctx->escape_pending = true;
		} else {
			filtered_buf[filtered_len++] = rx_buf[i];
		}
	}

	if (filtered_len) {
		host_posix_service_update_locked(service, 0, 0);
		io_receiver_submit(ctx->binding, filtered_buf, filtered_len, stdio_consumed, ctx);
	}
}

static void stdio_failed(void *data, int error)
{
	stdio_fail(data, error);
}

static int char_posix_bind(struct char_backend *dev, struct io_receiver *binding, size_t rx_limit)
{
	struct char_posix_ctx *ctx = dev->priv;
	ctx->binding = binding;
	ctx->rx_limit = rx_limit;
	ctx->escape_pending = false;
	ctx->rx_closed = false;
	struct host_posix_source sources[] = {
		{ STDIN_FILENO, HOST_POSIX_READ, char_posix_rx_cb, dev },
		{ STDOUT_FILENO, 0, stdio_tx_ready, ctx },
	};
	ctx->service = host_posix_service_create(sources, 2, stdio_failed, ctx);
	if (IS_ERR(ctx->service)) {
		int ret = PTR_ERR(ctx->service);
		ctx->service = NULL;
		ctx->binding = NULL;
		return ret;
	}
	return 0;
}

static void char_posix_unbind(struct char_backend *dev)
{
	struct char_posix_ctx *ctx = dev->priv;
	host_posix_service_destroy(ctx->service);
	ctx->service = NULL;
	ctx->binding = NULL;
	ctx->tx_len = 0;
}

static void char_posix_destroy(struct char_backend *dev)
{
	struct char_posix_ctx *ctx;

	if (WARN_ON(!dev))
		return;

	ctx = dev->priv;

	BUG_ON(atomic_load(&dev->binding));
	int ret = stdio_flush(ctx);
	if (ret < 0 || ctx->tx_len)
		pr_warn("console closed with undelivered output\n");
	if (ctx->input_flags >= 0)
		fcntl(STDIN_FILENO, F_SETFL, ctx->input_flags);
	if (ctx->output_flags >= 0)
		fcntl(STDOUT_FILENO, F_SETFL, ctx->output_flags);

	if (ctx->is_saved)
		tcsetattr(STDIN_FILENO, TCSANOW, &ctx->orig_termios);

	free(ctx);
	free(dev);
	atomic_store(&stdio_owned, false);
}

static const struct char_ops char_posix_ops = {
	.write = char_posix_write,
	.bind = char_posix_bind,
	.unbind = char_posix_unbind,
	.destroy = char_posix_destroy,
};

/**
 * char_posix_create - claim process stdin/stdout for console input/output
 * @opts: unused; this backend has no options
 *
 * Uses raw terminal input when stdin is a terminal. The receive callback
 * handles console escape commands before forwarding bytes to the guest.
 *
 * Return: an allocated character device object, or ERR_PTR(-VM_E*) on failure.
 */
static struct char_backend *char_posix_create(const char *opts)
{
	struct char_backend *dev;
	struct char_posix_ctx *ctx;
	struct termios raw;
	int error;

	(void)opts;
	if (atomic_exchange(&stdio_owned, true))
		return ERR_PTR(-VM_EBUSY);

	dev = calloc(1, sizeof(*dev));
	ctx = calloc(1, sizeof(*ctx));
	if (!dev || !ctx) {
		free(dev);
		free(ctx);
		atomic_store(&stdio_owned, false);
		return ERR_PTR(-VM_ENOMEM);
	}

	atomic_init(&dev->binding, NULL);
	dev->name = "posix-stdio";
	dev->ops = &char_posix_ops;
	dev->priv = ctx;

	ctx->input_flags = ctx->output_flags = -1;
	ctx->input_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (ctx->input_flags < 0)
		goto fail;
	ctx->output_flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
	if (ctx->output_flags < 0 || fcntl(STDIN_FILENO, F_SETFL, ctx->input_flags | O_NONBLOCK) < 0 || fcntl(STDOUT_FILENO, F_SETFL, ctx->output_flags | O_NONBLOCK) < 0)
		goto fail;

	if (tcgetattr(STDIN_FILENO, &ctx->orig_termios) == 0) {
		ctx->is_saved = true;
		raw = ctx->orig_termios;
		cfmakeraw(&raw);
		if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
			goto fail;
	}

	return dev;
fail:
	error = -host_error_from_errno(errno);
	char_posix_destroy(dev);
	return ERR_PTR(error);
}

static const struct char_desc posix_stdio_desc = {
	.name = "posix-stdio",
	.create = char_posix_create,
};

static void __attribute__((constructor)) char_posix_register(void)
{
	char_register(&posix_stdio_desc);
}
