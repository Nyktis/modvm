/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_PCI_H
#define MODVM_HW_VIRTIO_VIRTIO_PCI_H

#include <modvm/util/types.h>

struct pci_bus;
struct virtio_device;

/**
 * struct virtio_pci_pdata - platform routing data for a Virtio-PCI transport
 * @pci_bus: the PCI host bridge bus to attach to
 * @vdev: VirtIO device model to attach to this transport
 * @devfn: requested device/function number, or PCI_AUTO_DEVFN for automatic assignment
 * @interrupt_pin: PCI interrupt pin (1=INTA, 2=INTB, 3=INTC, 4=INTD)
 * @bar0_base: requested BAR0 address, or PCI_AUTO_MMIO for automatic allocation
 */
struct virtio_pci_pdata {
	struct pci_bus *pci_bus;
	struct virtio_device *vdev;
	pci_devfn_t devfn;
	uint8_t interrupt_pin;
	gpa_t bar0_base;
};

#endif /* MODVM_HW_VIRTIO_VIRTIO_PCI_H */
