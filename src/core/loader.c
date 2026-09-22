/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/loader.h>
#include <modvm/core/vm.h>
#include <modvm/errno.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

#define MAX_LOADER_DESCS 8

static const struct loader_desc *loader_descs[MAX_LOADER_DESCS];
static unsigned int nr_loader_descs;

struct loader_instance_ctx {
	const struct loader_desc *desc;
	void *priv;
};

static void loader_instance_cleanup(void *data)
{
	struct loader_instance_ctx *inst = data;

	if (inst->desc->destroy && inst->priv)
		inst->desc->destroy(inst->priv);
}

/**
 * loader_register - register a boot protocol implementation
 * @desc: the loader implementation description to expose to the system
 */
void loader_register(const struct loader_desc *desc)
{
	if (!desc || !desc->name || !*desc->name || !desc->load || !desc->setup_bsp)
		panic(pr_fmt("invalid description\n"));

	for (unsigned int i = 0; i < nr_loader_descs; i++) {
		if (!strcmp(loader_descs[i]->name, desc->name))
			panic(pr_fmt("duplicate registration: %s\n"), desc->name);
	}

	if (nr_loader_descs == MAX_LOADER_DESCS)
		panic(pr_fmt("registry full\n"));

	loader_descs[nr_loader_descs++] = desc;
}

static const struct loader_desc *loader_find(const char *name)
{
	if (WARN_ON(!name))
		return NULL;

	for (unsigned int i = 0; i < nr_loader_descs; i++) {
		if (!strcmp(loader_descs[i]->name, name))
			return loader_descs[i];
	}

	return NULL;
}

/**
 * loader_execute - load guest images and initialize the bootstrap processor
 * @ctx: the owning VM context
 * @name: the string identifier of the requested protocol
 * @opts: configuration string consumed by the selected loader
 *
 * This function decouples the motherboard topology from the software boot
 * process. It delegates memory injection and CPU state manipulation to the
 * selected loader, managing its lifecycle in the VM resource pool.
 *
 * Return: 0 on success, or a negative error code.
 */
int loader_execute(struct vm_ctx *ctx, const char *name, const char *opts)
{
	const struct loader_desc *desc;
	struct loader_instance_ctx *inst;
	int ret;

	if (WARN_ON(!ctx || !name || !opts || !ctx->vcpus || !ctx->config.nr_vcpus || !ctx->vcpus[0]))
		return -VM_EINVAL;

	desc = loader_find(name);
	if (!desc) {
		pr_err("boot protocol '%s' is not supported\n", name);
		return -VM_ENOENT;
	}

	inst = res_zalloc(&ctx->resources, sizeof(*inst));
	if (!inst)
		return -VM_ENOMEM;

	inst->desc = desc;

	/* Own any published loader state, including a partially failed load. */
	ret = res_add_action_or_reset(&ctx->resources, loader_instance_cleanup, inst);
	if (ret < 0) {
		return ret;
	}

	ret = desc->load(ctx, opts, &inst->priv);
	if (ret < 0) {
		pr_err("loader '%s' failed to inject payloads into memory\n", name);
		return ret;
	}

	ret = desc->setup_bsp(ctx->vcpus[0], inst->priv);
	if (ret < 0) {
		pr_err("loader '%s' failed to manipulate processor state\n", name);
		return ret;
	}

	pr_info("successfully handed over execution to '%s' boot protocol\n", name);
	return 0;
}
