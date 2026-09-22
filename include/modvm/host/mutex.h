/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_MUTEX_H
#define MODVM_HOST_MUTEX_H

#include <modvm/util/compiler.h>

struct __ctx_lock_type(host_mutex) host_mutex;

/* create returns a handle or ERR_PTR(-VM_E*). Destroy only after all users stop. */
struct host_mutex *host_mutex_create(void);

void host_mutex_lock(struct host_mutex *mutex) __acquires(mutex);
void host_mutex_unlock(struct host_mutex *mutex) __releases(mutex);

void host_mutex_destroy(struct host_mutex *mutex);

#endif /* MODVM_HOST_MUTEX_H */
