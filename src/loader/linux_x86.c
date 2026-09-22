/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/host/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/core/vm.h>
#include <modvm/core/memory.h>
#include <modvm/core/vcpu.h>
#include <modvm/arch/x86/regs.h>
#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>
#include <modvm/util/cmdline.h>
#include <modvm/util/err.h>
#include <modvm/util/types.h>

#include "image.h"

#include "e820.h"

#undef pr_fmt
#define pr_fmt(fmt) "linux_loader: " fmt

/* x86 Linux Boot Protocol standard offsets and magic numbers */
#define LINUX_MAGIC_HDR 0x53726448 /* "HdrS" */
#define BOOT_PARAM_E820_ENTRIES 0x01E8
#define BOOT_PARAM_HDR_OFFSET 0x01F1
#define BOOT_PARAM_E820_TABLE 0x02D0

#define SETUP_SECTS_OFFSET 0x01F1
#define SETUP_SYSSIZE_OFFSET 0x01F4
#define SETUP_VID_MODE 0x01FA
#define SETUP_MAGIC_OFFSET 0x0202
#define SETUP_VERSION 0x0206
#define SETUP_INITRD_ADDR_MAX 0x022C
#define SETUP_KERNEL_ALIGNMENT 0x0230
#define SETUP_RELOCATABLE 0x0234
#define SETUP_PREF_ADDRESS 0x0258
#define SETUP_INIT_SIZE 0x0260
#define SETUP_TYPE_OF_LOADER 0x0210
#define SETUP_LOADFLAGS 0x0211
#define SETUP_RAMDISK_IMAGE 0x0218
#define SETUP_RAMDISK_SIZE 0x021C
#define SETUP_HEAP_END_PTR 0x0224
#define SETUP_CMDLINE_PTR 0x0228

/* Hardcoded memory placement addresses commonly used by VMMs */
#define ZERO_PAGE_GPA 0x090000ULL
#define CMDLINE_GPA 0x09E000ULL
#define KERNEL_GPA 0x100000ULL /* 1MB boundary for protected mode */

struct linux_loader_ctx {
	uint64_t entry_pc;
	uint64_t zero_page;
};

/**
 * linux_x86_e820_table_build - synthesize the physical memory map for the guest kernel
 * @ctx: the owning VM context containing the registered memory regions
 * @zero_page: host virtual address of the linux boot_params structure
 *
 * Builds ordered E820 entries from registered memory regions. Reserved or
 * read-only regions are excluded from usable RAM. Unmapped gaps remain absent.
 *
 * Return: 0 on success, or -VM_E2BIG if the boot table cannot hold all entries.
 */
static int linux_x86_e820_table_build(struct vm_ctx *ctx, uint8_t *zero_page)
{
	struct e820_entry *table = (struct e820_entry *)(zero_page + BOOT_PARAM_E820_TABLE);
	uint8_t *nr_entries = zero_page + BOOT_PARAM_E820_ENTRIES;
	unsigned count = 0;
	struct mem_region *region;
	list_for_each_entry(region, &ctx->mem_space.regions, node)
	{
		if (count == 128)
			return -VM_E2BIG;
		/* Keep the boot map ordered even if regions were registered out of order. */
		unsigned index = count;
		while (index && table[index - 1].addr > GPA_VAL(region->gpa)) {
			table[index] = table[index - 1];
			index--;
		}
		table[index] = (struct e820_entry){ .addr = GPA_VAL(region->gpa),
							  .size = region->size,
							  .type = region->flags & (MEM_RESERVED | MEM_READONLY) ? VM_E820_RESERVED : VM_E820_RAM };
		count++;
	}

	*nr_entries = count;
	return 0;
}

/**
 * linux_loader_load - parse bzImage and inject it into guest physical memory
 * @ctx: the owning VM context
 * @opts: configuration string provided by the user
 * @out_priv: output pointer for the opaque loader context
 *
 * Parses the Linux boot protocol header, loads the kernel payload,
 * optionally loads the initrd, and builds the zero page.
 *
 * Return: 0 on success, or a negative error code.
 */
static int linux_loader_load(struct vm_ctx *ctx, const char *opts, void **out_priv)
{
	struct linux_loader_ctx *lctx;
	char *kernel_path = NULL;
	char *cmdline = NULL;
	char *initrd_path = NULL;
	FILE *fp = NULL;
	FILE *rd_fp = NULL;
	uint8_t *hva_zero_page;
	uint8_t *hva_cmdline;
	uint8_t header_buf[1024];
	uint32_t magic;
	uint8_t setup_sects;
	uint32_t setup_size;
	long file_size;
	long rd_size;
	size_t payload_size;
	uint64_t initrd_gpa;
	uint64_t low_ram, runtime_start, runtime_end, initrd_limit;
	uint64_t pref_address;
	uint32_t alignment, init_size, initrd_max;
	uint16_t version;
	int ret = 0;

	/* append consumes the remaining loader options, including any commas. */
	size_t options_size = opts ? strlen(opts) + 1 : 1;
	char *options = malloc(options_size);
	if (!options)
		return -VM_ENOMEM;
	memcpy(options, opts ? opts : "", options_size);
	for (char *field = options; field;) {
		if (!strncmp(field, "append=", 7)) {
			size_t length = strlen(field + 7) + 1;
			cmdline = malloc(length);
			if (cmdline)
				memcpy(cmdline, field + 7, length);
			else
				cmdline = ERR_PTR(-VM_ENOMEM);
			*field = '\0';
			break;
		}
		field = strchr(field, ',');
		if (field)
			field++;
	}
	kernel_path = cmdline_extract_opt(options, "kernel");
	initrd_path = cmdline_extract_opt(options, "initrd");
	free(options);

	if (IS_ERR(kernel_path) || IS_ERR(cmdline) || IS_ERR(initrd_path)) {
		ret = IS_ERR(kernel_path) ? PTR_ERR(kernel_path) : IS_ERR(cmdline) ? PTR_ERR(cmdline) : PTR_ERR(initrd_path);
		if (IS_ERR(kernel_path))
			kernel_path = NULL;
		if (IS_ERR(cmdline))
			cmdline = NULL;
		if (IS_ERR(initrd_path))
			initrd_path = NULL;
		goto err_free_opts;
	}
	if (WARN_ON(!kernel_path)) {
		pr_err("linux protocol strictly requires 'kernel=<path>' option\n");
		ret = -VM_EINVAL;
		goto err_free_opts;
	}

	fp = fopen(kernel_path, "rb");
	if (!fp) {
		ret = -host_error_from_errno(errno);
		pr_err("failed to acquire bzImage handle: %s (errno: %d)\n", kernel_path, errno);
		goto err_free_opts;
	}

	if (fread(header_buf, 1, sizeof(header_buf), fp) != sizeof(header_buf)) {
		pr_err("failed to read setup header from bzImage\n");
		ret = -VM_EIO;
		goto err_close_kernel;
	}

	memcpy(&magic, header_buf + SETUP_MAGIC_OFFSET, sizeof(magic));
	if (magic != LINUX_MAGIC_HDR) {
		pr_err("invalid Linux magic signature: expected 0x%x, got 0x%x\n", LINUX_MAGIC_HDR, magic);
		ret = -VM_EINVAL;
		goto err_close_kernel;
	}

	/* init_size is required to keep the initrd out of decompression memory. */
	memcpy(&version, header_buf + SETUP_VERSION, sizeof(version));
	if (version < 0x020a) {
		pr_err("linux-x86 requires boot protocol 2.10 or newer\n");
		ret = -VM_EINVAL;
		goto err_close_kernel;
	}
	memcpy(&pref_address, header_buf + SETUP_PREF_ADDRESS, sizeof(pref_address));
	memcpy(&alignment, header_buf + SETUP_KERNEL_ALIGNMENT, sizeof(alignment));
	memcpy(&init_size, header_buf + SETUP_INIT_SIZE, sizeof(init_size));
	memcpy(&initrd_max, header_buf + SETUP_INITRD_ADDR_MAX, sizeof(initrd_max));
	/* Find the writable usable interval above 1 MiB from the actual board map. */
	low_ram = KERNEL_GPA;
	bool extended;
	do {
		extended = false;
		struct mem_region *region;
		list_for_each_entry(region, &ctx->mem_space.regions, node)
		{
			uint64_t base = GPA_VAL(region->gpa), end = base + region->size;
			if (!(region->flags & (MEM_READONLY | MEM_RESERVED)) && base <= low_ram && end > low_ram) {
				low_ram = end;
				extended = true;
			}
		}
	} while (extended);
	if (low_ram > (1ULL << 32))
		low_ram = 1ULL << 32;
	runtime_start = pref_address;
	if (header_buf[SETUP_RELOCATABLE]) {
		if (!alignment || (alignment & (alignment - 1))) {
			ret = -VM_EINVAL;
			goto err_close_kernel;
		}
		if (runtime_start < KERNEL_GPA)
			runtime_start = KERNEL_GPA;
		if (runtime_start > UINT64_MAX - (alignment - 1)) {
			ret = -VM_EINVAL;
			goto err_close_kernel;
		}
		runtime_start = (runtime_start + alignment - 1) & ~((uint64_t)alignment - 1);
	}
	if (!init_size || runtime_start < KERNEL_GPA || runtime_start >= low_ram || init_size > low_ram - runtime_start) {
		pr_err("kernel initialization workspace exceeds guest low RAM\n");
		ret = -VM_ENOSPC;
		goto err_close_kernel;
	}
	runtime_end = runtime_start + init_size;
	initrd_limit = (uint64_t)initrd_max + 1;
	if (initrd_limit > low_ram)
		initrd_limit = low_ram;

	setup_sects = *(uint8_t *)(header_buf + SETUP_SECTS_OFFSET);
	if (setup_sects == 0)
		setup_sects = 4;
	setup_size = (setup_sects + 1) * 512;

	if (fseek(fp, 0, SEEK_END)) {
		ret = -host_error_from_errno(errno);
		goto err_close_kernel;
	}
	file_size = ftell(fp);
	if (file_size < setup_size) {
		pr_err("corrupted bzImage: file size smaller than setup data\n");
		ret = -VM_EINVAL;
		goto err_close_kernel;
	}

	hva_zero_page = mem_map_range(&ctx->mem_space, TO_GPA(ZERO_PAGE_GPA), 4096, true);
	hva_cmdline = mem_map_range(&ctx->mem_space, TO_GPA(CMDLINE_GPA), 4096, true);

	if (!hva_zero_page || !hva_cmdline) {
		pr_err("failed to resolve guest physical memory for Linux injection\n");
		ret = -VM_EFAULT;
		goto err_close_kernel;
	}

	memset(hva_zero_page, 0, 4096);
	memcpy(hva_zero_page + BOOT_PARAM_HDR_OFFSET, header_buf + BOOT_PARAM_HDR_OFFSET, sizeof(header_buf) - BOOT_PARAM_HDR_OFFSET);

	hva_zero_page[SETUP_TYPE_OF_LOADER] = 0xFF;

	if (cmdline) {
		strncpy((char *)hva_cmdline, cmdline, 4095);
		hva_cmdline[4095] = '\0';
		*(uint32_t *)(hva_zero_page + SETUP_CMDLINE_PTR) = (uint32_t)CMDLINE_GPA;
	} else {
		*(uint32_t *)(hva_zero_page + SETUP_CMDLINE_PTR) = 0;
	}

	ret = linux_x86_e820_table_build(ctx, hva_zero_page);
	if (ret < 0)
		goto err_close_kernel;

	if (fseek(fp, setup_size, SEEK_SET)) {
		ret = -host_error_from_errno(errno);
		goto err_close_kernel;
	}
	payload_size = file_size - setup_size;

	if (KERNEL_GPA + payload_size > low_ram) {
		pr_err("kernel payload exceeds low ram contiguous boundary\n");
		ret = -VM_ENOSPC;
		goto err_close_kernel;
	}

	for (size_t done = 0; done < payload_size;) {
		size_t chunk;
		void *hva = mem_map_chunk(&ctx->mem_space, TO_GPA(KERNEL_GPA + done), payload_size - done, true, &chunk);
		if (!hva || !chunk) {
			ret = -VM_EFAULT;
			goto err_close_kernel;
		}
		if (fread(hva, 1, chunk, fp) != chunk) {
			ret = -VM_EIO;
			goto err_close_kernel;
		}
		done += chunk;
	}

	if (initrd_path) {
		rd_fp = fopen(initrd_path, "rb");
		if (!rd_fp) {
			ret = -host_error_from_errno(errno);
			pr_err("failed to acquire initrd handle: %s (errno: %d)\n", initrd_path, errno);
			goto err_close_kernel;
		}

		if (fseek(rd_fp, 0, SEEK_END)) {
			ret = -host_error_from_errno(errno);
			goto err_close_initrd;
		}
		rd_size = ftell(rd_fp);
		if (fseek(rd_fp, 0, SEEK_SET)) {
			ret = -host_error_from_errno(errno);
			goto err_close_initrd;
		}

		if (rd_size <= 0) {
			pr_err("empty or unreadable initrd\n");
			ret = -VM_EINVAL;
			goto err_close_initrd;
		}

		/* Place at the top of addressable RAM, outside both kernel ranges. */
		if ((uint64_t)rd_size > initrd_limit) {
			ret = -VM_ENOSPC;
			goto err_close_initrd;
		}
		initrd_gpa = (initrd_limit - (uint64_t)rd_size) & ~4095ULL;

		if (initrd_gpa < runtime_end || initrd_gpa < KERNEL_GPA + payload_size) {
			pr_err("initrd does not fit outside kernel initialization workspace\n");
			ret = -VM_ENOSPC;
			goto err_close_initrd;
		}

		for (size_t done = 0; done < (size_t)rd_size;) {
			size_t chunk;
			void *hva = mem_map_chunk(&ctx->mem_space, TO_GPA(initrd_gpa + done), rd_size - done, true, &chunk);
			if (!hva || !chunk) {
				ret = -VM_EFAULT;
				goto err_close_initrd;
			}
			if (fread(hva, 1, chunk, rd_fp) != chunk) {
				ret = -VM_EIO;
				goto err_close_initrd;
			}
			done += chunk;
		}

		*(uint32_t *)(hva_zero_page + SETUP_RAMDISK_IMAGE) = (uint32_t)initrd_gpa;
		*(uint32_t *)(hva_zero_page + SETUP_RAMDISK_SIZE) = (uint32_t)rd_size;

		pr_info("successfully streamed %ld bytes from '%s' to gpa 0x%08lx\n", rd_size, initrd_path, initrd_gpa);

		fclose(rd_fp);
		rd_fp = NULL;
	}

	lctx = malloc(sizeof(*lctx));
	if (!lctx) {
		ret = -VM_ENOMEM;
		goto err_close_kernel;
	}

	lctx->entry_pc = KERNEL_GPA;
	lctx->zero_page = ZERO_PAGE_GPA;
	*out_priv = lctx;

	pr_info("linux direct boot parameters successfully provisioned\n");
	pr_info("cmdline injected: %s\n", cmdline ? cmdline : "<none>");

	fclose(fp);
	free(kernel_path);
	free(cmdline);
	free(initrd_path);
	return 0;

err_close_initrd:
	if (rd_fp)
		fclose(rd_fp);
err_close_kernel:
	if (fp)
		fclose(fp);
err_free_opts:
	free(kernel_path);
	free(cmdline);
	free(initrd_path);
	return ret;
}

/**
 * linux_loader_setup_bsp - set the initial 32-bit protected-mode registers
 * @vcpu: the bootstrap processor
 * @priv: the opaque linux context holding the entry point
 *
 * Sets x86 segment and control registers for flat 32-bit protected mode,
 * with the entry address and boot-parameter pointer expected by this loader.
 *
 * Return: 0 on success, or a negative error code.
 */
static int linux_loader_setup_bsp(struct vcpu *vcpu, void *priv)
{
	struct linux_loader_ctx *lctx = priv;
	struct x86_sregs sregs;
	int ret;

	ret = vcpu_get_regs(vcpu, REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	sregs.cr0 = 0x01;
	sregs.cr4 = 0;

	sregs.cs.base = 0;
	sregs.cs.limit = 0xFFFFFFFF;
	sregs.cs.selector = 0x10;
	sregs.cs.type = 0x0b;
	sregs.cs.present = 1;
	sregs.cs.dpl = 0;
	sregs.cs.db = 1;
	sregs.cs.s = 1;
	sregs.cs.l = 0;
	sregs.cs.g = 1;
	sregs.cs.unusable = 0;

	sregs.ds.base = 0;
	sregs.ds.limit = 0xFFFFFFFF;
	sregs.ds.selector = 0x18;
	sregs.ds.type = 0x03;
	sregs.ds.present = 1;
	sregs.ds.dpl = 0;
	sregs.ds.db = 1;
	sregs.ds.s = 1;
	sregs.ds.l = 0;
	sregs.ds.g = 1;
	sregs.ds.unusable = 0;

	sregs.es = sregs.ds;
	sregs.fs = sregs.ds;
	sregs.gs = sregs.ds;
	sregs.ss = sregs.ds;

	ret = vcpu_set_regs(vcpu, REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(vcpu, X86_REG_RFLAGS, 0x02);
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(vcpu, X86_REG_RIP, lctx->entry_pc);
	if (WARN_ON(ret < 0))
		return ret;

	ret = vcpu_set_reg(vcpu, X86_REG_RSI, lctx->zero_page);
	if (WARN_ON(ret < 0))
		return ret;

	return 0;
}

/**
 * linux_loader_destroy - free memory associated with the loader context
 * @priv: the opaque linux context
 */
static void linux_loader_destroy(void *priv)
{
	free(priv);
}

static const struct loader_desc linux_desc = {
	.name = "linux-x86",
	.load = linux_loader_load,
	.setup_bsp = linux_loader_setup_bsp,
	.destroy = linux_loader_destroy,
};

static void __attribute__((constructor)) register_linux_loader(void)
{
	loader_register(&linux_desc);
}
