/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ONCE_LITE_H
#define MODVM_ONCE_LITE_H

#include <stdbool.h>
#include <stdatomic.h>
#include <modvm/util/compiler.h>

/* Invoke the function at most once per macro expansion site, when the condition
 * is true. Other threads do not wait for that invocation to finish; this is not
 * an initialization barrier. The result is the condition, not whether this
 * caller ran the function.
 */
#define DO_ONCE_LITE(func, ...) DO_ONCE_LITE_IF(true, func, ##__VA_ARGS__)

#define __ONCE_LITE_IF(condition)                                                                                          \
	({                                                                                                                 \
		static atomic_bool __already_done;                                                                         \
		bool __ret_once = !!(condition) && !atomic_exchange_explicit(&__already_done, true, memory_order_relaxed); \
		unlikely(__ret_once);                                                                                      \
	})

#define DO_ONCE_LITE_IF(condition, func, ...)       \
	({                                          \
		bool __ret_do_once = !!(condition); \
                                                    \
		if (__ONCE_LITE_IF(__ret_do_once))  \
			func(__VA_ARGS__);          \
                                                    \
		unlikely(__ret_do_once);            \
	})

#endif /* MODVM_ONCE_LITE_H */
