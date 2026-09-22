/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <modvm/core/irq.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

struct irq {
	irq_cb_t cb;
	void *data;
};

/**
 * irq_alloc - allocate a resource-pool-owned interrupt line
 * @pool: the resource pool to manage this interrupt's lifecycle
 * @cb: the function to invoke for each level request
 * @data: contextual closure payload
 *
 * The allocated IRQ line is freed when pool is released. Its callback data
 * must remain valid until the last level request has completed.
 *
 * Return: allocated interrupt line, or NULL on failure.
 */
struct irq *irq_alloc(struct res_pool *pool, irq_cb_t cb, void *data)
{
	if (!pool || !cb)
		return NULL;
	struct irq *irq = res_zalloc(pool, sizeof(*irq));
	if (irq) {
		irq->cb = cb;
		irq->data = data;
	}
	return irq;
}

/**
 * irq_set_level - assert or deassert the virtual interrupt line
 * @irq: the interrupt line instance
 * @level: the interrupt level (0 deasserted, 1 asserted)
 */
void irq_set_level(struct irq *irq, int level)
{
	if (likely(irq && irq->cb))
		irq->cb(irq->data, level);
}
