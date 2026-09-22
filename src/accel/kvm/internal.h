/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ACCEL_KVM_INTERNAL_H
#define MODVM_ACCEL_KVM_INTERNAL_H

#include <linux/kvm.h>
#include <modvm/core/vcpu.h>

/* KVM-Specific Typing */
typedef struct {
	int __fd;
} kvm_fd_t;
typedef struct {
	int __fd;
} vm_fd_t;
typedef struct {
	int __fd;
} vcpu_fd_t;
typedef struct {
	uint32_t __val;
} mem_slot_t;

#define TO_KVM_FD(x) ((kvm_fd_t){ (int)(x) })
#define TO_VM_FD(x) ((vm_fd_t){ (int)(x) })
#define TO_VCPU_FD(x) ((vcpu_fd_t){ (int)(x) })
#define TO_MEM_SLOT(x) ((mem_slot_t){ (uint32_t)(x) })

#define FD_VAL(x) ((x).__fd)
#define MEM_SLOT_VAL(x) ((x).__val)

#define INVALID_KVM_FD TO_KVM_FD(-1)
#define INVALID_VM_FD TO_VM_FD(-1)
#define INVALID_VCPU_FD TO_VCPU_FD(-1)

#define IS_VALID_FD(x) (FD_VAL(x) >= 0)

/**
 * struct kvm_state - KVM specific virtual machine acceleration state
 * @kvm_fd: handle to /dev/kvm owned by this accelerator instance
 * @vm_fd: handle to this specific virtual machine instance
 * @mem_slot_idx: counter for allocating sequential hardware memory slots
 */
struct kvm_state {
	kvm_fd_t kvm_fd;
	vm_fd_t vm_fd;
	mem_slot_t mem_slot_idx;
};

/**
 * struct kvm_vcpu_state - KVM specific virtual processor state
 * @vcpu_fd: handle to this specific virtual processor
 * @run_size: size of the memory-mapped hypervisor run structure
 * @run: shared memory region for hypervisor communication
 */
struct kvm_vcpu_state {
	vcpu_fd_t vcpu_fd;
	int run_size;
	struct kvm_run *run;
};

int kvm_arch_vcpu_init(struct vcpu *vcpu);
int kvm_arch_vcpu_get_regs(struct vcpu *vcpu, enum reg_class reg_class, void *buf, size_t size);
int kvm_arch_vcpu_set_regs(struct vcpu *vcpu, enum reg_class reg_class, const void *buf, size_t size);
int kvm_arch_vcpu_get_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t *val);
int kvm_arch_vcpu_set_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t val);
int kvm_arch_vcpu_handle_exit(struct vcpu *vcpu, struct kvm_run *run);

extern const struct vcpu_ops kvm_vcpu_ops;

#endif /* MODVM_ACCEL_KVM_INTERNAL_H */
