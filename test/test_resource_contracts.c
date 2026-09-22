/* SPDX-License-Identifier: GPL-2.0 */
#include <modvm/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <modvm/util/res_pool.h>
#include <modvm/core/vm.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
static int fail_next;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t s)
{
	if (fail_next) {
		fail_next = 0;
		return NULL;
	}
	return __real_calloc(n, s);
}

struct object {
	int order;
};
static int calls, order[8];
static void object_cleanup(struct object *obj)
{
	order[calls++] = obj->order;
}

static void cleanup_action(void *p)
{
	object_cleanup(p);
}

struct release_probe {
	struct res_pool *pool, *other;
	int calls;
};
static void during_release(void *data)
{
	struct release_probe *probe = data;
	probe->calls++;
	CHECK(!res_zalloc(probe->pool, 16));
	CHECK(res_add_action(probe->pool, during_release, probe) == -VM_ESHUTDOWN);
	CHECK(res_transfer_action(probe->other, probe->pool, during_release, probe) == -VM_ESHUTDOWN);
	CHECK(res_transfer_action(probe->pool, probe->other, during_release, probe) == -VM_ESHUTDOWN);
	res_release_all(probe->pool);
}

int main(void)
{
	struct res_pool a, b;
	struct object first = { 1 }, second = { 2 }, third = { 3 };
	log_init();
	res_pool_init(&a);
	res_pool_init(&b);
	for (size_t size = 1; size <= 256; size++) {
		void *p = res_zalloc(&a, size);
		CHECK(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
	}
	long double *number = res_zalloc(&a, sizeof(*number));
	CHECK(number);
	*number = 1.25L;
	CHECK(*number == 1.25L);
	CHECK(res_add_action(&a, cleanup_action, &first) == 0);
	CHECK(res_add_action_or_reset(&a, cleanup_action, &first) == -VM_EEXIST);
	CHECK(res_add_action(&a, cleanup_action, &second) == 0);
	CHECK(res_transfer_action(&b, &a, cleanup_action, &second) == -VM_ENOENT);
	CHECK(res_cancel_action(&b, cleanup_action, &second) == -VM_ENOENT);
	CHECK(res_transfer_action(&a, &b, cleanup_action, &second) == 0);
	CHECK(res_add_action(&a, cleanup_action, &third) == 0);
	res_cancel_action(&a, cleanup_action, &third);
	res_release_all(&a);
	CHECK(calls == 1 && order[0] == 1);
	res_release_all(&b);
	CHECK(calls == 2 && order[1] == 2);
	fail_next = 1;
	CHECK(res_add_action_or_reset(&a, cleanup_action, &third) == -VM_ENOMEM);
	CHECK(calls == 3 && order[2] == 3);
	res_release_all(&a);
	CHECK(calls == 3);
	CHECK(res_add_action(&a, cleanup_action, &first) == 0);
	CHECK(res_add_action_or_reset(&a, cleanup_action, &first) == -VM_EEXIST);
	CHECK(res_add_action(&a, cleanup_action, &second) == 0);
	res_release_all(&a);
	CHECK(calls == 5 && order[3] == 2 && order[4] == 1);
	struct release_probe probe = { &a, &b, 0 };
	CHECK(res_add_action(&a, during_release, &probe) == 0);
	res_release_all(&a);
	CHECK(probe.calls == 1 && !a.releasing);
	CHECK(res_zalloc(&a, 16));
	res_release_all(&a);
	log_destroy();
	puts("alignment, typed adapters, transfer, cancellation, rollback and LIFO passed");
	return 0;
}
