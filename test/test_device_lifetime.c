/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include "fixture.h"
#include <stdlib.h>
#include <modvm/core/vm.h>
#include <modvm/hw/pci/pci.h>
#include <modvm/core/io_map.h>
#include <modvm/util/res_pool.h>
#include <modvm/io/net.h>
#include <modvm/hw/pci/pio_bridge.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/util/log.h>
#include "../src/hw/virtio/virtio_mmio_reg.h"
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int level;
static void irq(void *p, int value)
{
	(void)p;
	level = value;
}

static const struct device_desc endpoint = { .name = "lifetime-test" };
static int bind_rx(struct net_backend *net, struct io_receiver *binding)
{
	(void)binding;
	(void)net;
	return 0;
}

static void unbind(struct net_backend *net)
{
	(void)net;
}

static int get_mac(struct net_backend *net, uint8_t mac[6])
{
	(void)net;
	for (unsigned i = 0; i < 6; i++)
		mac[i] = i + 2;
	return 0;
}

int main(void)
{
	struct vm_ctx vm = { 0 };
	log_init();
	device_register(&endpoint);
	res_pool_init(&vm.resources);
	CHECK(test_io_init(&vm) == 0);
	INIT_LIST_HEAD(&vm.devices);
	INIT_LIST_HEAD(&vm.io_map.pio_regions);
	INIT_LIST_HEAD(&vm.io_map.mmio_regions);
	struct device *bridge = device_alloc(&vm, "pci-pio-bridge");
	CHECK(bridge);
	struct pci_bus *bus;
	struct pio_bridge_pdata p = { .config_addr_port = TO_GPA(0xcf8), .mmio_base = TO_GPA(0xc0000000), .mmio_size = 0x100000, .out_bus = &bus };
	for (int i = 0; i < 4; i++) {
		p.pirq[i] = irq_alloc(&bridge->resources, irq, NULL);
		CHECK(p.pirq[i]);
	}
	CHECK(device_realize(bridge, &p) == 0);
	struct device *a = device_alloc(&vm, "lifetime-test"), *b = device_alloc(&vm, "lifetime-test");
	CHECK(a && b);
	CHECK(device_realize(a, NULL) == 0 && device_realize(b, NULL) == 0);
	struct pci_device pa = { .devfn = TO_PCI_DEVFN(1, 0), .owner = a, .interrupt_pin = 1 };
	struct pci_device pb = { .devfn = TO_PCI_DEVFN(5, 0), .owner = b, .interrupt_pin = 1 };
	CHECK(pci_device_register_locked(bus, &pa) == 0 && pci_device_register_locked(bus, &pb) == 0);
	pci_device_set_irq_locked(&pa, 1);
	CHECK(level == 1);
	pci_device_set_irq_locked(&pb, 1);
	pci_device_set_irq_locked(&pa, 0);
	CHECK(level == 1);
	/* Masking one INTx source must release only its contribution. */
	pb.config_space[5] = 4;
	pci_device_set_irq_locked(&pb, pb.irq_level);
	CHECK(level == 0);
	pb.config_space[5] = 0;
	pci_device_set_irq_locked(&pb, pb.irq_level);
	CHECK(level == 1);
	device_destroy(b);
	CHECK(level == 0 && !pb.bus);
	CHECK(a->parent == bridge);
	device_destroy(bridge);
	CHECK(!pa.bus && list_empty(&vm.devices) && list_empty(&vm.io_map.pio_regions));
	/* MMIO advertises per-queue limits, rejects truncated selectors, and fully resets. */
	struct net_ops ops = { .get_mac = get_mac, .unbind = unbind, .bind = bind_rx };
	struct net_backend net = { .ops = &ops };
	struct device *dev = device_alloc(&vm, "virtio-mmio");
	CHECK(dev);
	struct virtio_device *v = virtio_net_create(&vm, &net);
	CHECK(v);
	struct mem_region ram = { .gpa = TO_GPA(0), .size = 0x10000, .hva = calloc(1, 0x10000) };
	CHECK(ram.hva);
	INIT_LIST_HEAD(&vm.mem_space.regions);
	list_add_tail(&ram.node, &vm.mem_space.regions);
	struct virtio_mmio_pdata mm = { .base = TO_GPA(0x100000), .vdev = v };
	mm.irq = irq_alloc(&dev->resources, irq, NULL);
	CHECK(mm.irq);
	CHECK(device_realize(dev, &mm) == 0);
#define READ(off) io_map_read(&vm.io_map, IO_MMIO, TO_GPA(0x100000 + (off)), 4)
#define WRITE(off, val) io_map_write(&vm.io_map, IO_MMIO, TO_GPA(0x100000 + (off)), val, 4)
	CHECK(READ(VIRTIO_MMIO_QUEUE_NUM_MAX) == 256);
	WRITE(VIRTIO_MMIO_QUEUE_SEL, 2);
	CHECK(READ(VIRTIO_MMIO_QUEUE_NUM_MAX) == 64);
	WRITE(VIRTIO_MMIO_QUEUE_SEL, 65536);
	CHECK(READ(VIRTIO_MMIO_QUEUE_NUM_MAX) == 0);
	WRITE(VIRTIO_MMIO_QUEUE_READY, 1);
	WRITE(VIRTIO_MMIO_QUEUE_SEL, 0);
	CHECK(READ(VIRTIO_MMIO_QUEUE_READY) == 0);
	WRITE(VIRTIO_MMIO_QUEUE_NUM, 8);
	WRITE(VIRTIO_MMIO_QUEUE_DESC_LOW, 0x1000);
	WRITE(VIRTIO_MMIO_QUEUE_AVAIL_LOW, 0x2000);
	WRITE(VIRTIO_MMIO_QUEUE_USED_LOW, 0x3000);
	WRITE(VIRTIO_MMIO_QUEUE_READY, 1);
	CHECK(READ(VIRTIO_MMIO_QUEUE_READY) == 1);
	WRITE(VIRTIO_MMIO_STATUS, 0);
	CHECK(READ(VIRTIO_MMIO_QUEUE_READY) == 0);
	WRITE(VIRTIO_MMIO_QUEUE_READY, 1);
	CHECK(READ(VIRTIO_MMIO_QUEUE_READY) == 0);
	vm_destroy(&vm);
	CHECK(!atomic_load(&net.binding));
	free(ram.hva);
	log_destroy();
	puts("shared PCI IRQ, endpoint unregister, parent removal and MMIO reset passed");
	return 0;
}
