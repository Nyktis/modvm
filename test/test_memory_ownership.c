/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <modvm/core/vm.h>
#include <modvm/core/board.h>
#include <modvm/core/device.h>
#include <modvm/host/page.h>
#include <modvm/util/err.h>

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			exit(1);                                        \
		}                                                       \
	} while (0)

static struct vm_ctx *active;
static int mode, calloc_at, pages, map_calls;
static bool backend_live, cpu_live, device_stopped;
static unsigned char *mapped;

void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size)
{
	if (calloc_at && !--calloc_at)
		return NULL;
	return __real_calloc(n, size);
}

void *__real_host_page_alloc(size_t size);
void *__wrap_host_page_alloc(size_t size)
{
	if (mode == 5)
		return ERR_PTR(-VM_ENOMEM);
	void *ptr = __real_host_page_alloc(size);
	if (!IS_ERR(ptr))
		pages++;
	return ptr;
}

void __real_host_page_free(void *ptr, size_t size);
void __wrap_host_page_free(void *ptr, size_t size)
{
	if (ptr == mapped) {
		CHECK(!backend_live && !cpu_live && device_stopped);
		mapped = NULL;
	}
	pages--;
	__real_host_page_free(ptr, size);
}

static int fake_accel_init(struct accel *accel)
{
	(void)accel;
	backend_live = true;
	return 0;
}

static int map_ram(struct accel *accel, const struct mem_region *region)
{
	(void)accel;
	CHECK(backend_live && !active->mem_space.total_ram && list_empty(&active->mem_space.regions));
	map_calls++;
	if (mode == 1)
		return -VM_EIO;
	mapped = region->hva;
	CHECK(!mapped[0]);
	mapped[0] = 0xa5;
	return 0;
}

static void fake_accel_destroy(struct accel *accel)
{
	(void)accel;
	CHECK(!cpu_live);
	if (mapped)
		CHECK(device_stopped && mapped[0] == 0xa5);
	backend_live = false;
}

static int cpu_init(struct vcpu *cpu)
{
	(void)cpu;
	cpu_live = true;
	return mode == 3 ? -VM_ENXIO : 0;
}

static void cpu_destroy(struct vcpu *cpu)
{
	(void)cpu;
	CHECK(backend_live && mapped && mapped[0] == 0xa5 && device_stopped);
	cpu_live = false;
}

static int cpu_run(struct vcpu *cpu)
{
	(void)cpu;
	CHECK(vm_add_ram(active, TO_GPA(0), active->mem_space.host_page_size, 0) == -VM_EBUSY);
	vm_request_shutdown(active);
	return 0;
}

static void device_stop(struct device *device)
{
	(void)device;
	CHECK(backend_live);
	if (mapped)
		CHECK(mapped[0] == 0xa5);
	device_stopped = true;
}

static int board_init(struct vm_ctx *vm)
{
	static const struct device_ops ops = { .stop = device_stop };
	struct device *dev = device_alloc(vm, "memory-owner-device");
	CHECK(dev);
	dev->ops = &ops;
	CHECK(!device_realize(dev, NULL));
	size_t page = vm->mem_space.host_page_size;
	struct io_region *mmio = io_map_register_region_locked(IO_MMIO, TO_GPA(page * 2), page, dev);
	CHECK(!IS_ERR(mmio));
	CHECK(vm_add_ram(vm, TO_GPA(page * 2), page, 0) == -VM_EBUSY);
	CHECK(!map_calls && !pages);
	/* Port addresses do not reserve guest physical addresses. */
	CHECK(!IS_ERR(io_map_register_region_locked(IO_PIO, TO_GPA(0), page, dev)));
	if (mode == 4)
		calloc_at = 1;
	int ret = vm_add_ram(vm, TO_GPA(0), page, 0);
	if (ret < 0) {
		CHECK(list_empty(&vm->mem_space.regions) && !vm->mem_space.total_ram && !pages);
		return ret;
	}
	CHECK(vm->mem_space.total_ram == page && map_calls == 1 && pages == 1);
	CHECK(vm_add_ram(vm, TO_GPA(0), page, 0) == -VM_EBUSY);
	CHECK(PTR_ERR(io_map_register_region_locked(IO_MMIO, TO_GPA(0), page, dev)) == -VM_EBUSY);
	CHECK(io_map_relocate_region_locked(mmio, TO_GPA(0)) == -VM_EBUSY);
	CHECK(GPA_VAL(mmio->base) == page * 2 && map_calls == 1);
	return mode == 2 ? -VM_ENOSPC : 0;
}

int main(void)
{
	const struct accel_ops ao = { .init = fake_accel_init, .destroy = fake_accel_destroy, .map_ram = map_ram };
	const struct vcpu_ops vo = { .init = cpu_init, .destroy = cpu_destroy, .run = cpu_run };
	const struct accel_desc accel = { .name = "memory-owner", .accel_ops = &ao, .vcpu_ops = &vo };
	const struct device_desc device = { .name = "memory-owner-device" };
	const struct board_ops bo = { .init = board_init };
	const struct board_desc board = { .name = "memory-owner-board", .ops = &bo };
	const struct vm_config config = { .accel_name = accel.name, .nr_vcpus = 1, .board = &board };
	const int expected[] = { 0, -VM_EIO, -VM_ENOSPC, -VM_ENXIO, -VM_ENOMEM, -VM_ENOMEM };
	accel_register(&accel);
	device_register(&device);
	for (mode = 0; mode < 6; mode++) {
		struct vm_ctx vm;
		active = &vm;
		map_calls = 0;
		device_stopped = false;
		CHECK(vm_init(&vm, &config) == expected[mode]);
		if (!mode) {
			CHECK(vm_add_ram(&vm, TO_GPA(vm.mem_space.host_page_size * 4), vm.mem_space.host_page_size, 0) == -VM_EBUSY);
			CHECK(!vm_run(&vm));
			CHECK(vm_add_ram(&vm, TO_GPA(0), vm.mem_space.host_page_size, 0) == -VM_EBUSY);
		}
		vm_destroy(&vm);
		CHECK(!backend_live && !cpu_live && !mapped && !pages && device_stopped);
		CHECK(list_empty(&vm.mem_space.regions) && !vm.mem_space.total_ram);
	}
	return 0;
}
