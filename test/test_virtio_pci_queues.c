/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <modvm/core/board.h>
#include <modvm/core/io_map.h>
#include <modvm/core/vm.h>
#include <modvm/io/net.h>
#include <modvm/util/log.h>

#define CHECK(expr)                                                        \
	do {                                                               \
		if (!(expr)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
			exit(1);                                           \
		}                                                          \
	} while (0)

static int get_mac(struct net_backend *net, uint8_t mac[6])
{
	(void)net;
	for (unsigned i = 0; i < 6; i++)
		mac[i] = i + 2;
	return 0;
}

static int bind_rx(struct net_backend *net, struct io_receiver *binding)
{
	(void)binding;
	(void)net;
	return 0;
}

static void unbind(struct net_backend *net)
{
	(void)net;
}

static ptrdiff_t write_frame(struct net_backend *net, const uint8_t *buf, size_t len)
{
	(void)net;
	(void)buf;
	return len;
}

int main(void)
{
	const struct net_ops ops = { .write = write_frame, .get_mac = get_mac, .unbind = unbind, .bind = bind_rx };
	struct net_backend net = { .ops = &ops };
	struct net_backend *nets[] = { &net };
	struct vm_ctx vm = { 0 };
	struct vm_config cfg = { .accel_name = "kvm", .ram_size = 64 * 1024 * 1024, .nr_vcpus = 1, .nets = nets, .nr_nets = 1 };
	uint32_t bar;

	log_init();
	cfg.board = board_find("pc");
	CHECK(cfg.board && vm_init(&vm, &cfg) == 0);
	/* Only the host bridge and the network endpoint are present. */
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcf8), 0x80000810, 4);
	bar = io_map_read(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 4);
	CHECK(bar != UINT32_MAX);
	bar &= ~15U;
	/* Configure all three queues first, then select them again to enable:
	 * this is the ordering used by the Linux modern PCI driver.
	 */
	for (unsigned int q = 0; q < 3; q++) {
		io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 22), q, 2);
		io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 24), 16, 2);
		for (unsigned int field = 0; field < 3; field++)
			io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 32 + 8 * field), 0x10000 + q * 0x10000 + field * 0x2000, 4);
	}
	for (unsigned int q = 0; q < 3; q++) {
		io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 22), q, 2);
		for (unsigned int field = 0; field < 3; field++)
			CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 32 + 8 * field), 4) == 0x10000 + q * 0x10000 + field * 0x2000);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 24), 2) == 16);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 28), 2) == 0);
		io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 28), 1, 2);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 28), 2) == 1);
	}
	/* A full reset clears all queue addresses and enable bits. */
	io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 20), 0, 1);
	for (unsigned int q = 0; q < 3; q++) {
		io_map_write(&vm.io_map, IO_MMIO, TO_GPA(bar + 22), q, 2);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 28), 2) == 0);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 32), 4) == 0);
		CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 24), 2) == (q == 2 ? 64 : 256));
	}
	/* Read-only IDs, BAR probing, relocation and command decode. */
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcf8), 0x80000800, 4);
	uint32_t id = io_map_read(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 4);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 0, 4);
	CHECK(io_map_read(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 4) == id);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcf8), 0x80000810, 4);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), UINT32_MAX, 4);
	CHECK(io_map_read(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 4) == 0xfffff000);
	uint32_t moved = bar + 0x10000;
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), moved, 4);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(bar + 18), 2) == UINT64_MAX);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(moved + 18), 2) == 3);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcf8), 0x80000804, 4);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 0, 2);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(moved + 18), 2) == UINT64_MAX);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 2, 2);
	CHECK(io_map_read(&vm.io_map, IO_MMIO, TO_GPA(moved + 18), 2) == 3);
	vm_destroy(&vm);
	CHECK(!atomic_load(&net.binding));
	log_destroy();
	puts("Virtio PCI preserves independent queue addresses before enable");
	return 0;
}
