# RV32E GPU tests

Every case follows the Vortex-style split: `main.cpp` runs in the Native
frontend, `common.h` defines the launch arguments, and `kernel.cpp` runs on the
RV32E GPU. The host allocates and uploads buffers, launches the kernel, reads
the result back, and validates it.

Current cases:

- `vecadd`: 35-element integer addition, exercising an irregular work size.
- `vecsub`: 17-element integer subtraction.
- `bitxor`: 19-element bitwise XOR with boundary-pattern inputs.
- `sizes`: repeated vecadd launches at N=1, 2, 3, 35, and 64, plus Runtime
  allocator and error-path checks.
- `topology`: validates the core/warp/thread decomposition of every `mhartid`.
- `divergence`: sends even and odd lanes down different control-flow paths.

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
