/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_COMPILER_H
#define MODVM_COMPILER_H

#include <modvm/util/compiler_types.h>

#ifndef __ASSEMBLY__

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Optimization barrier */
#ifndef barrier
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("" : : : "memory")
#endif

#ifndef barrier_data
/*
 * This version is i.e. to prevent dead stores elimination on @ptr
 * where gcc and llvm may behave differently when otherwise using
 * normal barrier(): while gcc behavior gets along with a normal
 * barrier(), llvm needs an explicit input variable to be assumed
 * clobbered. The issue is as follows: while the inline asm might
 * access any memory it wants, the compiler could have fit all of
 * @ptr into memory registers instead, and since @ptr never escaped
 * from that, it proved that the inline asm wasn't touching any of
 * it. This version works well with both compilers, i.e. we're telling
 * the compiler that the inline asm absolutely may see the contents
 * of @ptr. See also: https://llvm.org/bugs/show_bug.cgi?id=15495
 */
#define barrier_data(ptr) __asm__ __volatile__("" : : "r"(ptr) : "memory")
#endif

/* workaround for GCC PR82365 if needed */
#ifndef barrier_before_unreachable
#define barrier_before_unreachable() \
	do {                         \
	} while (0)
#endif

/*
 * Mark a position in code as unreachable.  This can be used to
 * suppress control flow warnings after asm blocks that transfer
 * control elsewhere.
 */
#define unreachable()                         \
	do {                                  \
		barrier_before_unreachable(); \
		__builtin_unreachable();      \
	} while (0)

#ifndef RELOC_HIDE
#define RELOC_HIDE(ptr, off)                      \
	({                                        \
		unsigned long __ptr;              \
		__ptr = (unsigned long)(ptr);     \
		(__typeof__(ptr))(__ptr + (off)); \
	})
#endif

#define absolute_pointer(val) RELOC_HIDE((void *)(val), 0)

#ifndef OPTIMIZER_HIDE_VAR
/* Make the optimizer believe the variable can be manipulated arbitrarily. */
#define OPTIMIZER_HIDE_VAR(var) __asm__("" : "=r"(var) : "0"(var))
#endif

/* Format: __UNIQUE_ID_<name>_<__COUNTER__> */
#define __UNIQUE_ID(name) __PASTE(__UNIQUE_ID_, __PASTE(name, __PASTE(_, __COUNTER__)))

#define __BUILD_BUG_ON_ZERO_MSG(e, msg, ...) ((int)sizeof(struct { _Static_assert(!(e), msg); }))

/* &a[0] degrades to a pointer: a different type from an array */
#define __is_array(a) (!__same_type((a), &(a)[0]))
#define __must_be_array(a) __BUILD_BUG_ON_ZERO_MSG(!__is_array(a), "must be array")

#define __is_byte_array(a) (__is_array(a) && sizeof((a)[0]) == 1)
#define __must_be_byte_array(a) __BUILD_BUG_ON_ZERO_MSG(!__is_byte_array(a), "must be byte array")

/*
 * If the "nonstring" attribute isn't available, we have to return true
 * so the __must_*() checks pass when "nonstring" isn't supported.
 */
#if __has_attribute(__nonstring__) && defined(__annotated)
#define __is_cstr(a) (!__annotated(a, nonstring))
#define __is_noncstr(a) (__annotated(a, nonstring))
#else
#define __is_cstr(a) (true)
#define __is_noncstr(a) (true)
#endif

/* Require C Strings (i.e. NUL-terminated) lack the "nonstring" attribute. */
#define __must_be_cstr(p) __BUILD_BUG_ON_ZERO_MSG(!__is_cstr(p), "must be C-string (NUL-terminated)")
#define __must_be_noncstr(p) __BUILD_BUG_ON_ZERO_MSG(!__is_noncstr(p), "must be non-C-string (not NUL-terminated)")

/*
 * Define TYPEOF_UNQUAL() to use __typeof_unqual__() as typeof
 * operator when available, to return an unqualified type of the exp.
 */
#if defined(USE_TYPEOF_UNQUAL)
#define TYPEOF_UNQUAL(exp) __typeof_unqual__(exp)
#else
#define TYPEOF_UNQUAL(exp) __typeof__(exp)
#endif

/**
 * offset_to_ptr - convert a relative memory offset to an absolute pointer
 * @off:	the address of the 32-bit offset value
 */
static inline void *offset_to_ptr(const int *off)
{
	return (void *)((unsigned long)off + *off);
}

#endif /* __ASSEMBLY__ */

/*
 * This returns a constant expression while determining if an argument is
 * a constant expression, most importantly without evaluating the argument.
 * Glory to Martin Uecker <Martin.Uecker@med.uni-goettingen.de>
 *
 * Details:
 * - sizeof() return an integer constant expression, and does not evaluate
 *   the value of its operand; it only examines the type of its operand.
 * - The results of comparing two integer constant expressions is also
 *   an integer constant expression.
 * - The first literal "8" isn't important. It could be any literal value.
 * - The second literal "8" is to avoid warnings about unaligned pointers;
 *   this could otherwise just be "1".
 * - (long)(x) is used to avoid warnings about 64-bit types on 32-bit
 *   architectures.
 * - The C Standard defines "null pointer constant", "(void *)0", as
 *   distinct from other void pointers.
 * - If (x) is an integer constant expression, then the "* 0l" resolves
 *   it into an integer constant expression of value 0. Since it is cast to
 *   "void *", this makes the second operand a null pointer constant.
 * - If (x) is not an integer constant expression, then the second operand
 *   resolves to a void pointer (but not a null pointer constant: the value
 *   is not an integer constant 0).
 * - The conditional operator's third operand, "(int *)8", is an object
 *   pointer (to type "int").
 * - The behavior (including the return type) of the conditional operator
 *   ("operand1 ? operand2 : operand3") depends on the kind of expressions
 *   given for the second and third operands. This is the central mechanism
 *   of the macro:
 *   - When one operand is a null pointer constant (i.e. when x is an integer
 *     constant expression) and the other is an object pointer (i.e. our
 *     third operand), the conditional operator returns the type of the
 *     object pointer operand (i.e. "int *"). Here, within the sizeof(), we
 *     would then get:
 *       sizeof(*((int *)(...))  == sizeof(int)  == 4
 *   - When one operand is a void pointer (i.e. when x is not an integer
 *     constant expression) and the other is an object pointer (i.e. our
 *     third operand), the conditional operator returns a "void *" type.
 *     Here, within the sizeof(), we would then get:
 *       sizeof(*((void *)(...)) == sizeof(void) == 1
 * - The equality comparison to "sizeof(int)" therefore depends on (x):
 *     sizeof(int) == sizeof(int)     (x) was a constant expression
 *     sizeof(int) != sizeof(void)    (x) was not a constant expression
 */
#define __is_constexpr(x) (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

/*
 * Whether 'type' is a signed type or an unsigned type. Supports scalar types,
 * bool and also pointer types.
 */
#define is_signed_type(type) (((type)(-1)) < (__force type)1)
#define is_unsigned_type(type) (!is_signed_type(type))

/*
 * Useful shorthand for "is this condition known at compile-time?"
 *
 * Note that the condition may involve non-constant values,
 * but the compiler may know enough about the details of the
 * values to determine that the condition is statically true.
 */
#define statically_true(x) (__builtin_constant_p(x) && (x))

/*
 * Similar to statically_true() but produces a constant expression
 *
 * To be used in conjunction with macros, such as BUILD_BUG_ON_ZERO(),
 * which require their input to be a constant expression and for which
 * statically_true() would otherwise fail.
 *
 * This is a trade-off: const_true() requires all its operands to be
 * compile time constants. Else, it would always returns false even on
 * the most trivial cases like:
 *
 *   true || non_const_var
 *
 * On the opposite, statically_true() is able to fold more complex
 * tautologies and will return true on expressions such as:
 *
 *   !(non_const_var * 8 % 4)
 *
 * For the general case, statically_true() is better.
 */
#define const_true(x) __builtin_choose_expr(__is_constexpr(x), x, false)

#endif /* MODVM_COMPILER_H */
