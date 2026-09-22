/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_ERROR_H
#define MODVM_HOST_ERROR_H

#include <modvm/errno.h>

/* Map a positive C runtime/native errno to a positive project error; 0 stays 0.
 * Unclassified errors map to VM_EIO. Native OS APIs need their own mapping. */
int host_error_from_errno(int error);

#endif /* MODVM_HOST_ERROR_H */
