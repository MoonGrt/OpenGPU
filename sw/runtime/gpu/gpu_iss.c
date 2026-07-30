#include <gpu_iss.h>
#include <gpu.h>
#include <device/memory.h>

#define GPU_NUM_HARTS \
  (CONFIG_GPU_NUM_CORES * CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS)

static gpu_iss_core_t cores[GPU_NUM_HARTS];
static uint32_t ncores;
static uint32_t rr;

static inline int32_t sext(uint32_t value, unsigned bits) {
  return (int32_t)(value << (32 - bits)) >> (32 - bits);
}

static inline uint32_t load_mem(uint32_t addr, unsigned funct3) {
  switch (funct3) {
    case 0: return (uint32_t)sext(paddr_read(addr, 1), 8);
    case 1: return (uint32_t)sext(paddr_read(addr, 2), 16);
    case 2: return paddr_read(addr, 4);
    case 4: return paddr_read(addr, 1);
    case 5: return paddr_read(addr, 2);
    default: panic("GPU ISS: unsupported load funct3=%u", funct3);
  }
}

static inline void store_mem(gpu_iss_core_t *c, uint32_t addr,
                             uint32_t value, unsigned funct3) {
  uint32_t mask;
  switch (funct3) {
    case 0: mask = 1; paddr_write(addr, 1, value); break;
    case 1: mask = 3; paddr_write(addr, 2, value); break;
    case 2: mask = 15; paddr_write(addr, 4, value); break;
    default: panic("GPU ISS: unsupported store funct3=%u", funct3);
  }
  gpu_trace_store(c->hartid, addr, mask, value);
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
    panic("GPU ISS: RV32E register out of range at 0x%08x", c->pc);
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
        default: panic("GPU ISS: unsupported OP-IMM");
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
        default: panic("GPU ISS: unsupported OP");
      }
      break;
    case 0x03: result = load_mem(a + sext(inst >> 20, 12), funct3); break;
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
        default: panic("GPU ISS: unsupported branch");
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
    case 0x67:
      result = c->pc + 4;
      next = (a + sext(inst >> 20, 12)) & ~1u;
      break;
    case 0x73:
      if (inst == 0x00100073) { c->halted = true; return; }               // EBREAK
      if (funct3 == 2 && rs1 == 0 && ((inst >> 20) & 0xfff) == 0xf14) {    // CSRRS mhartid,x0
        result = c->hartid;
      } else {
        panic("GPU ISS: unsupported SYSTEM instruction 0x%08x", inst);
      }
      break;
    default: panic("GPU ISS: unsupported opcode 0x%02x at 0x%08x", opcode, c->pc);
  }
  if (uses_rd && rd != 0) c->gpr[rd] = result;
  c->gpr[0] = 0;
  c->pc = next;
}

void gpu_iss_init(uint32_t num_cores) {
  Assert(num_cores >= 1 && num_cores <= GPU_NUM_HARTS,
      "invalid GPU hardware-thread count");
  ncores = num_cores;
  rr = 0;
  memset(cores, 0, sizeof(cores));
}

bool gpu_iss_launch(uint32_t entry) {
  for (uint32_t i = 0; i < ncores; ++i) {
    memset(&cores[i], 0, sizeof(cores[i]));
    cores[i].pc = entry;
    cores[i].hartid = i;
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
  for (uint32_t i = 0; i < ncores; ++i)
    if (!cores[i].halted) return false;
  return ncores != 0;
}

uint32_t gpu_iss_num_cores(void) { return ncores; }
