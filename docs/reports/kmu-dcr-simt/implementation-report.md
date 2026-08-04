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
- Scala RTL generation depends on `.config` topology values.
- The Runtime always asks the hardware makefile to check RTL prerequisites.
- Verilator directories and archives are recreated on model changes so stale
  split objects cannot survive topology transitions.
- Verilator compilation uses one job to keep wide generated models within the
  available memory limit.

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
