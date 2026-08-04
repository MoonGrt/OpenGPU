SPINAL_MAIN := gpu.spinal.GPUTop
SPINAL_SRCS := $(shell find $(OPENGPU_HOME)/hw/spinal/src -name '*.scala')
RTL_FINAL := $(RTL_DIR)/GPUTop.v

$(RTL_FINAL): $(SPINAL_SRCS) $(OPENGPU_HOME)/.config
	@echo "+ SPINAL $@"
	@mkdir -p $(RTL_DIR)
	@if test "$(TOOL)" = sbt; then \
		cd $(OPENGPU_HOME)/hw/spinal && GPU_NUM_CORES=$(CONFIG_GPU_NUM_CORES) GPU_NUM_WARPS=$(CONFIG_GPU_NUM_WARPS) GPU_NUM_THREADS=$(CONFIG_GPU_NUM_THREADS) SPINAL_TARGET_DIR=$(RTL_DIR) $(SBT) "runMain $(SPINAL_MAIN)"; \
	else \
		cd $(OPENGPU_HOME)/hw/spinal && GPU_NUM_CORES=$(CONFIG_GPU_NUM_CORES) GPU_NUM_WARPS=$(CONFIG_GPU_NUM_WARPS) GPU_NUM_THREADS=$(CONFIG_GPU_NUM_THREADS) SPINAL_TARGET_DIR=$(RTL_DIR) $(MILL) --no-server spinal.runMain $(SPINAL_MAIN); \
	fi
