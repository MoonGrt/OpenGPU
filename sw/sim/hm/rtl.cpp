#include "VGPUTop.h"
#include "backend.h"
#include <common.h>
#include <device/memory.h>
#include <gpu.h>
#include <verilated.h>
#if defined(CONFIG_DIFFTEST)
#include "difftest.h"
#endif

#if defined(CONFIG_WAVE)
#include <filesystem>
#include <verilated_vcd_c.h>
static VerilatedVcdC *wave;
#endif

static VGPUTop *top = new VGPUTop;
static uint64_t cycles;
static bool diff_failed;
#if defined(CONFIG_DIFFTEST)
constexpr size_t DiffCoreWords =
    28 + 20 * CONFIG_GPU_NUM_WARPS + 20 * CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS;
#endif
#if defined(CONFIG_WAVE)
static uint64_t sim_time;
#endif

static void dump_wave(void) {
#if defined(CONFIG_WAVE)
  if (cycles >= CONFIG_WAVE_START && cycles < CONFIG_WAVE_END) {
    wave->dump(sim_time++);
    wave->flush();
  }
#endif
}

static void tick(void) {
#if defined(CONFIG_DIFFTEST)
  const uint32_t *rtl_diff_state = dpi_diff_state();
#endif
  top->clock = 0;
  top->eval();
#if defined(CONFIG_DIFFTEST)
  for (size_t core = 0; core < CONFIG_GPU_NUM_CORES; ++core) {
    size_t base = 23 + core * DiffCoreWords;
    if (rtl_diff_state[base + 23]) {
      uint32_t addr = rtl_diff_state[base + 25];
      uint8_t mask = (uint8_t)rtl_diff_state[base + 24];
      uint32_t data = rtl_diff_state[base + 26];
      cycle_diff_observe_store(addr, mask, data);
      for (int byte = 0; byte < 4; ++byte)
        if (mask & (1 << byte))
          paddr_write(addr + byte, 1, data >> (8 * byte));
    }
  }
  cycle_diff_set_inputs(
      top->reset, top->io_dcr_valid, top->io_dcr_addr, top->io_dcr_data, top->io_gpu_launch);
  cycle_diff_step();
#endif
  dump_wave();
  top->clock = 1;
  top->eval();
#if defined(CONFIG_DIFFTEST)
  top->eval();
  if (dpi_diff_failed())
    diff_failed = true;
  if (!diff_failed && !cycle_diff_check_stores())
    diff_failed = true;
  const uint32_t *reference = cycle_diff_state();
  for (size_t word = 0; !diff_failed && word < cycle_diff_state_words(); ++word) {
    uint32_t actual = rtl_diff_state[word];
    if (reference[word] != actual) {
      char field[96];
      fprintf(stderr,
              "[DIFF][cycle %llu] %s mismatch: ref=%08x rtl=%08x\n",
              (unsigned long long)cycle_diff_cycle(),
              cycle_diff_field_name(word, field, sizeof(field)),
              reference[word],
              actual);
      diff_failed = true;
    }
  }
  if (!diff_failed && !cycle_diff_check_memory(guest_to_host(CONFIG_MBASE), CONFIG_MSIZE, false))
    diff_failed = true;
  cycle_diff_apply_stores();
#endif
  dump_wave();
  ++cycles;
}

static void reset(void) {
  top->reset = 1;
  tick();
  top->reset = 0;
}

extern "C" void rtl_init(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  top->io_gpu_launch = 0;
  top->io_dcr_valid = 0;
  top->io_dcr_addr = 0;
  top->io_dcr_data = 0;
#if defined(CONFIG_DIFFTEST)
  cycle_diff_init();
  diff_failed = false;
  dpi_diff_reset();
#endif
#if defined(CONFIG_WAVE)
  Verilated::traceEverOn(true);
  wave = new VerilatedVcdC;
  top->trace(wave, 99);
  std::filesystem::create_directories(WAVEOUT);
  wave->open(WAVEOUT "/wave.vcd");
#endif
  reset();
#if defined(CONFIG_DIFFTEST)
  if (!dpi_diff_state_seen()) {
    fprintf(stderr, "[DIFF] RTL state DPI bridge did not provide a snapshot\n");
    diff_failed = true;
  }
#endif
}

extern "C" void rtl_exit(void) {
#if defined(CONFIG_WAVE)
  wave->close();
  delete wave;
#endif
  delete top;
}

extern "C" void gpu_rtl_init(void) {
  top->io_gpu_launch = 0;
  tick();
}

extern "C" bool gpu_rtl_launch(uint32_t entry) {
  (void)entry;
#if defined(CONFIG_DIFFTEST)
  cycle_diff_begin(guest_to_host(CONFIG_MBASE), CONFIG_MSIZE);
#endif
  top->io_gpu_launch = 1;
  tick();
  top->io_gpu_launch = 0;
  tick();
  return true;
}

extern "C" void gpu_rtl_dcr_write(uint32_t addr, uint32_t data) {
  top->io_dcr_addr = addr;
  top->io_dcr_data = data;
  top->io_dcr_valid = 1;
  tick();
  top->io_dcr_valid = 0;
}

extern "C" bool gpu_rtl_fault(void) {
  return top->io_gpu_fault || diff_failed;
}

extern "C" bool gpu_rtl_wait(uint64_t max_cycles) {
  for (uint64_t cycle = 0; cycle < max_cycles; ++cycle) {
    tick();
    if (top->io_gpu_fault || diff_failed)
      return false;
    if (!top->io_gpu_busy) {
#if defined(CONFIG_DIFFTEST)
      if (!cycle_diff_check_memory(guest_to_host(CONFIG_MBASE), CONFIG_MSIZE, true)) {
        diff_failed = true;
        return false;
      }
      printf("[DIFF] cycle lockstep matched for %llu cycles\n",
             (unsigned long long)cycle_diff_cycle());
#endif
      return true;
    }
  }
  return !top->io_gpu_busy && !top->io_gpu_fault;
}
