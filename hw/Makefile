OPENGPU_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
export OPENGPU_HOME

-include $(OPENGPU_HOME)/.config
BACKEND ?= auto
TOOL ?= $(if $(CONFIG_SBT),sbt,mill)
SBT ?= sbt
MILL ?= mill

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

ifneq ($(filter $(HDL_BACKEND),verilog spinal chisel),$(HDL_BACKEND))
  $(error Unsupported BACKEND=$(BACKEND); use verilog, spinal, or chisel)
endif
ifneq ($(filter $(TOOL),mill sbt),$(TOOL))
  $(error Unsupported TOOL=$(TOOL); use mill or sbt)
endif

include $(OPENGPU_HOME)/hw/scripts/rtl.mk

clean:
	rm -rf $(OPENGPU_HOME)/hw/build

clean-all: clean
	rm -rf $(OPENGPU_HOME)/hw/chisel/out $(OPENGPU_HOME)/hw/spinal/out \
	       $(OPENGPU_HOME)/hw/chisel/build $(OPENGPU_HOME)/hw/spinal/build \
	       $(OPENGPU_HOME)/hw/chisel/target $(OPENGPU_HOME)/hw/spinal/target

.PHONY: clean clean-all
