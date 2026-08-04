#include <common.h>
#include <gpu.h>
#include <device/memory.h>
#include <verilated.h>
#include "VGPUTop.h"

#if defined(CONFIG_WAVE)
#include <verilated_vcd_c.h>
static VerilatedVcdC *wave;
#endif

static VGPUTop *top = new VGPUTop;
static uint64_t cycles;
#if defined(CONFIG_WAVE)
static uint64_t sim_time;
#endif

extern "C" int dpi_paddr_read(int addr) {
  if (!in_pmem((uint32_t)addr)) return 0;
  return (int)paddr_read((uint32_t)addr, 4);
}

extern "C" void dpi_paddr_write(int addr, char mask, int data) {
  for (int byte = 0; byte < 4; ++byte) {
    if (mask & (1 << byte))
      paddr_write((uint32_t)addr + byte, 1,
          ((uint32_t)data >> (8 * byte)) & 0xff);
  }
}

static void dump_wave(void) {
#if defined(CONFIG_WAVE)
  if (cycles >= CONFIG_WAVE_START &&
      cycles < CONFIG_WAVE_END) {
    wave->dump(sim_time++);
    wave->flush();
  }
#endif
}

static void tick(void) {
  top->clock = 0;
  top->eval();
  dump_wave();
  top->clock = 1;
  top->eval();
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
#if defined(CONFIG_WAVE)
  Verilated::traceEverOn(true);
  wave = new VerilatedVcdC;
  top->trace(wave, 99);
  wave->open(WAVEOUT "/wave.vcd");
#endif
  reset();
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
  return top->io_gpu_fault;
}

extern "C" bool gpu_rtl_wait(uint64_t max_cycles) {
  for (uint64_t cycle = 0; cycle < max_cycles; ++cycle) {
    tick();
    if (top->io_gpu_fault) return false;
    if (!top->io_gpu_busy) return true;
  }
  return !top->io_gpu_busy && !top->io_gpu_fault;
}
