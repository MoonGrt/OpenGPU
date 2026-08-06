# KMU DCR and Warp-SIMT Implementation Report

## Result

The KMU DCR and Warp-SIMT refactor is implemented in SystemVerilog, Chisel,
and SpinalHDL. All three use the same Runtime API, top-level signals, DCR and
CSR numbers, CTA order, fault policy, and full-PMEM DiffTest oracle.

## Interface changes

The host launch API now accepts `gpu_launch_info_t` with three-dimensional grid
and block dimensions plus `args_host` and `args_size`. It rejects zero
dimensions, overflowing products, blocks larger than the physical Core
capacity, and nonzero argument sizes with null host pointers.

The Runtime allocates a private argument scratch object for each launch, copies
the host bytes, writes DCR `0x010`–`0x019`, and emits a separate launch pulse.
`gpu_wait` releases the scratch. Kernels receive only
`const kernel_arg_t *args` in `a0`.

CSR `0xCC0`–`0xCCF` provides thread/block/grid coordinates and dimensions,
argument address, physical ID, warp ID, and lane ID. `mhartid` is the dynamic
launch-global ID. Startup assembly uses the physical-ID CSR for private stacks
and the argument-address CSR for `a0`.

## SystemVerilog reference

The reference contains a write-only DCR register bank, X→Y→Z CTA walker,
ready/valid round-robin dispatch, and one resident CTA per Core. CTA admission
clears lane GPRs, derives 3D coordinates, creates active Warps, and masks the
partial final Warp.

Each Warp owns one PC, active mask, valid state, and an eight-entry deferred
path stack. Each Thread owns only 16 RV32E GPRs, coordinates, values, and
addresses. Branch predicates are evaluated per active lane. Uniform branches
update the shared PC directly; a divergent branch executes fall-through first
and pushes the taken PC/mask. `ebreak` restores a deferred path or completes
the Warp. Memory operations are serialized across active lanes.

Illegal instructions/CSRs, RV32E register violations, lane-divergent `jalr`,
and stack overflow assert `fault`; the Runtime returns `GPU_ERROR_BACKEND`.

The C ISS creates dynamic launch-global contexts, attaches topology CSR state,
uses the same X-major CTA numbering and physical Core mapping, reports illegal
instructions as backend faults, and detects lane-divergent `jalr`. It remains a
functional PMEM oracle rather than a cycle-level Warp scheduler.

## Chisel parity

Chisel has a native KMU and native Warp-SIMT Core with the frozen interface and
fault semantics. Single-element Vec index warnings remain informational in the
1×1×1 elaboration; the generated model passes the complete suite.

## SpinalHDL parity

SpinalHDL has the same native modules. Compile-time accessors handle one-Warp
and one-Lane Vecs, and load data is explicitly registered across the DPI memory
request/response states. Strict width and exhaustive-switch checks pass.

## Verification results

| Matrix | SystemVerilog | Chisel | SpinalHDL |
| --- | --- | --- | --- |
| 2×2×2, all six tests, full PMEM DiffTest | Pass | Pass | Pass |
| 1×1×1, all six tests | Pass | Pass | Pass |
| 1×1×16, fault/argument suite | Pass | Pass | Pass |
| 4×4×4 elaboration/lint | Pass | Pass | Pass |
| Illegal instruction fault | Pass | Pass | Pass |
| Lane-divergent `jalr` fault | Pass | Pass | Pass |
| Eight-entry stack overflow fault | Pass | Pass | Pass |

Representative commands:

```sh
make BACKEND=verilog -C tests run
make BACKEND=chisel TOOL=mill -C tests run
make BACKEND=spinal TOOL=mill -C tests run
verilator --lint-only --unroll-count 1024 \
  -GNUM_CORES=4 -GNUM_WARPS=4 -GNUM_THREADS=4 ...
git diff --check
```

The regression was also run across backend switches without cleaning to verify
backend-specific Runtime/Verilator cache isolation.

## Build-system changes

- Runtime objects and archives are isolated per HDL backend.
- DCR and KMU/CTA interface types live under each RTL backend's `Interface/`
  directory; the DCR register map and Runtime-facing simulation ABI remain
  unchanged.
- SystemVerilog `GPUTop` keeps a flat DCR simulation boundary and constructs
  `DcrIf` internally. `GPUKmu` consumes `DcrIf.slave`, while `GPUKmu` and every
  `GPUCore` communicate through `KmuIf.master/slave`. This avoids a separate
  simulator-only top-level wrapper.
- Simulation models are separated into `sw/sim/hm` (Verilator harness) and
  `sw/sim/sm` (C ISS). Software-only builds produce `libgpu-runtime-sm.a`;
  hardware builds produce `libgpu-runtime-hw-<rtl>.a`, matching the
  `gpu_hw_<rtl>_defconfig` naming hierarchy.
- The Backend menu first selects Software Model or Hardware Model. Hardware
  Model exposes a nested Implementation choice for Chisel, SpinalHDL, or
  SystemVerilog.
- Hardware presets use the uniform `gpu_hw_<rtl>_defconfig` naming scheme;
  the redundant generic `gpu_hm_defconfig` preset was removed.
- VCD files are isolated by hardware backend under
  `hw/build/wave/hw-<rtl>/wave.vcd`, so backend switches do not overwrite an
  existing waveform.
- SystemVerilog and SpinalHDL instantiate both `SimInstMem` and `SimDataMem`
  above each Core. Their cores expose instruction and data request/response
  ports and no longer contain simulation-memory components.
- The default topology is 2 cores × 4 warps × 4 lanes.
- Scala RTL generation depends on `.config` topology values.
- The Runtime always asks the hardware makefile to check RTL prerequisites.
- Verilator directories and archives are recreated on model changes so stale
  split objects cannot survive topology transitions.
- Verilator compilation uses one job to keep wide generated models within the
  available memory limit.

The post-layout-refactor smoke regression passed `vecadd` on the software
model and all three RTL backends at the new 2×4×4 default. Each RTL run also
passed full-PMEM DiffTest.

## Cycle-level full-state DiffTest

`CONFIG_DIFFTEST` now runs an independent cycle-accurate C++ model alongside
the functional C ISS. Each backend assembles the same packed 32-bit word ABI
for DCR/KMU state, normalized Core FSM and memory state, every Warp PC/mask/path
stack, every Lane coordinate, and all sixteen RV32E registers. Module-local DPI
bridges in the Top, KMU, and each Core copy disjoint ABI ranges directly into C;
the state is neither exposed through `GPUTop` nor routed between RTL modules.
The Runtime advances the model on every rising edge and stops at the first
mismatching word with the cycle and hierarchical field path.

Stores are sampled from normalized pre-edge state instead of DPI callback
scheduling. Reference and RTL transactions are checked before dirty PMEM bytes
are compared. A complete PMEM scan remains at completion, followed by the C ISS
final-PMEM comparison. This preserves a per-cycle PMEM invariant without
scanning all 128 MiB every cycle.

The default 2 Core × 4 Warp × 4 Lane configuration passed `vecadd`, `vecsub`,
`bitxor`, `sizes`, `topology`, and `divergence` on SystemVerilog, Chisel, and
SpinalHDL with cycle lockstep enabled.

The cross-backend `vecadd` timing checks matched exactly: 738 cycles for the
default 2×4×4 topology and 2,871 cycles for 1×1×1. A Verilog `vecadd` run with
`CONFIG_DIFFTEST` disabled also passed on all three RTL backends. In that mode,
SystemVerilog preprocessing and Chisel/SpinalHDL elaboration remove state
packing and every `DpiGpuStateBB` instance.

## Readability pass

A repository-level C/C++ formatting policy now fixes indentation, brace style,
and a 100-column limit. The cycle model and Runtime driver were reformatted;
the model's helper names and phase comments now describe sign extension, Lane
register access, memory addressing, CTA admission, Core stepping, and ABI
packing directly. The added DiffTest packing logic in all three HDL backends
was expanded into named loops and one assignment per line. Existing compressed
RISC-V decode and load-result cases in SystemVerilog and SpinalHDL were also
split into conventional switch/case blocks. No maintained C/C++, Scala, or
SystemVerilog source line remains longer than 160 characters.

The exact state layout is documented in `difftest-state-abi.md`. After the
readability-only edits, all three backends still passed `vecadd` in 738 cycles
with cycle lockstep and final C ISS PMEM comparison.

The first implementation exposed the packed state as the top-level
`io_diff_state` output. It was replaced with the same DPI array-copy pattern
used by `source/YSYX/memu` for GPR/CSR synchronization, then distributed into
the owning Top, KMU, and Core modules. Generated Verilator headers for all three
backends now contain `gpu_diff_state(int base, const svOpenArrayHandle)` and no
`io_diff_state` member.

## Deviations and remaining work

- The C ISS models launch-global contexts and final functional state, not the
  Warp issue timeline or deferred-path stack cycle by cycle.
- No join-PC reconvergence is implemented. A common suffix can execute once per
  path mask; only lane-private or correctly masked side effects are supported.
- Barriers and Warp-global side effects after divergence are unsupported.
- Multi-entry kernels, CTA clusters, multiple resident CTAs per Core, and local
  memory remain outside this version.
- No synthesis-based area/timing measurement was performed; the added dominant
  state is per-Warp PC/mask/path-stack storage and per-lane coordinate storage.
