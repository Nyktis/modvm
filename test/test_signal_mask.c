/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/accel/kvm/signal.h"
#include <modvm/host/thread.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
int main(void)
{
	struct {
		unsigned char before[32];
		sigset_t mask;
		unsigned char after[32];
	} data;
	memset(&data, 0xa5, sizeof(data));
	CHECK(kvm_signal_run_mask(&data.mask) == -VM_EINVAL);
	CHECK(kvm_signal_block() == -VM_EINVAL);
	CHECK(kvm_signal_init() == 0);
	CHECK(kvm_signal_run_mask(NULL) == -VM_EINVAL);
	sigset_t original, block;
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	CHECK(pthread_sigmask(SIG_BLOCK, &block, &original) == 0);
	CHECK(kvm_signal_block() == 0);
	CHECK(kvm_signal_run_mask(&data.mask) == 0);
	CHECK(sigismember(&data.mask, SIGUSR1) == 1);
	for (unsigned i = 0; i < 32; i++)
		CHECK(data.before[i] == 0xa5 && data.after[i] == 0xa5);
	CHECK(pthread_sigmask(SIG_SETMASK, &original, NULL) == 0);
	puts("native signal mask size, initialized state and unrelated signals preserved");
	return 0;
}
