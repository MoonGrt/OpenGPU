ARCH ?= riscv32-gpu
KERNEL_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OPENGPU_HOME ?= $(abspath $(KERNEL_DIR)/../..)
export OPENGPU_HOME
ifneq ($(ARCH),riscv32-gpu)
  $(error Only ARCH=riscv32-gpu is supported)
endif
ifeq ($(wildcard $(KERNEL_DIR)/include/gpu.h),)
  $(error $(KERNEL_DIR) is not a GPU kernel runtime)
endif

WORK_DIR := $(shell pwd)
DST_DIR := $(WORK_DIR)/build/$(ARCH)
IMAGE_REL := build/$(NAME)-$(ARCH)
IMAGE := $(abspath $(IMAGE_REL))
KERNEL_BUILD_DIR := $(KERNEL_DIR)/build/$(ARCH)
KERNEL_STARTUP := $(KERNEL_DIR)/src/start.S
KERNEL_STARTUP_OBJ := $(KERNEL_BUILD_DIR)/start.o
KERNEL_LIB := $(KERNEL_DIR)/build/libgpu-$(ARCH).a

# RV32E GPU kernel toolchain and platform settings.
CROSS_COMPILE := riscv64-linux-gnu-
COMMON_CFLAGS := -fno-pic -march=rv32e_zicsr -mabi=ilp32e \
                 -ffreestanding -fno-builtin -msmall-data-limit=0
CFLAGS += $(COMMON_CFLAGS) -I$(KERNEL_DIR)/include
CXXFLAGS += $(COMMON_CFLAGS)
ASFLAGS += $(COMMON_CFLAGS)
LDFLAGS += -melf32lriscv --gc-sections -e _start
LDSCRIPTS += $(KERNEL_DIR)/scripts/linker-gpu.ld

AS := $(CROSS_COMPILE)gcc
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
OBJDUMP := $(CROSS_COMPILE)objdump
OBJCOPY := $(CROSS_COMPILE)objcopy

INC_PATH += $(WORK_DIR)/include $(KERNEL_DIR)/include
INCFLAGS := $(addprefix -I,$(INC_PATH))
CFLAGS += -O2 -MMD -Wall -Werror $(INCFLAGS) -fno-stack-protector
CXXFLAGS += $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS += -MMD $(INCFLAGS)
LDFLAGS += -z noexecstack $(addprefix -T,$(LDSCRIPTS))

OBJS := $(addprefix $(DST_DIR)/,$(addsuffix .o,$(basename $(SRCS))))

$(DST_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo + CC $<
	@$(CC) -std=gnu11 $(CFLAGS) -c -o $@ $(realpath $<)

$(DST_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo + CXX $<
	@$(CXX) -std=gnu++17 $(CXXFLAGS) -c -o $@ $(realpath $<)

$(DST_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo + AS $<
	@$(AS) $(ASFLAGS) -c -o $@ $(realpath $<)

-include $(OBJS:.o=.d)

$(KERNEL_STARTUP_OBJ): $(KERNEL_STARTUP)
	@mkdir -p $(dir $@)
	@echo + AS $<
	@$(AS) $(ASFLAGS) -c -o $@ $<

$(KERNEL_LIB): $(KERNEL_STARTUP_OBJ)
	@mkdir -p $(dir $@)
	@echo + AR $@
	@$(AR) rcs $@ $^

$(IMAGE).elf: $(OBJS) $(KERNEL_LIB) $(LDSCRIPTS)
	@mkdir -p $(dir $@)
	@echo + LD $@
	@$(LD) $(LDFLAGS) -o $@ --start-group $(OBJS) $(KERNEL_LIB) --end-group

image-dep: $(IMAGE).elf
image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).asm
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -O binary $(IMAGE).elf $(IMAGE).bin

-include $(OPENGPU_HOME)/.config
BACKEND ?= auto
TOOL ?= $(if $(CONFIG_SBT),sbt,mill)

HOST_CXX ?= g++
HOST_BUILD_DIR := $(WORK_DIR)/build/native
HOST_MAIN_OBJ := $(HOST_BUILD_DIR)/main.o
HOST_BINARY := $(WORK_DIR)/build/$(NAME)-native
RUNTIME_LIB := $(OPENGPU_HOME)/sw/build/libgpu-runtime.a
ifneq ($(BACKEND),auto)
 RUNTIME_HDL_BACKEND := $(BACKEND)
else ifeq ($(CONFIG_SPINAL),y)
 RUNTIME_HDL_BACKEND := spinal
else ifeq ($(CONFIG_VERILOG),y)
 RUNTIME_HDL_BACKEND := verilog
else
 RUNTIME_HDL_BACKEND := chisel
endif
RUNTIME_VLIB := $(if $(CONFIG_HM),$(OPENGPU_HOME)/hw/build/verilated/$(RUNTIME_HDL_BACKEND)/libVGPUTop.a)
GPU_TRACE_LIMIT ?= 10000
GPU_TRACE_ARGS := $(if $(GPU_TRACE),--gpu-trace --gpu-trace-limit $(GPU_TRACE_LIMIT))

$(RUNTIME_LIB): force-runtime
	@$(MAKE) -s -C $(OPENGPU_HOME)/sw BACKEND=$(BACKEND) TOOL=$(TOOL) runtime

force-runtime:

$(HOST_MAIN_OBJ): main.cpp common.h
	@mkdir -p $(dir $@)
	@echo + HOST-CXX $<
	@$(HOST_CXX) -std=c++17 -O2 -MMD -Wall -Werror \
	  -I$(OPENGPU_HOME)/sw/include -Dmain=frontend_main -c -o $@ $<

$(HOST_BINARY): $(HOST_MAIN_OBJ) $(RUNTIME_LIB)
	@echo + HOST-LD $@
	@$(HOST_CXX) -o $@ $(HOST_MAIN_OBJ) $(RUNTIME_LIB) $(RUNTIME_VLIB)

host: $(HOST_BINARY)

run: image host
	$(HOST_BINARY) $(GPU_TRACE_ARGS) -k $(IMAGE).bin $(ARGS)

clean:
	rm -rf $(WORK_DIR)/build

-include $(HOST_MAIN_OBJ:.o=.d)

.PHONY: image image-dep host run clean force-runtime
