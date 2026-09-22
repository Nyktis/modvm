/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_IRQ_H
#define MODVM_CORE_IRQ_H

#include <modvm/util/res_pool.h>

struct irq;

/**
 * typedef irq_cb_t - callback receiving interrupt level requests
 * @data: contextual data pointer provided during allocation
 * @level: the interrupt level (0 deasserted, 1 asserted)
 */
typedef void (*irq_cb_t)(void *data, int level);

struct irq *irq_alloc(struct res_pool *pool, irq_cb_t cb, void *data);
void irq_set_level(struct irq *irq, int level);

#endif /* MODVM_CORE_IRQ_H */
