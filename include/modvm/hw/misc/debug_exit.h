/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_MISC_DEBUG_EXIT_H
#define MODVM_HW_MISC_DEBUG_EXIT_H

#include <modvm/core/io_map.h>
#include <modvm/util/types.h>

/**
 * struct debug_exit_pdata - platform routing data for debug exit device
 * @io_space: the target address space (PIO or MMIO)
 * @base: the absolute starting address in the selected I/O space
 */
struct debug_exit_pdata {
	enum io_space io_space;
	gpa_t base;
};

#endif /* MODVM_HW_MISC_DEBUG_EXIT_H */
