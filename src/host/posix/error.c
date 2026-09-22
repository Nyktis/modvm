/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
#include <modvm/host/error.h>

int host_error_from_errno(int error)
{
	if (!error)
		return 0;
#include "errno_map.inc"
	return VM_EIO;
}
