/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/memory.h>
#include <modvm/core/vm.h>
#include <modvm/errno.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>

#include "mptable.h"

#define MP_BASE 0xf0000
#define MP_BIOS_SIZE 0x10000
#define MP_IOAPIC_ID PC_MAX_VCPUS
#define MP_INT_PCI 0xf /* Active low, level triggered. */
#define MP_INT_ISA 5 /* Active high, edge triggered. */

struct mp_float {
	char signature[4];
	le32_t table;
	uint8_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t features[5];
} __packed;

struct mp_header {
	char signature[4];
	le16_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem[8];
	char product[12];
	le32_t oem_table;
	le16_t oem_size;
	le16_t entries;
	le32_t lapic;
	le16_t extended_length;
	uint8_t extended_checksum;
	uint8_t reserved;
} __packed;

struct mp_cpu {
	uint8_t type;
	uint8_t id;
	uint8_t version;
	uint8_t flags;
	le32_t signature;
	le32_t features;
	le32_t reserved[2];
} __packed;

struct mp_bus {
	uint8_t type;
	uint8_t id;
	char name[6];
} __packed;

struct mp_ioapic {
	uint8_t type;
	uint8_t id;
	uint8_t version;
	uint8_t flags;
	le32_t address;
} __packed;

struct mp_irq {
	uint8_t type;
	uint8_t interrupt_type;
	le16_t flags;
	uint8_t bus;
	uint8_t source;
	uint8_t destination;
	uint8_t pin;
} __packed;

_Static_assert(sizeof(struct mp_float) == 16, "MP floating pointer layout");
_Static_assert(sizeof(struct mp_header) == 44, "MP header layout");
_Static_assert(sizeof(struct mp_cpu) == 20, "MP processor layout");
_Static_assert(sizeof(struct mp_irq) == 8, "MP interrupt layout");

static uint8_t checksum(const void *data, size_t size)
{
	const uint8_t *p = data;
	uint8_t sum = 0;

	for (size_t i = 0; i < size; i++)
		sum += p[i];
	return (uint8_t)-sum;
}

/* The caller reserves space for the counted entries before emitting any. */
#define APPEND(entry)                               \
	do {                                        \
		memcpy(p, &(entry), sizeof(entry)); \
		p += sizeof(entry);                 \
		count++;                            \
	} while (0)

int pc_build_mptable(struct vm_ctx *ctx, struct pci_bus *bus)
{
	struct pci_device *dev;
	struct mp_float *fp;
	struct mp_header *table;
	uint8_t *p;
	uint16_t pci_irqs = 0;
	unsigned int count = 0;
	size_t routes = 0, size;

	if (!ctx->config.nr_vcpus || ctx->config.nr_vcpus > MP_IOAPIC_ID)
		return -VM_EINVAL;

	list_for_each_entry(dev, &bus->devices, node)
	{
		if (dev->interrupt_pin && dev->interrupt_pin <= 4)
			routes++;
	}

	size = sizeof(*fp) + sizeof(*table) + ctx->config.nr_vcpus * sizeof(struct mp_cpu) + 2 * sizeof(struct mp_bus) + sizeof(struct mp_ioapic) +
	       (routes + 16 + 1) * sizeof(struct mp_irq);
	if (size > MP_BIOS_SIZE)
		return -VM_ENOSPC;

	fp = mem_map_range(&ctx->mem_space, TO_GPA(MP_BASE), size, true);
	if (!fp)
		return -VM_ENOSPC;

	memset(fp, 0, size);
	table = (struct mp_header *)(fp + 1);
	p = (uint8_t *)(table + 1);
	for (unsigned int i = 0; i < ctx->config.nr_vcpus; i++) {
		struct mp_cpu cpu = { .id = i, .version = 0x14, .flags = i == 0 ? 3 : 1, .signature = cpu_to_le32(0x600), .features = cpu_to_le32(0x201) };
		APPEND(cpu);
	}

	struct mp_bus pci = { .type = 1, .id = 0, .name = "PCI   " };
	struct mp_bus isa = { .type = 1, .id = 1, .name = "ISA   " };
	struct mp_ioapic ioapic = { .type = 2, .id = MP_IOAPIC_ID, .version = 0x11, .flags = 1, .address = cpu_to_le32(0xfec00000) };
	APPEND(pci);
	APPEND(isa);
	APPEND(ioapic);

	list_for_each_entry(dev, &bus->devices, node)
	{
		uint8_t pin = dev->interrupt_pin;
		uint8_t irq = dev->config_space[PCI_INTERRUPT_LINE];

		if (!pin || pin > 4)
			continue;
		struct mp_irq route = { .type = 3, .flags = cpu_to_le16(MP_INT_PCI), .source = (PCI_SLOT(dev->devfn) << 2) | (pin - 1), .destination = MP_IOAPIC_ID, .pin = irq };
		APPEND(route);
		if (irq < 16)
			pci_irqs |= 1U << irq;
	}

	for (unsigned int irq = 0; irq < 16; irq++) {
		if (irq == 2 || (pci_irqs & (1U << irq)))
			continue;
		/* KVM routes ISA GSIs to the same-numbered IOAPIC pins. */
		struct mp_irq route = { .type = 3, .flags = cpu_to_le16(MP_INT_ISA), .bus = 1, .source = irq, .destination = MP_IOAPIC_ID, .pin = irq };
		APPEND(route);
	}

	struct mp_irq nmi = { .type = 4, .interrupt_type = 1, .bus = 1, .destination = 0xff, .pin = 1 };
	APPEND(nmi);

	memcpy(table->signature, "PCMP", 4);
	table->length = cpu_to_le16(p - (uint8_t *)table);
	table->revision = 4;
	memcpy(table->oem, "MODVM   ", 8);
	memcpy(table->product, "MODVM PC    ", 12);
	table->entries = cpu_to_le16(count);
	table->lapic = cpu_to_le32(0xfee00000);
	table->checksum = checksum(table, p - (uint8_t *)table);

	memcpy(fp->signature, "_MP_", 4);
	fp->table = cpu_to_le32(MP_BASE + sizeof(*fp));
	fp->length = 1;
	fp->revision = 4;
	fp->checksum = checksum(fp, sizeof(*fp));

	pr_info("MP table: %u CPUs, IOAPIC and PCI/ISA interrupt routes\n", ctx->config.nr_vcpus);
	return 0;
}
