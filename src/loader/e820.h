/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_LOADER_E820_H
#define MODVM_LOADER_E820_H

#include <stdint.h>

#define VM_E820_RAM 1
#define VM_E820_RESERVED 2
#define VM_E820_ACPI 3
#define VM_E820_NVS 4
#define VM_E820_UNUSABLE 5

/**
 * struct e820_entry - standard PC memory map entry
 * @addr: start physical address of the memory range
 * @size: size of the memory range in bytes
 * @type: memory type (e.g., usable RAM, reserved)
 */
struct e820_entry {
	uint64_t addr;
	uint64_t size;
	uint32_t type;
} __packed;

#endif /* MODVM_LOADER_E820_H */
