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
