/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/errno.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <modvm/core/board.h>
#include <modvm/core/io_map.h>
#include <modvm/core/loader.h>
#include <modvm/core/memory.h>
#include <modvm/core/vm.h>
#include <modvm/util/log.h>

#define CHECK(expr)                                                                        \
	do {                                                                               \
		if (!(expr)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expr); \
			exit(EXIT_FAILURE);                                                \
		}                                                                          \
	} while (0)
#define MIB (1024U * 1024U)

/* Synthetic protocol-2.10 bzImage: a small payload, a large runtime area. */
static uint8_t kernel[8192];
static uint8_t initrd[8193];

static void put_le(uint8_t *buf, size_t offset, uint64_t value, size_t size)
{
	for (size_t i = 0; i < size; i++)
		buf[offset + i] = (uint8_t)(value >> (i * 8));
}

static uint32_t get_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static void write_image(const char *path, const void *buf, size_t size)
{
	FILE *fp = fopen(path, "wb");
	CHECK(fp);
	CHECK(fwrite(buf, 1, size, fp) == size);
	CHECK(fclose(fp) == 0);
}

static uint32_t pci_read(struct vm_ctx *vm, uint32_t addr, uint8_t off, uint8_t size)
{
	io_map_write(&vm->io_map, IO_PIO, TO_GPA(0xcf8), addr, 4);
	return io_map_read(&vm->io_map, IO_PIO, TO_GPA(0xcfc + off), size);
}

int main(void)
{
	struct vm_ctx vm;
	struct vm_config cfg = {
		.accel_name = "kvm",

		.ram_size = 64 * MIB,
		.nr_vcpus = 1,
	};
	char kernel_path[] = "/tmp/modvm-kernel-XXXXXX";
	char initrd_path[] = "/tmp/modvm-initrd-XXXXXX";
	char opts[256];
	uint8_t *zero_page, *ram;
	uint32_t rd_addr;
	int fd;

	log_init();
	cfg.board = board_find("pc");
	CHECK(cfg.board);
	CHECK(vm_init(&vm, &cfg) == 0);

	/* Exercise the same CF8/CFC class probe used by Linux, without a disk. */
	CHECK(pci_read(&vm, 0x80000008, 2, 2) == 0x0600);
	CHECK(pci_read(&vm, 0x80000008, 3, 1) == 0x06);
	CHECK(pci_read(&vm, 0x80000000, 0, 4) == 0x00081b36);
	CHECK(pci_read(&vm, 0x80000800, 0, 4) == UINT32_MAX);
	CHECK(pci_read(&vm, 0x00000000, 0, 4) == UINT32_MAX);
	/* Identification/class fields must survive guest writes. */
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcf8), 0x80000008, 4);
	io_map_write(&vm.io_map, IO_PIO, TO_GPA(0xcfc), 0, 4);
	CHECK(pci_read(&vm, 0x80000008, 2, 2) == 0x0600);

	fd = mkstemp(kernel_path);
	CHECK(fd >= 0);
	CHECK(close(fd) == 0);
	fd = mkstemp(initrd_path);
	CHECK(fd >= 0);
	CHECK(close(fd) == 0);
	kernel[0x1f1] = 1;
	put_le(kernel, 0x202, 0x53726448, 4);
	put_le(kernel, 0x206, 0x020a, 2);
	put_le(kernel, 0x22c, 0x37ffffff, 4);
	put_le(kernel, 0x230, 16 * MIB, 4);
	kernel[0x234] = 1;
	put_le(kernel, 0x258, 16 * MIB, 8);
	put_le(kernel, 0x260, 38 * MIB, 4);
	memset(initrd, 0xa5, sizeof(initrd));
	write_image(kernel_path, kernel, sizeof(kernel));
	write_image(initrd_path, initrd, sizeof(initrd));
	CHECK(snprintf(opts, sizeof(opts), "kernel=%s,initrd=%s,append=console=ttyS0", kernel_path, initrd_path) < (int)sizeof(opts));
	CHECK(loader_execute(&vm, "linux-x86", opts) == 0);
	zero_page = mem_map_range(&vm.mem_space, TO_GPA(0x90000), 4096, false);
	CHECK(zero_page);
	rd_addr = get_le32(zero_page + 0x218);
	CHECK(rd_addr >= 54 * MIB);
	CHECK(rd_addr % 4096 == 0);
	CHECK((uint64_t)rd_addr + sizeof(initrd) <= cfg.ram_size);
	CHECK(get_le32(zero_page + 0x21c) == sizeof(initrd));
	ram = mem_map_range(&vm.mem_space, TO_GPA(rd_addr), sizeof(initrd), false);
	CHECK(ram && memcmp(ram, initrd, sizeof(initrd)) == 0);

	/* Respect the kernel's inclusive initrd_addr_max, even with extra RAM. */
	put_le(kernel, 0x22c, 60 * MIB - 1, 4);
	write_image(kernel_path, kernel, sizeof(kernel));
	CHECK(loader_execute(&vm, "linux-x86", opts) == 0);
	rd_addr = get_le32(zero_page + 0x218);
	CHECK(rd_addr >= 54 * MIB);
	CHECK((uint64_t)rd_addr + sizeof(initrd) <= 60 * MIB);

	/* An initrd cannot share the runtime area; insufficient RAM fails cleanly. */
	put_le(kernel, 0x22c, 54 * MIB - 1, 4);
	write_image(kernel_path, kernel, sizeof(kernel));
	CHECK(loader_execute(&vm, "linux-x86", opts) == -VM_ENOSPC);
	put_le(kernel, 0x260, 64 * MIB, 4);
	write_image(kernel_path, kernel, sizeof(kernel));
	CHECK(loader_execute(&vm, "linux-x86", opts) == -VM_ENOSPC);
	put_le(kernel, 0x206, 0x0209, 2);
	write_image(kernel_path, kernel, sizeof(kernel));
	CHECK(loader_execute(&vm, "linux-x86", opts) == -VM_EINVAL);

	CHECK(unlink(kernel_path) == 0);
	CHECK(unlink(initrd_path) == 0);
	vm_destroy(&vm);
	log_destroy();
	puts("Linux boot placement and PCI discovery checks passed");
	return 0;
}
