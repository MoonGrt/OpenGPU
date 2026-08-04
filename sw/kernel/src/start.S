.section entry, "ax"
.globl _start
_start:
  csrr t0, 0xccd
  lui  sp, 0x81800
  slli t1, t0, 12
  sub  sp, sp, t1
  csrr a0, 0xccc
  call kernel_main
  ebreak
