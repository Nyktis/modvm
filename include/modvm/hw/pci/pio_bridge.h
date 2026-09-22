/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_PCI_PIO_BRIDGE_H
#define MODVM_HW_PCI_PIO_BRIDGE_H

#include <modvm/util/types.h>

struct irq;
struct pci_bus;

/**
 * struct pio_bridge_pdata - platform routing data for the PIO Bridge
 * @config_addr_port: PIO port for CONFIG_ADDRESS; CONFIG_DATA is at +4
 * @mmio_base: starting physical address for PCI MMIO allocations
 * @mmio_size: maximum capacity of the MMIO window
 * @pirq: array mapping the 4 standard PCI routing lines (PIRQA-PIRQD) to system GSIs
 * @out_bus: OUT parameter; bridge will populate this with its logical bus pointer
 */
struct pio_bridge_pdata {
	gpa_t config_addr_port;
	gpa_t mmio_base;
	uint64_t mmio_size;
	struct irq *pirq[4];
	struct pci_bus **out_bus;
};

#endif /* MODVM_HW_PCI_PIO_BRIDGE_H */
