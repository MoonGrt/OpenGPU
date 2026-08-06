#include "VGPUTop.h"
#include <algorithm>
#include <common.h>
#include <cstring>
#include <device/memory.h>
#include <gpu.h>
#include <svdpi.h>
#include <verilated.h>
#if defined(CONFIG_DIFFTEST)
#include "cycle_difftest.h"
#include <array>
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
constexpr size_t DiffStateWords = 23 + CONFIG_GPU_NUM_CORES * DiffCoreWords;
static std::array<uint32_t, DiffStateWords> rtl_diff_state;
static std::array<bool, DiffStateWords> rtl_diff_state_valid;
static bool rtl_diff_state_seen;
#endif
#if defined(CONFIG_WAVE)
static uint64_t sim_time;
#endif

extern "C" int dpi_paddr_read(int addr) {
  if (!in_pmem((uint32_t)addr))
    return 0;
  return (int)paddr_read((uint32_t)addr, 4);
}

extern "C" void dpi_paddr_write(int addr, char mask, int data) {
#if defined(CONFIG_DIFFTEST)
  (void)addr;
  (void)mask;
  (void)data;
  return;
#endif
  for (int byte = 0; byte < 4; ++byte) {
    if (mask & (1 << byte))
      paddr_write((uint32_t)addr + byte, 1, ((uint32_t)data >> (8 * byte)) & 0xff);
  }
}

extern "C" void gpu_diff_state(int base, const svOpenArrayHandle state) {
#if defined(CONFIG_DIFFTEST)
  if (base < 0 || (size_t)base > rtl_diff_state.size()) {
    diff_failed = true;
    return;
  }
  size_t words = (size_t)svSize(state, 1);
  if (words > rtl_diff_state.size() - (size_t)base) {
    diff_failed = true;
    return;
  }
  const void *contiguous_state = svGetArrayPtr(state);
  if (contiguous_state) {
    memcpy(rtl_diff_state.data() + base, contiguous_state, words * sizeof(uint32_t));
  } else {
    // Keep a portable fallback for simulators that do not expose a contiguous
    // pointer for an unpacked DPI array.
    for (size_t word = 0; word < words; ++word) {
      const void *element = svGetArrElemPtr1(state, (int)word);
      if (!element) {
        diff_failed = true;
        return;
      }
      memcpy(&rtl_diff_state[base + word], element, sizeof(rtl_diff_state[word]));
    }
  }
  std::fill_n(rtl_diff_state_valid.begin() + base, words, true);
  rtl_diff_state_seen = std::all_of(
      rtl_diff_state_valid.begin(), rtl_diff_state_valid.end(), [](bool valid) { return valid; });
#else
  (void)base;
  (void)state;
#endif
}

static void dump_wave(void) {
#if defined(CONFIG_WAVE)
  if (cycles >= CONFIG_WAVE_START && cycles < CONFIG_WAVE_END) {
    wave->dump(sim_time++);
    wave->flush();
  }
#endif
}

static void tick(void) {
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
  rtl_diff_state_seen = false;
  rtl_diff_state_valid.fill(false);
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
  if (!rtl_diff_state_seen) {
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
