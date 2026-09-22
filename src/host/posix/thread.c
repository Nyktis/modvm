/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/host/error.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <modvm/host/thread.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

#include <modvm/host/posix/thread.h>

struct host_thread {
	pthread_t handle;
};

/**
 * host_thread_create - start a POSIX thread and allocate its handle
 * @func: execution entry point
 * @data: closure data
 *
 * Return: allocated thread handle, or ERR_PTR on failure.
 */
struct host_thread *host_thread_create(host_thread_func_cb_t func, void *data)
{
	struct host_thread *thread;
	int ret;

	if (WARN_ON(!func))
		return ERR_PTR(-VM_EINVAL);

	thread = calloc(1, sizeof(*thread));
	if (!thread)
		return ERR_PTR(-VM_ENOMEM);

	ret = pthread_create(&thread->handle, NULL, func, data);
	if (ret != 0) {
		free(thread);
		return ERR_PTR(-host_error_from_errno(ret));
	}

	return thread;
}

/**
 * host_thread_join - block until thread termination
 * @thread: handle to block against
 *
 * Return: 0 on success, or a negative project error.
 */
int host_thread_join(struct host_thread *thread)
{
	if (WARN_ON(!thread))
		return -VM_EINVAL;

	int ret = pthread_join(thread->handle, NULL);
	return -host_error_from_errno(ret);
}

/**
 * host_thread_destroy - free a joined thread handle without stopping or joining it
 * @thread: thread tracking block
 */
void host_thread_destroy(struct host_thread *thread)
{
	free(thread);
}

int host_posix_thread_signal(struct host_thread *thread, int signum)
{
	return -host_error_from_errno(pthread_kill(thread->handle, signum));
}
