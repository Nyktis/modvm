/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_POSIX_THREAD_H
#define MODVM_HOST_POSIX_THREAD_H

struct host_thread;

/* Send a POSIX signal to a live, unjoined thread; return 0 or negative project error. */
int host_posix_thread_signal(struct host_thread *thread, int signum);

#endif /* MODVM_HOST_POSIX_THREAD_H */
