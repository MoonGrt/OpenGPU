// Keep the memory-facing structure identical to the Scala backends while the
// DPI implementation stays isolated in Util/DPI.
module SimDpiMem(
  input wire ren,
  input wire wen,
  input wire [7:0] mask,
  input wire [31:0] addr,
  input wire [31:0] wdata,
  output wire [31:0] rdata
);
  DpiMem dpi_mem(
    .ren(ren), .wen(wen), .mask(mask), .addr(addr), .wdata(wdata), .rdata(rdata)
  );
endmodule
