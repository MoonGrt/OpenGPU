OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS {
  . = 0x81000000;
  .text : {
    KEEP(*(entry))
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : {
    *(.bss*)
    *(.sbss*)
    *(COMMON)
  }
  ASSERT(. < 0x8100f000, "GPU image overlaps the launch ABI")

  __gpu_launch = 0x8100f000;
  __gpu_status = 0x8100f100;
}
