/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_CONSOLE_H
#define MODVM_HOST_CONSOLE_H

#include <stddef.h>

enum host_console_stream {
	HOST_CONSOLE_OUT,
	HOST_CONSOLE_ERR,
};

/* Return 0 after writing all bytes, or a negative project error. A failed write
 * may already have emitted a prefix. The buffer is borrowed only during the call. */
int host_console_write(enum host_console_stream stream, const void *data, size_t size);

#endif /* MODVM_HOST_CONSOLE_H */
