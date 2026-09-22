/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_COMPILER_TYPES_H
#define MODVM_COMPILER_TYPES_H

/*
 * __has_builtin is supported on gcc >= 10, clang >= 3 and icc >= 21.
 * In the meantime, to support gcc < 10, we implement __has_builtin
 * by hand.
 */
#ifndef __has_builtin
#define __has_builtin(x) (0)
#endif

/* Indirect macros required for expanded argument pasting, eg. __LINE__. */
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)

#ifndef __ASSEMBLY__

/*
 * C23 introduces "auto" as a standard way to define type-inferred
 * variables, but "auto" has been a (useless) keyword even since K&R C,
 * so it has always been "namespace reserved."
 *
 * Until at some future time we require C23 support, we need the gcc
 * extension __auto_type, but there is no reason to put that elsewhere
 * in the source code.
 */
#if __STDC_VERSION__ < 202311L
#define auto __auto_type
#endif

#include <modvm/util/compiler-context-analysis.h>

#define __force
#define __nocast
#define __safe
#define __private
#define __user
#define __kernel
#define __iomem
#define __rcu
#define ACCESS_PRIVATE(p, member) ((p)->member)

/* Attributes */
#include <modvm/util/compiler_attributes.h>

/* Compiler specific macros. */
#ifdef __clang__
#include <modvm/util/compiler-clang.h>
#elif defined(__GNUC__)
/* The above compilers also define __GNUC__, so order is important here. */
#include <modvm/util/compiler-gcc.h>
#else
#error "Unknown compiler"
#endif

/*
 * Optional: only supported since gcc >= 15
 * Optional: only supported since clang >= 18
 *
 *   gcc: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108896
 * clang: https://clang.llvm.org/doc/AttributeReference.html#counted-by-counted-by-or-null-sized-by-sized-by-or-null
 *
 * __bdos on clang < 19.1.2 can erroneously return 0:
 * https://github.com/llvm/llvm-project/pull/110497
 *
 * __bdos on clang < 19.1.3 can be off by 4:
 * https://github.com/llvm/llvm-project/pull/112636
 */
#ifndef __counted_by
#if __has_attribute(__counted_by__)
#define __counted_by(member) __attribute__((__counted_by__(member)))
#else
#define __counted_by(member)
#endif
#endif

/*
 * Runtime track number of objects pointed to by a pointer member for use by
 * CONFIG_FORTIFY_SOURCE and CONFIG_UBSAN_BOUNDS.
 *
 * Optional: only supported since gcc >= 16
 * Optional: only supported since clang >= 22
 *
 *   gcc: https://gcc.gnu.org/pipermail/gcc-patches/2025-April/681727.html
 * clang: https://clang.llvm.org/doc/AttributeReference.html#counted-by-counted-by-or-null-sized-by-sized-by-or-null
 */
#if __has_attribute(__counted_by__)
#define __counted_by_ptr(member) __attribute__((__counted_by__(member)))
#else
#define __counted_by_ptr(member)
#endif

/*
 * Optional: only supported since gcc >= 15
 * Optional: not supported by Clang
 *
 * gcc: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=117178
 */
#if __has_attribute(__nonstring__)
#define __nonstring_array __attribute__((__nonstring__))
#else
#define __nonstring_array
#endif

/*
 * This designates the minimum number of elements a passed array parameter must
 * have. For example:
 *
 *     void some_function(u8 param[at_least 7]);
 *
 * If a caller passes an array with fewer than 7 elements, the compiler will
 * emit a warning.
 */
#define at_least static

#endif /* __ASSEMBLY__ */

/*
 * The below symbols may be defined for one or more, but not ALL, of the above
 * compilers. We don't consider that to be an error, so set them to nothing.
 * For example, some of them are for compiler specific plugins.
 */
#ifndef __latent_entropy
#define __latent_entropy
#endif

/*
 * Any place that could be marked with the "alloc_size" attribute is also
 * a place to be marked with the "malloc" attribute, except those that may
 * be performing a _reallocation_, as that may alias the existing pointer.
 * For these, use __realloc_size().
 */
#ifdef __alloc_size__
#define __alloc_size(x, ...) __alloc_size__(x, ##__VA_ARGS__) __malloc
#define __realloc_size(x, ...) __alloc_size__(x, ##__VA_ARGS__)
#else
#define __alloc_size(x, ...) __malloc
#define __realloc_size(x, ...)
#endif

/*
 * When the size of an allocated object is needed, use the best available
 * mechanism to find it. (For cases where sizeof() cannot be used.)
 *
 * Optional: only supported since gcc >= 12
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Object-Size-Checking.html
 * clang: https://clang.llvm.org/doc/LanguageExtensions.html#evaluating-object-size
 */
#if __has_builtin(__builtin_dynamic_object_size)
#define __struct_size(p) __builtin_dynamic_object_size(p, 0)
#define __member_size(p) __builtin_dynamic_object_size(p, 1)
#else
#define __struct_size(p) __builtin_object_size(p, 0)
#define __member_size(p) __builtin_object_size(p, 1)
#endif

/*
 * Determine if an attribute has been applied to a variable.
 * Using __annotated needs to check for __annotated being available,
 * or negative tests may fail when annotation cannot be checked. For
 * example, see the definition of __is_cstr().
 */
#if __has_builtin(__builtin_has_attribute)
#define __annotated(var, attr) __builtin_has_attribute(var, attr)
#endif

/*
 * Some versions of gcc do not mark 'asm goto' volatile:
 *
 *  https://gcc.gnu.org/bugzilla/show_bug.cgi?id=103979
 *
 * We do it here by hand, because it doesn't hurt.
 */
#ifndef asm_goto_output
#define asm_goto_output(x...) asm volatile goto(x)
#endif

/*
 * Clang has trouble with constraints with multiple
 * alternative behaviors ("g" , "rm" and "=rm").
 */
#ifndef ASM_INPUT_G
#define ASM_INPUT_G "g"
#define ASM_INPUT_RM "rm"
#define ASM_OUTPUT_RM "=rm"
#endif

#ifdef CC_HAS_ASM_INLINE
#define asm_inline asm __inline
#else
#define asm_inline asm
#endif

#ifndef __ASSEMBLY__
/*
 * Use __typeof_unqual__() when available.
 */
#if CC_HAS_TYPEOF_UNQUAL
#define USE_TYPEOF_UNQUAL 1
#endif

/* Are two types/vars the same type (ignoring qualifiers)? */
#define __same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))

/*
 * __unqual_scalar_typeof(x) - Declare an unqualified scalar type, leaving
 *			       non-scalar types unchanged.
 */
#ifndef USE_TYPEOF_UNQUAL
/*
 * Prefer C11 _Generic for better compile-times and simpler code. Note: 'char'
 * is not type-compatible with 'signed char', and we define a separate case.
 */
#define __scalar_type_to_expr_cases(type) unsigned type : (unsigned type)0, signed type : (signed type)0

#define __unqual_scalar_typeof(x)                                  \
	__typeof__(_Generic((x),                                   \
			   char: (char)0,                          \
			   __scalar_type_to_expr_cases(char),      \
			   __scalar_type_to_expr_cases(short),     \
			   __scalar_type_to_expr_cases(int),       \
			   __scalar_type_to_expr_cases(long),      \
			   __scalar_type_to_expr_cases(long long), \
			   default: (x)))
#else
#define __unqual_scalar_typeof(x) __typeof_unqual__(x)
#endif
#endif /* !__ASSEMBLY__ */

/*
 * __signed_scalar_typeof(x) - Declare a signed scalar type, leaving
 *			       non-scalar types unchanged.
 */

#define __scalar_type_to_signed_cases(type) unsigned type : (signed type)0, signed type : (signed type)0

#define __signed_scalar_typeof(x)                                    \
	__typeof__(_Generic((x),                                     \
			   char: (signed char)0,                     \
			   __scalar_type_to_signed_cases(char),      \
			   __scalar_type_to_signed_cases(short),     \
			   __scalar_type_to_signed_cases(int),       \
			   __scalar_type_to_signed_cases(long),      \
			   __scalar_type_to_signed_cases(long long), \
			   default: (x)))

/* Is this type a native word size -- useful for atomic operations */
#define __native_word(t) (sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

#ifdef __OPTIMIZE__
/*
 * #ifdef __OPTIMIZE__ is only a good approximation; for instance "make
 * CFLAGS_foo.o=-Og" defines __OPTIMIZE__, does not elide the conditional code
 * and can break compilation with wrong error message(s). Combine with
 * -U__OPTIMIZE__ when needed.
 */
#define __compiletime_assert(condition, msg, prefix, suffix)                          \
	do {                                                                          \
		/*							\
		 * __noreturn is needed to give the compiler enough	\
		 * information to avoid certain possibly-uninitialized	\
		 * warnings (regardless of the build failing).		\
		 */                                                            \
		__noreturn extern void prefix##suffix(void) __compiletime_error(msg); \
		if (!(condition))                                                     \
			prefix##suffix();                                             \
	} while (0)
#else
#define __compiletime_assert(condition, msg, prefix, suffix) ((void)(condition))
#endif

#define _compiletime_assert(condition, msg, prefix, suffix) __compiletime_assert(condition, msg, prefix, suffix)

/**
 * compiletime_assert - break build and emit msg if condition is false
 * @condition: a compile-time constant condition to check
 * @msg:       a message to emit if condition is false
 *
 * In tradition of POSIX assert, this macro will break the build if the
 * supplied condition is *false*, emitting the supplied error message if the
 * compiler has support to do so.
 */
#define compiletime_assert(condition, msg) _compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)

#define compiletime_assert_atomic_type(t) compiletime_assert(__native_word(t), "Need native word sized stores/loads for atomicity.")

/* Helpers for emitting diagnostics in pragmas. */
#ifndef __diag
#define __diag(string)
#endif

#define __diag_push() __diag(push)
#define __diag_pop() __diag(pop)

#endif /* MODVM_COMPILER_TYPES_H */
