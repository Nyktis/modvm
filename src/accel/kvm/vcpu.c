/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/host/error.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/io_map.h>
#include <modvm/core/vm.h>
#include <modvm/util/log.h>
#include <modvm/host/thread.h>
#include <modvm/util/compiler.h>
#include <modvm/util/types.h>

#include "signal.h"

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_vcpu: " fmt

/**
 * kvm_vcpu_init - request a hardware virtual processor from the host kernel
 * @vcpu: the core vcpu structure to populate
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_vcpu_init(struct vcpu *vcpu)
{
	struct kvm_vcpu_state *vcpu_state;
	struct kvm_state *state = vcpu->accel->priv;
	int raw_fd;
	int ret;

	vcpu_state = calloc(1, sizeof(*vcpu_state));
	if (!vcpu_state)
		return -VM_ENOMEM;

	vcpu_state->vcpu_fd = INVALID_VCPU_FD;
	vcpu->priv = vcpu_state;
	raw_fd = ioctl(FD_VAL(state->vm_fd), KVM_CREATE_VCPU, vcpu->id);
	if (raw_fd < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to instantiate hardware vcpu %d\n", vcpu->id);
		return ret;
	}
	vcpu_state->vcpu_fd = TO_VCPU_FD(raw_fd);

	ret = kvm_arch_vcpu_init(vcpu);
	if (ret < 0)
		return ret;
	vcpu_state->run_size = ioctl(FD_VAL(state->kvm_fd), KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vcpu_state->run_size < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to probe vcpu mmap size\n");
		return ret;
	}

	vcpu_state->run = mmap(NULL, (size_t)vcpu_state->run_size, PROT_READ | PROT_WRITE, MAP_SHARED, FD_VAL(vcpu_state->vcpu_fd), 0);
	if (vcpu_state->run == MAP_FAILED) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to map hypervisor communication window\n");
		return ret;
	}

	pr_info("vcpu %d mapped and initialized\n", vcpu->id);
	return 0;
}

static int kvm_vcpu_get_regs_wrap(struct vcpu *vcpu, enum reg_class reg_class, void *buf, size_t size)
{
	return kvm_arch_vcpu_get_regs(vcpu, reg_class, buf, size);
}

static int kvm_vcpu_set_regs_wrap(struct vcpu *vcpu, enum reg_class reg_class, const void *buf, size_t size)
{
	return kvm_arch_vcpu_set_regs(vcpu, reg_class, buf, size);
}

static int kvm_vcpu_get_reg_wrap(struct vcpu *vcpu, uint64_t reg_id, uint64_t *val)
{
	return kvm_arch_vcpu_get_reg(vcpu, reg_id, val);
}

static int kvm_vcpu_set_reg_wrap(struct vcpu *vcpu, uint64_t reg_id, uint64_t val)
{
	return kvm_arch_vcpu_set_reg(vcpu, reg_id, val);
}

/**
 * kvm_vcpu_handle_mmio_exit - route memory-mapped IO traps to the guest device mappings
 * @vcpu: the trapped virtual processor
 */
static void kvm_vcpu_handle_mmio_exit(struct vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;
	struct io_map *io_map = vcpu->accel->io_map;

	gpa_t gpa = TO_GPA(run->mmio.phys_addr);
	uint8_t size = run->mmio.len;
	uint8_t *data = run->mmio.data;
	uint64_t val = 0;

	if (run->mmio.is_write) {
		memcpy(&val, data, size);
		io_map_write(io_map, IO_MMIO, gpa, val, size);
	} else {
		val = io_map_read(io_map, IO_MMIO, gpa, size);
		memcpy(data, &val, size);
	}
}

static int kvm_vcpu_setup_sigmask(struct kvm_vcpu_state *state)
{
	struct kvm_signal_mask *kvm_mask;
	const uint32_t KERNEL_SIGSET_SIZE = 8;
	int ret;

	kvm_mask = malloc(sizeof(*kvm_mask) + KERNEL_SIGSET_SIZE);
	if (!kvm_mask)
		return -VM_ENOMEM;

	kvm_mask->len = KERNEL_SIGSET_SIZE;

	sigset_t mask;
	ret = kvm_signal_run_mask(&mask);
	if (ret < 0) {
		free(kvm_mask);
		return ret;
	}

	uint64_t bits = 0;
	for (unsigned signum = 1; signum <= 64; signum++) {
		if (sigismember(&mask, signum) == 1)
			bits |= UINT64_C(1) << (signum - 1);
	}
	memcpy(kvm_mask->sigset, &bits, sizeof(bits));
	ret = ioctl(FD_VAL(state->vcpu_fd), KVM_SET_SIGNAL_MASK, kvm_mask);
	int error = host_error_from_errno(errno);
	free(kvm_mask);

	if (ret < 0) {
		pr_err("failed to inject atomic signal mask: %d\n", error);
		return -error;
	}

	return 0;
}

/**
 * kvm_vcpu_run - enter the execution loop of the hardware processor
 * @vcpu: the virtual processor to run
 *
 * Return: 0 on successful exit, or a negative error code.
 */
static int kvm_vcpu_run(struct vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;
	int ret;

	pr_info("vcpu %d transitioning into hardware execution\n", vcpu->id);

	ret = kvm_signal_block();
	if (ret < 0)
		return ret;

	ret = kvm_vcpu_setup_sigmask(state);
	if (ret < 0)
		return ret;

	for (;;) {
		if (unlikely(atomic_load(vcpu->accel->stop_requested)))
			return 0;

		ret = ioctl(FD_VAL(state->vcpu_fd), KVM_RUN, 0);
		if (unlikely(ret < 0)) {
			if (likely(errno == EINTR || errno == EAGAIN))
				continue;

			int error = host_error_from_errno(errno);
			pr_err("KVM_RUN ioctl critically failed: %d\n", error);
			return -error;
		}

		switch (run->exit_reason) {
		case KVM_EXIT_MMIO:
			kvm_vcpu_handle_mmio_exit(vcpu);
			break;

		case KVM_EXIT_HLT:
			pr_info("vcpu %d received halt instruction\n", vcpu->id);
			return 0;

		case KVM_EXIT_INTERNAL_ERROR:
			pr_err("hypervisor internal error, suberror code: %d\n", run->internal.suberror);
			return -VM_EFAULT;

		default:
			ret = kvm_arch_vcpu_handle_exit(vcpu, run);
			if (unlikely(ret < 0))
				return ret;
			break;
		}
	}
}

/**
 * kvm_vcpu_destroy - safely unmap and release hardware vCPU allocations
 * @vcpu: the virtual processor to destroy
 */
static void kvm_vcpu_destroy(struct vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;

	if (!state)
		return;

	if (state->run && state->run != MAP_FAILED)
		munmap(state->run, (size_t)state->run_size);
	if (IS_VALID_FD(state->vcpu_fd))
		close(FD_VAL(state->vcpu_fd));

	free(state);
	vcpu->priv = NULL;
}

const struct vcpu_ops kvm_vcpu_ops = {
	.init = kvm_vcpu_init,
	.destroy = kvm_vcpu_destroy,
	.get_regs = kvm_vcpu_get_regs_wrap,
	.set_regs = kvm_vcpu_set_regs_wrap,
	.get_reg = kvm_vcpu_get_reg_wrap,
	.set_reg = kvm_vcpu_set_reg_wrap,
	.run = kvm_vcpu_run,
	.kick = kvm_vcpu_kick,
};
