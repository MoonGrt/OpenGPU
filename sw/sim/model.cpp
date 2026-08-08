#include "model.h"
#include "sm/backend.h"

#if defined(CONFIG_HM)
#include "hm/backend.h"
#endif

struct gpu_model {
  gpu_model_kind_t kind;
  uint32_t num_harts;
};

extern "C" gpu_model_t *gpu_model_create(gpu_model_kind_t kind) {
  if (kind != GPU_MODEL_SM && kind != GPU_MODEL_HM)
    return nullptr;
#if !defined(CONFIG_HM)
  if (kind == GPU_MODEL_HM)
    return nullptr;
#endif
  gpu_model_t *model = new gpu_model_t;
  model->kind = kind;
  model->num_harts = 0;
  return model;
}

extern "C" bool gpu_model_init(gpu_model_t *model, uint32_t num_harts, int argc, char **argv) {
  if (!model || num_harts == 0)
    return false;
  model->num_harts = num_harts;
  if (model->kind == GPU_MODEL_SM) {
    gpu_iss_init(num_harts);
    return true;
  }
#if defined(CONFIG_HM)
  rtl_init(argc, argv);
  gpu_rtl_init();
  return true;
#else
  return false;
#endif
}

extern "C" void gpu_model_destroy(gpu_model_t *model) {
  if (!model)
    return;
#if defined(CONFIG_HM)
  if (model->kind == GPU_MODEL_HM)
    rtl_exit();
#endif
  delete model;
}

extern "C" void gpu_model_dcr_write(gpu_model_t *model, uint32_t addr, uint32_t data) {
  if (!model)
    return;
  if (model->kind == GPU_MODEL_SM) {
    gpu_iss_dcr_write(addr, data);
    return;
  }
#if defined(CONFIG_HM)
  gpu_rtl_dcr_write(addr, data);
#endif
}

extern "C" bool gpu_model_launch(gpu_model_t *model) {
  if (!model)
    return false;
  if (model->kind == GPU_MODEL_SM) {
    gpu_iss_init(model->num_harts);
    return gpu_iss_launch();
  }
#if defined(CONFIG_HM)
  return gpu_rtl_launch(0);
#else
  return false;
#endif
}

extern "C" bool gpu_model_wait(gpu_model_t *model, uint64_t max_steps) {
  if (!model)
    return false;
  if (model->kind == GPU_MODEL_SM) {
    for (uint64_t step = 0; step < max_steps && !gpu_iss_done(); ++step)
      gpu_iss_step();
    return gpu_iss_done();
  }
#if defined(CONFIG_HM)
  return gpu_rtl_wait(max_steps);
#else
  return false;
#endif
}

extern "C" bool gpu_model_fault(const gpu_model_t *model) {
  if (!model)
    return true;
  if (model->kind == GPU_MODEL_SM)
    return gpu_iss_fault();
#if defined(CONFIG_HM)
  return gpu_rtl_fault();
#else
  return true;
#endif
}
