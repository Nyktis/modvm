/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MEMORY_INTERNAL_H
#define MODVM_CORE_MEMORY_INTERNAL_H

#include <modvm/core/memory.h>

/*
 * Guest memory lifetime private to the VM core
 *
 * These functions are strictly private to the modvm core engine.
 * Device models, board topologies, and loaders MUST NOT use them directly.
 */

int mem_space_init(struct mem_space *space);
void mem_space_destroy(struct mem_space *space);

#endif /* MODVM_CORE_MEMORY_INTERNAL_H */
