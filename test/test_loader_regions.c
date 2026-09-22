/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <modvm/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <modvm/core/loader.h>
#include <modvm/core/vm.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static void put(uint8_t *p, uint64_t value, unsigned size)
{
	for (unsigned i = 0; i < size; i++)
		p[i] = value >> (8 * i);
}

static int get_regs(struct vcpu *cpu, enum reg_class kind, void *data, size_t size)
{
	(void)cpu;
	(void)kind;
	memset(data, 0, size);
	return 0;
}

static int set_regs(struct vcpu *cpu, enum reg_class kind, const void *data, size_t size)
{
	(void)cpu;
	(void)kind;
	(void)data;
	(void)size;
	return 0;
}

static int set_reg(struct vcpu *cpu, uint64_t reg, uint64_t value)
{
	(void)cpu;
	(void)reg;
	(void)value;
	return 0;
}

int main(void)
{
	log_init();
	const struct vcpu_ops ops = { .get_regs = get_regs, .set_regs = set_regs, .set_reg = set_reg };
	struct vcpu cpu = { .ops = &ops }, *cpus[] = { &cpu };
	struct vm_ctx vm = { .vcpus = cpus, .config.nr_vcpus = 1 };
	res_pool_init(&vm.resources);
	INIT_LIST_HEAD(&vm.mem_space.regions);
	struct mem_region regions[] = {
		{ .gpa = TO_GPA(0x90000), .size = 4096 },    { .gpa = TO_GPA(0x9e000), .size = 4096 },	  { .gpa = TO_GPA(0x100000), .size = 4096 },
		{ .gpa = TO_GPA(0x101000), .size = 0x5000 }, { .gpa = TO_GPA(0x106000), .size = 0x2000 },
	};
	for (unsigned i = 0; i < 5; i++) {
		regions[i].hva = calloc(1, regions[i].size);
		CHECK(regions[i].hva);
		list_add_tail(&regions[i].node, &vm.mem_space.regions);
	}
	uint8_t kernel[8192] = { 0 }, initrd[8193];
	kernel[0x1f1] = 1;
	put(kernel + 0x202, 0x53726448, 4);
	put(kernel + 0x206, 0x20a, 2);
	put(kernel + 0x22c, 0x37ffffff, 4);
	put(kernel + 0x230, 4096, 4);
	kernel[0x234] = 1;
	put(kernel + 0x258, 0x100000, 8);
	put(kernel + 0x260, 8192, 4);
	memset(kernel + 1024, 0x59, sizeof(kernel) - 1024);
	memset(initrd, 0xa6, sizeof(initrd));
	char kp[] = "/tmp/modvm-loader-k-XXXXXX", rp[] = "/tmp/modvm-loader-r-XXXXXX", options[256];
	int kfd = mkstemp(kp), rfd = mkstemp(rp);
	CHECK(kfd >= 0 && rfd >= 0);
	CHECK(write(kfd, kernel, sizeof(kernel)) == sizeof(kernel));
	CHECK(write(rfd, initrd, sizeof(initrd)) == sizeof(initrd));
	close(kfd);
	close(rfd);
	CHECK(snprintf(options, sizeof(options), "kernel=%s,initrd=%s,append=console=ttyS0,115200,initrd=/not-a-loader-option", kp, rp) < (int)sizeof(options));
	CHECK(loader_execute(&vm, "linux-x86", options) == 0);
	CHECK(!strcmp(regions[1].hva, "console=ttyS0,115200,initrd=/not-a-loader-option"));
	CHECK(((uint8_t *)regions[2].hva)[4095] == 0x59 && ((uint8_t *)regions[3].hva)[0] == 0x59);
	CHECK(((uint8_t *)regions[3].hva)[0x4000] == 0xa6 && ((uint8_t *)regions[4].hva)[4096] == 0xa6);
	CHECK(((uint8_t *)regions[0].hva)[0x1e8] == 5); /* Actual regions, not config.ram_size. */
	regions[0].flags = MEM_READONLY;
	CHECK(loader_execute(&vm, "linux-x86", options) == -VM_EFAULT);
	regions[0].flags = 0;
	regions[0].size = 1;
	CHECK(loader_execute(&vm, "linux-x86", options) == -VM_EFAULT);
	regions[0].size = 4096;
	regions[3].flags = MEM_READONLY;
	CHECK(loader_execute(&vm, "linux-x86", options) == -VM_ENOSPC);
	regions[3].flags = 0;
	regions[3].gpa = TO_GPA(0x102000);
	CHECK(loader_execute(&vm, "linux-x86", options) == -VM_ENOSPC);
	res_release_all(&vm.resources);
	for (unsigned i = 0; i < 5; i++)
		free(regions[i].hva);
	unlink(kp);
	unlink(rp);
	log_destroy();
	puts("Linux loader: disjoint host buffers, actual boot map, gaps and write permissions passed");
	return 0;
}
