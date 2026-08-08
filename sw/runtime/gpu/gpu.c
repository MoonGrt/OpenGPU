#include <common.h>
#include <device/memory.h>
#include <gpu.h>
#include <model.h>
#include <runtime.h>

#define GPU_NUM_HARTS (CONFIG_GPU_NUM_CORES * CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS)
#define GPU_WAIT_LIMIT 100000000ULL
#define GPU_ALIGN 64u

#if (CONFIG_GPU_NUM_WARPS & (CONFIG_GPU_NUM_WARPS - 1)) != 0
#error "CONFIG_GPU_NUM_WARPS must be a power of two"
#endif
#if (CONFIG_GPU_NUM_THREADS & (CONFIG_GPU_NUM_THREADS - 1)) != 0
#error "CONFIG_GPU_NUM_THREADS must be a power of two"
#endif

typedef struct {
  gpu_addr_t address;
  uint32_t size;
  bool used;
} allocation_t;

struct gpu_kernel {
  struct gpu_device *device;
  uint32_t entry;
  uint32_t generation;
  struct gpu_kernel *next;
};

struct gpu_device {
  bool opened;
  bool running;
  uint32_t kernel_generation;
  allocation_t allocations[GPU_MAX_ALLOCS];
  struct gpu_kernel *kernels;
  gpu_addr_t launch_args;
  bool launch_args_used;
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  uint8_t *reference;
  bool reference_fault;
#endif
};

static struct gpu_device singleton;
static gpu_model_t *execution_model;
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
static gpu_model_t *reference_model;
#endif
static bool runtime_initialized;
static bool trace_requested;
static bool trace_suppressed;
static uint64_t trace_limit = 10000;
static uint64_t trace_events;

static uint32_t align_up(uint32_t value) {
  return (value + GPU_ALIGN - 1u) & ~(GPU_ALIGN - 1u);
}

static bool range_inside(uint32_t address, size_t size, uint32_t begin, uint32_t end) {
  return size != 0 && address >= begin && address < end && size <= UINT32_MAX &&
         (uint32_t)size <= end - address;
}

static bool mul_u32(uint32_t a, uint32_t b, uint32_t *out) {
  uint64_t product = (uint64_t)a * b;
  if (product > UINT32_MAX)
    return false;
  *out = (uint32_t)product;
  return true;
}

static void release_launch_args(gpu_device_h device) {
  if (device->launch_args_used) {
    (void)gpu_mem_free(device, device->launch_args);
    device->launch_args = 0;
    device->launch_args_used = false;
  }
}

static bool valid_device(gpu_device_h device) {
  return runtime_initialized && device == &singleton && device->opened;
}

static bool allocation_contains(gpu_device_h device, uint32_t address, size_t size) {
  if (!range_inside(address, size, GPU_HEAP_BASE, CONFIG_MBASE + CONFIG_MSIZE))
    return false;
  for (uint32_t i = 0; i < GPU_MAX_ALLOCS; ++i) {
    allocation_t *allocation = &device->allocations[i];
    if (allocation->used && address >= allocation->address && size <= allocation->size &&
        address - allocation->address <= allocation->size - size)
      return true;
  }
  return false;
}

static void raw_write(uint32_t address, const void *source, size_t size) {
  memcpy(guest_to_host(address), source, size);
}

static void configure_model(gpu_model_t *model,
                            uint32_t entry,
                            uint32_t args_addr,
                            uint32_t args_size,
                            const uint32_t grid_dim[3],
                            const uint32_t block_dim[3],
                            uint32_t block_size) {
  gpu_model_dcr_write(model, GPU_DCR_STARTUP_PC, entry);
  gpu_model_dcr_write(model, GPU_DCR_ARGS_ADDR, args_addr);
  gpu_model_dcr_write(model, GPU_DCR_ARGS_SIZE, args_size);
  gpu_model_dcr_write(model, GPU_DCR_BLOCK_DIM_X, block_dim[0]);
  gpu_model_dcr_write(model, GPU_DCR_BLOCK_DIM_Y, block_dim[1]);
  gpu_model_dcr_write(model, GPU_DCR_BLOCK_DIM_Z, block_dim[2]);
  gpu_model_dcr_write(model, GPU_DCR_GRID_DIM_X, grid_dim[0]);
  gpu_model_dcr_write(model, GPU_DCR_GRID_DIM_Y, grid_dim[1]);
  gpu_model_dcr_write(model, GPU_DCR_GRID_DIM_Z, grid_dim[2]);
  gpu_model_dcr_write(model, GPU_DCR_BLOCK_SIZE, block_size);
}

const char *gpu_result_string(gpu_result_t result) {
  switch (result) {
  case GPU_SUCCESS:
    return "success";
  case GPU_ERROR_INVALID_ARGUMENT:
    return "invalid argument";
  case GPU_ERROR_OUT_OF_MEMORY:
    return "out of memory";
  case GPU_ERROR_IO:
    return "I/O error";
  case GPU_ERROR_BAD_STATE:
    return "bad state";
  case GPU_ERROR_TIMEOUT:
    return "timeout";
  case GPU_ERROR_BACKEND:
    return "backend error";
  default:
    return "unknown error";
  }
}

int memu_runtime_init(int argc, char **argv, int *app_argc, char ***app_argv) {
  if (runtime_initialized || !app_argc || !app_argv)
    return -1;
  int output = 1;
  for (int input = 1; input < argc; ++input) {
    if (strcmp(argv[input], "--gpu-trace") == 0) {
      trace_requested = true;
      continue;
    }
    const char *value = NULL;
    if (strcmp(argv[input], "--gpu-trace-limit") == 0) {
      if (++input >= argc) {
        fprintf(stderr, "--gpu-trace-limit requires a positive number\n");
        return -1;
      }
      value = argv[input];
    } else if (strncmp(argv[input], "--gpu-trace-limit=", 18) == 0) {
      value = argv[input] + 18;
    }
    if (value) {
      char *end = NULL;
      unsigned long long parsed = strtoull(value, &end, 0);
      if (!end || *end != '\0' || parsed == 0) {
        fprintf(stderr, "--gpu-trace-limit requires a positive number\n");
        return -1;
      }
      trace_limit = parsed;
      continue;
    }
    argv[output++] = argv[input];
  }
  argv[output] = NULL;
  *app_argc = output;
  *app_argv = argv;

  init_mem();
#if defined(CONFIG_HM)
  execution_model = gpu_model_create(GPU_MODEL_HM);
#else
  execution_model = gpu_model_create(GPU_MODEL_SM);
#endif
  if (!execution_model || !gpu_model_init(execution_model, GPU_NUM_HARTS, output, argv)) {
    gpu_model_destroy(execution_model);
    execution_model = NULL;
    free_mem();
    return -1;
  }
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  reference_model = gpu_model_create(GPU_MODEL_SM);
  if (!reference_model || !gpu_model_init(reference_model, GPU_NUM_HARTS, output, argv)) {
    gpu_model_destroy(reference_model);
    reference_model = NULL;
    gpu_model_destroy(execution_model);
    execution_model = NULL;
    free_mem();
    return -1;
  }
#endif
  memset(&singleton, 0, sizeof(singleton));
  runtime_initialized = true;
  return 0;
}

void memu_runtime_fini(void) {
  if (!runtime_initialized)
    return;
  if (singleton.opened)
    (void)gpu_device_close(&singleton);
  gpu_model_destroy(execution_model);
  execution_model = NULL;
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  gpu_model_destroy(reference_model);
  reference_model = NULL;
#endif
  free_mem();
  runtime_initialized = false;
}

gpu_result_t gpu_device_open(uint32_t index, gpu_device_h *device) {
  if (!runtime_initialized || !device || index != 0)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (singleton.opened)
    return GPU_ERROR_BAD_STATE;
  memset(singleton.allocations, 0, sizeof(singleton.allocations));
  singleton.opened = true;
  singleton.running = false;
  singleton.kernel_generation = 0;
  singleton.kernels = NULL;
  singleton.launch_args = 0;
  singleton.launch_args_used = false;
  *device = &singleton;
  return GPU_SUCCESS;
}

gpu_result_t gpu_device_close(gpu_device_h device) {
  if (!valid_device(device))
    return GPU_ERROR_INVALID_ARGUMENT;
  if (device->running)
    return GPU_ERROR_BAD_STATE;
  struct gpu_kernel *kernel = device->kernels;
  while (kernel) {
    struct gpu_kernel *next = kernel->next;
    free(kernel);
    kernel = next;
  }
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  free(device->reference);
  device->reference = NULL;
#endif
  memset(device->allocations, 0, sizeof(device->allocations));
  device->kernels = NULL;
  device->opened = false;
  return GPU_SUCCESS;
}

gpu_result_t gpu_device_get_config(gpu_device_h device, gpu_device_config_t *config) {
  if (!valid_device(device) || !config)
    return GPU_ERROR_INVALID_ARGUMENT;
  config->physical_cores = CONFIG_GPU_NUM_CORES;
  config->warps_per_core = CONFIG_GPU_NUM_WARPS;
  config->threads_per_warp = CONFIG_GPU_NUM_THREADS;
  config->num_harts = GPU_NUM_HARTS;
  return GPU_SUCCESS;
}

gpu_result_t gpu_mem_alloc(gpu_device_h device, size_t size, gpu_addr_t *address) {
  if (!valid_device(device) || !address || size == 0 || size > UINT32_MAX)
    return GPU_ERROR_INVALID_ARGUMENT;
  uint32_t aligned_size = align_up((uint32_t)size);
  if (aligned_size < size)
    return GPU_ERROR_OUT_OF_MEMORY;

  uint32_t slot = GPU_MAX_ALLOCS;
  for (uint32_t i = 0; i < GPU_MAX_ALLOCS; ++i)
    if (!device->allocations[i].used) {
      slot = i;
      break;
    }
  if (slot == GPU_MAX_ALLOCS)
    return GPU_ERROR_OUT_OF_MEMORY;

  uint32_t candidate = GPU_HEAP_BASE;
  const uint32_t heap_end = CONFIG_MBASE + CONFIG_MSIZE;
  while (candidate < heap_end) {
    bool moved = false;
    for (uint32_t i = 0; i < GPU_MAX_ALLOCS; ++i) {
      allocation_t *current = &device->allocations[i];
      if (!current->used)
        continue;
      if (candidate < current->address + current->size &&
          candidate + aligned_size > current->address) {
        candidate = align_up(current->address + current->size);
        moved = true;
        break;
      }
    }
    if (!moved)
      break;
  }
  if (candidate >= heap_end || aligned_size > heap_end - candidate)
    return GPU_ERROR_OUT_OF_MEMORY;

  device->allocations[slot] =
      (allocation_t){.address = candidate, .size = aligned_size, .used = true};
  memset(guest_to_host(candidate), 0, aligned_size);
  *address = candidate;
  return GPU_SUCCESS;
}

gpu_result_t gpu_mem_free(gpu_device_h device, gpu_addr_t address) {
  if (!valid_device(device))
    return GPU_ERROR_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < GPU_MAX_ALLOCS; ++i) {
    allocation_t *allocation = &device->allocations[i];
    if (allocation->used && allocation->address == address) {
      allocation->used = false;
      return GPU_SUCCESS;
    }
  }
  return GPU_ERROR_INVALID_ARGUMENT;
}

gpu_result_t
gpu_mem_write(gpu_device_h device, gpu_addr_t address, const void *source, size_t size) {
  if (!valid_device(device) || !source || !allocation_contains(device, address, size))
    return GPU_ERROR_INVALID_ARGUMENT;
  raw_write(address, source, size);
  return GPU_SUCCESS;
}

gpu_result_t gpu_mem_read(gpu_device_h device, void *destination, gpu_addr_t address, size_t size) {
  if (!valid_device(device) || !destination || !allocation_contains(device, address, size))
    return GPU_ERROR_INVALID_ARGUMENT;
  memcpy(destination, guest_to_host(address), size);
  return GPU_SUCCESS;
}

gpu_result_t
gpu_kernel_load_memory(gpu_device_h device, const void *image, size_t size, gpu_kernel_h *kernel) {
  if (!valid_device(device) || !image || !kernel || size == 0 ||
      size > GPU_KERNEL_END - GPU_KERNEL_BASE)
    return GPU_ERROR_INVALID_ARGUMENT;
  struct gpu_kernel *loaded = calloc(1, sizeof(*loaded));
  if (!loaded)
    return GPU_ERROR_OUT_OF_MEMORY;
  memset(guest_to_host(GPU_KERNEL_BASE), 0, GPU_KERNEL_END - GPU_KERNEL_BASE);
  raw_write(GPU_KERNEL_BASE, image, size);
  loaded->device = device;
  loaded->entry = GPU_KERNEL_BASE;
  loaded->generation = ++device->kernel_generation;
  loaded->next = device->kernels;
  device->kernels = loaded;
  *kernel = loaded;
  return GPU_SUCCESS;
}

gpu_result_t gpu_kernel_load_file(gpu_device_h device, const char *path, gpu_kernel_h *kernel) {
  if (!valid_device(device) || !path || !kernel)
    return GPU_ERROR_INVALID_ARGUMENT;
  FILE *file = fopen(path, "rb");
  if (!file)
    return GPU_ERROR_IO;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return GPU_ERROR_IO;
  }
  long length = ftell(file);
  if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return GPU_ERROR_IO;
  }
  uint8_t *image = malloc((size_t)length);
  if (!image) {
    fclose(file);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  bool read_ok = fread(image, (size_t)length, 1, file) == 1;
  fclose(file);
  if (!read_ok) {
    free(image);
    return GPU_ERROR_IO;
  }
  gpu_result_t result = gpu_kernel_load_memory(device, image, (size_t)length, kernel);
  free(image);
  if (result == GPU_SUCCESS)
    Log("GPU kernel: %s (%ld bytes)", path, length);
  return result;
}

gpu_result_t gpu_launch(gpu_device_h device, gpu_kernel_h kernel, const gpu_launch_info_t *info) {
  if (!valid_device(device) || !kernel || kernel->device != device ||
      kernel->generation != device->kernel_generation || device->running || !info ||
      (info->args_size != 0 && !info->args_host) || info->args_size > UINT32_MAX)
    return GPU_ERROR_INVALID_ARGUMENT;
  uint32_t block_xy, block_size, grid_xy, grid_size;
  for (unsigned i = 0; i < 3; ++i)
    if (info->grid_dim[i] == 0 || info->block_dim[i] == 0)
      return GPU_ERROR_INVALID_ARGUMENT;
  if (!mul_u32(info->block_dim[0], info->block_dim[1], &block_xy) ||
      !mul_u32(block_xy, info->block_dim[2], &block_size) ||
      block_size > CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS ||
      !mul_u32(info->grid_dim[0], info->grid_dim[1], &grid_xy) ||
      !mul_u32(grid_xy, info->grid_dim[2], &grid_size) ||
      !mul_u32(grid_size, block_size, &grid_size))
    return GPU_ERROR_INVALID_ARGUMENT;

  device->launch_args = 0;
  device->launch_args_used = false;
  if (info->args_size) {
    gpu_result_t result = gpu_mem_alloc(device, info->args_size, &device->launch_args);
    if (result != GPU_SUCCESS)
      return result;
    device->launch_args_used = true;
    raw_write(device->launch_args, info->args_host, info->args_size);
  }
  configure_model(execution_model,
                  kernel->entry,
                  device->launch_args,
                  (uint32_t)info->args_size,
                  info->grid_dim,
                  info->block_dim,
                  block_size);
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  configure_model(reference_model,
                  kernel->entry,
                  device->launch_args,
                  (uint32_t)info->args_size,
                  info->grid_dim,
                  info->block_dim,
                  block_size);
#endif
  trace_events = 0;

#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  uint8_t *initial = malloc(CONFIG_MSIZE);
  device->reference = malloc(CONFIG_MSIZE);
  if (!initial || !device->reference) {
    free(initial);
    free(device->reference);
    device->reference = NULL;
    release_launch_args(device);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  memcpy(initial, guest_to_host(CONFIG_MBASE), CONFIG_MSIZE);
  gpu_trace_suppress(true);
  bool reference_started = gpu_model_launch(reference_model);
  bool reference_done = reference_started && gpu_model_wait(reference_model, GPU_WAIT_LIMIT);
  device->reference_fault = gpu_model_fault(reference_model);
  gpu_trace_suppress(false);
  if (!reference_done) {
    free(initial);
    free(device->reference);
    device->reference = NULL;
    release_launch_args(device);
    return GPU_ERROR_TIMEOUT;
  }
  memcpy(device->reference, guest_to_host(CONFIG_MBASE), CONFIG_MSIZE);
  memcpy(guest_to_host(CONFIG_MBASE), initial, CONFIG_MSIZE);
  free(initial);
  if (!gpu_model_launch(execution_model)) {
    free(device->reference);
    device->reference = NULL;
    release_launch_args(device);
    return GPU_ERROR_BACKEND;
  }
#elif defined(CONFIG_HM)
  if (!gpu_model_launch(execution_model)) {
    release_launch_args(device);
    return GPU_ERROR_BACKEND;
  }
#else
  if (!gpu_model_launch(execution_model)) {
    release_launch_args(device);
    return GPU_ERROR_BACKEND;
  }
#endif
  device->running = true;
  return GPU_SUCCESS;
}

gpu_result_t gpu_wait(gpu_device_h device) {
  if (!valid_device(device) || !device->running)
    return GPU_ERROR_BAD_STATE;
  bool completed = gpu_model_wait(execution_model, GPU_WAIT_LIMIT);
  device->running = false;
  release_launch_args(device);
  if (!completed) {
#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
    free(device->reference);
    device->reference = NULL;
#endif
    return gpu_model_fault(execution_model) ? GPU_ERROR_BACKEND : GPU_ERROR_TIMEOUT;
  }
  if (gpu_model_fault(execution_model))
    return GPU_ERROR_BACKEND;

#if defined(CONFIG_HM) && defined(CONFIG_DIFFTEST)
  if (device->reference_fault) {
    free(device->reference);
    device->reference = NULL;
    return GPU_ERROR_BACKEND;
  }
  uint8_t *actual = guest_to_host(CONFIG_MBASE);
  size_t mismatch = 0;
  while (mismatch < CONFIG_MSIZE && device->reference[mismatch] == actual[mismatch])
    ++mismatch;
  if (mismatch != CONFIG_MSIZE) {
    fprintf(stderr,
            "[DIFF] PMEM mismatch at 0x%08x: ref=%02x rtl=%02x\n",
            (uint32_t)(CONFIG_MBASE + mismatch),
            device->reference[mismatch],
            actual[mismatch]);
    free(device->reference);
    device->reference = NULL;
    return GPU_ERROR_BACKEND;
  }
  free(device->reference);
  device->reference = NULL;
  printf("[DIFF] C ISS and Core RTL PMEM match\n");
#endif
  return GPU_SUCCESS;
}

bool gpu_trace_enabled(void) {
  return trace_requested && !trace_suppressed && trace_events < trace_limit;
}

void gpu_trace_suppress(bool suppress) {
  trace_suppressed = suppress;
}

void gpu_trace_commit(int hartid, int pc, int inst) {
  if (!gpu_trace_enabled())
    return;
  printf("[GI] hart=%u pc=%08x inst=%08x\n", (uint32_t)hartid, (uint32_t)pc, (uint32_t)inst);
  ++trace_events;
}

void gpu_trace_store(int hartid, int addr, int mask, int data) {
  if (!gpu_trace_enabled())
    return;
  printf("[GM] hart=%u W addr=%08x mask=%x data=%08x\n",
         (uint32_t)hartid,
         (uint32_t)addr,
         (uint32_t)mask,
         (uint32_t)data);
  ++trace_events;
}
