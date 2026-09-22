/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTIL_CMDLINE_H
#define MODVM_UTIL_CMDLINE_H

/* Parses comma-separated key=value fields without escaping or special keys.
 * Caller frees the string. NULL means absent; ERR_PTR(-VM_ENOMEM) means allocation failure. */
char *cmdline_extract_opt(const char *opts, const char *key);

#endif /* MODVM_UTIL_CMDLINE_H */
