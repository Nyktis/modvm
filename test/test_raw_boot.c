/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/util/log.h>
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)
int main(void)
{
	/* mov byte [0x200],0x5a; mov dx,0x500; mov al,1; out dx,al; hlt */
	const unsigned char program[] = { 0xc6, 0x06, 0x00, 0x02, 0x5a, 0xba, 0x00, 0x05, 0xb0, 0x01, 0xee, 0xf4 };
	char path[] = "/tmp/modvm-raw-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	CHECK(write(fd, program, sizeof(program)) == sizeof(program));
	close(fd);
	struct vm_ctx vm;
	struct vm_config cfg = { .accel_name = "kvm", .ram_size = 64 * 1024 * 1024, .nr_vcpus = 1, .loader_name = "raw-x86", .loader_opts = path };
	log_init();
	cfg.board = board_find("pc");
	CHECK(cfg.board);
	CHECK(vm_init(&vm, &cfg) == 0);
	alarm(5);
	CHECK(vm_run(&vm) == 0);
	alarm(0);
	unsigned char *value = mem_map_range(&vm.mem_space, TO_GPA(0x200), 1, false);
	CHECK(value && *value == 0x5a);
	vm_destroy(&vm);
	unlink(path);
	log_destroy();
	puts("raw image executes at GPA zero and shuts down cleanly");
	return 0;
}
