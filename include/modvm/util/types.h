/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTIL_TYPES_H
#define MODVM_UTIL_TYPES_H

#include <stdint.h>

/*
 * By wrapping the raw integers inside structs, we force GCC and Clang to
 * perform strict type checking without relying on external static analyzers
 * like Sparse. Assigning a raw integer to these types, or mixing LE/BE types,
 * will result in an immediate compiler error:
 * "incompatible types when assigning to type 'le32_t' from type 'int'"
 */

/* Endian-Safe Types */
typedef struct {
	uint16_t __val;
} le16_t;
typedef struct {
	uint16_t __val;
} be16_t;

typedef struct {
	uint32_t __val;
} le32_t;
typedef struct {
	uint32_t __val;
} be32_t;

typedef struct {
	uint64_t __val;
} le64_t;
typedef struct {
	uint64_t __val;
} be64_t;

typedef le16_t virtio16_t;
typedef le32_t virtio32_t;
typedef le64_t virtio64_t;

/* Cross-Domain Address Spaces */
typedef struct {
	uint64_t __val;
} gpa_t;
typedef struct {
	uint64_t __val;
} gva_t;

#define TO_GPA(x) ((gpa_t){ (uint64_t)(x) })
#define TO_GVA(x) ((gva_t){ (uint64_t)(x) })

#define GPA_VAL(x) ((x).__val)
#define GVA_VAL(x) ((x).__val)

#define INVALID_GPA TO_GPA(~0ULL)

static inline gpa_t gpa_add(gpa_t base, uint64_t offset)
{
	return TO_GPA(GPA_VAL(base) + offset);
}

static inline gpa_t gpa_sub(gpa_t base, uint64_t offset)
{
	return TO_GPA(GPA_VAL(base) - offset);
}

static inline uint64_t gpa_offset(gpa_t high, gpa_t low)
{
	return GPA_VAL(high) - GPA_VAL(low);
}

static inline gpa_t gpa_align(gpa_t gpa, uint64_t align)
{
	return TO_GPA((GPA_VAL(gpa) + align - 1) & ~(align - 1));
}

static inline gpa_t gpa_align_down(gpa_t gpa, uint64_t align)
{
	return TO_GPA(GPA_VAL(gpa) & ~(align - 1));
}

#define GPA_CMP(gpa1, op, gpa2) (GPA_VAL(gpa1) op GPA_VAL(gpa2))
#define GPA_IS_VALID(gpa) (GPA_VAL(gpa) != ~0ULL)

/* Hardware & Routing Primitives */
typedef struct {
	uint32_t __val;
} gsi_t;

#define TO_GSI(x) ((gsi_t){ (uint32_t)(x) })
#define GSI_VAL(x) ((x).__val)

/* PCI device/function number within one bus; no bus number is stored. */
typedef struct {
	uint8_t __val;
} pci_devfn_t;

#define TO_PCI_DEVFN(slot, func) ((pci_devfn_t){ (uint8_t)(((slot) << 3) | ((func) & 0x07)) })
#define TO_PCI_DEVFN_RAW(val) ((pci_devfn_t){ (uint8_t)(val) })

#define PCI_DEVFN_VAL(x) ((x).__val)

#define PCI_SLOT(x) (PCI_DEVFN_VAL(x) >> 3)
#define PCI_FUNC(x) (PCI_DEVFN_VAL(x) & 0x07)

#define INVALID_PCI_DEVFN TO_PCI_DEVFN_RAW(0xFF)
#define PCI_DEVFN_CMP(a, op, b) (PCI_DEVFN_VAL(a) op PCI_DEVFN_VAL(b))

#endif /* MODVM_UTIL_TYPES_H */
