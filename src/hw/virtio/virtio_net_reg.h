#ifndef MODVM_SRC_HW_VIRTIO_NET_REG_H
#define MODVM_SRC_HW_VIRTIO_NET_REG_H
/* This header is BSD licensed so anyone can use the definitions to implement
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
 * SUCH DAMAGE. */
#include <modvm/util/types.h>
#include <modvm/util/compiler.h>

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_MAC 5 /* Host has given MAC address. */
#define VIRTIO_NET_F_STATUS 16 /* virtio_net_config.status available */
#define VIRTIO_NET_F_CTRL_VQ 17 /* Control channel available */
#define VIRTIO_NET_F_CTRL_MAC_ADDR 23 /* Set MAC address */

#define VIRTIO_NET_S_LINK_UP 1 /* Link is up */

/**
 * struct virtio_net_config - standard configuration space for net devices
 *
 * Defines the static and dynamic parameters exposed to the guest OS driver.
 * Strictly uses Little-Endian types to enforce Virtio 1.0 byte order rules.
 */
struct virtio_net_config {
	/* The config defining mac address (if VIRTIO_NET_F_MAC) */
	uint8_t mac[6];
	/* See VIRTIO_NET_F_STATUS and VIRTIO_NET_S_* above */
	virtio16_t status;
	/* Maximum number of each of transmit and receive queues;
	 * see VIRTIO_NET_F_MQ and VIRTIO_NET_CTRL_MQ.
	 * Legal values are between 1 and 0x8000
	 */
	virtio16_t max_virtqueue_pairs;
	/* Default maximum transmit unit advice */
	virtio16_t mtu;
	/*
	 * speed, in units of 1Mb. All values 0 to INT_MAX are legal.
	 * Any other value stands for unknown.
	 */
	le32_t speed;
	/*
	 * 0x00 - half duplex
	 * 0x01 - full duplex
	 * Any other value stands for unknown.
	 */
	uint8_t duplex;
	/* maximum size of RSS key */
	uint8_t rss_max_key_size;
	/* maximum number of indirection table entries */
	le16_t rss_max_indirection_table_length;
	/* bitmask of supported VIRTIO_NET_RSS_HASH_ types */
	le32_t supported_hash_types;
} __packed;

/**
 * struct virtio_net_hdr_v1 - modern network packet header
 *
 * This header comes first in the scatter-gather list for every TX and RX
 * packet. Offload and merged-buffer fields are part of the wire layout;
 * this device does not advertise those features.
 */
struct virtio_net_hdr_v1 {
	uint8_t flags;
	uint8_t gso_type;
	virtio16_t hdr_len; /* Ethernet + IP + tcp/udp hdrs */
	virtio16_t gso_size; /* Bytes to append to hdr_len per frame */
	virtio16_t csum_start;
	virtio16_t csum_offset;
	virtio16_t num_buffers; /* Number of merged rx buffers */
} __packed;

/**
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct virtio_net_ctrl_hdr {
	uint8_t class;
	uint8_t cmd;
} __packed;

#define VIRTIO_NET_OK 0
#define VIRTIO_NET_ERR 1

/*
 * Standard RX mode command definitions; this device does not advertise CTRL_RX.
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the VIRTIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with VIRTIO_NET_F_CTRL_RX_EXTRA.
 */
#define VIRTIO_NET_CTRL_RX 0
#define VIRTIO_NET_CTRL_RX_PROMISC 0
#define VIRTIO_NET_CTRL_RX_ALLMULTI 1

/* MAC control command codes. This device implements ADDR_SET only, with
 * VIRTIO_NET_F_CTRL_MAC_ADDR. Its payload is a six-byte MAC address.
 * TABLE_SET is defined here but is not implemented or advertised.
 */
#define VIRTIO_NET_CTRL_MAC 1
#define VIRTIO_NET_CTRL_MAC_TABLE_SET 0
#define VIRTIO_NET_CTRL_MAC_ADDR_SET 1

#endif /* MODVM_SRC_HW_VIRTIO_NET_REG_H */
