RTL_FINAL := $(OPENGPU_HOME)/hw/verilog/vsrc/GPU.sv
RTL_SOURCES := $(RTL_FINAL) \
               $(OPENGPU_HOME)/hw/verilog/vsrc/KMU.sv \
               $(OPENGPU_HOME)/hw/verilog/vsrc/Riscv/Core.sv \
               $(OPENGPU_HOME)/hw/verilog/vsrc/Peripheral/Mem.sv
DPI_SOURCE := $(OPENGPU_HOME)/hw/verilog/vsrc/Util/DPI.v
VERILATOR_PARAMS := -GNUM_CORES=$(CONFIG_GPU_NUM_CORES) \
                    -GNUM_WARPS=$(CONFIG_GPU_NUM_WARPS) \
                    -GNUM_THREADS=$(CONFIG_GPU_NUM_THREADS)
