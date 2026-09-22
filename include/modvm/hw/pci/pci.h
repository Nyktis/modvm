/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MODVM_HW_PCI_H
#define MODVM_HW_PCI_H

#include <stdint.h>
#include <modvm/core/io_map.h>
#include <modvm/util/list.h>
#include <modvm/util/types.h>

#define PCI_CONFIG_SPACE_SIZE 256

/* Standard PCI Configuration Space Offsets */
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN 0x3D

#define PCI_AUTO_DEVFN INVALID_PCI_DEVFN
#define PCI_AUTO_MMIO INVALID_GPA

struct pci_device;
struct pci_bus;

/**
 * typedef pci_set_irq_cb_t - callback for host bridge interrupt routing
 * @data: closure payload provided by the host bridge
 * @pci_dev: the endpoint device asserting or deasserting the interrupt
 * @level: interrupt level (0 deasserted, 1 asserted)
 */
typedef void (*pci_set_irq_cb_t)(void *data, struct pci_device *pci_dev, int level);

/**
 * struct pci_device_ops - operations for specific PCI endpoints
 * @read_config: optional device-specific reads at offsets >= 0x40
 * @write_config: optional device-specific writes at offsets >= 0x40
 */
struct pci_device_ops {
	uint32_t (*read_config)(struct pci_device *pci_dev, uint8_t offset, uint8_t size);
	void (*write_config)(struct pci_device *pci_dev, uint8_t offset, uint32_t val, uint8_t size);
};

/**
 * struct pci_device - PCI endpoint state
 * @node: linked list node for the host bridge's device registry
 * @bus: pointer to the parent PCI bus segment
 * @owner: generic device owning this PCI function's resources
 * @ops: dispatch table for PCI-specific operations
 * @priv: opaque pointer for endpoint-specific state
 * @devfn: Device and Function number
 * @interrupt_pin: PCI interrupt pin (0=None, 1=INTA, 2=INTB, 3=INTC, 4=INTD)
 * @irq_level: endpoint INTx assertion before command-register masking
 * @bars: BAR mappings and guest size-probe state
 * @bars.region: stable device-owned I/O region, or NULL for an unimplemented BAR
 * @bars.probe: whether reads return the BAR size mask
 * @config_space: cached 256-byte PCI configuration space layout
 */
struct pci_device {
	struct list_head node;
	struct pci_bus *bus;
	struct device *owner;
	const struct pci_device_ops *ops;
	void *priv;

	pci_devfn_t devfn;
	uint8_t interrupt_pin;
	bool irq_level;
	struct {
		struct io_region *region;
		bool probe;
	} bars[6];
	uint8_t config_space[PCI_CONFIG_SPACE_SIZE];
};

/**
 * struct pci_bus - PCI bus segment state
 * @devices: list of PCI endpoint devices attached to this bus
 * @set_irq_cb: host bridge hook for intercepting and swizzling interrupts
 * @irq_data: context passed to the host bridge interrupt callback
 * @owner: host bridge device owning this bus and its VM association
 * @mmio_base: inclusive start of the reusable automatic MMIO allocation window
 * @mmio_limit: exclusive end of the automatic MMIO allocation window
 */
struct pci_bus {
	struct list_head devices;
	pci_set_irq_cb_t set_irq_cb;
	void *irq_data;

	struct device *owner;
	gpa_t mmio_base;
	gpa_t mmio_limit;
};

/* Topology and PCI operations require the owning VM I/O lock, or exclusive
 * access during construction/teardown before/after all concurrent users. */
int pci_bus_init_locked(struct pci_bus *bus, struct device *owner, gpa_t mmio_base, uint64_t mmio_size, pci_set_irq_cb_t set_irq_cb, void *irq_data);
int pci_register_bar_locked(struct pci_device *dev, unsigned index, uint32_t size, gpa_t requested_base);
int pci_device_register_locked(struct pci_bus *bus, struct pci_device *pci_dev);
uint32_t pci_bus_read_config_locked(struct pci_bus *bus, pci_devfn_t devfn, uint8_t offset, uint8_t size);
void pci_bus_write_config_locked(struct pci_bus *bus, pci_devfn_t devfn, uint8_t offset, uint32_t val, uint8_t size);
void pci_device_set_irq_locked(struct pci_device *pci_dev, int level);

#endif /* MODVM_HW_PCI_H */
