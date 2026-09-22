/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_COMPILER_TYPES_H
#error "Please do not include <modvm/compiler-clang.h> directly, include <modvm/compiler.h> instead."
#endif

/* Compiler specific definitions for Clang compiler */

/*
 * Clang prior to 17 is being silly and considers many __cleanup() variables
 * as unused (because they are, their sole purpose is to go out of scope).
 *
 * https://github.com/llvm/llvm-project/commit/877210faa447f4cc7db87812f8ed80e398fedd61
 */
#undef __cleanup
#define __cleanup(func) __maybe_unused __attribute__((__cleanup__(func)))

/*
 * Turn individual warnings and errors on and off locally, depending
 * on version.
 */
#define __diag_clang(version, severity, s) __diag_clang_##version(__diag_clang_##severity s)

/* Severity used in pragma directives */
#define __diag_clang_ignore ignored
#define __diag_clang_warn warning
#define __diag_clang_error error

#define __diag_str1(s) #s
#define __diag_str(s) __diag_str1(s)
#define __diag(s) _Pragma(__diag_str(clang diagnostic s))

#define __diag_clang_13(s) __diag(s)

#define __diag_ignore_all(option, comment) __diag_clang(13, ignore, option)

/*
 * clang has horrible behavior with "g" or "rm" constraints for asm
 * inputs, turning them into something worse than "m". Avoid using
 * constraints with multiple possible uses (but "ir" seems to be ok):
 *
 *	https://github.com/llvm/llvm-project/issues/20571
 */
#define ASM_INPUT_G "ir"
#define ASM_INPUT_RM "r"
#define ASM_OUTPUT_RM "=r"

/*
 * Clang 11+ supports the 'asm inline' syntax for compatibility.
 */
#define CC_HAS_ASM_INLINE (__clang_major__ >= 11)

/*
 * Declare compiler support for __typeof_unqual__() operator.
 *
 * Bindgen uses LLVM even if our C compiler is GCC, so we cannot
 * rely on the auto-detected CONFIG_CC_HAS_TYPEOF_UNQUAL.
 */
#define CC_HAS_TYPEOF_UNQUAL (__clang_major__ > 19 || (__clang_major__ == 19 && __clang_minor__ > 0))
