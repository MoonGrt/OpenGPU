NAME = sizes
SRCS = kernel.cpp
OPENGPU_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
export ARCH ?= riscv32-gpu
include $(OPENGPU_HOME)/sw/kernel/Makefile
