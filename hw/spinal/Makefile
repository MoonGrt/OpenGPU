ROOT := $(abspath ..)
BACKEND := spinal
.PHONY: build rtl verilate test clean
build:; @$(MAKE) -C $(ROOT) BACKEND=$(BACKEND) build
rtl:; @$(MAKE) -C $(ROOT) BACKEND=$(BACKEND) rtl
verilate:; @$(MAKE) -C $(ROOT) BACKEND=$(BACKEND) verilate
test:; @$(MAKE) -C $(ROOT) BACKEND=$(BACKEND) test
clean:; @$(MAKE) -C $(ROOT) clean
