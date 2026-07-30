TESTS := vecadd vecsub bitxor sizes topology divergence
GPU_TRACE_LIMIT ?= 10000
OPENGPU_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
export OPENGPU_HOME

.PHONY: all image run clean $(TESTS)

all image: $(TESTS)

$(TESTS):
	$(MAKE) -C $@ OPENGPU_HOME=$(OPENGPU_HOME) ARCH=riscv32-gpu image

run:
	@set -e; for tests in $(TESTS); do \
	  $(MAKE) -C $$tests OPENGPU_HOME=$(OPENGPU_HOME) \
	    ARCH=riscv32-gpu run GPU_TRACE=$(GPU_TRACE) \
	    GPU_TRACE_LIMIT=$(GPU_TRACE_LIMIT); \
	done

clean:
	@for tests in $(TESTS); do $(MAKE) --no-print-directory -C $$tests clean; done
