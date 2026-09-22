/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_BOARDS_X86_64_ACPI_DEFS_H
#define MODVM_HW_BOARDS_X86_64_ACPI_DEFS_H

#include <stdint.h>
#include <modvm/util/types.h>
#include <modvm/util/stddef.h>
#include <modvm/util/compiler.h>

#define ACPI_SIG_DSDT "DSDT" /* Differentiated System Description Table */
#define ACPI_SIG_FADT "FACP" /* Fixed ACPI Description Table */
#define ACPI_SIG_FACS "FACS" /* Firmware ACPI Control Structure */
#define ACPI_SIG_OSDT "OSDT" /* Override System Description Table */
#define ACPI_SIG_PSDT "PSDT" /* Persistent System Description Table */
#define ACPI_SIG_RSDP "RSD PTR " /* Root System Description Pointer */
#define ACPI_SIG_RSDT "RSDT" /* Root System Description Table */
#define ACPI_SIG_XSDT "XSDT" /* Extended  System Description Table */
#define ACPI_SIG_SSDT "SSDT" /* Secondary System Description Table */
#define ACPI_SIG_MCFG "MCFG" /* PCI Memory Mapped Configuration table */
#define ACPI_SIG_HPET "HPET" /* High Precision Event Timer table */
#define ACPI_SIG_WAET "WAET" /* Windows ACPI Emulated devices Table */
#define ACPI_SIG_MADT "APIC" /* Multiple APIC Description Table */
#define ACPI_SIG_SRAT "SRAT" /* System Resource Affinity Table */
#define ACPI_SIG_TPM2 "TPM2" /* Trusted Platform Module 2.0 H/W interface table */

#define ACPI_OEM_ID_SIZE 6
#define ACPI_OEM_TABLE_ID_SIZE 8

/*******************************************************************************
 *
 * Master ACPI Table Header. This common header is used by all ACPI tables
 * except the RSDP and FACS.
 *
 ******************************************************************************/

struct acpi_table_header {
	uint8_t signature[4]; /* ASCII table signature */
	le32_t length; /* Length of table in bytes, including this header */
	uint8_t revision; /* ACPI Specification minor version number */
	uint8_t checksum; /* To make sum of entire table == 0 */
	uint8_t oem_id[ACPI_OEM_ID_SIZE];
	uint8_t oem_table_id[ACPI_OEM_TABLE_ID_SIZE];
	le32_t oem_revision; /* OEM revision number */
	uint8_t asl_compiler_id[4]; /* ASCII ASL compiler vendor ID */
	le32_t asl_compiler_revision; /* ASL compiler version */
} __packed;

/*******************************************************************************
 *
 * GAS - Generic Address Structure (ACPI 2.0+)
 *
 * Note: Since this structure is used in the ACPI tables, it is byte aligned.
 * If misaligned access is not supported by the hardware, accesses to the
 * 64-bit Address field must be performed with care.
 *
 ******************************************************************************/

struct acpi_generic_address {
	uint8_t space_id; /* Address space where struct or register exists */
	uint8_t bit_width; /* Size in bits of given register */
	uint8_t bit_offset; /* Bit offset within the register */
	uint8_t access_width; /* Minimum Access size (ACPI 3.0) */
	le64_t address; /* 64-bit address of struct or register */
} __packed;

/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (Signature is "RSD PTR ")
 *        Version 2
 *
 ******************************************************************************/

struct acpi_table_rsdp {
	uint8_t signature[8]; /* ACPI signature, contains "RSD PTR " */
	uint8_t checksum; /* ACPI 1.0 checksum */
	uint8_t oem_id[ACPI_OEM_ID_SIZE];
	uint8_t revision; /* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	le32_t rsdt_physical_address; /* 32-bit physical address of the RSDT */
	le32_t length; /* Table length in bytes, including header (ACPI 2.0+) */
	le64_t xsdt_physical_address; /* 64-bit physical address of the XSDT (ACPI 2.0+) */
	uint8_t extended_checksum; /* Checksum of entire table (ACPI 2.0+) */
	uint8_t reserved[3]; /* Reserved, must be zero */
} __packed;

/*******************************************************************************
 *
 * RSDT/XSDT - Root System Description Tables
 *             Version 1 (both)
 *
 ******************************************************************************/

struct acpi_table_xsdt {
	struct acpi_table_header header; /* Common ACPI table header */
	DECLARE_FLEX_ARRAY(le64_t, table_offset_entry); /* Array of pointers to ACPI tables */
} __packed;

/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure (FACS)
 *
 ******************************************************************************/

struct acpi_table_facs {
	uint8_t signature[4]; /* ASCII table signature "FACS" */
	le32_t length; /* Length of structure, in bytes */
	le32_t hardware_signature; /* Hardware configuration signature */
	le32_t firmware_waking_vector; /* 32-bit physical address of the Firmware Waking Vector */
	le32_t global_lock; /* Global Lock for shared hardware resources */
	le32_t flags;
	le64_t xfirmware_waking_vector; /* 64-bit version of the Firmware Waking Vector */
	uint8_t version; /* Version of this table (ACPI 2.0+) */
	uint8_t reserved[3]; /* Reserved, must be zero */
	le32_t ospm_flags; /* Flags to be set by OSPM (ACPI 4.0) */
	uint8_t reserved1[24]; /* Reserved, must be zero */
} __packed;

/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (Signature "FACP")
 *        Version 6
 *
 ******************************************************************************/

struct acpi_table_fadt {
	struct acpi_table_header header; /* Common ACPI table header */
	le32_t facs; /* 32-bit physical address of FACS */
	le32_t dsdt; /* 32-bit physical address of DSDT */
	uint8_t model; /* System Interrupt Model (ACPI 1.0) - not used in ACPI 2.0+ */
	uint8_t preferred_profile; /* Conveys preferred power management profile to OSPM. */
	le16_t sci_interrupt; /* System vector of SCI interrupt */
	le32_t smi_command; /* 32-bit Port address of SMI command port */
	uint8_t acpi_enable; /* Value to write to SMI_CMD to enable ACPI */
	uint8_t acpi_disable; /* Value to write to SMI_CMD to disable ACPI */
	uint8_t s4_bios_request; /* Value to write to SMI_CMD to enter S4BIOS state */
	uint8_t pstate_control; /* Processor performance state control */
	le32_t pm1a_event_block; /* 32-bit port address of Power Mgt 1a Event Reg Blk */
	le32_t pm1b_event_block; /* 32-bit port address of Power Mgt 1b Event Reg Blk */
	le32_t pm1a_control_block; /* 32-bit port address of Power Mgt 1a Control Reg Blk */
	le32_t pm1b_control_block; /* 32-bit port address of Power Mgt 1b Control Reg Blk */
	le32_t pm2_control_block; /* 32-bit port address of Power Mgt 2 Control Reg Blk */
	le32_t pm_timer_block; /* 32-bit port address of Power Mgt Timer Ctrl Reg Blk */
	le32_t gpe0_block; /* 32-bit port address of General Purpose Event 0 Reg Blk */
	le32_t gpe1_block; /* 32-bit port address of General Purpose Event 1 Reg Blk */
	uint8_t pm1_event_length; /* Byte Length of ports at pm1x_event_block */
	uint8_t pm1_control_length; /* Byte Length of ports at pm1x_control_block */
	uint8_t pm2_control_length; /* Byte Length of ports at pm2_control_block */
	uint8_t pm_timer_length; /* Byte Length of ports at pm_timer_block */
	uint8_t gpe0_block_length; /* Byte Length of ports at gpe0_block */
	uint8_t gpe1_block_length; /* Byte Length of ports at gpe1_block */
	uint8_t gpe1_base; /* Offset in GPE number space where GPE1 events start */
	uint8_t cst_control; /* Support for the _CST object and C-States change notification */
	le16_t c2_latency; /* Worst case HW latency to enter/exit C2 state */
	le16_t c3_latency; /* Worst case HW latency to enter/exit C3 state */
	le16_t flush_size; /* Processor memory cache line width, in bytes */
	le16_t flush_stride; /* Number of flush strides that need to be read */
	uint8_t duty_offset; /* Processor duty cycle index in processor P_CNT reg */
	uint8_t duty_width; /* Processor duty cycle value bit width in P_CNT register */
	uint8_t day_alarm; /* Index to day-of-month alarm in RTC CMOS RAM */
	uint8_t month_alarm; /* Index to month-of-year alarm in RTC CMOS RAM */
	uint8_t century; /* Index to century in RTC CMOS RAM */
	le16_t boot_flags; /* IA-PC Boot Architecture Flags */
	uint8_t reserved; /* Reserved, must be zero */
	le32_t flags; /* Miscellaneous flag bits */
	struct acpi_generic_address reset_register; /* 64-bit address of the Reset register */
	uint8_t reset_value; /* Value to write to the reset_register port to reset the system */
	le16_t arm_boot_flags; /* ARM-Specific Boot Flags (ACPI 5.1) */
	uint8_t minor_revision; /* FADT Minor Revision (ACPI 5.1) */
	le64_t xfacs; /* 64-bit physical address of FACS */
	le64_t xdsdt; /* 64-bit physical address of DSDT */
	struct acpi_generic_address xpm1a_event_block; /* 64-bit Extended Power Mgt 1a Event Reg Blk address */
	struct acpi_generic_address xpm1b_event_block; /* 64-bit Extended Power Mgt 1b Event Reg Blk address */
	struct acpi_generic_address xpm1a_control_block; /* 64-bit Extended Power Mgt 1a Control Reg Blk address */
	struct acpi_generic_address xpm1b_control_block; /* 64-bit Extended Power Mgt 1b Control Reg Blk address */
	struct acpi_generic_address xpm2_control_block; /* 64-bit Extended Power Mgt 2 Control Reg Blk address */
	struct acpi_generic_address xpm_timer_block; /* 64-bit Extended Power Mgt Timer Ctrl Reg Blk address */
	struct acpi_generic_address xgpe0_block; /* 64-bit Extended General Purpose Event 0 Reg Blk address */
	struct acpi_generic_address xgpe1_block; /* 64-bit Extended General Purpose Event 1 Reg Blk address */
	struct acpi_generic_address sleep_control; /* 64-bit Sleep Control register (ACPI 5.0) */
	struct acpi_generic_address sleep_status; /* 64-bit Sleep Status register (ACPI 5.0) */
	le64_t hypervisor_id; /* Hypervisor Vendor ID (ACPI 6.0) */
} __packed;

/* FADT IA-PC Boot Architecture Flags (boot_flags) */
#define ACPI_FADT_NO_VGA (1 << 2) /* 02: [V4] It is not safe to probe for VGA hardware */
#define ACPI_FADT_NO_CMOS_RTC (1 << 5) /* 05: [V5] No CMOS real-time clock present */

/**
 *  Generic subtable header (used in MADT, SRAT, etc.)
 */

struct acpi_subtable_header {
	uint8_t type;
	uint8_t length;
} __packed;

/*******************************************************************************
 *
 * MADT - Multiple APIC Description Table
 *        Version 3
 *
 ******************************************************************************/

struct acpi_table_madt {
	struct acpi_table_header header; /* Common ACPI table header */
	le32_t address; /* Physical address of local APIC */
	le32_t flags;
} __packed;

#define ACPI_MADT_PCAT_COMPAT (1) /* 00: System also has dual 8259s */

enum acpi_madt_type {
	ACPI_MADT_TYPE_LOCAL_APIC = 0,
	ACPI_MADT_TYPE_IO_APIC = 1,
	ACPI_MADT_TYPE_INTERRUPT_OVERRIDE = 2,
};

/* 0: Processor Local APIC */
struct acpi_madt_local_apic {
	struct acpi_subtable_header header;
	uint8_t processor_id; /* ACPI processor id */
	uint8_t id; /* Processor's local APIC id */
	le32_t lapic_flags;
} __packed;

#define ACPI_MADT_ENABLED (1) /* Processor is usable if set */

/* 1: IO APIC */
struct acpi_madt_io_apic {
	struct acpi_subtable_header header;
	uint8_t id; /* I/O APIC ID */
	uint8_t reserved; /* reserved - must be zero */
	le32_t address; /* APIC physical address */
	le32_t global_irq_base; /* Global system interrupt where INTI lines start */
} __packed;

/* 2: Interrupt Override */
struct acpi_madt_interrupt_override {
	struct acpi_subtable_header header;
	uint8_t bus; /* 0 - ISA */
	uint8_t source_irq; /* Interrupt source (IRQ) */
	le32_t global_irq; /* Global system interrupt */
	le16_t inti_flags;
} __packed;

/*******************************************************************************
 *
 * HPET - High Precision Event Timer table
 *        Version 1
 *
 * Conforms to "IA-PC HPET (High Precision Event Timers) Specification",
 * Version 1.0a, October 2004
 *
 ******************************************************************************/

struct acpi_table_hpet {
	struct acpi_table_header header; /* Common ACPI table header */
	le32_t id; /* Hardware ID of event timer block */
	struct acpi_generic_address address; /* Address of event timer block */
	uint8_t sequence; /* HPET sequence number */
	le16_t minimum_tick; /* Main counter min tick, periodic mode */
	uint8_t flags;
} __packed;

/*******************************************************************************
 *
 * WAET - Windows ACPI Emulated devices Table
 *        Version 1
 *
 * Conforms to "Windows ACPI Emulated Devices Table", version 1.0, April 6, 2009
 *
 ******************************************************************************/

struct acpi_table_waet {
	struct acpi_table_header header; /* Common ACPI table header */
	le32_t flags;
} __packed;

#define ACPI_WAET_RTC_NO_ACK (1) /* RTC requires no int acknowledge */

/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and subtable
 *        Version 1
 *
 * Conforms to "PCI Firmware Specification", Revision 3.0, June 20, 2005
 *
 ******************************************************************************/

struct acpi_table_mcfg {
	struct acpi_table_header header; /* Common ACPI table header */
	uint8_t reserved[8];
} __packed;

struct acpi_mcfg_allocation {
	le64_t address; /* Base address, processor-relative */
	le16_t pci_segment; /* PCI segment group number */
	uint8_t start_bus_number; /* Starting PCI Bus number */
	uint8_t end_bus_number; /* Final PCI Bus number */
	le32_t reserved;
} __packed;

#endif /* MODVM_HW_BOARDS_X86_64_ACPI_DEFS_H */
