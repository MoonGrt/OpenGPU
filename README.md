# OpenGPU

OpenGPU 是一个用于验证参数化 SIMT GPU 核心的轻量工程。它提供独立维护的
Chisel、SpinalHDL 与 SystemVerilog RTL 后端，以及同一套 Native Runtime、
RV32E kernel ABI、PMEM、Verilator 和 C ISS DiffTest 链路。

## 快速开始

先检查工具链：

```sh
scripts/setup.sh check
```

选择一个后端并运行默认测试 `vecadd`：

```sh
make gpu_verilog_defconfig
make run
```

成功时会出现：

```text
[DIFF] C ISS and Core RTL PMEM match
[HOST] vecadd n=35: PASS
```

运行其他测试：

```sh
make run TEST=vecsub
make run TEST=divergence
make run TEST=topology
```

可用测试位于 `tests/`：`vecadd`、`vecsub`、`bitxor`、`sizes`、`topology`
和 `divergence`。

## 选择后端

| 后端 | 配置命令 | Scala 构建器 |
| --- | --- | --- |
| SystemVerilog | `make gpu_verilog_defconfig` | 不需要 |
| Chisel | `make gpu_chisel_defconfig` | `TOOL=mill` 或 `TOOL=sbt` |
| SpinalHDL | `make gpu_spinal_defconfig` | `TOOL=mill` 或 `TOOL=sbt` |
| C ISS | `make gpu_sm_defconfig` | 不需要 |

例如：

```sh
make gpu_chisel_defconfig
make TOOL=mill run

make gpu_spinal_defconfig
make TOOL=sbt run
```

`gpu_hm_defconfig` 保留为 Chisel RTL 的兼容别名。也可不修改 `.config`
临时选择硬件后端：

```sh
make BACKEND=spinal TOOL=mill rtl
make BACKEND=verilog verilate
```

通过菜单配置后端、构建器和 core/warp/thread 拓扑：

```sh
make menuconfig
```

## 构建入口

根目录 Makefile 负责配置、编排和测试：

```sh
make build                 # 构建 Runtime
make rtl                   # 生成选定后端的 RTL
make verilate              # 构建 Verilator 静态库
make run TEST=vecadd       # 构建并运行一个 kernel 测试
make wave TEST=vecadd      # 先运行测试，再用 GTKWAVE 打开波形
make clean-all             # 移除全部构建产物
```

子目录入口可在开发时单独使用：

```sh
make -C hw BACKEND=verilog verilate
make -C sw BACKEND=verilog runtime
make -C tests/vecadd run
```

## 目录结构

```text
hw/                         三套独立 RTL 后端
  chisel/                   Chisel 源码、build.mill、build.sbt
  spinal/                   SpinalHDL 源码、build.mill、build.sbt
  verilog/                  SystemVerilog 源码与 DPI 模块
  build/                    RTL、Verilator 和波形构建产物
sw/                         软件与 kernel 支持
  include/                  对 Host 测试公开的 Runtime API
  launcher.cpp              Native 前端统一启动包装器
  runtime/                  Runtime、C ISS、设备内存与 RTL launcher
  kernel/                   RV32E GPU kernel ABI、启动代码和链接脚本
  build/                    Runtime 静态库与对象文件
tests/                      kernel 程序及各自的 Host 验证器
tools/                      Kconfig 与依赖追踪工具
configs/                    可复现的 defconfig
source/                     OpenPeriph、Vortex、YSYX 等参考工程
```

`hw/build/`、`sw/build/` 和各测试目录的 `build/` 均为可删除的生成物。

## Runtime 与 kernel ABI

公开 Host API 在 [sw/include/runtime.h](sw/include/runtime.h)，前缀统一为
`gpu_`，涵盖设备打开/关闭、内存分配与读写、kernel 加载、launch 和 wait。

kernel 侧 ABI 位于 [sw/kernel/include/gpu.h](sw/kernel/include/gpu.h)，目标
架构为 `riscv32-gpu`。每个硬件线程的逻辑 hart ID 为：

```text
mhartid = core_id * NUM_WARPS * NUM_THREADS
         + warp_id * NUM_THREADS + thread_id
```

三种 RTL 共享顶层 ABI `GPUTop`，包括 `io_gpu_launch`、`io_gpu_busy`、
`io_gpu_done`、active-warp 与 issue 调试信号。硬件模型中，Runtime 会先以
C ISS 执行 kernel，再恢复 PMEM 执行 RTL，最后比较完整 PMEM；这就是测试
输出中的 DiffTest 结果。

## 依赖版本

Scala 后端统一使用：Scala 2.13.12、SBT 1.10.7、Mill 1.1.2、
ScalaTest 3.2.19。Chisel 版本为 6.7.0，SpinalHDL 版本为 1.12.0。

系统还需要 GCC/G++、Verilator、Java、Flex、Bison 以及
`riscv64-linux-gnu-*` 交叉工具链。`scripts/setup.sh` 提供 `check`、`deps`、
`java`、`mill`、`sbt`、`verilator`、`toolchain` 和 `all` 子命令。
