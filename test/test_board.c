/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/core/device.h>
#include <modvm/util/res_pool.h>
#include <modvm/hw/misc/debug_exit.h>
#include <modvm/core/memory.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/io/char.h>
#include <modvm/core/irq.h>
#include <modvm/hw/char/serial.h>
#include <modvm/arch/x86/regs.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_board: " fmt

static const uint8_t fw_payload[] = { 0xba, 0xf8, 0x03, 0xb0, 0x4f, 0xee, 0xb0, 0x4b, 0xee, 0xb0, 0x0d, 0xee, 0xb0, 0x0a, 0xee, 0xba, 0x00, 0x05, 0xb0, 0x01, 0xee, 0xf4 };

struct mock_irq_route {
	struct accel *accel;
	uint32_t gsi;
};

static void mock_irq_handler(void *data, int level)
{
	struct mock_irq_route *route = data;
	accel_set_irq(route->accel, TO_GSI(route->gsi), level);
}

static int mock_board_init(struct vm_ctx *ctx)
{
	struct device *uart;
	struct device *exit_dev;
	struct serial_pdata uart_pdata;
	struct debug_exit_pdata exit_pdata;
	struct mock_irq_route *route;
	int ret;

	ret = vm_add_ram(ctx, TO_GPA(0x0000), 4096, 0);
	if (ret < 0)
		return ret;

	ret = accel_setup_irqchip(&ctx->accel);
	if (ret < 0) {
		pr_err("failed to initialize architectural irqchip\n");
		return ret;
	}

	uart = device_alloc(ctx, "uart-16550a");
	if (!uart)
		return -VM_ENOMEM;

	route = res_zalloc(&uart->resources, sizeof(*route));
	if (!route) {
		ret = -VM_ENOMEM;
		goto err_uart;
	}

	route->accel = &ctx->accel;
	route->gsi = 4;

	uart_pdata.io_space = IO_PIO;
	uart_pdata.base = TO_GPA(0x3f8);
	uart_pdata.reg_shift = 0;
	uart_pdata.console = ctx->config.console;
	uart_pdata.io_ctx = ctx->io_ctx;
	uart_pdata.irq = irq_alloc(&uart->resources, mock_irq_handler, route);
	if (!uart_pdata.irq) {
		ret = -VM_ENOMEM;
		goto err_uart;
	}

	ret = device_realize(uart, &uart_pdata);
	if (ret < 0) {
		pr_err("failed to probe uart peripheral\n");
		goto err_uart;
	}

	exit_dev = device_alloc(ctx, "debug-exit");
	if (!exit_dev)
		return -VM_ENOMEM;

	exit_pdata.io_space = IO_PIO;
	exit_pdata.base = TO_GPA(0x500);

	ret = device_realize(exit_dev, &exit_pdata);
	if (ret < 0) {
		pr_err("failed to probe debug exit device\n");
		device_destroy(exit_dev);
		return ret;
	}

	pr_info("mock hardware topology injected successfully\n");
	return 0;

err_uart:
	device_destroy(uart);
	return ret;
}

static int mock_board_reset(struct vm_ctx *ctx)
{
	struct x86_sregs sregs;
	void *hva;
	int ret;

	hva = mem_map_range(&ctx->mem_space, TO_GPA(0x0000), sizeof(fw_payload), true);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("failed to translate gpa 0x0000 for payload injection\n");
		return -VM_EFAULT;
	}

	memcpy(hva, fw_payload, sizeof(fw_payload));
	pr_info("injected %zu bytes of machine code into guest memory\n", sizeof(fw_payload));

	ret = vcpu_get_regs(ctx->vcpus[0], REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	sregs.cs.selector = 0x0000;
	sregs.cs.base = 0x00000000;

	ret = vcpu_set_regs(ctx->vcpus[0], REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(ctx->vcpus[0], X86_REG_RIP, 0x0000);
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(ctx->vcpus[0], X86_REG_RFLAGS, 0x02);
	if (WARN_ON(ret < 0))
		return ret;

	return 0;
}

static const struct board_ops mock_ops = {
	.init = mock_board_init,
	.boot = mock_board_reset,
};

static const struct board_desc mock_board = {
	.name = "mock",
	.desc = "Mock machine for integration testing",
	.ops = &mock_ops,
};

static void test_machine_lifecycle(void)
{
	struct res_pool backends;
	res_pool_init(&backends);
	struct vm_ctx vm;
	struct char_backend *console;
	int ret;

	console = char_create(&backends, "posix-stdio", NULL);
	if (WARN_ON(!console))
		panic("failed to create console backend\n");

	struct vm_config cfg = {
		.accel_name = "kvm",

		.ram_size = 4096,
		.nr_vcpus = 1,
		.loader_name = NULL,
		.loader_opts = NULL,
		.board = &mock_board,
		.console = console,
	};

	pr_info("initiating motherboard initialization sequence\n");

	ret = vm_init(&vm, &cfg);
	if (WARN_ON(ret < 0))
		panic("machine assembly failed\n");

	pr_info("igniting processor cores\n");

	ret = vm_run(&vm);
	if (WARN_ON(ret < 0))
		panic("hypervisor runtime encountered fatal error\n");

	pr_info("tearing down virtualization context\n");
	vm_destroy(&vm);
	res_release_all(&backends);
}

int main(void)
{
	log_init();
	pr_info("Initiating ModVM board integration test\n");
	test_machine_lifecycle();
	pr_info("SUCCESS: machine architecture test concluded successfully\n");
	log_destroy();
	return 0;
}
