#include <gpu_iss.h>
#include <gpu.h>
#include <device/memory.h>

static gpu_iss_core_t *cores;
static uint32_t ncores;
static uint32_t physical_count;
static uint32_t rr;
static uint32_t launch_args_addr;
static uint32_t launch_grid[3] = {1, 1, 1};
static uint32_t launch_block[3] = {1, 1, 1};
static bool faulted;

static void fault_core(gpu_iss_core_t *core) {
  faulted = true;
  core->halted = true;
}

static inline int32_t sext(uint32_t value, unsigned bits) {
  return (int32_t)(value << (32 - bits)) >> (32 - bits);
}

static inline uint32_t load_mem(
    gpu_iss_core_t *core, uint32_t addr, unsigned funct3) {
  switch (funct3) {
    case 0: return (uint32_t)sext(paddr_read(addr, 1), 8);
    case 1: return (uint32_t)sext(paddr_read(addr, 2), 16);
    case 2: return paddr_read(addr, 4);
    case 4: return paddr_read(addr, 1);
    case 5: return paddr_read(addr, 2);
    default: fault_core(core); return 0;
  }
}

static inline void store_mem(gpu_iss_core_t *c, uint32_t addr,
                             uint32_t value, unsigned funct3) {
  uint32_t mask;
  switch (funct3) {
    case 0: mask = 1; paddr_write(addr, 1, value); break;
    case 1: mask = 3; paddr_write(addr, 2, value); break;
    case 2: mask = 15; paddr_write(addr, 4, value); break;
    default: fault_core(c); return;
  }
  gpu_trace_store(c->hartid, addr, mask, value);
}

static bool same_warp(
    const gpu_iss_core_t *left, const gpu_iss_core_t *right) {
  return left->warp_id == right->warp_id &&
      left->block_idx[0] == right->block_idx[0] &&
      left->block_idx[1] == right->block_idx[1] &&
      left->block_idx[2] == right->block_idx[2];
}

static void step_core(gpu_iss_core_t *c) {
  uint32_t inst = paddr_read(c->pc, 4);
  uint32_t opcode = inst & 0x7f;
  uint32_t rd = (inst >> 7) & 0x1f;
  uint32_t funct3 = (inst >> 12) & 7;
  uint32_t rs1 = (inst >> 15) & 0x1f;
  uint32_t rs2 = (inst >> 20) & 0x1f;
  uint32_t funct7 = inst >> 25;
  uint32_t next = c->pc + 4;
  uint32_t a, b, result = 0;

  bool uses_rd = opcode == 0x37 || opcode == 0x17 || opcode == 0x13 ||
                 opcode == 0x33 || opcode == 0x03 || opcode == 0x6f ||
                 opcode == 0x67 || opcode == 0x73;
  bool uses_rs1 = opcode == 0x13 || opcode == 0x33 || opcode == 0x03 ||
                  opcode == 0x23 || opcode == 0x63 || opcode == 0x67;
  bool uses_rs2 = opcode == 0x33 || opcode == 0x23 || opcode == 0x63;
  /* Unused register bit slices overlap immediate fields and must not be checked. */
  if ((uses_rd && rd >= 16) || (uses_rs1 && rs1 >= 16) ||
      (uses_rs2 && rs2 >= 16))
    { fault_core(c); return; }
  a = uses_rs1 ? c->gpr[rs1] : 0;
  b = uses_rs2 ? c->gpr[rs2] : 0;
  gpu_trace_commit(c->hartid, c->pc, inst);

  switch (opcode) {
    case 0x37: result = inst & 0xfffff000; break;                         // LUI
    case 0x17: result = c->pc + (inst & 0xfffff000); break;               // AUIPC
    case 0x13: {                                                            // OP-IMM
      int32_t imm = sext(inst >> 20, 12);
      switch (funct3) {
        case 0: result = a + imm; break;
        case 2: result = (int32_t)a < imm; break;
        case 3: result = a < (uint32_t)imm; break;
        case 4: result = a ^ (uint32_t)imm; break;
        case 6: result = a | (uint32_t)imm; break;
        case 7: result = a & (uint32_t)imm; break;
        case 1: result = a << ((inst >> 20) & 31); break;
        case 5: result = (funct7 == 0x20) ? (uint32_t)((int32_t)a >> ((inst >> 20) & 31)) : a >> ((inst >> 20) & 31); break;
        default: fault_core(c); return;
      }
      break;
    }
    case 0x33:                                                               // OP
      switch (funct3) {
        case 0: result = (funct7 == 0x20) ? a - b : a + b; break;
        case 1: result = a << (b & 31); break;
        case 2: result = (int32_t)a < (int32_t)b; break;
        case 3: result = a < b; break;
        case 4: result = a ^ b; break;
        case 5: result = (funct7 == 0x20) ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
        case 6: result = a | b; break;
        case 7: result = a & b; break;
        default: fault_core(c); return;
      }
      break;
    case 0x03: result = load_mem(c, a + sext(inst >> 20, 12), funct3); break;
    case 0x23: {
      uint32_t imm = ((inst >> 7) & 0x1f) | ((inst >> 25) << 5);
      store_mem(c, a + sext(imm, 12), b, funct3);
      break;
    }
    case 0x63: {
      uint32_t imm = ((inst >> 7) & 0x1e) | ((inst >> 20) & 0x7e0)
                   | ((inst << 4) & 0x800) | ((inst >> 19) & 0x1000);
      bool take = false;
      switch (funct3) {
        case 0: take = a == b; break;
        case 1: take = a != b; break;
        case 4: take = (int32_t)a < (int32_t)b; break;
        case 5: take = (int32_t)a >= (int32_t)b; break;
        case 6: take = a < b; break;
        case 7: take = a >= b; break;
        default: fault_core(c); return;
      }
      if (take) next = c->pc + sext(imm, 13);
      break;
    }
    case 0x6f: {
      uint32_t imm = ((inst >> 20) & 0x7fe) | ((inst >> 9) & 0x800)
                   | (inst & 0xff000) | ((inst >> 11) & 0x100000);
      result = c->pc + 4;
      next = c->pc + sext(imm, 21);
      break;
    }
    case 0x67: {
      result = c->pc + 4;
      next = (a + sext(inst >> 20, 12)) & ~1u;
      for (uint32_t i = 0; i < ncores; ++i) {
        gpu_iss_core_t *lane = &cores[i];
        if (lane != c && !lane->halted && lane->pc == c->pc &&
            same_warp(c, lane)) {
          uint32_t lane_target =
              (lane->gpr[rs1] + sext(inst >> 20, 12)) & ~1u;
          if (lane_target != next) { fault_core(c); return; }
        }
      }
      break;
    }
    case 0x73:
      if (inst == 0x00100073) { c->halted = true; return; }               // EBREAK
      if (funct3 == 2 && rs1 == 0) {
        switch ((inst >> 20) & 0xfff) {
          case 0xf14: result = c->hartid; break;
          case 0xcc0: result = c->thread_idx[0]; break;
          case 0xcc1: result = c->thread_idx[1]; break;
          case 0xcc2: result = c->thread_idx[2]; break;
          case 0xcc3: result = c->block_idx[0]; break;
          case 0xcc4: result = c->block_idx[1]; break;
          case 0xcc5: result = c->block_idx[2]; break;
          case 0xcc6: result = c->block_dim[0]; break;
          case 0xcc7: result = c->block_dim[1]; break;
          case 0xcc8: result = c->block_dim[2]; break;
          case 0xcc9: result = c->grid_dim[0]; break;
          case 0xcca: result = c->grid_dim[1]; break;
          case 0xccb: result = c->grid_dim[2]; break;
          case 0xccc: result = c->args_addr; break;
          case 0xccd: result = c->physical_id; break;
          case 0xcce: result = c->warp_id; break;
          case 0xccf: result = c->lane_id; break;
          default: fault_core(c); return;
        }
      } else {
        fault_core(c); return;
      }
      break;
    default: fault_core(c); return;
  }
  if (uses_rd && rd != 0) c->gpr[rd] = result;
  c->gpr[0] = 0;
  c->pc = next;
}

void gpu_iss_init(uint32_t num_cores) {
  Assert(num_cores >= 1, "invalid GPU hardware-thread count");
  free(cores);
  cores = NULL;
  ncores = 0;
  physical_count = num_cores;
  rr = 0;
  faulted = false;
}

void gpu_iss_configure(uint32_t args_addr,
    const uint32_t grid_dim[3], const uint32_t block_dim[3]) {
  launch_args_addr = args_addr;
  memcpy(launch_grid, grid_dim, sizeof(launch_grid));
  memcpy(launch_block, block_dim, sizeof(launch_block));
}

bool gpu_iss_launch(uint32_t entry) {
  uint64_t blocks = (uint64_t)launch_grid[0] * launch_grid[1] * launch_grid[2];
  uint64_t block_size =
      (uint64_t)launch_block[0] * launch_block[1] * launch_block[2];
  uint64_t total = blocks * block_size;
  if (total == 0 || total > UINT32_MAX) return false;
  free(cores);
  cores = calloc((size_t)total, sizeof(*cores));
  if (!cores) return false;
  ncores = (uint32_t)total;
  rr = 0;
  const uint32_t lanes_per_core =
      CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS;
  const uint32_t hardware_cores = physical_count / lanes_per_core;
  for (uint32_t i = 0; i < ncores; ++i) {
    uint32_t local = i % (uint32_t)block_size;
    uint32_t block = i / (uint32_t)block_size;
    cores[i].pc = entry;
    cores[i].hartid = i;
    cores[i].physical_id =
        (block % hardware_cores) * lanes_per_core + local;
    cores[i].args_addr = launch_args_addr;
    cores[i].lane_id = local % CONFIG_GPU_NUM_THREADS;
    cores[i].warp_id = local / CONFIG_GPU_NUM_THREADS;
    cores[i].thread_idx[0] = local % launch_block[0];
    cores[i].thread_idx[1] = (local / launch_block[0]) % launch_block[1];
    cores[i].thread_idx[2] = local / (launch_block[0] * launch_block[1]);
    cores[i].block_idx[0] = block % launch_grid[0];
    cores[i].block_idx[1] = (block / launch_grid[0]) % launch_grid[1];
    cores[i].block_idx[2] = block / (launch_grid[0] * launch_grid[1]);
    memcpy(cores[i].block_dim, launch_block, sizeof(launch_block));
    memcpy(cores[i].grid_dim, launch_grid, sizeof(launch_grid));
  }
  return true;
}

bool gpu_iss_step(void) {
  if (gpu_iss_done()) return false;
  for (uint32_t scanned = 0; scanned < ncores; ++scanned) {
    gpu_iss_core_t *c = &cores[rr];
    rr = (rr + 1) % ncores;
    if (!c->halted) { step_core(c); return true; }
  }
  return false;
}

bool gpu_iss_done(void) {
  if (faulted) return true;
  for (uint32_t i = 0; i < ncores; ++i)
    if (!cores[i].halted) return false;
  return ncores != 0;
}

uint32_t gpu_iss_num_cores(void) { return ncores; }

bool gpu_iss_fault(void) { return faulted; }
