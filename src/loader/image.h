/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_LOADER_IMAGE_H
#define MODVM_LOADER_IMAGE_H

#include <modvm/util/types.h>

struct mem_space;

/*
 * Private image loading shared by boot protocol implementations.
 *
 * Strictly reserved for specific boot protocol implementations.
 */

int loader_load_raw(struct mem_space *space, const char *path, gpa_t gpa);

#endif /* MODVM_LOADER_IMAGE_H */
