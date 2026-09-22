/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_LOADER_H
#define MODVM_CORE_LOADER_H

struct vm_ctx;
struct vcpu;

/**
 * struct loader_desc - boot protocol implementation description
 * @name: name unique within the loader registry (e.g., "linux-x86")
 * @load: injects payloads into memory and establishes initial state.
 * Returns an opaque context pointer via out_priv. Published state is owned by
 * the VM even when load fails; do not free it after publishing.
 * @setup_bsp: manipulates the Bootstrap Processor (vCPU 0) to meet the
 * entry requirements of this specific protocol.
 * @destroy: destroys published private state, including partially loaded state.
 * Optional when load publishes no owned resources; does not free the description.
 */
struct loader_desc {
	const char *name;
	int (*load)(struct vm_ctx *ctx, const char *opts, void **out_priv);
	int (*setup_bsp)(struct vcpu *vcpu, void *priv);
	void (*destroy)(void *priv);
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void loader_register(const struct loader_desc *desc);

int loader_execute(struct vm_ctx *ctx, const char *name, const char *opts);

#endif /* MODVM_CORE_LOADER_H */
