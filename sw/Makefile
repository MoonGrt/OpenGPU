OPENGPU_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
export OPENGPU_HOME

-include $(OPENGPU_HOME)/.config
BACKEND ?= auto
TOOL ?= $(if $(CONFIG_SBT),sbt,mill)
BUILD_DIR := $(OPENGPU_HOME)/sw/build
OBJ_DIR := $(BUILD_DIR)/obj-runtime
RUNTIME_LIB := $(BUILD_DIR)/libgpu-runtime.a

ifeq ($(BACKEND),auto)
  ifeq ($(CONFIG_SPINAL),y)
    HDL_BACKEND := spinal
  else ifeq ($(CONFIG_VERILOG),y)
    HDL_BACKEND := verilog
  else
    HDL_BACKEND := chisel
  endif
else
  HDL_BACKEND := $(BACKEND)
endif
VBUILD := $(OPENGPU_HOME)/hw/build/verilated/$(HDL_BACKEND)
VLIB := $(VBUILD)/libVGPUTop.a
VROOT ?= $(shell verilator -V 2>/dev/null | sed -n 's/^[[:space:]]*VERILATOR_ROOT[[:space:]]*=[[:space:]]*//p' | head -1)

CC := gcc
CXX := g++
AR := ar
RUNTIME_C_SRCS := runtime/gpu/gpu.c runtime/gpu/gpu_iss.c runtime/device/memory.c
RUNTIME_CXX_SRCS := launcher.cpp
ifeq ($(CONFIG_HM),y)
RUNTIME_CXX_SRCS += runtime/rtl/exec.cc
endif
INC_PATH := $(OPENGPU_HOME)/sw/include $(OPENGPU_HOME)/sw/runtime \
            $(OPENGPU_HOME)/sw/runtime/gpu/include $(OPENGPU_HOME)/sw/runtime/gpu
VERILATOR_INC_FLAGS :=
ifeq ($(CONFIG_HM),y)
VERILATOR_INC_FLAGS := -isystem $(VROOT)/include -isystem $(VROOT)/include/vltstd -I$(VBUILD)
endif
HOST_CFLAGS := -MMD -Wall -Werror $(CONFIG_CC_OPT) $(addprefix -I,$(INC_PATH)) $(VERILATOR_INC_FLAGS)
HOST_CXXFLAGS := $(HOST_CFLAGS) -std=c++17
ifeq ($(CONFIG_WAVE),y)
HOST_CFLAGS += -DWAVEOUT=\"$(OPENGPU_HOME)/hw/build\"
HOST_CXXFLAGS += -DWAVEOUT=\"$(OPENGPU_HOME)/hw/build\"
endif
RUNTIME_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(RUNTIME_C_SRCS)) \
                $(patsubst %.cc,$(OBJ_DIR)/%.o,$(filter %.cc,$(RUNTIME_CXX_SRCS))) \
                $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(RUNTIME_CXX_SRCS)))

ifneq ($(filter y,$(CONFIG_FRONTEND_RISCV) $(CONFIG_FRONTEND_MIPS)),)
runtime:
	@echo "OpenGPU: selected frontend is not implemented" >&2
	@false
else
$(OBJ_DIR)/%.o: %.c
	@echo + CC $<
	@mkdir -p $(dir $@)
	@$(CC) $(HOST_CFLAGS) -c -o $@ $<
$(OBJ_DIR)/%.o: %.cc
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(HOST_CXXFLAGS) -c -o $@ $<
$(OBJ_DIR)/%.o: %.cpp
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(HOST_CXXFLAGS) -c -o $@ $<
ifeq ($(CONFIG_HM),y)
$(OBJ_DIR)/runtime/rtl/exec.o: $(VLIB)
$(VLIB):
	$(MAKE) -C $(OPENGPU_HOME)/hw BACKEND=$(HDL_BACKEND) TOOL=$(TOOL) verilate
endif
$(RUNTIME_LIB): $(RUNTIME_OBJS)
	@echo + AR $@
	@mkdir -p $(dir $@)
	@$(AR) rcs $@ $(RUNTIME_OBJS)
runtime: $(RUNTIME_LIB)
endif

clean:
	rm -rf $(BUILD_DIR)

-include $(RUNTIME_OBJS:.o=.d)
.PHONY: runtime clean
