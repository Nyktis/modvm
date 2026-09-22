/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <modvm/errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <modvm/core/vm.h>
#include "../src/core/memory_internal.h"
#include <modvm/io/block.h>
#include <modvm/util/err.h>
#include <modvm/io/net.h>
#include <modvm/core/loader.h>
#include "image.h"
#include <modvm/util/cmdline.h>
#include <modvm/util/byteorder.h>
#include <modvm/util/log.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_blk.h>
#include <modvm/hw/virtio/virtio_net.h>
#include "../src/hw/virtio/virtqueue.h"
#include "../src/hw/virtio/virtio_blk_reg.h"
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static unsigned reads, writes, flushes;
static ptrdiff_t rd(struct block_backend *b, void *p, size_t n, uint64_t o)
{
	(void)b;
	(void)o;
	reads++;
	memset(p, 0x5a, n);
	return n;
}

static ptrdiff_t wr(struct block_backend *b, const void *p, size_t n, uint64_t o)
{
	(void)b;
	(void)p;
	(void)o;
	writes++;
	return n;
}

static int flush(struct block_backend *b)
{
	(void)b;
	flushes++;
	return -VM_EIO;
}

static uint64_t capacity(struct block_backend *b)
{
	(void)b;
	return 4096;
}

static void put16(uint8_t *p, unsigned n)
{
	le16_t v = cpu_to_le16(n);
	memcpy(p, &v, 2);
}

static unsigned get16(uint8_t *p)
{
	le16_t v;
	memcpy(&v, p, 2);
	return le16_to_cpu(v);
}

static void setdesc(struct vring_desc *d, uint64_t addr, unsigned len, unsigned flags, unsigned next)
{
	*d = (struct vring_desc){ cpu_to_le64(addr), cpu_to_le32(len), cpu_to_le16(flags), cpu_to_le16(next) };
}

static void transport_irq(void *data)
{
	(void)data;
}
static const struct virtio_transport_ops transport = { .notify_queue = transport_irq, .notify_config = transport_irq };

int main(void)
{
	struct res_pool backends;
	res_pool_init(&backends);
	log_init();
	struct vm_ctx vm = { 0 };
	res_pool_init(&vm.resources);
	struct mem_region ram = { .gpa = TO_GPA(0), .size = 16384 };
	ram.hva = calloc(1, ram.size);
	CHECK(ram.hva);
	uint8_t *mem = ram.hva;
	INIT_LIST_HEAD(&vm.mem_space.regions);
	list_add_tail(&ram.node, &vm.mem_space.regions);
	CHECK(!mem_map_range(&vm.mem_space, TO_GPA(16380), 8, true));
	CHECK(!mem_map_range(&vm.mem_space, TO_GPA(UINT64_MAX - 2), 8, true));
	ram.flags = MEM_READONLY;
	CHECK(!mem_map_range(&vm.mem_space, TO_GPA(0), 8, true));
	size_t mapped = 123;
	CHECK(!mem_map_chunk(&vm.mem_space, TO_GPA(0), 8, true, &mapped) && mapped == 0);
	CHECK(mem_map_chunk(&vm.mem_space, TO_GPA(16380), 8, false, &mapped) == mem + 16380 && mapped == 4);
	CHECK(!mem_map_chunk(&vm.mem_space, TO_GPA(16384), 8, false, &mapped) && mapped == 0);
	CHECK(!mem_map_chunk(&vm.mem_space, TO_GPA(UINT64_MAX - 2), 8, false, &mapped) && mapped == 0);
	ram.flags = 0;
	CHECK(!res_zalloc(&vm.resources, SIZE_MAX));
	CHECK(!cmdline_extract_opt("notdriver=bad,append=driver=bad", "driver"));
	char *opt = cmdline_extract_opt("append=ordinary,notdriver=bad,driver=good", "driver");
	CHECK(opt && !strcmp(opt, "good"));
	free(opt);
	opt = cmdline_extract_opt("append=ordinary,driver=good", "append");
	CHECK(opt && !IS_ERR(opt) && !strcmp(opt, "ordinary"));
	free(opt);
	struct net_ops netops = { 0 };
	struct net_backend net = { .ops = &netops };
	virtio_device_destroy(virtio_net_create(&vm, &net));
	struct virtqueue *vq = virtqueue_create(&vm.mem_space, 8);
	CHECK(vq);
	CHECK(virtqueue_set_addrs(vq, TO_GPA(16368), TO_GPA(4096), TO_GPA(8192)) < 0);
	CHECK(virtqueue_set_addrs(vq, TO_GPA(1), TO_GPA(4096), TO_GPA(8192)) < 0);
	CHECK(virtqueue_set_addrs(vq, TO_GPA(0), TO_GPA(4096), TO_GPA(8192)) == 0);
	struct vring_desc *desc = (void *)mem;
	setdesc(desc, 0, 0, VRING_DESC_F_NEXT, 0);
	put16(mem + 4098, 1);
	struct virtqueue_chain chain;

	CHECK(virtqueue_pop(vq, &chain) == -VM_ELOOP);
	virtqueue_reset(vq);
	CHECK(virtqueue_pop(vq, &chain) == 0);
	virtqueue_destroy(vq);
	char path[] = "/tmp/modvm-hardening-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, 32768) == 0);
	CHECK(loader_load_raw(&vm.mem_space, path, TO_GPA(0)) < 0);
	CHECK(ftruncate(fd, 4096) == 0);
	close(fd);
	char opts[256];
	snprintf(opts, sizeof(opts), "path=%s", path);
	struct block_backend *file = block_create(&backends, "linux-file", opts);
	CHECK(file);
	CHECK(file->ops->write(file, mem, 512, UINT64_MAX - 255) < 0);
	CHECK(file->ops->read(file, mem, 512, 4095) < 0);
	CHECK(file->ops->write(file, mem, 512, 0) == 512);
	CHECK(file->ops->flush(file) == 0);
	res_release_all(&backends);
	unlink(path);
	memset(mem, 0, ram.size);
	const struct block_ops ops = { .read = rd, .write = wr, .flush = flush, .get_capacity = capacity };
	struct block_backend backend = { .ops = &ops };
	struct device parent = { .ctx = &vm };
	res_pool_init(&parent.resources);
	struct virtio_device *blk = virtio_blk_create(&vm, &backend);
	CHECK(blk);
	CHECK(virtio_device_adopt_locked(&parent, blk, &transport, NULL) == 0);
	CHECK(virtio_device_realize_locked(blk) == 0);
	blk->driver_features = blk->device_ops->get_features(blk) | VIRTIO_F_VERSION_1;
	virtio_device_set_status_locked(blk, 15);
	CHECK(blk->device_ops->get_features(blk) == ((1ULL << VIRTIO_BLK_F_BLK_SIZE) | (1ULL << VIRTIO_BLK_F_GEOMETRY) | (1ULL << VIRTIO_BLK_F_FLUSH)));
	CHECK(virtqueue_set_addrs(blk->vqs[0], TO_GPA(0), TO_GPA(4096), TO_GPA(8192)) == 0);
	struct virtio_blk_outhdr *hdr = (void *)(mem + 12288);
	uint8_t *status = mem + 13312;
	setdesc(desc, 12288, sizeof(*hdr), VRING_DESC_F_NEXT, 1);
	setdesc(desc + 1, 12800, 512, VRING_DESC_F_NEXT | VRING_DESC_F_WRITE, 2);
	setdesc(desc + 2, 13312, 1, VRING_DESC_F_WRITE, 0);
	hdr->type = cpu_to_le32(VIRTIO_BLK_T_IN);
	hdr->sector = cpu_to_le64(UINT64_MAX / 512 + 1);
	put16(mem + 4098, 1);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(*status == VIRTIO_BLK_S_IOERR && !reads && get16(mem + 8194) == 1);
	/* Wrong direction must be rejected before any backend I/O. */
	hdr->sector = cpu_to_le64(0);
	desc[1].flags = cpu_to_le16(VRING_DESC_F_NEXT);
	put16(mem + 4098, 2);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(*status == VIRTIO_BLK_S_IOERR && !reads && !writes);
	/* A valid read still completes with data. */
	desc[1].flags = cpu_to_le16(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE);
	put16(mem + 4098, 3);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(*status == VIRTIO_BLK_S_OK && reads == 1 && mem[12800] == 0x5a);
	/* FLUSH invokes the backend and propagates its failure. */
	hdr->type = cpu_to_le32(VIRTIO_BLK_T_FLUSH);
	desc[0].next = cpu_to_le16(2);
	put16(mem + 4098, 4);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(flushes == 1 && *status == VIRTIO_BLK_S_IOERR);
	/* A request header may cross separately allocated host memory regions. */
	struct mem_region tail = { .gpa = TO_GPA(12296), .size = 4088, .hva = calloc(1, 4088) };
	CHECK(tail.hva);
	ram.size = 12296;
	list_add_tail(&tail.node, &vm.mem_space.regions);
	struct virtio_blk_outhdr split_hdr = { .type = cpu_to_le32(VIRTIO_BLK_T_IN) };
	memcpy(mem + 12288, &split_hdr, 8);
	memcpy(tail.hva, (uint8_t *)&split_hdr + 8, 8);
	desc[0].next = cpu_to_le16(1);
	put16(mem + 4098, 5);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(reads == 2 && ((uint8_t *)tail.hva)[12800 - 12296] == 0x5a && ((uint8_t *)tail.hva)[13312 - 12296] == VIRTIO_BLK_S_OK);
	list_del(&tail.node);
	free(tail.hva);
	ram.size = 16384;
	/* Missing status halts I/O until reset, without writing into the header. */
	desc[0].flags = cpu_to_le16(0);
	put16(mem + 4098, 6);
	blk->device_ops->notify_queue(blk, 0);
	CHECK(get16(mem + 8194) == 5 && (blk->status & VIRTIO_STATUS_NEEDS_RESET));
	CHECK(virtio_device_destroy(blk) == -VM_EBUSY);
	res_release_all(&parent.resources);
	free(mem);
	log_destroy();
	puts("memory, descriptors, loader, allocation, parsing and block failure paths passed");
	return 0;
}
