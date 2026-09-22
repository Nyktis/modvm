/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ERR_H
#define MODVM_ERR_H

#include <modvm/errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <modvm/util/compiler.h>

/*
 * Maximum error number.
 * Project error codes (like VM_ENOMEM, VM_EINVAL) are well within the
 * 1 to 4095 range.
 */
#define MAX_ERRNO 4095

/**
 * IS_ERR_VALUE - determine if a pointer value falls in the error range
 * @x: The uintptr_t representation of the pointer
 *
 * In a 64-bit address space, valid user-space pointers will never
 * naturally fall into the extremely high range representing negative
 * integers from -1 to -4095.
 */
#define IS_ERR_VALUE(x) ((uintptr_t)(x) >= (uintptr_t)-(intptr_t)MAX_ERRNO)

/**
 * ERR_PTR - Convert a negative error code to a pointer
 * @error: The negative error code (e.g., -VM_ENOMEM)
 */
static inline void *ERR_PTR(int error)
{
	return (void *)(intptr_t)error;
}

/**
 * PTR_ERR - extract the error code from an error pointer
 * @ptr: the error pointer
 *
 * Return: the negative error code.
 */
static inline int PTR_ERR(const void *ptr)
{
	return (int)(intptr_t)ptr;
}

/**
 * IS_ERR - check if a pointer is actually an error code
 * @ptr: the pointer to check
 */
static inline bool IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((uintptr_t)ptr);
}

/**
 * IS_ERR_OR_NULL - check if a pointer is NULL or an error code
 * @ptr: the pointer to check
 */
static inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((uintptr_t)ptr);
}

#endif /* MODVM_ERR_H */
