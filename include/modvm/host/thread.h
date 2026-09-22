/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_THREAD_H
#define MODVM_HOST_THREAD_H

/**
 * typedef host_thread_func_cb_t - signature for host OS thread entry points
 * @data: pointer to caller-defined execution closure
 */
typedef void *(*host_thread_func_cb_t)(void *data);

struct host_thread;

/* Starts immediately; returns a handle or ERR_PTR(-VM_E*). */
struct host_thread *host_thread_create(host_thread_func_cb_t func, void *data);

/* Waits for termination; returns 0 or negative project error. Does not free the handle. */
int host_thread_join(struct host_thread *thread);

/* Frees the thread handle after join; does not stop or join the worker. */
void host_thread_destroy(struct host_thread *thread);

#endif /* MODVM_HOST_THREAD_H */
