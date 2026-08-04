# RV32E GPU tests

Every case follows the Vortex-style split: `main.cpp` runs in the Native
frontend, `common.h` defines the launch arguments, and `kernel.cpp` runs on the
RV32E GPU. The host allocates and uploads buffers, launches the kernel, reads
the result back, and validates it.

Current cases:

- `vecadd`: 35-element integer addition, exercising an irregular work size.
- `vecsub`: 17-element integer subtraction.
- `bitxor`: 19-element bitwise XOR with boundary-pattern inputs.
- `sizes`: repeated vecadd launches with changing argument contents and sizes,
  plus Runtime validation, allocator checks, illegal instructions, divergent
  indirect jumps, and path-stack overflow fault handling.
- `topology`: validates 3D thread/block/grid CSRs, global IDs, physical IDs,
  CTA reuse, and a partial final warp.
- `divergence`: covers uniform and nested divergent branches and path restoration.

Build every image:

```sh
make image
```

Run every image on the backend selected by MEMU menuconfig:

```sh
make run
```

Add `GPU_TRACE=1 GPU_TRACE_LIMIT=200` to enable `[GI]` instruction and `[GM]`
store tracing.
