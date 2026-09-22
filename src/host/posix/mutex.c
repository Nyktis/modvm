/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/host/error.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <modvm/host/mutex.h>
#include <modvm/host/condition.h>
#include <modvm/util/bug.h>
#include <modvm/util/err.h>

struct host_mutex {
	pthread_mutex_t handle;
};

struct host_mutex *host_mutex_create(void)
{
	struct host_mutex *mutex = calloc(1, sizeof(*mutex));
	if (!mutex)
		return ERR_PTR(-VM_ENOMEM);
	int ret = pthread_mutex_init(&mutex->handle, NULL);
	if (ret) {
		free(mutex);
		return ERR_PTR(-host_error_from_errno(ret));
	}
	return mutex;
}

/* pthread primitives form the trusted boundary for capability analysis. */
void host_mutex_lock(struct host_mutex *mutex) __no_context_analysis
{
	if (likely(mutex))
		pthread_mutex_lock(&mutex->handle);
}

void host_mutex_unlock(struct host_mutex *mutex) __no_context_analysis
{
	if (likely(mutex))
		pthread_mutex_unlock(&mutex->handle);
}

void host_mutex_destroy(struct host_mutex *mutex)
{
	if (mutex) {
		pthread_mutex_destroy(&mutex->handle);
		free(mutex);
	}
}

struct host_condition {
	pthread_cond_t handle;
};

struct host_condition *host_condition_create(void)
{
	struct host_condition *c = calloc(1, sizeof(*c));
	if (!c)
		return ERR_PTR(-VM_ENOMEM);
	int ret = pthread_cond_init(&c->handle, NULL);
	if (ret) {
		free(c);
		return ERR_PTR(-host_error_from_errno(ret));
	}
	return c;
}

void host_condition_wait(struct host_condition *c, struct host_mutex *m)
{
	BUG_ON(pthread_cond_wait(&c->handle, &m->handle));
}

void host_condition_signal(struct host_condition *c)
{
	BUG_ON(pthread_cond_signal(&c->handle));
}

void host_condition_destroy(struct host_condition *c)
{
	BUG_ON(pthread_cond_destroy(&c->handle));
	free(c);
}
