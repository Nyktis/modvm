/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VCPU_H
#define MODVM_CORE_VCPU_H

#include <modvm/errno.h>
#include <stdint.h>
#include <modvm/util/stddef.h>

struct accel;
struct vcpu;
struct host_thread;

/**
 * enum reg_class - generic identifiers for architectural register groups
 * @REG_GPR: general purpose registers (e.g., RAX on x86, X0 on ARM)
 * @REG_SREGS: special and system control registers (e.g., CR0 on x86, SCTLR_EL1 on ARM)
 */
enum reg_class {
	REG_GPR,
	REG_SREGS,
};

/**
 * struct vcpu_ops - backend operations for virtual processors
 * @init: initialize processor state
 * @destroy: ends processor lifetime and frees private resources
 * @get_regs: read a complete architectural register group
 * @set_regs: write a complete architectural register group
 * @get_reg: read a single architectural register by abstract ID
 * @set_reg: write a single architectural register by abstract ID
 * @kick: interrupt execution after stop is published; called with a live thread handle
 * @run: enter hardware/emulator execution loop
 */
/* init and run are mandatory. Register accessors are optional (-VM_ENOTSUP).
 * destroy is optional for stateless implementations; otherwise it must handle
 * partial init. Register access requires a stopped vCPU or its executing thread. */
struct vcpu_ops {
	int (*init)(struct vcpu *vcpu);
	void (*destroy)(struct vcpu *vcpu);
	int (*get_regs)(struct vcpu *vcpu, enum reg_class reg_class, void *buf, size_t size);
	int (*set_regs)(struct vcpu *vcpu, enum reg_class reg_class, const void *buf, size_t size);
	int (*get_reg)(struct vcpu *vcpu, uint64_t reg_id, uint64_t *val);
	int (*set_reg)(struct vcpu *vcpu, uint64_t reg_id, uint64_t val);
	/* Optional only when run observes stop without an external interrupt.
	 * May race run entry/exit; must not acquire the VM thread lock.
	 * Return 0 or negative project error. The caller keeps the thread unjoined. */
	int (*kick)(struct vcpu *vcpu, struct host_thread *thread);
	int (*run)(struct vcpu *vcpu);
};

/**
 * struct vcpu - virtual processor instance
 * @id: VM-local sequential processor index; mapped to an architectural ID by the accelerator
 * @accel: pointer to the parent virtualization engine context
 * @ops: cached pointer to the hardware operations table
 * @priv: opaque pointer to accelerator-specific CPU state
 */
struct vcpu {
	int id;
	struct accel *accel;
	const struct vcpu_ops *ops;
	void *priv;
};

/* The owner registers destruction before init. Published private state remains
 * owned on failure and is released by destroy, not by the failed init path. */
int vcpu_init(struct vcpu *vcpu, struct accel *accel, int id);
int vcpu_get_regs(struct vcpu *vcpu, enum reg_class reg_class, void *buf, size_t size);
int vcpu_set_regs(struct vcpu *vcpu, enum reg_class reg_class, const void *buf, size_t size);
int vcpu_get_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t *val);
int vcpu_set_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t val);
int vcpu_run(struct vcpu *vcpu);
void vcpu_destroy(struct vcpu *vcpu);

#endif /* MODVM_CORE_VCPU_H */
