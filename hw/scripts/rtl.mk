# Shared Verilator/DPI build.  Each HDL backend supplies RTL_FINAL and its
# source prerequisites through hw/scripts/<backend>.mk.
GTKWAVE ?= gtkwave
VERILATOR ?= verilator
VTOP := GPUTop
HW_BUILD_DIR := $(OPENGPU_HOME)/hw/build

ifndef HDL_BACKEND
  ifeq ($(CONFIG_SPINAL),y)
    HDL_BACKEND := spinal
  else ifeq ($(CONFIG_VERILOG),y)
    HDL_BACKEND := verilog
  else
    HDL_BACKEND := chisel
  endif
endif

WAVE_VARIANT := hw-$(HDL_BACKEND)
WAVE_FILE := $(HW_BUILD_DIR)/wave/$(WAVE_VARIANT)/wave.vcd

RTL_DIR := $(HW_BUILD_DIR)/rtl/$(HDL_BACKEND)
include $(OPENGPU_HOME)/hw/scripts/$(HDL_BACKEND).mk
RTL_TOP ?= $(VTOP)
VERILATOR_PREFIX ?= V$(VTOP)

# Isolate generated C++ objects per source backend.  The same Verilator top
# name is intentionally used by all three implementations.
VBUILD := $(HW_BUILD_DIR)/verilated/$(HDL_BACKEND)
VLIB := $(VBUILD)/libV$(VTOP).a

VERILATOR_CFLAGS := -cc -MMD -O3 --x-assign fast --x-initial fast \
                    --timescale "1ns/1ns" --autoflush --unroll-count 1024 -j 1
ifeq ($(CONFIG_WAVE),y)
VERILATOR_CFLAGS += --trace
endif
RTL_SOURCES ?= $(RTL_FINAL)
DPI_SOURCE ?= $(OPENGPU_HOME)/hw/verilog/vsrc/Util/DPI.v
VSRCS := $(RTL_SOURCES) $(DPI_SOURCE)
VROOT ?= $(shell $(VERILATOR) -V 2>/dev/null | \
          sed -n 's/^[[:space:]]*VERILATOR_ROOT[[:space:]]*=[[:space:]]*//p' | head -1)
INC_PATH += $(VROOT)/include $(VROOT)/include/vltstd $(VBUILD)

$(VLIB): $(RTL_SOURCES) $(OPENGPU_HOME)/.config $(DPI_SOURCE) \
         $(OPENGPU_HOME)/hw/scripts/rtl.mk \
         $(OPENGPU_HOME)/hw/scripts/$(HDL_BACKEND).mk
	@echo "+ VERILATE $(HDL_BACKEND) $<"
	@$(RM) -r $(VBUILD)
	@mkdir -p $(VBUILD)
	$(VERILATOR) $(VERILATOR_CFLAGS) $(VERILATOR_PARAMS) $(VSRCS) \
	  --top-module $(RTL_TOP) --prefix $(VERILATOR_PREFIX) --Mdir $(VBUILD)
	$(MAKE) -C $(VBUILD) -f V$(VTOP).mk
	$(MAKE) -C $(VBUILD) -f V$(VTOP).mk verilated.o verilated_dpi.o \
	  $(if $(CONFIG_WAVE),verilated_vcd_c.o)
	# Some Verilator releases put support objects beside --Mdir while others
	# place them in its parent.  wildcard suppresses a non-existent glob so ar
	# never receives a literal `verilated*.o` filename.
	$(RM) $@
	ar rcs $@ $(VBUILD)/*.o $(wildcard $(VBUILD)/../verilated*.o)

rtl: $(RTL_FINAL)
verilate: $(VLIB)
wave:
	@test -f $(WAVE_FILE) || { \
	  echo "No waveform found at $(WAVE_FILE); enable CONFIG_WAVE and run a hardware test first." >&2; \
	  exit 2; \
	}
	$(GTKWAVE) $(WAVE_FILE) >/dev/null 2>&1 &

.PHONY: rtl verilate wave
