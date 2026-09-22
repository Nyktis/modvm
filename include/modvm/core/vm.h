/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VM_H
#define MODVM_CORE_VM_H

#include <modvm/host/mutex.h>
#include <modvm/util/res_pool.h>
#include <modvm/core/accel.h>
#include <modvm/core/memory.h>
#include <modvm/core/io_map.h>
#include <modvm/core/vcpu.h>
#include <modvm/host/thread.h>
#include <modvm/io/ctx.h>
#include <modvm/util/list.h>
#include <modvm/util/types.h>

struct board_desc;
struct vm_ctx;
struct char_backend;
struct block_backend;
struct net_backend;

/**
 * struct vm_config - virtual machine configuration
 * @accel_name: hardware acceleration backend requested
 * @ram_size: total capacity of system memory in bytes
 * @nr_vcpus: number of virtual processors to allocate
 * @loader_name: boot protocol plugin identifier
 * @loader_opts: protocol-specific configuration string
 * @board: selected board description
 * @console: character device backend for the primary system console
 * @drives: array of abstracted host storage backends
 * @nr_drives: number of active storage drives
 * @nets: array of abstracted host network backends
 * @nr_nets: number of active network interfaces
 */
/* init copies strings and backend pointer arrays. Backend objects and the board
 * description remain borrowed and must outlive ctx. Backend instances must
 * satisfy their factory contracts; release their owner pools after destroy. */
struct vm_config {
	const char *accel_name;
	size_t ram_size;
	unsigned int nr_vcpus;
	const char *loader_name;
	const char *loader_opts;
	const struct board_desc *board;
	struct char_backend *console;
	struct block_backend **drives;
	size_t nr_drives;
	struct net_backend **nets;
	size_t nr_nets;
};

enum vm_state { VM_BUILDING, VM_READY, VM_RUNNING, VM_STOPPED };

/**
 * struct vm_ctx - the isolated virtual machine context
 * @thread_lock: VM-owned lock for publishing and withdrawing wakeable threads; not held during join
 * @state: lifecycle phase; init/run/destroy are serialized by the caller
 * @stop_requested: monotonic stop request; reset only by a new initialization
 * @run_error: first negative runtime error, atomically recorded; zero means no error
 * @config: immutable configuration for this session
 * @resources: resource pool for context-level automated teardown
 * @mem_space: VM-owned RAM layout and host backing storage
 * @accel: hardware acceleration engine container
 * @io_map: memory and port I/O routing topologies
 * @io_ctx: device callback execution context
 * @vcpus: array of virtual processor instances
 * @vcpu_threads: array of host OS threads driving the processors
 * @devices: topological registry of all instantiated peripherals
 *
 * Each context owns its runtime state. Shared host backends retain their own
 * binding restrictions; a backend cannot be bound to two VMs at once.
 */
struct vm_ctx {
	struct host_mutex *thread_lock; /* Thread publication and wakeup; never held during join. */
	enum vm_state state;
	atomic_bool stop_requested;
	atomic_int run_error;
	struct vm_config config;
	struct res_pool resources;
	struct mem_space mem_space;
	struct accel accel;
	struct io_map io_map;
	struct io_ctx *io_ctx;
	struct vcpu **vcpus;
	struct host_thread **vcpu_threads;
	struct list_head devices;
};

/* init/run/destroy are serialized by the caller. After any init return, shutdown
 * and destroy are valid. Shutdown may run concurrently with run, but destruction
 * must wait for run to return. A successful initialization supports one run. */
int vm_init(struct vm_ctx *ctx, const struct vm_config *config);
/* Construction-only RAM allocation and backend mapping; rejects RAM/MMIO overlap.
 * Requires exclusive VM construction, after accelerator initialization. Failure
 * publishes no region and releases all storage allocated for that request. */
int vm_add_ram(struct vm_ctx *ctx, gpa_t gpa, size_t size, uint32_t flags);
int vm_run(struct vm_ctx *ctx);
void vm_request_shutdown(struct vm_ctx *ctx);
void vm_report_error(struct vm_ctx *ctx, int error);
void vm_destroy(struct vm_ctx *ctx);

#endif /* MODVM_CORE_VM_H */
