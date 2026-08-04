# KMU DCR and Warp-SIMT Issue Log

## I-001 — Pre-existing working-tree changes

- **Stage/backend:** Initial state, SystemVerilog and Chisel
- **Symptom:** Both Core sources contained uncommitted formatting/comment changes before implementation began.
- **Reproduction:** `git diff -- hw/verilog/vsrc/Riscv/Core.sv hw/chisel/src/main/scala/Riscv/Core.scala`
- **Root cause:** User-owned edits already present in the shared worktree.
- **Resolution:** Preserve the edits and replace/refactor only the surrounding implementation required by this project.
- **Validation:** Review the final diff to ensure unrelated `.devcontainer` content remains untouched.
- **Status:** Resolved; user-owned `.devcontainer/README.md` remains untouched.

## I-002 — Verilator width warnings are fatal

- **Stage/backend:** SystemVerilog elaboration
- **Symptom:** Verilator rejected otherwise legal comparisons between parameter-sized integers and narrow warp/core indices.
- **Reproduction:** `make BACKEND=verilog verilate`
- **Root cause:** The project treats Verilator width warnings as fatal.
- **Resolution:** Add explicit `WB'(...)`, `TB'(...)`, and `CB'(...)` casts at parameter-width boundaries and convert indices to `int` only for loop arithmetic.
- **Validation:** `make BACKEND=verilog verilate`
- **Status:** Resolved.

## I-003 — Dynamic ISS launch exceeds physical context count

- **Stage/backend:** Runtime/C ISS
- **Symptom:** A grid larger than the number of physical cores cannot fit the original fixed `GPU_NUM_HARTS` ISS array.
- **Reproduction:** Launch `vecadd` with 35 elements on the default 8 physical lanes.
- **Root cause:** The old ISS modeled one permanent context per physical lane, while KMU reuses physical lanes across multiple CTAs.
- **Resolution:** Allocate reference contexts for the launch-global thread count and attach block/thread/CSR metadata to every functional ISS context. RTL remains the timing/resource model.
- **Validation:** Full SystemVerilog PMEM DiffTest suite, including grids larger than the core count.
- **Status:** Resolved; the ISS is a functional oracle rather than a cycle model.

## I-004 — Runtime Verilator object reused across HDL backends

- **Stage/backend:** Cross-backend regression
- **Symptom:** After running SpinalHDL, a SystemVerilog topology launch completed without executing any CTA and DiffTest reported untouched output memory.
- **Reproduction:** Run a SpinalHDL test, then `make BACKEND=verilog -C tests/topology run` without cleaning `sw/build`.
- **Root cause:** All backends shared `sw/build/obj-runtime`; `exec.cc` was therefore compiled against one backend's generated `VGPUTop.h` and later linked to another backend's model. The software build also treated an existing Verilator archive as source-independent. Generated class layouts are not ABI-compatible even when their logical ports match.
- **Resolution:** Isolate Runtime objects under `obj-runtime/<backend>`, produce a backend-specific Runtime archive, force the hardware makefile to check Scala/RTL prerequisites before compiling the harness, and give every backend the common top-level clock name `clock`.
- **Validation:** Run the complete test suite in the order SystemVerilog → Chisel → SpinalHDL → SystemVerilog without cleaning between backends.
- **Status:** Resolved; cross-backend regression passed.

## I-005 — Scala RTL did not regenerate after topology changes

- **Stage/backend:** Chisel/SpinalHDL configuration matrix
- **Symptom:** Runtime reported a 1×1×1 topology while generated Chisel RTL still contained the previous 2×2×2 parameters, producing a topology DiffTest mismatch.
- **Reproduction:** Change only `CONFIG_GPU_NUM_CORES/WARPS/THREADS`, sync Kconfig, then run a Scala backend without deleting generated RTL.
- **Root cause:** The generated Chisel and SpinalHDL top files depended on Scala sources but not `.config`.
- **Resolution:** Add `.config` as a prerequisite of both Scala-generated RTL targets.
- **Validation:** Run the full three-backend suite at 1 Core × 1 Warp × 1 Lane, restore 2×2×2, and repeat.
- **Status:** Resolved; 1×1×1 and restored 2×2×2 regressions passed.

## I-006 — Stale Verilator objects remained after topology regeneration

- **Stage/backend:** Chisel 1×1×1 regression
- **Symptom:** Native linking failed with duplicate `VGPUTop` definitions after regenerating a smaller model.
- **Reproduction:** Verilate 2×2×2, change to 1×1×1, regenerate into the same backend build directory, and link the Runtime.
- **Root cause:** Verilator changed between split objects and `VGPUTop__ALL.o`; the old object files and archive members were retained because `ar rcs` updates rather than replaces an archive.
- **Resolution:** Recreate the backend-specific Verilator directory and archive whenever the model target is rebuilt.
- **Validation:** Repeat topology changes in both directions without `make clean`.
- **Status:** Resolved; topology transitions passed without cleaning.

## I-007 — Wide SpinalHDL model exceeded parallel compile memory

- **Stage/backend:** SpinalHDL 1×1×16 fault test
- **Symptom:** Verilator C++ compilation was terminated while building the wide generated Core.
- **Reproduction:** Generate 1 Core × 1 Warp × 16 Lanes and compile with Verilator `-j 8` in the constrained build environment.
- **Root cause:** Lane-unrolled generated C++ and waveform support created a high parallel compiler memory peak.
- **Resolution:** Set the shared Verilator compilation job count to one. The resulting model is unchanged; only build parallelism is reduced.
- **Validation:** `make BACKEND=spinal TOOL=mill -C tests/sizes run` at 1×1×16.
- **Status:** Resolved.
