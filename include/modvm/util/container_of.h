/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CONTAINER_OF_H
#define MODVM_CONTAINER_OF_H

#include <modvm/util/build_bug.h>
#include <modvm/util/stddef.h>

#define typeof_member(T, m) __typeof__(((T *)0)->m)

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * WARNING: any const qualifier of @ptr is lost.
 * Do not use container_of() in new code.
 */
#define container_of(ptr, type, member)                                                                                                                \
	({                                                                                                                                             \
		void *__mptr = (void *)(ptr);                                                                                                          \
		BUILD_BUG_ON_MSG(!(__same_type(*(ptr), ((type *)0)->member) || __same_type(*(ptr), void)), "pointer type mismatch in container_of()"); \
		((type *)(__mptr - offsetof(type, member)));                                                                                           \
	})

/**
 * container_of_const - cast a member of a structure out to the containing
 *			structure and preserve the const-ness of the pointer
 * @ptr:		the pointer to the member
 * @type:		the type of the container struct this is embedded in.
 * @member:		the name of the member within the struct.
 *
 * Always prefer container_of_const() instead of container_of() in new code.
 */
#define container_of_const(ptr, type, member) \
	_Generic(ptr, const __typeof__(*(ptr)) *: ((const type *)container_of(ptr, type, member)), default: ((type *)container_of(ptr, type, member)))

#endif /* MODVM_CONTAINER_OF_H */
