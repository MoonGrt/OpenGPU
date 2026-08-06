import "DPI-C" function int dpi_paddr_read(input int addr);
import "DPI-C" function void dpi_paddr_write(
  input int addr, input byte mask, input int data
);

module DpiMem(
  input wire ren,
  input wire wen,
  input wire [7:0] mask,
  input wire [31:0] addr,
  input wire [31:0] wdata,
  output reg [31:0] rdata
);
  always @(*) begin
    rdata = ren ? dpi_paddr_read(addr) : 0;
    if (wen) dpi_paddr_write(addr, mask, wdata);
  end
endmodule

import "DPI-C" function void gpu_trace_commit(
  input int hartid, input int pc, input int inst
);
module DpiGpuTraceBB(
  input clk, en,
  input [31:0] hartid, pc, inst
);
  always @(posedge clk)
    if (en) gpu_trace_commit(hartid, pc, inst);
endmodule

import "DPI-C" function void gpu_trace_store(
  input int hartid, input int addr, input int mask, input int data
);
module DpiGpuStoreTraceBB(
  input clk, en,
  input [31:0] hartid, addr, mask, data
);
  always @(posedge clk)
    if (en) gpu_trace_store(hartid, addr, mask, data);
endmodule

// Keep the normalized DiffTest state inside RTL. As in source/YSYX/memu's
// GPR/CSR bridges, an unpacked int array is copied into the C simulator by DPI.
`ifdef CONFIG_DIFFTEST
import "DPI-C" function void gpu_diff_state(input int base, input int state[]);
module DpiGpuStateBB #(
  parameter integer WORDS = 1,
  parameter integer BASE = 0
) (
  input wire [WORDS*32-1:0] state
);
  int state_words [0:WORDS-1];
  integer word;

  always @(*) begin
    for (word = 0; word < WORDS; word = word + 1)
      state_words[word] = state[word*32 +: 32];
  end

  always @(*) gpu_diff_state(BASE, state_words);
endmodule
`endif
