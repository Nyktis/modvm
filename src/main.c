/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/util/log.h>
#include <modvm/util/err.h>
#include <modvm/util/cmdline.h>
#include <modvm/io/char.h>
#include <modvm/io/block.h>
#include <modvm/io/net.h>
#include <modvm/util/types.h>

#undef pr_fmt
#define pr_fmt(fmt) "main: " fmt

#define MAX_DRIVES_SUPPORTED 4
#define MAX_NETS_SUPPORTED 4

/**
 * print_usage - display command line interface documentation
 * @prog_name: the executable name invoked by the user
 */
static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [options]\n\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -board <name>        select emulated motherboard (default: pc)\n");
	fprintf(stderr, "  -m <megabytes>       set guest ram size in mb (default: 16)\n");
	fprintf(stderr, "  -smp <cpus>          set number of virtual cpus (default: 1)\n");
	fprintf(stderr, "  -accel <name>        select hypervisor backend (default: kvm)\n");
	fprintf(stderr, "  -loader <name>       select boot protocol plugin (default: raw-x86)\n");
	fprintf(stderr, "  -loader-opts <opts>  pass configuration string to the loader plugin\n");
	fprintf(stderr, "  -drive <opts>        attach a host storage backend (e.g., driver=linux-file,path=img.raw)\n");
	fprintf(stderr, "  -net <opts>          attach a host network backend (e.g., driver=linux-tap,ifname=tap0)\n");
	fprintf(stderr, "  -h                   show this help message\n");
}

static int positive_number(const char *s, size_t limit, size_t *out)
{
	char *end;
	if (!s || *s < '0' || *s > '9')
		return -1;
	errno = 0;
	unsigned long long n = strtoull(s, &end, 10);
	if (errno || *end || !n || n > limit)
		return -1;
	*out = n;
	return 0;
}

/**
 * main - primary entry point for the virtualization engine
 * @argc: argument count
 * @argv: argument vector
 *
 * Parses user configurations, provisions the requested hardware topology
 * and bootloaders, and transitions execution to the hypervisor core.
 *
 * Return: EXIT_SUCCESS on successful shutdown, EXIT_FAILURE on fatal errors.
 */
int main(int argc, char **argv)
{
	struct vm_ctx vm;
	struct res_pool backends;
	struct block_backend *drives[MAX_DRIVES_SUPPORTED] = { 0 };
	struct net_backend *nets[MAX_NETS_SUPPORTED] = { 0 };
	size_t nr_drives = 0;
	size_t nr_nets = 0;
	struct vm_config cfg = {
		.accel_name = "kvm",

		.ram_size = 16 * 1024 * 1024,
		.nr_vcpus = 1,
		.loader_name = "raw-x86",
		.loader_opts = NULL,
		.board = NULL,
		.console = NULL,
		.drives = drives,
		.nr_drives = 0,
		.nets = nets,
		.nr_nets = 0,
	};
	const char *board_name = "pc";
	char *drv_name;
	int i;
	size_t number;
	int ret;

	if (argc == 1) {
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}

	res_pool_init(&backends);
	log_init();
	pr_info("starting modvm hypervisor engine\n");

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			ret = EXIT_SUCCESS;
			goto out_cleanup_backends;
		} else if (strcmp(argv[i], "-board") == 0 && i + 1 < argc) {
			board_name = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			if (positive_number(argv[++i], SIZE_MAX / (1024 * 1024), &number)) {
				pr_err("invalid positive numeric argument: %s\n", argv[i]);
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			cfg.ram_size = number * 1024 * 1024;
		} else if (strcmp(argv[i], "-smp") == 0 && i + 1 < argc) {
			if (positive_number(argv[++i], UINT_MAX, &number)) {
				pr_err("invalid positive numeric argument: %s\n", argv[i]);
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			cfg.nr_vcpus = number;
		} else if (strcmp(argv[i], "-accel") == 0 && i + 1 < argc) {
			cfg.accel_name = argv[++i];
		} else if (strcmp(argv[i], "-loader") == 0 && i + 1 < argc) {
			cfg.loader_name = argv[++i];
		} else if (strcmp(argv[i], "-loader-opts") == 0 && i + 1 < argc) {
			cfg.loader_opts = argv[++i];
		} else if (strcmp(argv[i], "-drive") == 0 && i + 1 < argc) {
			if (nr_drives >= MAX_DRIVES_SUPPORTED) {
				pr_err("maximum number of drives exceeded\n");
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			drv_name = cmdline_extract_opt(argv[++i], "driver");
			if (IS_ERR(drv_name)) {
				pr_err("cannot parse backend driver: %ld\n", (long)PTR_ERR(drv_name));
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			if (!drv_name) {
				pr_err("drive argument requires 'driver=' property\n");
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			drives[nr_drives] = block_create(&backends, drv_name, argv[i]);
			free(drv_name);

			if (IS_ERR(drives[nr_drives])) {
				pr_err("backend creation failed: %ld\n", (long)PTR_ERR(drives[nr_drives]));
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			nr_drives++;
			cfg.nr_drives = nr_drives;
		} else if (strcmp(argv[i], "-net") == 0 && i + 1 < argc) {
			if (nr_nets >= MAX_NETS_SUPPORTED) {
				pr_err("maximum number of network interfaces exceeded\n");
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			drv_name = cmdline_extract_opt(argv[++i], "driver");
			if (IS_ERR(drv_name)) {
				pr_err("cannot parse backend driver: %ld\n", (long)PTR_ERR(drv_name));
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			if (!drv_name) {
				pr_err("net argument requires 'driver=' property\n");
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			nets[nr_nets] = net_create(&backends, drv_name, argv[i]);
			free(drv_name);

			if (IS_ERR(nets[nr_nets])) {
				pr_err("backend creation failed: %ld\n", (long)PTR_ERR(nets[nr_nets]));
				ret = EXIT_FAILURE;
				goto out_cleanup_backends;
			}
			nr_nets++;
			cfg.nr_nets = nr_nets;
		} else {
			pr_err("unknown or incomplete option: %s\n", argv[i]);
			print_usage(argv[0]);
			ret = EXIT_FAILURE;
			goto out_cleanup_backends;
		}
	}

	if (!cfg.loader_opts)
		pr_warn("no loader options specified, processor may lack a boot payload\n");

	cfg.board = board_find(board_name);
	if (!cfg.board) {
		pr_err("unsupported motherboard type '%s'\n", board_name);
		ret = EXIT_FAILURE;
		goto out_cleanup_backends;
	}

	/* Instantiate default console backend */
	cfg.console = char_create(&backends, "posix-stdio", NULL);
	if (IS_ERR(cfg.console)) {
		pr_err("backend creation failed: %ld\n", (long)PTR_ERR(cfg.console));
		pr_err("failed to create standard io console backend\n");
		ret = EXIT_FAILURE;
		goto out_cleanup_backends;
	}

	ret = vm_init(&vm, &cfg);
	if (ret < 0) {
		pr_err("failed to initialize virtual machine context\n");
		goto err_destroy_vm;
	}

	ret = vm_run(&vm);
	if (ret < 0) {
		pr_err("hypervisor runtime exited with fatal error\n");
		goto err_destroy_vm;
	}

	vm_destroy(&vm);
	ret = EXIT_SUCCESS;
	goto out_cleanup_backends;

err_destroy_vm:
	vm_destroy(&vm);
	ret = EXIT_FAILURE;

out_cleanup_backends:
	res_release_all(&backends);

	pr_info("hypervisor engine shutdown completed\n");
	log_destroy();
	return ret;
}
