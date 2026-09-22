/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/host/error.h>
#include <modvm/host/posix/io.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

ssize_t host_posix_write_nosigpipe(int fd, const void *data, size_t size)
{
	if (size > SSIZE_MAX)
		return -VM_EOVERFLOW;
	sigset_t blocked, previous, pending;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	int ret = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
	if (ret)
		return -host_error_from_errno(ret);
	if (sigpending(&pending) < 0) {
		ret = -host_error_from_errno(errno);
		pthread_sigmask(SIG_SETMASK, &previous, NULL);
		return ret;
	}
	bool was_pending = sigismember(&pending, SIGPIPE) == 1;
	ssize_t count;
	do {
		count = write(fd, data, size);
	} while (count < 0 && errno == EINTR);
	int error = count < 0 ? errno : 0;
	/* Consume only the signal produced by this write, preserving earlier signals. */
	if (error == EPIPE && !was_pending) {
		const struct timespec timeout = { 0 };
		while (sigtimedwait(&blocked, NULL, &timeout) < 0 && errno == EINTR)
			;
	}
	pthread_sigmask(SIG_SETMASK, &previous, NULL);
	return error ? -host_error_from_errno(error) : count;
}
