/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_CHAR_SERIAL_H
#define MODVM_HW_CHAR_SERIAL_H

#include <modvm/core/irq.h>
#include <modvm/core/io_map.h>
#include <modvm/io/char.h>
#include <modvm/util/types.h>

struct io_ctx;

/**
 * struct serial_pdata - hardware configuration for serial devices
 * @io_space: target system address space (PIO or MMIO)
 * @base: the starting address in the selected I/O space
 * @reg_shift: register stride exponent; must be below 61 so all eight registers fit
 * @irq: the pre-wired interrupt line to signal the processor
 * @console: the host character device backend for data stream routing
 * @io_ctx: execution context for asynchronous receive delivery
 */
struct serial_pdata {
	enum io_space io_space;
	gpa_t base;
	uint8_t reg_shift;
	struct irq *irq;
	struct char_backend *console;
	struct io_ctx *io_ctx;
};

#endif /* MODVM_HW_CHAR_SERIAL_H */
