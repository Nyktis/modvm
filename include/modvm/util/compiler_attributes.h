/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_COMPILER_ATTRIBUTES_H
#define MODVM_COMPILER_ATTRIBUTES_H

/*
 * The attributes in this file are unconditionally defined and they directly
 * map to compiler attribute(s), unless one of the compilers does not support
 * the attribute. In that case, __has_attribute is used to check for support
 * and the reason is stated in its comment ("Optional: ...").
 *
 * Any other "attributes" (i.e. those that depend on a configuration option,
 * on a compiler, on an architecture, on plugins, on other attributes...)
 * should be defined elsewhere (e.g. compiler_types.h or compiler-*.h).
 * The intention is to keep this file as simple as possible, as well as
 * compiler- and version-agnostic (e.g. avoiding GCC_VERSION checks).
 *
 * This file is meant to be sorted (by actual attribute name,
 * not by #define identifier). Use the __attribute__((__name__)) syntax
 * (i.e. with underscores) to avoid future collisions with other macros.
 * Provide links to the documentation of each supported compiler, if it exists.
 */

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-alias-function-attribute
 */
#define __alias(symbol) __attribute__((__alias__(#symbol)))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-aligned-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-aligned-type-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-aligned-variable-attribute
 */
#define __aligned(x) __attribute__((__aligned__(x)))
#define __aligned_largest __attribute__((__aligned__))

/*
 * Note: do not use this directly. Instead, use __alloc_size() since it is conditionally
 * available and includes other attributes. For GCC < 9.1, __alloc_size__ gets undefined
 * in compiler-gcc.h, due to misbehaviors.
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-alloc_005fsize-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#alloc-size
 */
/* #define __alloc_size__(x, ...) __attribute__((__alloc_size__(x, ##__VA_ARGS__))) */

/*
 * Note: users of __always_inline currently do not write "inline" themselves,
 * which seems to be required by gcc to apply the attribute according
 * to its docs (and also "warning: always_inline function might not be
 * inlinable [-Wattributes]" is emitted).
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-always_005finline-function-attribute
 * clang: mentioned
 */
#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif

/*
 * The second argument is optional (default 0), so we use a variadic macro
 * to make the shorthand.
 *
 * Beware: Do not apply this to functions which may return
 * ERR_PTRs. Also, it is probably unwise to apply it to functions
 * returning extra information in the low bits (but in that case the
 * compiler should see some alignment anyway, when the return value is
 * massaged by 'flags = ptr & 3; ptr &= ~3;').
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-assume_005faligned-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#assume-aligned
 */
#define __assume_aligned(a, ...) __attribute__((__assume_aligned__(a, ##__VA_ARGS__)))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-cleanup-variable-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#cleanup
 */
#define __cleanup(func) __attribute__((__cleanup__(func)))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-cold-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Label-Attributes.html#index-cold-label-attribute
 *
 * When -falign-functions=N is in use, we must avoid the cold attribute as
 * GCC drops the alignment for cold functions. Worse, GCC can implicitly mark
 * callees of cold functions as cold themselves, so it's not sufficient to add
 * __function_aligned here as that will not ensure that callees are correctly
 * aligned.
 *
 * See:
 *
 *   https://lore.kernel.org/lkml/Y77%2FqVgvaJidFpYt@FVFF77S0Q05N
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=88345#c9
 */
#if __has_attribute(__cold__)
#define __cold __attribute__((__cold__))
#else
#define __cold
#endif

/*
 * Note the long name.
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-const-function-attribute
 */
#ifndef __attribute_const__
#define __attribute_const__ __attribute__((__const__))
#endif

/*
 * Optional: only supported since gcc >= 9
 * Optional: not supported by clang
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-copy-function-attribute
 */
#if __has_attribute(__copy__)
#define __copy(symbol) __attribute__((__copy__(symbol)))
#else
#define __copy(symbol)
#endif

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-deprecated-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-deprecated-type-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-deprecated-variable-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Enumerator-Attributes.html#index-deprecated-enumerator-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#deprecated
 */
#define __deprecated __attribute__((__deprecated__))

/*
 * Optional: not supported by clang
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-designated_005finit-type-attribute
 */
#if __has_attribute(__designated_init__)
#define __designated_init __attribute__((__designated_init__))
#else
#define __designated_init
#endif

/*
 * Optional: only supported since clang >= 14.0
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-error-function-attribute
 */
#if __has_attribute(__error__)
#define __compiletime_error(msg) __attribute__((__error__(msg)))
#else
#define __compiletime_error(msg)
#endif

/*
 * Optional: not supported by clang
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-externally_005fvisible-function-attribute
 */
#if __has_attribute(__externally_visible__)
#define __visible __attribute__((__externally_visible__))
#else
#define __visible
#endif

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-format-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#format
 */
#define __printf(a, b) __attribute__((__format__(printf, a, b)))
#define __scanf(a, b) __attribute__((__format__(scanf, a, b)))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-gnu_005finline-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#gnu-inline
 */
#define __gnu_inline __attribute__((__gnu_inline__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-malloc-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#malloc
 */
#define __malloc __attribute__((__malloc__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-mode-type-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-mode-variable-attribute
 */
#define __mode(x) __attribute__((__mode__(x)))

/*
 * Optional: not supported by clang
 *
 *  gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-noclone-function-attribute
 */
#if __has_attribute(__noclone__)
#define __noclone __attribute__((__noclone__))
#else
#define __noclone
#endif

/*
 * Add the pseudo keyword 'fallthrough' so case statement blocks
 * must end with any of these keywords:
 *   break;
 *   fallthrough;
 *   continue;
 *   goto <label>;
 *   return [expression];
 *
 *  gcc: https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html#Statement-Attributes
 */
#if __has_attribute(__fallthrough__)
#define fallthrough __attribute__((__fallthrough__))
#else
#define fallthrough \
	do {        \
	} while (0) /* fallthrough */
#endif

/*
 * gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#Common-Function-Attributes
 * clang: https://clang.llvm.org/doc/AttributeReference.html#flatten
 */
#define __flatten __attribute__((flatten))

/*
 * Note the missing underscores.
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-noinline-function-attribute
 * clang: mentioned
 */
#define noinline __attribute__((__noinline__))

/*
 * Optional: only supported since gcc >= 8
 * Optional: not supported by clang
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-nonstring-variable-attribute
 */
#if __has_attribute(__nonstring__)
#define __nonstring __attribute__((__nonstring__))
#else
#define __nonstring
#endif

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-noreturn-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#noreturn
 * clang: https://clang.llvm.org/doc/AttributeReference.html#id1
 */
#define __noreturn __attribute__((__noreturn__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-packed-type-attribute
 * clang: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-packed-variable-attribute
 */
#define __packed __attribute__((__packed__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-pure-function-attribute
 */
#define __pure __attribute__((__pure__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-section-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-section-variable-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#section-declspec-allocate
 */
#define __section(section) __attribute__((__section__(section)))

/*
 * Optional: only supported since gcc >= 12
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-uninitialized-variable-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#uninitialized
 */
#if __has_attribute(__uninitialized__)
#define __uninitialized __attribute__((__uninitialized__))
#else
#define __uninitialized
#endif

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-unused-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#index-unused-type-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-unused-variable-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Label-Attributes.html#index-unused-label-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#maybe-unused-unused
 */
#define __always_unused __attribute__((__unused__))
#define __maybe_unused __attribute__((__unused__))

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-used-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-used-variable-attribute
 */
#define __used __attribute__((__used__))

/*
 * The __used attribute guarantees that the attributed variable will be
 * always emitted by a compiler. It doesn't prevent the compiler from
 * throwing 'unused' warnings when it can't detect how the variable is
 * actually used. It's a compiler implementation details either emit
 * the warning in that case or not.
 *
 * The combination of both 'used' and 'unused' attributes ensures that
 * the variable would be emitted, and will not trigger 'unused' warnings.
 * The attribute is applicable for functions, static and global variables.
 */
#define __always_used __used __maybe_unused

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-warn_005funused_005fresult-function-attribute
 * clang: https://clang.llvm.org/doc/AttributeReference.html#nodiscard-warn-unused-result
 */
#define __must_check __attribute__((__warn_unused_result__))

/*
 * Optional: only supported since clang >= 14.0
 *
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-warning-function-attribute
 */
#if __has_attribute(__warning__)
#define __compiletime_warning(msg) __attribute__((__warning__(msg)))
#else
#define __compiletime_warning(msg)
#endif

/*
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#index-weak-function-attribute
 *   gcc: https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-weak-variable-attribute
 */
#define __weak __attribute__((__weak__))

#endif /* MODVM_COMPILER_ATTRIBUTES_H */
