/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/host/error.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <limits.h>

#include <modvm/io/block.h>
#include <modvm/util/cmdline.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "linux_file: " fmt

struct block_linux_ctx {
	int fd;
	uint64_t capacity;
	bool readonly;
};

/**
 * linux_transfer - synchronously transfer a complete byte range to or from the host file
 * @blk: the block backend instance
 * @buf: source for writes or destination for reads
 * @count: number of bytes to transfer
 * @offset: absolute byte offset within the file
 * @write_op: true for pwrite, false for pread
 *
 * Return: count on success, or negative project error; failure may follow partial I/O.
 */
static ptrdiff_t linux_transfer(struct block_backend *blk, void *buf, size_t count, uint64_t offset, bool write_op)
{
	struct block_linux_ctx *ctx = blk->priv;
	size_t done = 0;
	if (write_op && ctx->readonly)
		return -VM_EROFS;
	if (offset > ctx->capacity || count > ctx->capacity - offset || offset > INT64_MAX || count > INT64_MAX - offset || count > PTRDIFF_MAX)
		return -VM_EINVAL;
	while (done < count) {
		size_t chunk = count - done > SSIZE_MAX ? SSIZE_MAX : count - done;
		ssize_t n = write_op ? pwrite(ctx->fd, (char *)buf + done, chunk, offset + done) : pread(ctx->fd, (char *)buf + done, chunk, offset + done);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -host_error_from_errno(errno);
		}
		if (!n)
			return -VM_EIO;
		done += n;
	}
	return done;
}

static ptrdiff_t block_linux_read(struct block_backend *blk, void *buf, size_t count, uint64_t offset)
{
	return linux_transfer(blk, buf, count, offset, false);
}

static ptrdiff_t block_linux_write(struct block_backend *blk, const void *buf, size_t count, uint64_t offset)
{
	return linux_transfer(blk, (void *)buf, count, offset, true);
}

static int block_linux_flush(struct block_backend *blk)
{
	struct block_linux_ctx *ctx = blk->priv;
	int ret;
	do {
		ret = fsync(ctx->fd);
	} while (ret < 0 && errno == EINTR);
	return ret < 0 ? -host_error_from_errno(errno) : 0;
}

/**
 * block_linux_get_capacity - retrieve the cached file or block-device capacity
 * @blk: the block backend instance
 *
 * Return: total capacity in bytes.
 */
static uint64_t block_linux_get_capacity(struct block_backend *blk)
{
	struct block_linux_ctx *ctx = blk->priv;
	return ctx->capacity;
}

static void block_linux_destroy(struct block_backend *blk)
{
	struct block_linux_ctx *ctx;

	if (WARN_ON(!blk))
		return;

	ctx = blk->priv;
	if (ctx->fd >= 0)
		close(ctx->fd);

	free(ctx);
	free(blk);
}

static const struct block_ops block_linux_ops = {
	.flush = block_linux_flush,
	.read = block_linux_read,
	.write = block_linux_write,
	.get_capacity = block_linux_get_capacity,
	.destroy = block_linux_destroy,
};

/**
 * block_linux_create - instantiate a file-backed block storage device
 * @opts: comma-separated options; path is required, readonly=on requests read-only access
 *
 * Return: initialized block backend pointer, or ERR_PTR(-VM_E*) on failure.
 */
static struct block_backend *block_linux_create(const char *opts)
{
	struct block_backend *blk;
	struct block_linux_ctx *ctx;
	struct stat st;
	char *path;
	char *ro_str;
	bool readonly = false;
	int flags, ret = -VM_EINVAL;

	path = cmdline_extract_opt(opts, "path");
	if (IS_ERR(path))
		return (void *)path;
	if (!path) {
		pr_err("linux-file requires 'path=' argument\n");
		return ERR_PTR(-VM_EINVAL);
	}

	ro_str = cmdline_extract_opt(opts, "readonly");
	if (IS_ERR(ro_str)) {
		free(path);
		return (void *)ro_str;
	}
	if (ro_str) {
		if (!strcmp(ro_str, "on") || !strcmp(ro_str, "1"))
			readonly = true;
		else if (strcmp(ro_str, "off") && strcmp(ro_str, "0")) {
			free(ro_str);
			free(path);
			return ERR_PTR(-VM_EINVAL);
		}
	}
	free(ro_str);

	blk = calloc(1, sizeof(*blk));
	ctx = calloc(1, sizeof(*ctx));
	if (!blk || !ctx) {
		free(path);
		free(blk);
		free(ctx);
		return ERR_PTR(-VM_ENOMEM);
	}

	flags = readonly ? O_RDONLY : O_RDWR;
	ctx->fd = open(path, flags | O_CLOEXEC);
	if (ctx->fd < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to open backing image '%s': %d\n", path, errno);
		goto err_free;
	}

	if (fstat(ctx->fd, &st) < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to probe image capacity '%s': %d\n", path, errno);
		goto err_close;
	}

	if (!S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode)) {
		pr_err("backing image '%s' must be a regular file or block device\n", path);
		goto err_close;
	}

	if (S_ISBLK(st.st_mode)) {
		if (ioctl(ctx->fd, BLKGETSIZE64, &ctx->capacity) < 0) {
			ret = -host_error_from_errno(errno);
			goto err_close;
		}
	} else {
		if (st.st_size < 0)
			goto err_close;
		ctx->capacity = (uint64_t)st.st_size;
	}
	blk->readonly = readonly;
	ctx->readonly = readonly;

	blk->name = "linux-file";
	blk->ops = &block_linux_ops;
	blk->priv = ctx;

	pr_info("mounted block backend '%s', capacity: %llu MB%s\n", path, (unsigned long long)(ctx->capacity / (1024 * 1024)), readonly ? " (RO)" : "");

	free(path);
	return blk;

err_close:
	close(ctx->fd);
err_free:
	free(path);
	free(ctx);
	free(blk);
	return ERR_PTR(ret);
}

static const struct block_desc linux_file_desc = {
	.name = "linux-file",
	.create = block_linux_create,
};

static void __attribute__((constructor)) block_linux_register(void)
{
	block_register(&linux_file_desc);
}
