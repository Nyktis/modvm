/* SPDX-License-Identifier: GPL-2.0 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <modvm/io/block.h>
#include <modvm/io/char.h>
#include <modvm/io/net.h>
#include <modvm/core/board.h>
#include <modvm/core/device.h>
#include <modvm/core/loader.h>
#include <modvm/core/accel.h>
#include <modvm/core/vcpu.h>

static int load(struct vm_ctx *ctx, const char *opts, void **priv)
{
	(void)ctx;
	(void)opts;
	(void)priv;
	return 0;
}

static int setup_bsp(struct vcpu *cpu, void *priv)
{
	(void)cpu;
	(void)priv;
	return 0;
}

static struct block_backend *create_block(const char *opts)
{
	(void)opts;
	return NULL;
}
static struct char_backend *create_char(const char *opts)
{
	(void)opts;
	return NULL;
}
static struct net_backend *create_net(const char *opts)
{
	(void)opts;
	return NULL;
}

int main(void)
{
	/* Each child has its own registry and must terminate at the invalid registration. */
	for (unsigned kind = 0; kind < 7; kind++) {
		for (unsigned failure = 0; failure < 4; failure++) {
			pid_t child = fork();
			if (child < 0)
				return 1;
			if (!child) {
				struct rlimit limit = { 0, 0 };
				if (setrlimit(RLIMIT_CORE, &limit))
					_exit(2);
				char names[80][32];
				struct block_desc blocks[80];
				struct char_desc chars[80];
				struct net_desc nets[80];
				struct board_desc boards[80];
				struct device_desc devices[80];
				struct loader_desc loaders[80];
				struct accel_desc accels[80];
				const struct accel_desc *kvm = accel_find("kvm");
				if (!kvm)
					_exit(4);
				for (unsigned i = 0; i < 80; i++) {
					snprintf(names[i], sizeof(names[i]), "registration-test-%u", failure == 1 ? 0 : i);
					const char *name = failure == 0 ? NULL : failure == 3 ? "" : names[i];
					blocks[i] = (struct block_desc){ .name = name, .create = create_block };
					chars[i] = (struct char_desc){ .name = name, .create = create_char };
					nets[i] = (struct net_desc){ .name = name, .create = create_net };
					boards[i] = (struct board_desc){ .name = name };
					devices[i] = (struct device_desc){ .name = name };
					loaders[i] = (struct loader_desc){ .name = name, .load = load, .setup_bsp = setup_bsp };
					accels[i] = *kvm;
					accels[i].name = name;
					if (kind == 0)
						block_register(&blocks[i]);
					else if (kind == 1)
						char_register(&chars[i]);
					else if (kind == 2)
						net_register(&nets[i]);
					else if (kind == 3)
						board_register(&boards[i]);
					else if (kind == 4)
						device_register(&devices[i]);
					else if (kind == 5)
						loader_register(&loaders[i]);
					else
						accel_register(&accels[i]);
				}
				_exit(3);
			}
			int status;
			if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT)
				return 1;
		}
	}
	return 0;
}
