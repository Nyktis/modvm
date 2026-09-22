/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_BOARD_H
#define MODVM_CORE_BOARD_H

struct vm_ctx;

/**
 * struct board_ops - board initialization callbacks
 * @init: early initialization (e.g., RAM mapping, pre-vCPU IRQ controllers).
 * @late_init: initialization that requires vCPUs to exist.
 * @boot: one-time boot image loading and initial CPU setup; not a reset operation.
 */
struct board_ops {
	int (*init)(struct vm_ctx *ctx);
	int (*late_init)(struct vm_ctx *ctx);
	int (*boot)(struct vm_ctx *ctx);
};

/**
 * struct board_desc - board description
 * @name: name unique within the board registry, selected on the command line
 * @desc: human-readable description.
 * @ops: pointer to the operational methods.
 */
struct board_desc {
	const char *name;
	const char *desc;
	const struct board_ops *ops;
};

/* Startup-only registration; invalid, duplicate or excess entries are fatal programming errors. */
void board_register(const struct board_desc *board);
const struct board_desc *board_find(const char *name);

#endif /* MODVM_CORE_BOARD_H */
