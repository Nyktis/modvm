/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_COMPILER_TYPES_H
#error "Please do not include <modvm/compiler-gcc.h> directly, include <modvm/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

/*
 * This macro obfuscates arithmetic on a variable address so that gcc
 * shouldn't recognize the original var, and make assumptions about it.
 *
 * This is needed because the C standard makes it undefined to do
 * pointer arithmetic on "objects" outside their boundaries and the
 * gcc optimizers assume this is the case. In particular they
 * assume such arithmetic does not wrap.
 *
 * A miscompilation has been observed because of this on PPC.
 * To work around it we hide the relationship of the pointer and the object
 * using this macro.
 *
 * Versions of the ppc64 compiler before 4.1 had a bug where use of
 * RELOC_HIDE could trash r30. The bug can be worked around by changing
 * the inline assembly constraint from =g to =r, in this particular
 * case either is valid.
 */
#define RELOC_HIDE(ptr, off)                          \
	({                                            \
		unsigned long __ptr;                  \
		__asm__("" : "=r"(__ptr) : "0"(ptr)); \
		(__typeof__(ptr))(__ptr + (off));     \
	})

/*
 * calling noreturn functions, __builtin_unreachable() and __builtin_trap()
 * confuse the stack allocation in gcc, leading to overly large stack
 * frames, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82365
 *
 * Adding an empty inline assembly before it works around the problem
 */
#define barrier_before_unreachable() asm volatile("")

/*
 * Turn individual warnings and errors on and off locally, depending
 * on version.
 */
#define __diag_GCC(version, severity, s) __diag_GCC_##version(__diag_GCC_##severity s)

/* Severity used in pragma directives */
#define __diag_GCC_ignore ignored
#define __diag_GCC_warn warning
#define __diag_GCC_error error

#define __diag_str1(s) #s
#define __diag_str(s) __diag_str1(s)
#define __diag(s) _Pragma(__diag_str(GCC diagnostic s))

#if GCC_VERSION >= 80000
#define __diag_GCC_8(s) __diag(s)
#else
#define __diag_GCC_8(s)
#endif

#define __diag_ignore_all(option, comment) __diag(__diag_GCC_ignore option)

/*
 * GCC 9.1+ supports the 'asm inline' syntax to tell the inliner
 * not to overestimate the size of the assembly block.
 */
#define CC_HAS_ASM_INLINE (GCC_VERSION >= 90100)

/*
 * Declare compiler support for __typeof_unqual__() operator.
 *
 * Bindgen uses LLVM even if our C compiler is GCC, so we cannot
 * rely on the auto-detected CONFIG_CC_HAS_TYPEOF_UNQUAL.
 */
#define CC_HAS_TYPEOF_UNQUAL (__GNUC__ >= 14)
