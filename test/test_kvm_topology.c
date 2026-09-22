/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <modvm/core/board.h>
#include <modvm/core/vm.h>
#include <modvm/util/log.h>
#include "../src/accel/kvm/internal.h"
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
int main(void)
{
	struct vm_ctx vm = { 0 };
	struct vm_config cfg = { .accel_name = "kvm", .ram_size = 64 * 1024 * 1024, .nr_vcpus = 3 };
	struct kvm_cpuid2 *cpuid = calloc(1, sizeof(*cpuid) + 1024 * sizeof(struct kvm_cpuid_entry2));
	CHECK(cpuid);
	log_init();
	cfg.board = board_find("pc");
	CHECK(vm_init(&vm, &cfg) == 0);
	for (unsigned int cpu = 0; cpu < cfg.nr_vcpus; cpu++) {
		struct kvm_vcpu_state *state = vm.vcpus[cpu]->priv;
		unsigned int basic = 0;
		cpuid->nent = 1024;
		CHECK(ioctl(FD_VAL(state->vcpu_fd), KVM_GET_CPUID2, cpuid) == 0);
		for (unsigned int i = 0; i < cpuid->nent; i++) {
			struct kvm_cpuid_entry2 *e = &cpuid->entries[i];
			if (e->function == 1) {
				CHECK(e->ebx >> 24 == cpu);
				CHECK(((e->ebx >> 16) & 255) == 1);
				CHECK(!(e->edx & (1U << 28)));
				basic++;
			}
			if (e->function == 0xb || e->function == 0x1f) {
				CHECK(e->edx == cpu && e->eax == 0);
				CHECK(e->ebx == (e->index < 2 ? 1U : 0U));
			}
			if (e->function == 4 || e->function == 0x8000001d)
				CHECK(!(e->eax >> 14));
		}
		CHECK(basic == 1);
	}
	vm_destroy(&vm);
	log_destroy();
	free(cpuid);
	return 0;
}
