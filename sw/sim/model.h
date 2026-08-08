#ifndef OPENGPU_SIM_MODEL_H
#define OPENGPU_SIM_MODEL_H

#include <common.h>

typedef enum {
  GPU_MODEL_SM,
  GPU_MODEL_HM,
} gpu_model_kind_t;

typedef struct gpu_model gpu_model_t;

#ifdef __cplusplus
extern "C" {
#endif

gpu_model_t *gpu_model_create(gpu_model_kind_t kind);
bool gpu_model_init(gpu_model_t *model, uint32_t num_harts, int argc, char **argv);
void gpu_model_destroy(gpu_model_t *model);
void gpu_model_dcr_write(gpu_model_t *model, uint32_t addr, uint32_t data);
bool gpu_model_launch(gpu_model_t *model);
bool gpu_model_wait(gpu_model_t *model, uint64_t max_steps);
bool gpu_model_fault(const gpu_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
