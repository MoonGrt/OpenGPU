CHISEL_MAIN := TOP
SCALA_SRCS := $(shell find $(OPENGPU_HOME)/hw/chisel/src -name '*.scala')
RTL_SV := $(RTL_DIR)/GPUTop.sv
RTL_FINAL := $(RTL_DIR)/GPUTop.v

$(RTL_FINAL): $(SCALA_SRCS) $(OPENGPU_HOME)/.config
	@echo "+ CHISEL $@"
	@mkdir -p $(RTL_DIR)
	@if test "$(TOOL)" = sbt; then \
		cd $(OPENGPU_HOME)/hw/chisel && GPU_NUM_CORES=$(CONFIG_GPU_NUM_CORES) GPU_NUM_WARPS=$(CONFIG_GPU_NUM_WARPS) GPU_NUM_THREADS=$(CONFIG_GPU_NUM_THREADS) $(SBT) "runMain $(CHISEL_MAIN) --target-dir $(RTL_DIR)"; \
	else \
		cd $(OPENGPU_HOME)/hw/chisel && GPU_NUM_CORES=$(CONFIG_GPU_NUM_CORES) GPU_NUM_WARPS=$(CONFIG_GPU_NUM_WARPS) GPU_NUM_THREADS=$(CONFIG_GPU_NUM_THREADS) $(MILL) --no-server chisel.runMain $(CHISEL_MAIN) --target-dir $(RTL_DIR); \
	fi
	@mv $(RTL_SV) $@
	@sed -i '/firrtl_black_box_resource_files.f/,$$d' $@
