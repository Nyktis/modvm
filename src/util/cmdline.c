/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stdlib.h>
#include <string.h>
#include <modvm/util/err.h>

#include <modvm/util/cmdline.h>

/**
 * cmdline_extract_opt - parse a key-value string option
 * @opts: comma-separated key=value fields; keys have no special meaning
 * @key: the key to search for
 *
 * Return: dynamically allocated string containing the value, NULL if not found, or ERR_PTR(-VM_ENOMEM).
 * The caller is strictly responsible for freeing the returned string.
 */
char *cmdline_extract_opt(const char *opts, const char *key)
{
	const char *start;
	const char *end;
	char *val;
	size_t len;
	size_t key_len;

	if (!opts || !key || !*key)
		return NULL;
	key_len = strlen(key);
	start = opts;
	while (strncmp(start, key, key_len) || start[key_len] != '=') {
		start = strchr(start, ',');
		if (!start)
			return NULL;
		start++;
	}
	start += key_len + 1;

	end = strchr(start, ',');
	if (!end)
		end = start + strlen(start);

	len = end - start;
	val = malloc(len + 1);
	if (val) {
		strncpy(val, start, len);
		val[len] = '\0';
	}
	return val ? val : ERR_PTR(-VM_ENOMEM);
}
