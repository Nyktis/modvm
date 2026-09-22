/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <limits.h>
#include <modvm/host/console.h>
#include <modvm/host/posix/io.h>
#include <modvm/errno.h>

int host_console_write(enum host_console_stream stream, const void *data, size_t size)
{
	if (stream != HOST_CONSOLE_OUT && stream != HOST_CONSOLE_ERR)
		return -VM_EINVAL;
	int fd = stream == HOST_CONSOLE_ERR ? STDERR_FILENO : STDOUT_FILENO;
	const char *p = data;
	while (size) {
		size_t chunk = size > SSIZE_MAX ? SSIZE_MAX : size;
		ssize_t n = host_posix_write_nosigpipe(fd, p, chunk);
		if (n <= 0)
			return n < 0 ? (int)n : -VM_EIO;
		size -= (size_t)n;
		p += n;
	}
	return 0;
}
