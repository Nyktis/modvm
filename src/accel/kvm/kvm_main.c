/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/host/error.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <modvm/core/accel.h>
#include <modvm/core/memory.h>
#include <modvm/core/vcpu.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/types.h>

#include "internal.h"
#include "signal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm: " fmt

/**
 * kvm_accel_map_ram - bridge between core memory allocator and KVM hardware paging
 * @accel: the accelerator receiving the RAM mapping
 * @reg: the specific memory region to map
 *
 * Invoked during VM construction when the core allocates a new memory region.
 * It strictly binds the host virtual address to the guest physical address
 * via the KVM_SET_USER_MEMORY_REGION ioctl.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_map_ram(struct accel *accel, const struct mem_region *reg)
{
	struct kvm_state *state = accel->priv;

	uint32_t slot_val = MEM_SLOT_VAL(state->mem_slot_idx);

	struct kvm_userspace_memory_region hw_reg = {
		.slot = slot_val,
		.guest_phys_addr = GPA_VAL(reg->gpa),
		.memory_size = reg->size,
		.userspace_addr = (uint64_t)reg->hva,
		.flags = 0,
	};

	if (reg->flags & MEM_READONLY)
		hw_reg.flags |= KVM_MEM_READONLY;

	if (ioctl(FD_VAL(state->vm_fd), KVM_SET_USER_MEMORY_REGION, &hw_reg) < 0) {
		int error = host_error_from_errno(errno);
		pr_err("failed to commit hardware memory slot: %d\n", error);
		return -error;
	}

	state->mem_slot_idx = TO_MEM_SLOT(slot_val + 1);

	return 0;
}

/**
 * kvm_accel_init - initialize the KVM acceleration context
 * @accel: the acceleration engine object to populate
 *
 * Opens the hypervisor device node, validates the API version, and creates
 * the root virtual machine file descriptor.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_init(struct accel *accel)
{
	struct kvm_state *state;
	int raw_fd;
	int ret;

	ret = kvm_signal_init();
	if (ret < 0)
		return ret;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -VM_ENOMEM;

	state->kvm_fd = INVALID_KVM_FD;
	state->vm_fd = INVALID_VM_FD;
	accel->priv = state;
	raw_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (raw_fd < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to open hypervisor device node\n");
		return ret;
	}
	state->kvm_fd = TO_KVM_FD(raw_fd);

	ret = ioctl(FD_VAL(state->kvm_fd), KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		pr_err("unsupported hypervisor api version: %d\n", ret);
		ret = -VM_ENOTSUP;
		return ret;
	}

	raw_fd = ioctl(FD_VAL(state->kvm_fd), KVM_CREATE_VM, 0);
	if (raw_fd < 0) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to instantiate virtual machine container\n");
		return ret;
	}
	state->vm_fd = TO_VM_FD(raw_fd);

	state->mem_slot_idx = TO_MEM_SLOT(0);


	pr_info("acceleration container established successfully\n");
	return 0;
}

/**
 * kvm_accel_irqchip_setup - synthesize architectural interrupt controllers
 * @accel: the initialized acceleration context
 *
 * Requests the kernel to instantiate the local APIC, IOAPIC, and legacy PIT.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_irqchip_setup(struct accel *accel)
{
	struct kvm_pit_config pit_conf = { .flags = 0 };
	struct kvm_state *state = accel->priv;

	if (ioctl(FD_VAL(state->vm_fd), KVM_CREATE_IRQCHIP, 0) < 0) {
		int error = host_error_from_errno(errno);
		pr_err("failed to synthesize architectural irqchip: %d\n", error);
		return -error;
	}

	if (ioctl(FD_VAL(state->vm_fd), KVM_CREATE_PIT2, &pit_conf) < 0) {
		int error = host_error_from_errno(errno);
		pr_err("failed to synthesize programmable interval timer: %d\n", error);
		return -error;
	}

	pr_info("architectural interrupt routing online\n");
	return 0;
}

/**
 * kvm_accel_set_irq - inject a hardware interrupt signal
 * @accel: the acceleration context
 * @gsi: the Global System Interrupt number
 * @level: interrupt level (0 deasserted, 1 asserted)
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_set_irq(struct accel *accel, gsi_t gsi, int level)
{
	struct kvm_irq_level irq_level;
	struct kvm_state *state = accel->priv;

	irq_level.irq = GSI_VAL(gsi);
	irq_level.level = level;

	if (ioctl(FD_VAL(state->vm_fd), KVM_IRQ_LINE, &irq_level) < 0) {
		int error = host_error_from_errno(errno);
		pr_err("failed to assert hardware irq line %u\n", GSI_VAL(gsi));
		return -error;
	}

	return 0;
}

/**
 * kvm_accel_destroy - release host resources tied to the KVM subsystem
 * @accel: the acceleration context to tear down
 */
static void kvm_accel_destroy(struct accel *accel)
{
	struct kvm_state *state = accel->priv;

	if (!state)
		return;
	if (IS_VALID_FD(state->vm_fd))
		close(FD_VAL(state->vm_fd));
	if (IS_VALID_FD(state->kvm_fd))
		close(FD_VAL(state->kvm_fd));
	free(state);
	accel->priv = NULL;
}

static const struct accel_ops kvm_ops = {
	.init = kvm_accel_init,
	.destroy = kvm_accel_destroy,
	.map_ram = kvm_accel_map_ram,
	.setup_irqchip = kvm_accel_irqchip_setup,
	.set_irq = kvm_accel_set_irq,
};

static const struct accel_desc kvm_desc = {
	.name = "kvm",
	.accel_ops = &kvm_ops,
	.vcpu_ops = &kvm_vcpu_ops,
};

static void __attribute__((constructor)) register_kvm_backend(void)
{
	accel_register(&kvm_desc);
}
