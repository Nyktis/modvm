/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <modvm/host/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <modvm/util/log.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>
#include <modvm/util/compiler.h>
#include <modvm/host/console.h>

/* Don't do this:
 *
 * #undef pr_fmt
 * #define pr_fmt(fmt) "log: " fmt
 */

#define LOG_LINE_MAX_LENGTH 1024

static const char *const level_prefixes[] = {
	[LOG_EMERG] = "[EMERG]  ", [LOG_ALERT] = "[ALERT]  ",  [LOG_CRIT] = "[CRIT]   ", [LOG_ERR] = "[ERROR]  ",
	[LOG_WARN] = "[WARN]   ",	 [LOG_NOTICE] = "[NOTICE] ", [LOG_INFO] = "[INFO]   ", [LOG_DEBUG] = "[DEBUG]  ",
};

static struct host_mutex *log_lock = NULL;

/**
 * log_init - initialize the global logging subsystem
 *
 * Allocates the synchronization primitive required to prevent log tearing
 * in symmetric multiprocessing environments. Must be invoked early.
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
int log_init(void)
{
	if (!log_lock) {
		log_lock = host_mutex_create();
		if (IS_ERR(log_lock)) {
			int error = PTR_ERR(log_lock);
			log_lock = NULL;
			return error;
		}
	}
	return 0;
}

/**
 * log_destroy - tear down the global logging subsystem
 */
void log_destroy(void)
{
	if (log_lock) {
		host_mutex_destroy(log_lock);
		log_lock = NULL;
	}
}

/**
 * printk - emits a formatted message to the standard output streams
 * @level: the severity level of the message determining its output target
 * @fmt: the format specification string
 *
 * Assembles the severity prefix and the payload into a local stack buffer.
 * Serializes logging calls once initialized; early logging runs without a lock.
 *
 * Return: formatted character count (before CR insertion), or negative project error on output failure.
 * Preserves the caller's errno even if diagnostic output fails.
 */
int printk(enum log_level level, const char *fmt, ...)
{
	int saved_errno = errno;
	va_list args;
	int ret;
	int prefix_len = 0;
	int payload_len;
	int avail;
	int i;
	char buf[LOG_LINE_MAX_LENGTH];
	enum host_console_stream stream = level <= LOG_ERR ? HOST_CONSOLE_ERR : HOST_CONSOLE_OUT;

	if (level >= LOG_EMERG && level <= LOG_DEBUG) {
		prefix_len = snprintf(buf, sizeof(buf), "%s", level_prefixes[level]);
		if (unlikely(prefix_len < 0))
			prefix_len = 0;
		else if (unlikely(prefix_len >= (int)sizeof(buf)))
			prefix_len = sizeof(buf) - 1;
	}

	avail = sizeof(buf) - prefix_len;
	if (likely(avail > 0)) {
		va_start(args, fmt);
		ret = vsnprintf(buf + prefix_len, avail, fmt, args);
		va_end(args);

		if (unlikely(ret < 0)) {
			/*
			 * Encountered an encoding error during string formatting.
			 * Discard the payload but retain the prefix.
			 */
			payload_len = 0;
		} else if (unlikely(ret >= avail)) {
			/*
			 * The formatted output exceeded the buffer capacity.
			 * The vsnprintf function returns the projected length, not the
			 * actual bytes written. We must strictly clamp the length to
			 * the buffer boundary minus the null terminator to prevent
			 * a stack out-of-bounds read vulnerability.
			 */
			payload_len = avail - 1;
		} else {
			payload_len = ret;
		}
	} else {
		payload_len = 0;
	}

	/*
	 * We conditionally lock here. If the subsystem hasn't been initialized yet
	 * (e.g., extremely early boot errors), we still attempt to print, risking
	 * log tearing rather than losing critical diagnostics.
	 */
	host_mutex_lock(log_lock);

	char output[2 * LOG_LINE_MAX_LENGTH];
	size_t length = 0;
	for (i = 0; i < prefix_len + payload_len; i++) {
		if (buf[i] == '\n')
			output[length++] = '\r';
		output[length++] = buf[i];
	}
	ret = prefix_len + payload_len;
	int error = host_console_write(stream, output, length);
	if (error < 0)
		ret = error;
	host_mutex_unlock(log_lock);
	errno = saved_errno;
	return ret;
}

/**
 * panic - abort the virtualization engine on unrecoverable errors
 * @fmt: the descriptive format string explaining the fatality
 *
 * Attempts to write a final diagnostic to standard error, then aborts the
 * host process. Does not return.
 */
void panic(const char *fmt, ...)
{
	va_list args;
	char buf[LOG_LINE_MAX_LENGTH];
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(LOG_EMERG, "MODVM PANIC: %s\nSystem halted.\n", buf);
	abort();
}
