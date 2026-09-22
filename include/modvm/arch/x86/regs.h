/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ARCH_X86_REGS_H
#define MODVM_ARCH_X86_REGS_H

#include <stdint.h>

/**
 * enum x86_reg_id - standardized abstract IDs for x86 architectural registers
 * Used exclusively by loaders and backends for single-register routing.
 */
enum x86_reg_id {
	X86_REG_RAX = 0,
	X86_REG_RBX,
	X86_REG_RCX,
	X86_REG_RDX,
	X86_REG_RSI,
	X86_REG_RDI,
	X86_REG_RSP,
	X86_REG_RBP,
	X86_REG_R8,
	X86_REG_R9,
	X86_REG_R10,
	X86_REG_R11,
	X86_REG_R12,
	X86_REG_R13,
	X86_REG_R14,
	X86_REG_R15,
	X86_REG_RIP,
	X86_REG_RFLAGS,
};

/**
 * struct x86_segment - standardized x86 segment descriptor
 * @base: linear base address
 * @limit: segment limit
 * @selector: segment selector index
 * @type: segment type and access rights
 * @present: present flag
 * @dpl: descriptor privilege level
 * @db: default operation size (0 = 16-bit, 1 = 32-bit)
 * @s: descriptor type (0 = system, 1 = code/data)
 * @l: 64-bit code segment flag
 * @g: granularity flag
 * @unusable: unusable segment flag
 */
struct x86_segment {
	uint64_t base;
	uint32_t limit;
	uint16_t selector;
	uint8_t type;
	uint8_t present;
	uint8_t dpl;
	uint8_t db;
	uint8_t s;
	uint8_t l;
	uint8_t g;
	uint8_t unusable;
};

/**
 * struct x86_sregs - standardized x86 special registers
 * @cs: code segment
 * @ds: data segment
 * @es: extra segment
 * @fs: fs segment
 * @gs: gs segment
 * @ss: stack segment
 * @tr: task register
 * @ldt: local descriptor table
 * @cr0: control register 0
 * @cr2: control register 2
 * @cr3: control register 3
 * @cr4: control register 4
 * @cr8: control register 8
 * @efer: extended feature enable register
 * @apic_base: local apic base address
 */
struct x86_sregs {
	struct x86_segment cs;
	struct x86_segment ds;
	struct x86_segment es;
	struct x86_segment fs;
	struct x86_segment gs;
	struct x86_segment ss;
	struct x86_segment tr;
	struct x86_segment ldt;
	uint64_t cr0;
	uint64_t cr2;
	uint64_t cr3;
	uint64_t cr4;
	uint64_t cr8;
	uint64_t efer;
	uint64_t apic_base;
};

/**
 * struct x86_regs - standardized x86 general purpose registers
 */
struct x86_regs {
	uint64_t rax, rbx, rcx, rdx;
	uint64_t rsi, rdi, rsp, rbp;
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
	uint64_t rip;
	uint64_t rflags;
};

#endif /* MODVM_ARCH_X86_REGS_H */
