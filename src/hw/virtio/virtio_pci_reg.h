/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef MODVM_SRC_HW_VIRTIO_PCI_REG_H
#define MODVM_SRC_HW_VIRTIO_PCI_REG_H

#include <modvm/util/types.h>
#include <modvm/util/compiler.h>

/* Virtio ABI version, this must match exactly */
#define VIRTIO_PCI_ABI_VERSION 0

/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG 0x2

/* VirtIO PCI capability type IDs. */

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
/* ISR access */
#define VIRTIO_PCI_CAP_ISR_CFG 3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG 5

/**
 * struct virtio_pci_cap - base PCI capability header for virtio
 * @cap_vndr: Generic PCI field: PCI_CAP_ID_VNDR
 * @cap_next: Generic PCI field: next capability pointer
 * @cap_len: Generic PCI field: capability length
 * @cfg_type: Identifies the structure (e.g., VIRTIO_PCI_CAP_COMMON_CFG)
 * @bar: Which BAR this configuration resides in
 * @id: Multiple capabilities of the same type identifier
 * @offset: Offset within the specified BAR
 * @length: Length of the structure, in bytes
 */
struct virtio_pci_cap {
	uint8_t cap_vndr; /* Generic PCI field: PCI_CAP_ID_VNDR */
	uint8_t cap_next; /* Generic PCI field: next ptr. */
	uint8_t cap_len; /* Generic PCI field: capability length */
	uint8_t cfg_type; /* Identifies the structure. */
	uint8_t bar; /* Where to find it. */
	uint8_t id; /* Multiple capabilities of the same type */
	uint8_t padding[2]; /* Pad to full dword. */
	le32_t offset; /* Offset within bar. */
	le32_t length; /* Length of the structure, in bytes. */
} __packed;

/**
 * struct virtio_pci_notify_cap - notification capability header
 * @cap: standard virtio pci capability header
 * @notify_off_multiplier: Multiplier for queue_notify_off
 */
struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	le32_t notify_off_multiplier; /* Multiplier for queue_notify_off. */
} __packed;

/**
 * struct virtio_pci_common_cfg - modern virtio pci common configuration
 *
 * Maps exactly to the memory structure defined in Virtio 1.0 Specification.
 * Provides independent control over device features and queue parameters.
 */
struct virtio_pci_common_cfg {
	/* About the whole device. */
	le32_t device_feature_select; /* read-write */
	le32_t device_feature; /* read-only */
	le32_t guest_feature_select; /* read-write */
	le32_t guest_feature; /* read-write */
	le16_t msix_config; /* read-write */
	le16_t num_queues; /* read-only */
	uint8_t device_status; /* read-write */
	uint8_t config_generation; /* read-only */

	/* About a specific virtqueue. */
	le16_t queue_select; /* read-write */
	le16_t queue_size; /* read-write, power of 2. */
	le16_t queue_msix_vector; /* read-write */
	le16_t queue_enable; /* read-write */
	le16_t queue_notify_off; /* read-only */
	le32_t queue_desc_lo; /* read-write */
	le32_t queue_desc_hi; /* read-write */
	le32_t queue_avail_lo; /* read-write */
	le32_t queue_avail_hi; /* read-write */
	le32_t queue_used_lo; /* read-write */
	le32_t queue_used_hi; /* read-write */
} __packed;

#endif /* MODVM_SRC_HW_VIRTIO_PCI_REG_H */
