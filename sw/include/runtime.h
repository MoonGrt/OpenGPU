#ifndef __OPENGPU_RUNTIME_H__
#define __OPENGPU_RUNTIME_H__

#include <stddef.h>
#include <stdint.h>

typedef uint32_t gpu_addr_t;
typedef struct gpu_device *gpu_device_h;
typedef struct gpu_kernel *gpu_kernel_h;

typedef enum {
  GPU_SUCCESS = 0,
  GPU_ERROR_INVALID_ARGUMENT,
  GPU_ERROR_OUT_OF_MEMORY,
  GPU_ERROR_IO,
  GPU_ERROR_BAD_STATE,
  GPU_ERROR_TIMEOUT,
  GPU_ERROR_BACKEND,
} gpu_result_t;

typedef struct {
  uint32_t physical_cores;
  uint32_t warps_per_core;
  uint32_t threads_per_warp;
  uint32_t num_harts;
} gpu_device_config_t;

#ifdef __cplusplus
extern "C" {
#endif

const char *gpu_result_string(gpu_result_t result);
gpu_result_t gpu_device_open(uint32_t index, gpu_device_h *device);
gpu_result_t gpu_device_close(gpu_device_h device);
gpu_result_t gpu_device_get_config(
    gpu_device_h device, gpu_device_config_t *config);

gpu_result_t gpu_mem_alloc(
    gpu_device_h device, size_t size, gpu_addr_t *address);
gpu_result_t gpu_mem_free(
    gpu_device_h device, gpu_addr_t address);
gpu_result_t gpu_mem_write(
    gpu_device_h device, gpu_addr_t address,
    const void *source, size_t size);
gpu_result_t gpu_mem_read(
    gpu_device_h device, void *destination,
    gpu_addr_t address, size_t size);

gpu_result_t gpu_kernel_load_file(
    gpu_device_h device, const char *path, gpu_kernel_h *kernel);
gpu_result_t gpu_kernel_load_memory(
    gpu_device_h device, const void *image, size_t size,
    gpu_kernel_h *kernel);
gpu_result_t gpu_launch(
    gpu_device_h device, gpu_kernel_h kernel,
    const void *arguments, size_t argument_size);
gpu_result_t gpu_wait(gpu_device_h device);

#ifdef __cplusplus
}
#endif

#endif
