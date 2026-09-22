/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <modvm/host/error.h>
#include <modvm/host/mutex.h>
#include <modvm/host/thread.h>
#include <modvm/host/posix/service.h>
#include <modvm/util/err.h>
#include <modvm/util/bug.h>

struct service_source {
	struct host_posix_source spec;
	uint64_t revision;
};

struct host_posix_service {
	struct host_mutex *lock;
	struct host_thread *thread;
	int wakeup[2];
	bool stopped;
	size_t count;
	void (*fatal)(void *, int);
	void *data;
	struct service_source sources[];
};

static void service_wake(struct host_posix_service *s)
{
	ssize_t n;
	do {
		n = write(s->wakeup[1], "x", 1);
	} while (n < 0 && errno == EINTR);
}

static void *service_run(void *data)
{
	struct host_posix_service *s = data;
	struct pollfd fds[65];
	uint64_t versions[64];
	for (;;) {
		host_mutex_lock(s->lock);
		if (s->stopped) {
			host_mutex_unlock(s->lock);
			break;
		}
		for (size_t i = 0; i < s->count; i++) {
			struct host_posix_source *p = &s->sources[i].spec;
			fds[i] = (struct pollfd){ .fd = p->events ? p->fd : -1,
						  .events = ((p->events & HOST_POSIX_READ) ? POLLIN : 0) | ((p->events & HOST_POSIX_WRITE) ? POLLOUT : 0) };
			versions[i] = s->sources[i].revision;
		}
		fds[s->count] = (struct pollfd){ .fd = s->wakeup[0], .events = POLLIN };
		host_mutex_unlock(s->lock);
		int ret = poll(fds, s->count + 1, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			int error = -host_error_from_errno(errno);
			host_mutex_lock(s->lock);
			if (!s->stopped)
				s->fatal(s->data, error);
			host_mutex_unlock(s->lock);
			break;
		}
		if (fds[s->count].revents) {
			char buf[128];
			ssize_t n;
			do {
				n = read(s->wakeup[0], buf, sizeof(buf));
			} while (n > 0 || (n < 0 && errno == EINTR));
		}
		for (size_t i = 0; i < s->count; i++) {
			if (!fds[i].revents)
				continue;
			host_mutex_lock(s->lock);
			if (!s->stopped && versions[i] == s->sources[i].revision) {
				uint32_t events = ((fds[i].revents & (POLLIN | POLLHUP)) ? HOST_POSIX_READ : 0) | ((fds[i].revents & POLLOUT) ? HOST_POSIX_WRITE : 0) |
						  ((fds[i].revents & (POLLERR | POLLNVAL)) ? HOST_POSIX_ERROR : 0);
				s->sources[i].spec.ready(s, events, s->sources[i].spec.data);
			}
			host_mutex_unlock(s->lock);
		}
	}
	return NULL;
}

struct host_posix_service *host_posix_service_create(const struct host_posix_source *sources, size_t count, void (*fatal)(void *, int), void *data)
{
	if (!sources || !count || count > 64 || !fatal)
		return ERR_PTR(-VM_EINVAL);
	for (size_t i = 0; i < count; i++) {
		if (sources[i].fd < 0 || !sources[i].ready || (sources[i].events & ~(HOST_POSIX_READ | HOST_POSIX_WRITE)))
			return ERR_PTR(-VM_EINVAL);
		for (size_t j = 0; j < i; j++)
			if (sources[i].fd == sources[j].fd)
				return ERR_PTR(-VM_EEXIST);
	}
	struct host_posix_service *s = calloc(1, sizeof(*s) + count * sizeof(s->sources[0]));
	if (!s)
		return ERR_PTR(-VM_ENOMEM);
	s->lock = host_mutex_create();
	if (IS_ERR(s->lock)) {
		int error = PTR_ERR(s->lock);
		free(s);
		return ERR_PTR(error);
	}
	if (pipe2(s->wakeup, O_CLOEXEC | O_NONBLOCK) < 0) {
		int error = -host_error_from_errno(errno);
		host_mutex_destroy(s->lock);
		free(s);
		return ERR_PTR(error);
	}
	s->count = count;
	s->fatal = fatal;
	s->data = data;
	for (size_t i = 0; i < count; i++)
		s->sources[i].spec = sources[i];
	s->thread = host_thread_create(service_run, s);
	if (IS_ERR(s->thread)) {
		int error = PTR_ERR(s->thread);
		close(s->wakeup[0]);
		close(s->wakeup[1]);
		host_mutex_destroy(s->lock);
		free(s);
		return ERR_PTR(error);
	}
	return s;
}

void host_posix_service_lock(struct host_posix_service *s) __no_context_analysis
{
	host_mutex_lock(s->lock);
}

void host_posix_service_unlock(struct host_posix_service *s) __no_context_analysis
{
	host_mutex_unlock(s->lock);
}

void host_posix_service_update_locked(struct host_posix_service *s, size_t source, uint32_t events)
{
	BUG_ON(source >= s->count || (events & ~(HOST_POSIX_READ | HOST_POSIX_WRITE)));
	s->sources[source].spec.events = events;
	s->sources[source].revision++;
	service_wake(s);
}

void host_posix_service_destroy(struct host_posix_service *s)
{
	if (!s)
		return;
	host_mutex_lock(s->lock);
	s->stopped = true;
	service_wake(s);
	host_mutex_unlock(s->lock);
	BUG_ON(host_thread_join(s->thread) < 0);
	host_thread_destroy(s->thread);
	close(s->wakeup[0]);
	close(s->wakeup[1]);
	host_mutex_destroy(s->lock);
	free(s);
}
