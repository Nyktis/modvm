/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <modvm/host/error.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <modvm/host/page.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>
#include <modvm/util/compiler.h>

/**
 * host_page_size - retrieve the native hardware page size
 *
 * Interrogates the host operating system for the underlying architecture's
 * default page frame size (typically 4KB).
 *
 * Return: the host page size in bytes, or 0 if the query fails.
 */
size_t host_page_size(void)
{
	long size = sysconf(_SC_PAGESIZE);
	return size > 0 ? (size_t)size : 0;
}

/**
 * host_page_alloc - allocate page-aligned anonymous memory
 * @size: the total amount of memory to allocate in bytes
 *
 * Uses mmap to obtain zero-filled memory aligned to the host page size.
 *
 * Return: pointer to the allocated memory, or an ERR_PTR on failure.
 */
void *host_page_alloc(size_t size)
{
	void *ptr;

	if (WARN_ON(size == 0))
		return ERR_PTR(-VM_EINVAL);

	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr == MAP_FAILED)
		return ERR_PTR(-host_error_from_errno(errno));

	return ptr;
}

/**
 * host_page_free - release previously allocated page-aligned memory
 * @ptr: the base address of the memory block
 * @size: the exact size provided during allocation
 */
void host_page_free(void *ptr, size_t size)
{
	if (WARN_ON(!ptr || IS_ERR(ptr) || size == 0))
		return;

	munmap(ptr, size);
}
