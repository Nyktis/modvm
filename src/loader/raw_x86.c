/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/loader.h>
#include <modvm/core/vm.h>
#include <modvm/core/vcpu.h>
#include <modvm/arch/x86/regs.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>

#include "image.h"

#undef pr_fmt
#define pr_fmt(fmt) "raw_loader: " fmt

static int raw_loader_load(struct vm_ctx *ctx, const char *opts, void **out_priv)
{
	/* Treat opts directly as the file path for simplicity */
	if (WARN_ON(!opts || strlen(opts) == 0))
		return -VM_EINVAL;

	*out_priv = NULL; /* No state needed */

	return loader_load_raw(&ctx->mem_space, opts, TO_GPA(0x0000));
}

static int raw_loader_setup_bsp(struct vcpu *vcpu, void *priv)
{
	struct x86_sregs sregs;
	int ret;

	(void)priv;

	ret = vcpu_get_regs(vcpu, REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	/* Raw programs start in real mode at the load address, GPA zero. */
	sregs.cs.selector = 0;
	sregs.cs.base = 0;

	ret = vcpu_set_regs(vcpu, REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(vcpu, X86_REG_RIP, 0);
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(vcpu, X86_REG_RFLAGS, 0x02);
	if (WARN_ON(ret < 0))
		return ret;

	return 0;
}

static const struct loader_desc raw_desc = {
	.name = "raw-x86",
	.load = raw_loader_load,
	.setup_bsp = raw_loader_setup_bsp,
};

static void __attribute__((constructor)) register_raw_loader(void)
{
	loader_register(&raw_desc);
}
