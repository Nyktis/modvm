/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/util/err.h>
#include <modvm/core/device.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <modvm/util/compiler.h>
#include <modvm/util/build_bug.h>
#include <modvm/util/stddef.h>
#include <modvm/util/container_of.h>
#include <modvm/util/list.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/res_pool.h>
#include <modvm/util/types.h>

#include <modvm/core/io_map.h>
#include <modvm/core/vm.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_infra: " fmt

struct mock_hw_regs {
	uint32_t ctrl_reg;
	uint32_t stat_reg;
	struct_group(dma_regs, uint32_t dma_src; uint32_t dma_dst;);
	DECLARE_FLEX_ARRAY(uint8_t, fifo);
};

ASSERT_STRUCT_OFFSET(struct mock_hw_regs, ctrl_reg, 0);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, stat_reg, 4);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, dma_src, 8);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, fifo, 16);

static_assert(__same_type(uint32_t, unsigned int), "type matching failed");

struct mock_task {
	int id;
	const char *name;
	struct list_head node;
};

static void test_container_of(void)
{
	struct mock_task parent = { .id = 42 };
	struct list_head *link = &parent.node;

	struct mock_task *recovered = container_of(link, struct mock_task, node);

	if (WARN_ON(recovered->id != 42))
		panic("container_of macro structural translation failed\n");

	pr_info("container_of mapping passed\n");
}

static void test_list(void)
{
	LIST_HEAD(run_queue);

	struct mock_task t1 = { .id = 1, .name = "init" };
	struct mock_task t2 = { .id = 2, .name = "network" };
	struct mock_task t3 = { .id = 3, .name = "block_io" };

	list_add_tail(&t1.node, &run_queue);
	list_add_tail(&t2.node, &run_queue);
	list_add_tail(&t3.node, &run_queue);

	if (WARN_ON(list_empty(&run_queue)))
		panic("queue falsely reported as empty after insertions\n");

	struct mock_task *pos, *n;
	int expected_id = 1;

	list_for_each_entry_safe(pos, n, &run_queue, node)
	{
		pr_debug("scheduling task: %s (id %d)\n", pos->name, pos->id);

		if (pos->id != expected_id++)
			panic("linked list topological ordering corrupted\n");

		list_del_init(&pos->node);
	}

	if (WARN_ON(!list_empty(&run_queue)))
		panic("list node deletion failed, artifacts remain\n");

	pr_info("doubly-linked list operations passed\n");
}

struct mock_timer_ctx {
	uint32_t ticks;
};

static uint64_t mock_timer_read(struct io_region *region, uint64_t offset, uint8_t size)
{
	struct device *dev = region->dev;
	struct mock_timer_ctx *ctx = dev->priv;

	if (WARN_ON(size == 0))
		return 0;

	if (offset == 0)
		return ctx->ticks++;

	return 0;
}

static void mock_timer_write(struct io_region *region, uint64_t offset, uint64_t val, uint8_t size)
{
	struct device *dev = region->dev;
	struct mock_timer_ctx *ctx = dev->priv;
	(void)size;

	if (offset == 0)
		ctx->ticks = (uint32_t)val;
}

static const struct device_ops mock_timer_ops = {
	.read = mock_timer_read,
	.write = mock_timer_write,
};

static void test_bus_routing(void)
{
	struct vm_ctx mock_ctx;
	struct mock_timer_ctx timer_ctx = { .ticks = 100 };
	struct device timer_dev = {
		.desc = &(const struct device_desc){ .name = "mock_timer" },
		.ops = &mock_timer_ops,
		.priv = &timer_ctx,
		.ctx = &mock_ctx,
	};
	struct device dummy_dev = {
		.desc = &(const struct device_desc){ .name = "dummy" },
		.ops = &mock_timer_ops,
		.ctx = &mock_ctx,
	};
	uint64_t val;

	pr_info("evaluating system bus topological routing\n");

	memset(&mock_ctx, 0, sizeof(mock_ctx));
	INIT_LIST_HEAD(&mock_ctx.io_map.pio_regions);
	INIT_LIST_HEAD(&mock_ctx.io_map.mmio_regions);
	INIT_LIST_HEAD(&mock_ctx.mem_space.regions);

	res_pool_init(&timer_dev.resources);
	res_pool_init(&dummy_dev.resources);

	struct io_region *region = io_map_register_region_locked(IO_PIO, TO_GPA(0x40), 4, &timer_dev);
	if (WARN_ON(IS_ERR(region)))
		panic("failed to register pio peripheral\n");

	region = io_map_register_region_locked(IO_PIO, TO_GPA(0x42), 1, &dummy_dev);
	if (WARN_ON(!IS_ERR(region)))
		panic("bus permitted overlapping address registration\n");

	region = io_map_register_region_locked(IO_MMIO, TO_GPA(0x42), 1, &dummy_dev);
	if (WARN_ON(IS_ERR(region)))
		panic("bus failed to isolate pio and mmio spaces\n");

	io_map_write(&mock_ctx.io_map, IO_PIO, TO_GPA(0x40), 500, 4);
	if (WARN_ON(timer_ctx.ticks != 500))
		panic("write routing failed to mutate state\n");

	val = io_map_read(&mock_ctx.io_map, IO_PIO, TO_GPA(0x40), 4);
	if (WARN_ON(val != 500))
		panic("read routing returned incorrect data\n");

	val = io_map_read(&mock_ctx.io_map, IO_PIO, TO_GPA(0x3f8), 1);
	if (WARN_ON(val != ~0ULL))
		panic("unmapped port failed floating bus constraint\n");

	/* Invalid dispatch requests must never reach a device callback. */
	io_map_write(&mock_ctx.io_map, (enum io_space)99, TO_GPA(0x42), 1, 1);
	io_map_write(&mock_ctx.io_map, IO_PIO, TO_GPA(0x40), 1, 0);
	io_map_write(&mock_ctx.io_map, IO_PIO, TO_GPA(0x40), 1, 3);
	if (WARN_ON(timer_ctx.ticks != 501 || io_map_read(&mock_ctx.io_map, IO_PIO, TO_GPA(0x40), 0) != ~0ULL))
		panic("invalid bus request reached device\n");

	res_release_all(&timer_dev.resources);
	res_release_all(&dummy_dev.resources);

	pr_info("bus routing and topology isolation passed\n");
}

int main(void)
{
	log_init();
	pr_info("initiating modvm infrastructure diagnostic sequence\n");

	test_container_of();
	test_list();
	test_bus_routing();

	pr_info("SUCCESS: all core infrastructure checks completed\n");
	log_destroy();
	return 0;
}
