/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_PAGE_H
#define MODVM_HOST_PAGE_H

#include <modvm/util/stddef.h>

/* Host page size in bytes, or 0 if unavailable. */
size_t host_page_size(void);

/* Zero-filled page-aligned storage; returns ERR_PTR(-VM_E*) on failure. */
void *host_page_alloc(size_t size);
void host_page_free(void *ptr, size_t size);

#endif /* MODVM_HOST_PAGE_H */
