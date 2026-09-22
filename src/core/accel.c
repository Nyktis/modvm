/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/accel.h>
#include <modvm/core/vcpu.h>
#include <modvm/errno.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "accel: " fmt

#define MAX_ACCEL_DESCS 8

static const struct accel_desc *accel_descs[MAX_ACCEL_DESCS];
static unsigned int nr_accel_descs;

/**
 * accel_register - statically register a hypervisor backend
 * @desc: the acceleration engine definition
 */
void accel_register(const struct accel_desc *desc)
{
	if (!desc || !desc->name || !*desc->name ||
	    !desc->accel_ops || !desc->accel_ops->init || !desc->accel_ops->destroy ||
	    !desc->vcpu_ops || !desc->vcpu_ops->init || !desc->vcpu_ops->run)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_accel_descs; i++) {
		if (!strcmp(accel_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_accel_descs == MAX_ACCEL_DESCS)
		panic(pr_fmt("registry full\n"));

	accel_descs[nr_accel_descs++] = desc;
}

/**
 * accel_find - retrieve an acceleration engine by its identifier
 * @name: the string identifier of the backend type
 *
 * Return: pointer to the backend definition, or NULL if unavailable.
 */
const struct accel_desc *accel_find(const char *name)
{
	if (WARN_ON(!name))
		return NULL;

	for (unsigned int i = 0; i < nr_accel_descs; i++) {
		if (!strcmp(accel_descs[i]->name, name))
			return accel_descs[i];
	}

	return NULL;
}

/**
 * accel_init - instantiate the virtual machine acceleration context
 * @accel: the acceleration state to initialize
 * @name: the requested acceleration backend name
 * @io_map: guest device mappings for MMIO/PIO exit dispatch
 * @stop_requested: borrowed VM stop request, valid through accelerator destruction
 *
 * Binds the abstract accelerator object to a specific platform driver
 * and triggers its initialization sequence.
 *
 * Return: 0 on success, or a negative error code.
 */
int accel_init(struct accel *accel, const char *name, struct io_map *io_map, const atomic_bool *stop_requested)
{
	if (WARN_ON(!accel || !name || !io_map || !stop_requested))
		return -VM_EINVAL;

	accel->desc = accel_find(name);
	if (!accel->desc) {
		pr_err("acceleration backend '%s' is not supported on this host\n", name);
		return -VM_ENOENT;
	}

	accel->io_map = io_map;
	accel->stop_requested = stop_requested;

	return accel->desc->accel_ops->init(accel);
}

/**
 * accel_setup_irqchip - synthesize architectural interrupt routing
 * @accel: the initialized acceleration context
 *
 * Delegates interrupt controller initialization to the selected accelerator.
 *
 * Return: 0 on success, or a negative error code.
 */
int accel_setup_irqchip(struct accel *accel)
{
	if (WARN_ON(!accel || !accel->desc))
		return -VM_EINVAL;

	if (!accel->desc->accel_ops->setup_irqchip)
		return -VM_ENOTSUP;

	return accel->desc->accel_ops->setup_irqchip(accel);
}

/**
 * accel_set_irq - inject a hardware interrupt signal into the guest
 * @accel: the acceleration context
 * @gsi: the Global System Interrupt number to assert
 * @level: interrupt level (0 deasserted, 1 asserted)
 *
 * Return: 0 on success, or a negative error code.
 */
int accel_set_irq(struct accel *accel, gsi_t gsi, int level)
{
	if (WARN_ON(!accel || !accel->desc))
		return -VM_EINVAL;

	if (!accel->desc->accel_ops->set_irq)
		return -VM_ENOTSUP;

	return accel->desc->accel_ops->set_irq(accel, gsi, level);
}

/**
 * accel_destroy - tear down the accelerator and release host resources
 * @accel: the context to destroy
 */
void accel_destroy(struct accel *accel)
{
	if (!accel || !accel->desc)
		return;

	if (accel->desc->accel_ops->destroy)
		accel->desc->accel_ops->destroy(accel);
}
