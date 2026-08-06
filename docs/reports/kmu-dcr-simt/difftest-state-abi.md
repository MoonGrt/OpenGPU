# Cycle DiffTest State ABI

The internal normalized state is a little-endian array of 32-bit words. Word
zero occupies the least-significant 32 bits. The same layout is assembled by
SystemVerilog, Chisel, and SpinalHDL. It is not a `GPUTop` port and is not
routed up the RTL hierarchy. A small `DpiGpuStateBB` lives in the Top, KMU, and
every Core. Each bridge converts its local packed state to an unpacked `int`
array and calls `gpu_diff_state(base, state)`. The C++ simulator uses `base` to
assemble the module-local snapshots into this common ABI.

The Top bridge writes words 0–2, the KMU bridge writes words 3–22, and Core N
writes its Core range beginning at `23 + N × core_words`. A snapshot becomes
valid only after every range has been observed in the current simulation.

## Top and KMU

| Words | State |
|---:|---|
| 0–2 | GPU busy, fault, per-Core done mask |
| 3–5 | Startup PC, argument address, argument size |
| 6–8 | Block dimensions X/Y/Z |
| 9–11 | Grid dimensions X/Y/Z |
| 12 | Block size |
| 13–15 | Current block index X/Y/Z |
| 16–22 | KMU running, round-robin Core, CTA valid, Core ready/busy, busy, selected Core |

## Per Core

Each Core occupies `28 + 20 × warps + 20 × warps × threads` words:

| Relative words | State |
|---:|---|
| 0–8 | FSM, CTA active/fault, scheduler and issued instruction state |
| 9–18 | Block/grid metadata and argument address |
| 19–27 | Active Warps and normalized instruction/data-memory interface |
| next `20 × warps` | Valid, PC, mask, stack pointer, and eight PC/mask stack entries per Warp |
| remaining words | Thread coordinates, global ID, and sixteen GPRs per Lane |

Invalid Warp, stack, and Lane storage is initialized and included. Purely
combinational decoder temporaries and generator-specific registers are not part
of the ABI.

`CONFIG_DIFFTEST` controls the observation hardware itself. When disabled,
SystemVerilog preprocessing and Scala elaboration omit the state packing and
all bridge instances; normal GPU ports and execution behavior are unchanged.
