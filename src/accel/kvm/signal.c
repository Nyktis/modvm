/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/host/error.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <modvm/host/posix/thread.h>
#include "signal.h"

static int wakeup_signum = -1;
static pthread_once_t wakeup_once = PTHREAD_ONCE_INIT;

static void kvm_interrupt_handler(int signum)
{
	(void)signum;
}

/**
 * initialize_wakeup_signal - claim an RT signal with default or ignored disposition
 */
static void initialize_wakeup_signal(void)
{
	struct sigaction sa;
	struct sigaction old_sa;
	int signum;

	sa.sa_handler = kvm_interrupt_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	for (signum = SIGRTMIN; signum <= SIGRTMAX; signum++) {
		if (sigaction(signum, NULL, &old_sa) == 0) {
			if (old_sa.sa_handler == SIG_DFL || old_sa.sa_handler == SIG_IGN) {
				if (sigaction(signum, &sa, NULL) == 0) {
					wakeup_signum = signum;
					break;
				}
			}
		}
	}
}

/* The wakeup signal is owned by the process-wide KVM backend. */
int kvm_signal_init(void)
{
	int ret = pthread_once(&wakeup_once, initialize_wakeup_signal);
	return ret ? -host_error_from_errno(ret) : wakeup_signum < 0 ? -VM_ENOSPC : 0;
}

/**
 * kvm_signal_block - block the wakeup signal in the calling thread
 */
int kvm_signal_block(void)
{
	sigset_t mask;

	if (wakeup_signum == -1)
		return -VM_EINVAL;

	sigemptyset(&mask);
	sigaddset(&mask, wakeup_signum);
	return -host_error_from_errno(pthread_sigmask(SIG_BLOCK, &mask, NULL));
}

/* Preserve unrelated signals; KVM_RUN atomically unblocks its interrupt signal. */
int kvm_signal_run_mask(sigset_t *mask)
{
	if (!mask || wakeup_signum == -1)
		return -VM_EINVAL;
	int ret = pthread_sigmask(SIG_SETMASK, NULL, mask);
	if (ret)
		return -host_error_from_errno(ret);
	if (sigdelset(mask, wakeup_signum) < 0)
		return -host_error_from_errno(errno);
	return 0;
}

int kvm_vcpu_kick(struct vcpu *vcpu, struct host_thread *thread)
{
	(void)vcpu;
	return host_posix_thread_signal(thread, wakeup_signum);
}
