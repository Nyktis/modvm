/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BUG_H
#define MODVM_BUG_H

#include <modvm/util/compiler.h>
#include <modvm/util/log.h>
#include <modvm/util/once_lite.h>

/**
 * BUG - trigger a fatal system error
 *
 * Use only when data structure corruption is detected and there's
 * no way to continue operation securely.
 *
 * If you're tempted to BUG(), think again:  is completely giving up
 * really the *only* solution?  There are usually better options.
 */
#define BUG()                                                                                              \
	do {                                                                                               \
		printk(LOG_EMERG, "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
		panic("BUG!");                                                                       \
	} while (0)

/**
 * BUG_ON - conditionally trigger a fatal system error
 * @condition: the condition to test
 */
#define BUG_ON(condition)                \
	do {                             \
		if (unlikely(condition)) \
			BUG();           \
	} while (0)

/**
 * WARN - print a warning message with file/line information
 * @condition: the condition to test
 * @format: format string
 *
 * Use this for recoverable issues. It evaluates the condition, prints a
 * warning if true, and returns the condition's value so it can be used
 * directly in an if-statement.
 */
#define WARN(condition, fmt, ...)                                                                                               \
	({                                                                                                                      \
		int __ret_warn_on = !!(condition);                                                                              \
		if (unlikely(__ret_warn_on))                                                                                    \
			printk(LOG_WARN, "WARNING: at %s:%d/%s()\n" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
		unlikely(__ret_warn_on);                                                                                        \
	})

#define WARN_ON(condition) WARN(condition, "")

/**
 * WARN_ON_ONCE - warn only once for a given condition
 * @condition: the condition to test
 */
#define WARN_ON_ONCE(condition) DO_ONCE_LITE_IF(condition, WARN_ON, 1)

#define WARN_ONCE(condition, fmt, ...) DO_ONCE_LITE_IF(condition, WARN, 1, fmt, ##__VA_ARGS__)

#endif /* MODVM_BUG_H */
