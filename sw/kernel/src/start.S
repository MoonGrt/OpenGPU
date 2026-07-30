.section entry, "ax"
.globl _start
_start:
  csrr t0, mhartid
  lui  sp, 0x81800
  slli t1, t0, 12
  sub  sp, sp, t1
  lui  a1, 0x8100f
  addi a0, a1, 0x100
  call kernel_main
  ebreak
