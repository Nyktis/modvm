/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_KVM_SIGNAL_H
#define MODVM_KVM_SIGNAL_H

#include <signal.h>

struct vcpu;
struct host_thread;

/* Process-wide reservation, performed before any KVM worker starts. */
int kvm_signal_init(void);
/* Block outside KVM_RUN; errors must prevent entry into guest execution. */
int kvm_signal_block(void);
/* Current thread mask with the KVM interrupt signal removed. */
int kvm_signal_run_mask(sigset_t *mask);
int kvm_vcpu_kick(struct vcpu *vcpu, struct host_thread *thread);

#endif /* MODVM_KVM_SIGNAL_H */
