# KMU DCR and Warp-SIMT Implementation Plan

## Goal

Replace the fixed launch ABI and per-thread-PC execution model with a DCR-configured KMU and a warp-oriented SIMT execution model. SystemVerilog is the reference implementation; Chisel and SpinalHDL follow only after the reference passes the complete regression suite.

## Frozen interfaces

The host launch descriptor contains `grid_dim[3]`, `block_dim[3]`, `args_host`, and `args_size`. The public `gpu_launch` function accepts a device, kernel, and descriptor.

The write-only, 12-bit DCR map is:

| Address | Register |
| ---: | --- |
| `0x010` | startup PC |
| `0x011` | argument address |
| `0x012` | argument size |
| `0x013`–`0x015` | block dimensions X/Y/Z |
| `0x016`–`0x018` | grid dimensions X/Y/Z |
| `0x019` | block size |

Custom read-only kernel CSRs occupy `0xCC0`–`0xCCF` and expose thread index, block index, block dimensions, grid dimensions, argument address, physical lane ID, warp ID, and lane ID. `mhartid` exposes the launch-global linear thread ID.

## Stage gates

1. Implement the Runtime, C ISS, kernel ABI, KMU, and Warp-SIMT core in SystemVerilog.
2. Pass all application, invalid-input, topology, partial-warp, repeated-launch, and divergence tests with PMEM DiffTest.
3. Port the frozen behavior to Chisel and repeat the suite.
4. Port it to SpinalHDL and repeat the suite.
5. Update project documentation and record results and remaining limitations.

The common verification matrix is:

| Configuration | Purpose |
| --- | --- |
| 2 cores × 2 warps × 2 lanes | Default full application and PMEM DiffTest regression |
| 1 core × 1 warp × 1 lane | Minimum legal topology and zero-width-index edge cases |
| 1 core × 1 warp × 16 lanes | Eight-entry divergence-stack overflow and wide partial-mask behavior |
| 4 cores × 4 warps × 4 lanes | Multi-core elaboration and parameter-width checks |

Acceptance requires all three backends to expose the same DCR/top-level ABI,
pass the default and minimum-topology suites, elaborate the larger topology,
and return `GPU_ERROR_BACKEND` for illegal instructions, lane-divergent `jalr`,
and path-stack overflow.

## Test coverage

- `vecadd`, `vecsub`, and `bitxor`: dynamic global ID/size launch behavior.
- `sizes`: zero/overflow validation, oversized CTA rejection, null argument
  validation, argument scratch updates across launches, allocator behavior,
  illegal instruction, divergent `jalr`, and stack overflow.
- `topology`: 3D thread/block/grid CSRs, global and physical IDs, eight CTAs
  over fewer physical cores, and a partial final warp.
- `divergence`: uniform branches, nested taken/fall-through splits, masked
  writes, and deferred-path restoration.

## First-version limits

- One resident CTA per core.
- No local memory, CTA clusters, multi-entry images, or barriers.
- Divergence uses an eight-entry deferred-path stack. Common suffixes can execute once per path mask, so kernels must avoid warp-global side effects after divergent control flow.
