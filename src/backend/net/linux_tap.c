/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/io/receiver.h>
#include <modvm/host/error.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <stdio.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/io/net.h>
#include <modvm/host/posix/service.h>
#include <modvm/util/cmdline.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "linux_tap: " fmt


/**
 * struct net_linux_ctx - Linux TAP backend private context
 * @fd: file descriptor for the /dev/net/tun interface
 * @mac: locally administered MAC address presented to the guest
 */
struct net_linux_ctx {
	int fd;
	struct host_posix_service *service;
	struct io_receiver *binding;
	uint8_t mac[6];
};

/**
 * net_linux_write - push an ethernet frame to the host TAP interface
 * @net: abstract network backend instance
 * @buf: frame payload
 * @len: payload length
 *
 * Hot path transmission function. Employs non-blocking I/O.
 *
 * Return: number of bytes successfully written, or a negative error code.
 */
static ptrdiff_t net_linux_write(struct net_backend *net, const uint8_t *buf, size_t len)
{
	struct net_linux_ctx *ctx = net->priv;
	ssize_t ret;

	if (len > PTRDIFF_MAX || len > SSIZE_MAX)
		return -VM_EOVERFLOW;
	if (unlikely(len == 0))
		return 0;

	do {
		ret = write(ctx->fd, buf, len);
	} while (ret < 0 && errno == EINTR);
	if (unlikely(ret < 0)) {
		return -host_error_from_errno(errno);
	}

	return ret;
}

static void tap_consumed(void *data)
{
	struct net_linux_ctx *ctx = data;
	host_posix_service_lock(ctx->service);
	host_posix_service_update_locked(ctx->service, 0, HOST_POSIX_READ);
	host_posix_service_unlock(ctx->service);
}

/* Native receive processing owns its buffer until the binding copies the frame. */
static void net_linux_rx_cb(struct host_posix_service *service, uint32_t events, void *data)
{
	struct net_linux_ctx *ctx = data;
	uint8_t buf[NET_MAX_FRAME_SIZE];
	ssize_t ret;
	int error;
	if (events & HOST_POSIX_ERROR) {
		error = -VM_EIO;
	} else {
		do {
			ret = read(ctx->fd, buf, sizeof(buf));
		} while (ret < 0 && errno == EINTR);
		if (ret > 0) {
			/* One frame in flight. Rearm only after delivery, not after enqueue. */
			host_posix_service_update_locked(service, 0, 0);
			io_receiver_submit(ctx->binding, buf, ret, tap_consumed, ctx);
			return;
		}
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		error = ret < 0 ? -host_error_from_errno(errno) : -VM_EIO;
	}
	host_posix_service_update_locked(service, 0, 0);
	pr_err("TAP receive failed: %d\n", error);
	io_receiver_notify(ctx->binding, error);
}

static void tap_failed(void *data, int error)
{
	struct net_linux_ctx *ctx = data;
	io_receiver_notify(ctx->binding, error);
}

static int net_linux_bind(struct net_backend *net, struct io_receiver *binding)
{
	struct net_linux_ctx *ctx = net->priv;
	ctx->binding = binding;
	struct host_posix_source source = { ctx->fd, HOST_POSIX_READ, net_linux_rx_cb, ctx };
	ctx->service = host_posix_service_create(&source, 1, tap_failed, ctx);
	if (IS_ERR(ctx->service)) {
		int ret = PTR_ERR(ctx->service);
		ctx->service = NULL;
		ctx->binding = NULL;
		return ret;
	}
	return 0;
}

static void net_linux_unbind(struct net_backend *net)
{
	struct net_linux_ctx *ctx = net->priv;
	host_posix_service_destroy(ctx->service);
	ctx->service = NULL;
	ctx->binding = NULL;
}

static int net_linux_get_mac(struct net_backend *net, uint8_t mac_out[6])
{
	struct net_linux_ctx *ctx = net->priv;

	memcpy(mac_out, ctx->mac, 6);
	return 0;
}

static void net_linux_destroy(struct net_backend *net)
{
	struct net_linux_ctx *ctx;

	if (WARN_ON(!net))
		return;

	ctx = net->priv;
	BUG_ON(atomic_load(&net->binding));
	if (ctx->fd >= 0)
		close(ctx->fd);

	free(ctx);
	free(net);
}

static const struct net_ops net_linux_ops = {
	.write = net_linux_write,
	.bind = net_linux_bind,
	.unbind = net_linux_unbind,
	.get_mac = net_linux_get_mac,
	.destroy = net_linux_destroy,
};

/**
 * net_linux_create - instantiate a Linux TAP interface backend
 * @opts: comma-separated options; optional ifname selects a TAP, otherwise the kernel chooses
 *
 * Utilizes IFF_NO_PI to strip the legacy 4-byte packet information header,
 * so callbacks receive Ethernet frames without a host packet-information prefix.
 *
 * Return: allocated network backend pointer, or ERR_PTR(-VM_E*) on failure.
 */
static struct net_backend *net_linux_create(const char *opts)
{
	struct net_backend *net;
	struct net_linux_ctx *ctx;
	struct ifreq ifr;
	char *ifname;
	int fd, ret = -VM_EINVAL;
	char *mac;

	ifname = cmdline_extract_opt(opts, "ifname");
	if (IS_ERR(ifname))
		return (void *)ifname;
	if (ifname && strlen(ifname) >= IFNAMSIZ) {
		free(ifname);
		return ERR_PTR(-VM_ENAMETOOLONG);
	}

	net = calloc(1, sizeof(*net));
	ctx = calloc(1, sizeof(*ctx));
	if (!net || !ctx) {
		free(ifname);
		free(net);
		free(ctx);
		return ERR_PTR(-VM_ENOMEM);
	}

	fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to open /dev/net/tun: %d\n", errno);
		goto err_free;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	if (ifname && strlen(ifname) > 0)
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to allocate tap interface: %d\n", errno);
		goto err_close;
	}

	ctx->fd = fd;
	mac = cmdline_extract_opt(opts, "mac");
	if (IS_ERR(mac)) {
		ret = PTR_ERR(mac);
		goto err_close;
	}
	if (mac) {
		unsigned octets[6];
		int used = 0;
		int n = sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x%n", &octets[0], &octets[1], &octets[2], &octets[3], &octets[4], &octets[5], &used);
		bool valid = n == 6 && used == 17 && !mac[used];
		free(mac);
		if (!valid)
			goto err_close;
		for (int i = 0; i < 6; i++)
			ctx->mac[i] = octets[i];
		if ((ctx->mac[0] & 1) || !memcmp(ctx->mac, "\0\0\0\0\0\0", 6))
			goto err_close;
	} else {
		ssize_t n;
		do {
			n = getrandom(ctx->mac, 6, 0);
		} while (n < 0 && errno == EINTR);
		if (n != 6) {
			ret = n < 0 ? -host_error_from_errno(errno) : -VM_EIO;
			goto err_close;
		}
		ctx->mac[0] = (ctx->mac[0] & 0xfc) | 2;
	}

	net->name = "linux-tap";
	net->ops = &net_linux_ops;
	net->priv = ctx;

	pr_info("attached to host tap interface '%s'\n", ifr.ifr_name);
	free(ifname);
	return net;

err_close:
	close(fd);
err_free:
	free(ifname);
	free(ctx);
	free(net);
	return ERR_PTR(ret);
}

static const struct net_desc linux_tap_desc = {
	.name = "linux-tap",
	.create = net_linux_create,
};

static void __attribute__((constructor)) net_linux_register(void)
{
	net_register(&linux_tap_desc);
}
