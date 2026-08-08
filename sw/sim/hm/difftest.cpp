#include "difftest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include <common.h>

namespace {
constexpr unsigned Cores = CONFIG_GPU_NUM_CORES;
constexpr unsigned Warps = CONFIG_GPU_NUM_WARPS;
constexpr unsigned Threads = CONFIG_GPU_NUM_THREADS;
constexpr unsigned Contexts = Warps * Threads;
constexpr unsigned StackDepth = 8;
constexpr unsigned CoreWords = 28 + 20 * Warps + 20 * Contexts;
constexpr unsigned Words = 23 + Cores * CoreWords;
enum State : uint32_t { SCHED, FETCH, EXEC, MEM_REQ, MEM_RESP };

// A store is captured before the rising edge. Sorting makes simultaneous
// multi-core stores independent of Verilator's module evaluation order.
struct Store {
  uint32_t addr, data;
  uint8_t mask;
};
bool operator<(const Store &a, const Store &b) {
  if (a.addr != b.addr)
    return a.addr < b.addr;
  if (a.mask != b.mask)
    return a.mask < b.mask;
  return a.data < b.data;
}
bool operator==(const Store &a, const Store &b) {
  return a.addr == b.addr && a.mask == b.mask && a.data == b.data;
}

struct Core {
  uint32_t state = SCHED, cta_active = 0, fault = 0, rr = 0;
  uint32_t iw = 0, im = 0, ip = 0, inst = 0, mt = 0;
  uint32_t block[3]{}, dim[3]{1, 1, 1}, grid[3]{1, 1, 1}, args = 0;
  uint32_t pc[Warps]{}, mask[Warps]{}, valid[Warps]{}, sp[Warps]{};
  uint32_t stack_pc[Warps][StackDepth]{}, stack_mask[Warps][StackDepth]{};
  uint32_t gpr[Contexts][16]{}, tx[Contexts]{}, ty[Contexts]{}, tz[Contexts]{}, gid[Contexts]{};
};

struct Model {
  uint32_t startup = 0x81000000, args = 0, args_size = 0;
  uint32_t dim[3]{1, 1, 1}, grid[3]{1, 1, 1}, block_size = 1, block[3]{};
  uint32_t running = 0, rr = 0;
  bool reset = false, dcr_valid = false, launch = false, active = false, failed = false;
  uint32_t dcr_addr = 0, dcr_data = 0;
  uint64_t cycle = 0;
  Core cores[Cores];
  std::vector<uint8_t> mem;
  std::vector<uint32_t> packed;
  std::vector<Store> expected, actual;
  std::vector<uint32_t> dirty;
} m;

// Reference-memory helpers. The shadow PMEM starts as an exact launch-time
// copy of RTL PMEM and is updated only after transactions have been checked.
uint32_t load32(uint32_t addr) {
  if (addr < CONFIG_MBASE || uint64_t(addr) + 4 > uint64_t(CONFIG_MBASE) + m.mem.size()) {
    m.failed = true;
    return 0;
  }
  uint32_t v;
  std::memcpy(&v, &m.mem[addr - CONFIG_MBASE], 4);
  return v;
}
void store_ref(const Store &s) {
  for (unsigned b = 0; b < 4; ++b)
    if (s.mask & (1u << b)) {
      uint64_t a = uint64_t(s.addr) + b;
      if (a < CONFIG_MBASE || a >= uint64_t(CONFIG_MBASE) + m.mem.size()) {
        m.failed = true;
        continue;
      }
      m.mem[a - CONFIG_MBASE] = uint8_t(s.data >> (8 * b));
      m.dirty.push_back(uint32_t(a - CONFIG_MBASE));
    }
}
int32_t sign_extend(uint32_t value, unsigned bits) {
  return int32_t(value << (32 - bits)) >> (32 - bits);
}
uint32_t lane_register(const Core &core, unsigned lane, unsigned register_index) {
  return core.gpr[core.iw * Threads + lane][register_index & 15];
}
uint32_t memory_address(const Core &core) {
  uint32_t opcode = core.inst & 0x7f;
  uint32_t source_register = (core.inst >> 15) & 31;
  uint32_t immediate =
      opcode == 3 ? uint32_t(sign_extend(core.inst >> 20, 12))
                  : uint32_t(sign_extend(((core.inst >> 7) & 31) | ((core.inst >> 25) << 5), 12));
  return lane_register(core, core.mt, source_register) + immediate;
}
uint32_t load_value(uint32_t addr, uint32_t f3) {
  uint32_t raw = load32(addr & ~3u), sh = raw >> ((addr & 3) * 8);
  switch (f3) {
  case 0:
    return uint32_t(sign_extend(sh, 8));
  case 1:
    return uint32_t(sign_extend(sh, 16));
  case 2:
    return raw;
  case 4:
    return sh & 255;
  case 5:
    return sh & 65535;
  default:
    return 0;
  }
}
void next_warp(Core &c) {
  c.rr = c.iw == Warps - 1 ? 0 : c.iw + 1;
  c.state = SCHED;
}

void accept_cta(Core &c, unsigned cid) {
  c.cta_active = 1;
  c.fault = 0;
  c.state = SCHED;
  c.rr = 0;
  c.args = m.args;
  std::memcpy(c.block, m.block, sizeof c.block);
  std::memcpy(c.dim, m.dim, sizeof c.dim);
  std::memcpy(c.grid, m.grid, sizeof c.grid);
  uint32_t bl = (m.block[2] * m.grid[1] + m.block[1]) * m.grid[0] + m.block[0];
  for (unsigned w = 0; w < Warps; ++w) {
    c.pc[w] = m.startup;
    c.sp[w] = 0;
    c.mask[w] = 0;
    for (unsigned t = 0; t < Threads; ++t)
      if (w * Threads + t < m.block_size)
        c.mask[w] |= 1u << t;
    c.valid[w] = w * Threads < m.block_size;
  }
  for (unsigned i = 0; i < Contexts; ++i) {
    c.tx[i] = i % m.dim[0];
    c.ty[i] = (i / m.dim[0]) % m.dim[1];
    c.tz[i] = i / (m.dim[0] * m.dim[1]);
    c.gid[i] = bl * m.block_size + i;
    std::memset(c.gpr[i], 0, sizeof c.gpr[i]);
  }
  (void)cid;
}

// Advance one Core by exactly one cycle of the shared five-state RTL contract.
void step_core(Core &c, unsigned cid, bool fire) {
  if (fire) {
    accept_cta(c, cid);
    return;
  }
  if (!c.cta_active)
    return;
  if (c.state == SCHED) {
    unsigned sel = 0;
    bool found = false;
    for (unsigned i = 0; i < Warps; ++i) {
      unsigned x = (c.rr + i) % Warps;
      if (!found && c.valid[x]) {
        found = true;
        sel = x;
      }
    }
    if (found) {
      c.iw = sel;
      c.im = c.mask[sel];
      c.ip = c.pc[sel];
      c.state = FETCH;
    } else
      c.cta_active = 0;
    return;
  }
  if (c.state == FETCH) {
    c.inst = load32(c.ip);
    c.state = EXEC;
    return;
  }
  uint32_t op = c.inst & 0x7f, rd = (c.inst >> 7) & 31, f3 = (c.inst >> 12) & 7,
           rs1 = (c.inst >> 15) & 31, rs2 = (c.inst >> 20) & 31, f7 = c.inst >> 25;
  bool ur = op == 0x37 || op == 0x17 || op == 0x13 || op == 0x33 || op == 3 || op == 0x6f ||
            op == 0x67 || op == 0x73;
  bool u1 = op == 0x13 || op == 0x33 || op == 3 || op == 0x23 || op == 0x63 || op == 0x67;
  bool u2 = op == 0x33 || op == 0x23 || op == 0x63;
  if (c.state == EXEC) {
    if ((ur && rd >= 16) || (u1 && rs1 >= 16) || (u2 && rs2 >= 16)) {
      c.fault = 1;
      c.cta_active = 0;
      c.state = SCHED;
      return;
    }
    if (op == 3 || op == 0x23) {
      c.mt = 0;
      c.state = MEM_REQ;
      return;
    }
    if (c.inst == 0x00100073) {
      if (c.sp[c.iw]) {
        unsigned s = --c.sp[c.iw];
        c.pc[c.iw] = c.stack_pc[c.iw][s];
        c.mask[c.iw] = c.stack_mask[c.iw][s];
      } else
        c.valid[c.iw] = 0;
      next_warp(c);
      return;
    }
    if (op == 0x63) {
      uint32_t tk = 0, fl = 0;
      bool good = f3 == 0 || f3 == 1 || f3 == 4 || f3 == 5 || f3 == 6 || f3 == 7;
      int32_t imm = sign_extend(((c.inst >> 7) & 0x1e) | ((c.inst >> 20) & 0x7e0) |
                                    ((c.inst << 4) & 0x800) | ((c.inst >> 19) & 0x1000),
                                13);
      for (unsigned t = 0; t < Threads; ++t)
        if (c.im & (1u << t)) {
          uint32_t a = lane_register(c, t, rs1), b = lane_register(c, t, rs2);
          bool take = false;
          switch (f3) {
          case 0:
            take = a == b;
            break;
          case 1:
            take = a != b;
            break;
          case 4:
            take = int32_t(a) < int32_t(b);
            break;
          case 5:
            take = int32_t(a) >= int32_t(b);
            break;
          case 6:
            take = a < b;
            break;
          case 7:
            take = a >= b;
            break;
          }
          (take ? tk : fl) |= 1u << t;
        }
      if (!good) {
        c.fault = 1;
        c.cta_active = 0;
      } else if (tk && fl) {
        if (c.sp[c.iw] >= StackDepth) {
          c.fault = 1;
          c.cta_active = 0;
        } else {
          unsigned s = c.sp[c.iw]++;
          c.stack_pc[c.iw][s] = c.ip + imm;
          c.stack_mask[c.iw][s] = tk;
          c.pc[c.iw] = c.ip + 4;
          c.mask[c.iw] = fl;
        }
      } else if (tk) {
        c.pc[c.iw] = c.ip + imm;
        c.mask[c.iw] = tk;
      } else {
        c.pc[c.iw] = c.ip + 4;
        c.mask[c.iw] = fl;
      }
      next_warp(c);
      return;
    }
    bool good = true, div = false, target_set = false;
    uint32_t first = 0, next = c.ip + 4;
    for (unsigned t = 0; t < Threads; ++t)
      if (c.im & (1u << t)) {
        uint32_t a = lane_register(c, t, rs1), b = lane_register(c, t, rs2), res = 0;
        bool wr = ur;
        int32_t ii = sign_extend(c.inst >> 20, 12);
        uint32_t csr = c.inst >> 20;
        switch (op) {
        case 0x37:
          res = c.inst & 0xfffff000;
          break;
        case 0x17:
          res = c.ip + (c.inst & 0xfffff000);
          break;
        case 0x13:
          switch (f3) {
          case 0:
            res = a + ii;
            break;
          case 2:
            res = int32_t(a) < ii;
            break;
          case 3:
            res = a < uint32_t(ii);
            break;
          case 4:
            res = a ^ uint32_t(ii);
            break;
          case 6:
            res = a | uint32_t(ii);
            break;
          case 7:
            res = a & uint32_t(ii);
            break;
          case 1:
            res = a << ((c.inst >> 20) & 31);
            break;
          case 5:
            res = (c.inst >> 30) & 1 ? uint32_t(int32_t(a) >> ((c.inst >> 20) & 31))
                                     : a >> ((c.inst >> 20) & 31);
            break;
          default:
            good = false;
          }
          break;
        case 0x33:
          switch (f3) {
          case 0:
            res = f7 == 0x20 ? a - b : a + b;
            break;
          case 1:
            res = a << (b & 31);
            break;
          case 2:
            res = int32_t(a) < int32_t(b);
            break;
          case 3:
            res = a < b;
            break;
          case 4:
            res = a ^ b;
            break;
          case 5:
            res = f7 == 0x20 ? uint32_t(int32_t(a) >> (b & 31)) : a >> (b & 31);
            break;
          case 6:
            res = a | b;
            break;
          case 7:
            res = a & b;
            break;
          default:
            good = false;
          }
          break;
        case 0x6f: {
          uint32_t im = ((c.inst >> 20) & 0x7fe) | ((c.inst >> 9) & 0x800) | (c.inst & 0xff000) |
                        ((c.inst >> 11) & 0x100000);
          res = c.ip + 4;
          next = c.ip + sign_extend(im, 21);
          break;
        }
        case 0x67: {
          uint32_t tg = (a + ii) & ~1u;
          res = c.ip + 4;
          next = tg;
          if (!target_set) {
            first = tg;
            target_set = true;
          } else if (first != tg)
            div = true;
          break;
        }
        case 0x73:
          if (f3 != 2 || rs1) {
            good = false;
          } else
            switch (csr) {
            case 0xf14:
              res = c.gid[c.iw * Threads + t];
              break;
            case 0xcc0:
              res = c.tx[c.iw * Threads + t];
              break;
            case 0xcc1:
              res = c.ty[c.iw * Threads + t];
              break;
            case 0xcc2:
              res = c.tz[c.iw * Threads + t];
              break;
            case 0xcc3:
            case 0xcc4:
            case 0xcc5:
              res = c.block[csr - 0xcc3];
              break;
            case 0xcc6:
            case 0xcc7:
            case 0xcc8:
              res = c.dim[csr - 0xcc6];
              break;
            case 0xcc9:
            case 0xcca:
            case 0xccb:
              res = c.grid[csr - 0xcc9];
              break;
            case 0xccc:
              res = c.args;
              break;
            case 0xccd:
              res = cid * Contexts + c.iw * Threads + t;
              break;
            case 0xcce:
              res = c.iw;
              break;
            case 0xccf:
              res = t;
              break;
            default:
              good = false;
            }
          break;
        default:
          good = false;
        }
        if (wr && rd)
          c.gpr[c.iw * Threads + t][rd] = res;
        c.gpr[c.iw * Threads + t][0] = 0;
      }
    if (!good || div) {
      c.fault = 1;
      c.cta_active = 0;
    } else
      c.pc[c.iw] = next;
    next_warp(c);
    return;
  }
  if (c.state == MEM_REQ) {
    if (!(c.im & (1u << c.mt))) {
      if (c.mt == Threads - 1) {
        c.pc[c.iw] = c.ip + 4;
        next_warp(c);
      } else
        ++c.mt;
    } else
      c.state = MEM_RESP;
    return;
  }
  if (c.state == MEM_RESP) {
    uint32_t a = memory_address(c);
    if (op == 3 && rd)
      c.gpr[c.iw * Threads + c.mt][rd] = load_value(a, f3);
    c.gpr[c.iw * Threads + c.mt][0] = 0;
    if (c.mt == Threads - 1) {
      c.pc[c.iw] = c.ip + 4;
      next_warp(c);
    } else {
      ++c.mt;
      c.state = MEM_REQ;
    }
    return;
  }
}

// Serialize model state into the backend-independent 32-bit DiffTest ABI.
void pack() {
  m.packed.assign(Words, 0);
  uint32_t ready = 0, busy = 0, done = 0, fault = 0;
  for (unsigned i = 0; i < Cores; ++i) {
    if (!m.cores[i].cta_active && m.cores[i].state == SCHED)
      ready |= 1u << i;
    if (m.cores[i].cta_active)
      busy |= 1u << i;
    else
      done |= 1u << i;
    if (m.cores[i].fault)
      fault = 1;
  }
  bool any = false;
  unsigned sel = 0;
  for (unsigned i = 0; i < Cores; ++i) {
    unsigned x = (m.rr + i) % Cores;
    if (!any && (ready & (1u << x))) {
      any = true;
      sel = x;
    }
  }
  uint32_t topbusy = m.running || busy;
  m.packed[0] = topbusy;
  m.packed[1] = fault;
  m.packed[2] = done;
  uint32_t *k = &m.packed[3];
  k[0] = m.startup;
  k[1] = m.args;
  k[2] = m.args_size;
  for (int i = 0; i < 3; ++i) {
    k[3 + i] = m.dim[i];
    k[6 + i] = m.grid[i];
    k[10 + i] = m.block[i];
  }
  k[9] = m.block_size;
  k[13] = m.running;
  k[14] = m.rr;
  k[15] = m.running && any ? 1u << sel : 0;
  k[16] = ready;
  k[17] = busy;
  k[18] = topbusy;
  k[19] = sel | (uint32_t(any) << 31);
  for (unsigned ci = 0; ci < Cores; ++ci) {
    Core &c = m.cores[ci];
    uint32_t *d = &m.packed[23 + ci * CoreWords];
    d[0] = c.state;
    d[1] = c.cta_active;
    d[2] = c.fault;
    d[3] = c.rr;
    d[4] = c.iw;
    d[5] = c.im;
    d[6] = c.ip;
    d[7] = c.inst;
    d[8] = c.mt;
    for (int i = 0; i < 3; ++i) {
      d[9 + i] = c.block[i];
      d[12 + i] = c.dim[i];
      d[15 + i] = c.grid[i];
    }
    d[18] = c.args;
    for (unsigned w = 0; w < Warps; ++w)
      if (c.valid[w])
        d[19] |= 1u << w;
    d[20] = c.state == FETCH;
    d[21] = c.ip;
    if ((c.state == MEM_REQ || c.state == MEM_RESP) && (c.im & (1u << c.mt))) {
      uint32_t op = c.inst & 0x7f, a = memory_address(c), f3 = (c.inst >> 12) & 7;
      d[22] = op == 3 && c.state == MEM_REQ;
      d[23] = op == 0x23 && c.state == MEM_REQ;
      d[24] = f3 == 0 ? 1u << (a & 3) : f3 == 1 ? 3u << (a & 3) : 15;
      d[25] = a & ~3u;
      d[26] = lane_register(c, c.mt, (c.inst >> 20) & 31) << ((a & 3) * 8);
      if (op == 3 && c.state == MEM_RESP)
        d[27] = load32(a & ~3u);
    }
    for (unsigned w = 0; w < Warps; ++w) {
      unsigned b = 28 + w * 20;
      d[b] = c.valid[w];
      d[b + 1] = c.pc[w];
      d[b + 2] = c.mask[w];
      d[b + 3] = c.sp[w];
      for (unsigned s = 0; s < 8; ++s) {
        d[b + 4 + s * 2] = c.stack_pc[w][s];
        d[b + 5 + s * 2] = c.stack_mask[w][s];
      }
    }
    for (unsigned x = 0; x < Contexts; ++x) {
      unsigned b = 28 + 20 * Warps + x * 20;
      d[b] = c.tx[x];
      d[b + 1] = c.ty[x];
      d[b + 2] = c.tz[x];
      d[b + 3] = c.gid[x];
      for (unsigned r = 0; r < 16; ++r)
        d[b + 4 + r] = c.gpr[x][r];
    }
  }
}
} // namespace

// Public hooks used by rtl.cpp. Input sampling, model stepping, state compare,
// transaction compare, and PMEM compare deliberately remain separate phases.
void cycle_diff_init() {
  m = Model{};
  pack();
}
void cycle_diff_reset() {
  m = Model{};
  pack();
}
void cycle_diff_begin(const uint8_t *p, size_t z) {
  m.mem.assign(p, p + z);
  m.active = true;
  m.failed = false;
  m.cycle = 0;
}
void cycle_diff_set_inputs(bool r, bool v, uint32_t a, uint32_t d, bool l) {
  m.reset = r;
  m.dcr_valid = v;
  m.dcr_addr = a;
  m.dcr_data = d;
  m.launch = l;
}
void cycle_diff_step() {
  m.expected.clear();
  m.dirty.clear();
  if (m.reset) {
    Model fresh;
    fresh.mem = std::move(m.mem);
    fresh.active = m.active;
    m = std::move(fresh);
    pack();
    return;
  }
  uint32_t ready = 0, busy = 0;
  for (unsigned i = 0; i < Cores; ++i) {
    if (!m.cores[i].cta_active && m.cores[i].state == SCHED)
      ready |= 1u << i;
    if (m.cores[i].cta_active)
      busy |= 1u << i;
  }
  bool any = false;
  unsigned sel = 0;
  for (unsigned i = 0; i < Cores; ++i) {
    unsigned x = (m.rr + i) % Cores;
    if (!any && (ready & (1u << x))) {
      any = true;
      sel = x;
    }
  }
  bool fire = m.running && any;
  for (unsigned i = 0; i < Cores; ++i) {
    Core &c = m.cores[i];
    if (c.cta_active && c.state == MEM_REQ && (c.im & (1u << c.mt)) && (c.inst & 0x7f) == 0x23) {
      uint32_t a = memory_address(c), f = (c.inst >> 12) & 7;
      Store s{a & ~3u,
              lane_register(c, c.mt, (c.inst >> 20) & 31) << ((a & 3) * 8),
              uint8_t(f == 0   ? 1u << (a & 3)
                      : f == 1 ? 3u << (a & 3)
                               : 15)};
      m.expected.push_back(s);
    }
  }
  for (unsigned i = 0; i < Cores; ++i)
    step_core(m.cores[i], i, fire && i == sel);
  if (m.dcr_valid)
    switch (m.dcr_addr) {
    case 0x10:
      m.startup = m.dcr_data;
      break;
    case 0x11:
      m.args = m.dcr_data;
      break;
    case 0x12:
      m.args_size = m.dcr_data;
      break;
    case 0x13:
    case 0x14:
    case 0x15:
      m.dim[m.dcr_addr - 0x13] = m.dcr_data;
      break;
    case 0x16:
    case 0x17:
    case 0x18:
      m.grid[m.dcr_addr - 0x16] = m.dcr_data;
      break;
    case 0x19:
      m.block_size = m.dcr_data;
      break;
    }
  if (m.launch) {
    m.block[0] = m.block[1] = m.block[2] = 0;
    m.running = m.block_size && m.grid[0] && m.grid[1] && m.grid[2];
  } else if (fire) {
    m.rr = sel == Cores - 1 ? 0 : sel + 1;
    if (m.block[0] + 1 == m.grid[0]) {
      m.block[0] = 0;
      if (m.block[1] + 1 == m.grid[1]) {
        m.block[1] = 0;
        if (m.block[2] + 1 == m.grid[2]) {
          m.block[2] = 0;
          m.running = 0;
        } else
          ++m.block[2];
      } else
        ++m.block[1];
    } else
      ++m.block[0];
  }
  std::sort(m.expected.begin(), m.expected.end());
  for (const auto &s : m.expected)
    store_ref(s);
  ++m.cycle;
  pack();
}
const uint32_t *cycle_diff_state() {
  return m.packed.data();
}
size_t cycle_diff_state_words() {
  return Words;
}
void cycle_diff_observe_store(uint32_t a, uint8_t k, uint32_t d) {
  if (m.active)
    m.actual.push_back({a, k ? d : 0, k});
}
bool cycle_diff_check_stores() {
  std::sort(m.actual.begin(), m.actual.end());
  if (m.actual == m.expected)
    return true;
  m.failed = true;
  std::fprintf(stderr,
               "[DIFF][cycle %llu] memory transaction mismatch: ref=%zu rtl=%zu\n",
               (unsigned long long)m.cycle,
               m.expected.size(),
               m.actual.size());
  return false;
}
void cycle_diff_apply_stores() {
  m.actual.clear();
}
bool cycle_diff_check_memory(const uint8_t *a, size_t z, bool full) {
  if (!m.active)
    return true;
  if (z != m.mem.size())
    return false;
  if (full) {
    if (std::memcmp(a, m.mem.data(), z) == 0)
      return true;
    for (size_t i = 0; i < z; ++i)
      if (a[i] != m.mem[i]) {
        std::fprintf(stderr,
                     "[DIFF][cycle %llu] PMEM mismatch at 0x%08x: ref=%02x rtl=%02x\n",
                     (unsigned long long)m.cycle,
                     uint32_t(CONFIG_MBASE + i),
                     m.mem[i],
                     a[i]);
        break;
      }
    m.failed = true;
    return false;
  }
  std::sort(m.dirty.begin(), m.dirty.end());
  m.dirty.erase(std::unique(m.dirty.begin(), m.dirty.end()), m.dirty.end());
  for (uint32_t i : m.dirty)
    if (a[i] != m.mem[i]) {
      m.failed = true;
      std::fprintf(stderr,
                   "[DIFF][cycle %llu] PMEM dirty-byte mismatch at 0x%08x: ref=%02x rtl=%02x\n",
                   (unsigned long long)m.cycle,
                   uint32_t(CONFIG_MBASE + i),
                   m.mem[i],
                   a[i]);
      return false;
    }
  return true;
}
bool cycle_diff_failed() {
  return m.failed;
}
uint64_t cycle_diff_cycle() {
  return m.cycle;
}
const char *cycle_diff_field_name(size_t word, char *buf, size_t n) {
  static const char *top[] = {"top.busy", "top.fault", "top.done"};
  static const char *kmu[] = {"startup_pc",  "args_addr",   "args_size",   "block_dim.x",
                              "block_dim.y", "block_dim.z", "grid_dim.x",  "grid_dim.y",
                              "grid_dim.z",  "block_size",  "block_idx.x", "block_idx.y",
                              "block_idx.z", "running",     "rr_core",     "cta_valid",
                              "core_ready",  "core_busy",   "busy",        "selected"};
  static const char *core[] = {
      "state",       "cta_active",   "fault",       "rr_warp",     "issue_warp",  "issue_mask",
      "issue_pc",    "issue_inst",   "mem_thread",  "block_idx.x", "block_idx.y", "block_idx.z",
      "block_dim.x", "block_dim.y",  "block_dim.z", "grid_dim.x",  "grid_dim.y",  "grid_dim.z",
      "args_addr",   "active_warps", "imem.ren",    "imem.addr",   "dmem.ren",    "dmem.wen",
      "dmem.mask",   "dmem.addr",    "dmem.wdata",  "dmem.rdata"};
  if (word < 3)
    return top[word];
  if (word < 23) {
    std::snprintf(buf, n, "kmu.%s", kmu[word - 3]);
    return buf;
  }
  size_t q = word - 23, ci = q / CoreWords, o = q % CoreWords;
  if (o < 28) {
    std::snprintf(buf, n, "core[%zu].%s", ci, core[o]);
    return buf;
  }
  if (o < 28 + 20 * Warps) {
    size_t w = (o - 28) / 20, x = (o - 28) % 20;
    if (x < 4) {
      static const char *wn[] = {"valid", "pc", "mask", "stack_sp"};
      std::snprintf(buf, n, "core[%zu].warp[%zu].%s", ci, w, wn[x]);
    } else
      std::snprintf(
          buf, n, "core[%zu].warp[%zu].stack[%zu].%s", ci, w, (x - 4) / 2, (x & 1) ? "mask" : "pc");
    return buf;
  }
  size_t x = o - 28 - 20 * Warps, l = x / 20, f = x % 20;
  if (f < 4) {
    static const char *ln[] = {"thread_idx.x", "thread_idx.y", "thread_idx.z", "global_id"};
    std::snprintf(buf, n, "core[%zu].lane[%zu].%s", ci, l, ln[f]);
  } else
    std::snprintf(buf, n, "core[%zu].lane[%zu].gpr[%zu]", ci, l, f - 4);
  return buf;
}
