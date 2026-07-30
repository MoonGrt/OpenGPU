OPENGPU_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export OPENGPU_HOME

-include $(OPENGPU_HOME)/.config
include $(OPENGPU_HOME)/scripts/config.mk

BACKEND ?= auto
TOOL ?= $(if $(CONFIG_SBT),sbt,mill)
TEST ?= vecadd
TEST_DIR := $(OPENGPU_HOME)/tests/$(TEST)
SUPPORTED_BACKENDS := auto verilog spinal chisel

ifneq ($(filter $(BACKEND),$(SUPPORTED_BACKENDS)),$(BACKEND))
  $(error Unsupported BACKEND=$(BACKEND); use verilog, spinal, or chisel)
endif
ifneq ($(filter $(TOOL),mill sbt),$(TOOL))
  $(error Unsupported TOOL=$(TOOL); use mill or sbt)
endif

.DEFAULT_GOAL := build

build app runtime:
	$(MAKE) -C $(OPENGPU_HOME)/sw BACKEND=$(BACKEND) TOOL=$(TOOL) runtime

rtl verilate:
	$(MAKE) -C $(OPENGPU_HOME)/hw BACKEND=$(BACKEND) TOOL=$(TOOL) $@

run test:
	@test -f $(TEST_DIR)/Makefile || { \
	  echo "Unknown TEST=$(TEST); choose a directory under tests/" >&2; exit 2; }
	$(MAKE) -C $(TEST_DIR) OPENGPU_HOME=$(OPENGPU_HOME) \
	  BACKEND=$(BACKEND) TOOL=$(TOOL) ARCH=riscv32-gpu run \
	  GPU_TRACE=$(GPU_TRACE) GPU_TRACE_LIMIT=$(GPU_TRACE_LIMIT) ARGS="$(ARGS)"

# A waveform is produced by the Runtime during a test run, not by RTL
# elaboration alone.  Run the selected test first, then open its VCD.
wave: run
	@test -f $(OPENGPU_HOME)/hw/build/wave.vcd || { \
	  echo "No waveform was generated; enable CONFIG_WAVE in menuconfig." >&2; exit 2; }
	$(MAKE) -C $(OPENGPU_HOME)/hw BACKEND=$(BACKEND) TOOL=$(TOOL) wave

clean:
	$(MAKE) -C $(OPENGPU_HOME)/sw clean
	$(MAKE) -C $(OPENGPU_HOME)/sw/kernel clean
	$(MAKE) -C $(OPENGPU_HOME)/tests clean

clean-all: clean
	$(MAKE) -C $(OPENGPU_HOME)/hw clean-all
	find $(OPENGPU_HOME)/tools $(OPENGPU_HOME)/sw/kernel $(OPENGPU_HOME)/tests \
	  -type d -name build -prune -exec $(RM) -r {} +
	@echo "All workspace build artifacts have been removed."

.PHONY: build app runtime rtl verilate wave run test clean clean-all
