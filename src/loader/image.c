/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <errno.h>
#include <modvm/host/error.h>
#include <modvm/core/memory.h>
#include <modvm/util/bug.h>
#include <modvm/util/log.h>
#include "image.h"

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

/**
 * loader_load_raw - stream a binary payload directly into guest memory
 * @space: the target physical memory space
 * @path: host filesystem path to the payload
 * @gpa: destination physical address for the payload
 *
 * A generic utility function utilized by legacy raw loaders and testing
 * infrastructures to bypass complex protocol parsing.
 *
 * Return: 0 on success, or a negative error code.
 */
int loader_load_raw(struct mem_space *space, const char *path, gpa_t gpa)
{
	FILE *fp;
	long size;
	size_t read_len;
	void *hva;

	if (WARN_ON(!space || !path))
		return -VM_EINVAL;

	fp = fopen(path, "rb");
	if (!fp) {
		int error = host_error_from_errno(errno);
		pr_err("failed to acquire image handle: %s (errno: %d)\n", path, error);
		return -error;
	}

	if (fseek(fp, 0, SEEK_END) < 0) {
		fclose(fp);
		return -VM_EIO;
	}

	size = ftell(fp);
	if (size <= 0) {
		pr_err("payload image rejected due to zero or negative length: %s\n", path);
		fclose(fp);
		return -VM_EINVAL;
	}

	rewind(fp);

	hva = mem_map_range(space, gpa, (size_t)size, true);
	if (!hva) {
		pr_err("address translation trap: unmapped gpa 0x%llx\n", (unsigned long long)GPA_VAL(gpa));
		fclose(fp);
		return -VM_EFAULT;
	}

	read_len = fread(hva, 1, (size_t)size, fp);
	if (read_len != (size_t)size) {
		pr_err("short stream read: expected %ld bytes, acquired %zu\n", size, read_len);
		fclose(fp);
		return -VM_EIO;
	}

	pr_info("successfully streamed %zu bytes from '%s' to gpa 0x%08llx\n", read_len, path, (unsigned long long)GPA_VAL(gpa));

	fclose(fp);
	return 0;
}
