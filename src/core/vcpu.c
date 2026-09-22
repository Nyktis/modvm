/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/accel.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

/**
 * vcpu_init - allocate and map a virtual processor
 * @vcpu: the vcpu structure to populate
 * @accel: the parent acceleration container
 * @id: sequential architectural ID
 *
 * Return: 0 on success, or a negative error code.
 */
int vcpu_init(struct vcpu *vcpu, struct accel *accel, int id)
{
	if (WARN_ON(!vcpu || !accel || !accel->desc))
		return -VM_EINVAL;

	vcpu->id = id;
	vcpu->accel = accel;
	vcpu->ops = accel->desc->vcpu_ops;

	return vcpu->ops->init(vcpu);
}

/**
 * vcpu_get_regs - fetch architectural register state from backend
 * @vcpu: the virtual processor
 * @reg_class: the identifier of the register group to read
 * @buf: destination buffer
 * @size: expected size of the architectural register structure
 *
 * Return: 0 on success, or a negative error code.
 */
int vcpu_get_regs(struct vcpu *vcpu, enum reg_class reg_class, void *buf, size_t size)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !buf || size == 0))
		return -VM_EINVAL;

	if (!vcpu->ops->get_regs)
		return -VM_ENOTSUP;

	return vcpu->ops->get_regs(vcpu, reg_class, buf, size);
}

/**
 * vcpu_set_regs - commit architectural register state to backend
 * @vcpu: the virtual processor
 * @reg_class: the identifier of the register group to write
 * @buf: source buffer containing the new state
 * @size: expected size of the architectural register structure
 *
 * Return: 0 on success, or a negative error code.
 */
int vcpu_set_regs(struct vcpu *vcpu, enum reg_class reg_class, const void *buf, size_t size)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !buf || size == 0))
		return -VM_EINVAL;

	if (!vcpu->ops->set_regs)
		return -VM_ENOTSUP;

	return vcpu->ops->set_regs(vcpu, reg_class, buf, size);
}

/**
 * vcpu_get_reg - read a single abstract register
 * @vcpu: the virtual processor
 * @reg_id: abstract architectural register identifier
 * @val: pointer to store the retrieved value
 *
 * Return: 0 on success, or a negative error code.
 */
int vcpu_get_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t *val)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !val))
		return -VM_EINVAL;

	if (!vcpu->ops->get_reg)
		return -VM_ENOTSUP;

	return vcpu->ops->get_reg(vcpu, reg_id, val);
}

/**
 * vcpu_set_reg - write a single abstract register
 * @vcpu: the virtual processor
 * @reg_id: abstract architectural register identifier
 * @val: the payload to commit
 *
 * Return: 0 on success, or a negative error code.
 */
int vcpu_set_reg(struct vcpu *vcpu, uint64_t reg_id, uint64_t val)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -VM_EINVAL;

	if (!vcpu->ops->set_reg)
		return -VM_ENOTSUP;

	return vcpu->ops->set_reg(vcpu, reg_id, val);
}

/**
 * vcpu_run - transition into the hardware execution loop
 * @vcpu: the virtual processor to run
 *
 * Return: 0 on successful guest exit, or a negative error code.
 */
int vcpu_run(struct vcpu *vcpu)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -VM_EINVAL;

	return vcpu->ops->run(vcpu);
}

/**
 * vcpu_destroy - safely release hardware processor resources
 * @vcpu: the virtual processor to destroy
 */
void vcpu_destroy(struct vcpu *vcpu)
{
	if (!vcpu || !vcpu->ops)
		return;

	if (vcpu->ops->destroy)
		vcpu->ops->destroy(vcpu);
}
