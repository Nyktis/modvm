/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_POSIX_IO_H
#define MODVM_HOST_POSIX_IO_H

#include <stddef.h>
#include <sys/types.h>

/* POSIX descriptor I/O: retry EINTR, return a possibly short count or negative
 * project error, and suppress SIGPIPE caused by this write. No ownership transfer. Reject sizes above SSIZE_MAX. */
ssize_t host_posix_write_nosigpipe(int fd, const void *data, size_t size);

#endif /* MODVM_HOST_POSIX_IO_H */
