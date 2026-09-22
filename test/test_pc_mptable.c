/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/core/device.h>
#include <modvm/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modvm/core/vm.h>
#include <modvm/core/memory.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/util/log.h>
#include "../src/hw/board/x86_64/mptable.h"

#define CHECK(expr)                                                        \
	do {                                                               \
		if (!(expr)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
			exit(1);                                           \
		}                                                          \
	} while (0)

static unsigned int get_le(const uint8_t *p, unsigned int size)
{
	unsigned int value = 0;
	for (unsigned int i = 0; i < size; i++)
		value |= (unsigned int)p[i] << (8 * i);
	return value;
}

static uint8_t sum(const uint8_t *p, size_t size)
{
	uint8_t value = 0;
	for (size_t i = 0; i < size; i++)
		value += p[i];
	return value;
}

int main(void)
{
	struct vm_ctx vm = { 0 };
	struct pci_bus bus;
	struct mem_region ram = { .gpa = TO_GPA(0), .size = 1024 * 1024 };
	struct pci_device disk = { .devfn = TO_PCI_DEVFN(1, 0), .interrupt_pin = 1 };
	struct pci_device net = { .devfn = TO_PCI_DEVFN(2, 0), .interrupt_pin = 1 };
	uint8_t *fp, *table, *p, *end;
	unsigned int cpus = 0, entries = 0, pci = 0, isa_mask = 0;

	log_init();
	ram.hva = calloc(1, ram.size);
	CHECK(ram.hva);
	INIT_LIST_HEAD(&vm.mem_space.regions);
	list_add_tail(&ram.node, &vm.mem_space.regions);
	vm.config.nr_vcpus = 3;
	struct device parent = { .ctx = &vm };
	res_pool_init(&parent.resources);
	disk.owner = net.owner = &parent;
	CHECK(pci_bus_init_locked(&bus, &parent, TO_GPA(0xc0000000), 0x10000000, NULL, NULL) == 0);
	CHECK(pci_device_register_locked(&bus, &disk) == 0);
	CHECK(pci_device_register_locked(&bus, &net) == 0);
	disk.config_space[PCI_INTERRUPT_LINE] = 9;
	net.config_space[PCI_INTERRUPT_LINE] = 10;
	CHECK(pc_build_mptable(&vm, &bus) == 0);

	fp = (uint8_t *)ram.hva + 0xf0000;
	CHECK(memcmp(fp, "_MP_", 4) == 0);
	CHECK(fp[8] == 1 && fp[9] == 4 && sum(fp, 16) == 0);
	CHECK(get_le(fp + 4, 4) == 0xf0010);
	table = fp + 16;
	CHECK(memcmp(table, "PCMP", 4) == 0);
	CHECK(get_le(table + 36, 4) == 0xfee00000);
	CHECK(sum(table, get_le(table + 4, 2)) == 0);
	end = table + get_le(table + 4, 2);
	CHECK(end < (uint8_t *)ram.hva + ram.size);
	for (p = table + 44; p < end; entries++) {
		CHECK(p + 8 <= end);
		switch (p[0]) {
		case 0:
			CHECK(p + 20 <= end);
			CHECK(p[1] == cpus && p[3] == (cpus == 0 ? 3 : 1));
			cpus++;
			p += 20;
			continue;
		case 1:
			CHECK(p[1] <= 1);
			CHECK(memcmp(p + 2, p[1] ? "ISA   " : "PCI   ", 6) == 0);
			break;
		case 2:
			CHECK(p[1] == 254 && p[3] == 1);
			CHECK(get_le(p + 4, 4) == 0xfec00000);
			break;
		case 3:
			CHECK(p[6] == 254);
			if (p[4] == 0) {
				CHECK(get_le(p + 2, 2) == 15);
				CHECK((p[5] == 4 && p[7] == 9) || (p[5] == 8 && p[7] == 10));
				pci++;
			} else {
				CHECK(p[4] == 1 && p[5] < 16);
				CHECK(get_le(p + 2, 2) == 5 && p[7] == p[5]);
				isa_mask |= 1U << p[5];
			}
			break;
		case 4:
			CHECK(p[1] == 1 && p[6] == 255 && p[7] == 1);
			break;
		default:
			CHECK(0);
		}
		p += 8;
	}
	CHECK(p == end && entries == get_le(table + 34, 2));
	CHECK(cpus == 3 && pci == 2);
	CHECK(isa_mask == (0xffffU & ~((1U << 2) | (1U << 9) | (1U << 10))));
	vm.config.nr_vcpus = 255;
	CHECK(pc_build_mptable(&vm, &bus) == -VM_EINVAL);
	vm.config.nr_vcpus = 1;
	ram.size = 0xf0000;
	CHECK(pc_build_mptable(&vm, &bus) == -VM_ENOSPC);
	res_release_all(&parent.resources);
	free(ram.hva);
	log_destroy();
	puts("MP table checksums, CPU IDs and interrupt routes passed");
	return 0;
}
