/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/host/console.h>
#include <modvm/host/error.h>
#include <modvm/host/posix/io.h>
#include <modvm/util/err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static size_t written;
static bool broken;
ssize_t __wrap_host_posix_write_nosigpipe(int fd, const void *data, size_t size)
{
	CHECK(fd == 2 && size == 6 - written);
	CHECK(!memcmp(data, &"abcdef"[written], size));
	if (broken && written == 2)
		return -VM_EPIPE;
	written += 2;
	return 2;
}

int main(void)
{
	/* Linux x86_64 uses asm-generic errno numbering, with two reserved holes. */
	for (int error = 1; error <= 133; error++) {
		if (error != 41 && error != 58)
			CHECK(host_error_from_errno(error) == error);
	}
	CHECK(host_error_from_errno(41) == VM_EIO);
	CHECK(host_error_from_errno(58) == VM_EIO);
	CHECK(VM_ENOEXEC == ENOEXEC && VM_EDQUOT == EDQUOT);
	CHECK(VM_EKEYREJECTED == EKEYREJECTED && VM_EHWPOISON == EHWPOISON);
	CHECK(VM_EWOULDBLOCK == VM_EAGAIN);
	CHECK(VM_EDEADLOCK == VM_EDEADLK);
	CHECK(VM_ENOTSUP == VM_EOPNOTSUPP);
	CHECK(host_error_from_errno(EDEADLOCK) == VM_EDEADLK);
	CHECK(host_error_from_errno(ENOTSUP) == VM_EOPNOTSUPP);
	CHECK(host_error_from_errno(0) == 0);
	CHECK(host_error_from_errno(EACCES) == VM_EACCES);
	CHECK(host_error_from_errno(ENOMEM) == VM_ENOMEM);
	CHECK(host_error_from_errno(EWOULDBLOCK) == VM_EAGAIN);
	CHECK(host_error_from_errno(INT32_MAX) == VM_EIO);
	CHECK(IS_ERR(ERR_PTR(-VM_EOVERFLOW)) && PTR_ERR(ERR_PTR(-VM_EOVERFLOW)) == -VM_EOVERFLOW);
	int local;
	CHECK(!IS_ERR(&local));
	CHECK(host_console_write(HOST_CONSOLE_ERR, "abcdef", 6) == 0 && written == 6);
	written = 0;
	broken = true;
	CHECK(host_console_write(HOST_CONSOLE_ERR, "abcdef", 6) == -VM_EPIPE && written == 2);
	CHECK(host_console_write((enum host_console_stream)99, "", 0) == -VM_EINVAL);
	puts("native error normalization, error pointers and console short writes passed");
	return 0;
}
