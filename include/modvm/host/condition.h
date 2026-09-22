/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HOST_CONDITION_H
#define MODVM_HOST_CONDITION_H

struct host_mutex;
struct host_condition;

struct host_condition *host_condition_create(void);

/* Atomically releases lock while waiting, and reacquires it before returning. */
void host_condition_wait(struct host_condition *condition, struct host_mutex *lock);
void host_condition_signal(struct host_condition *condition);

void host_condition_destroy(struct host_condition *condition);

#endif /* MODVM_HOST_CONDITION_H */
