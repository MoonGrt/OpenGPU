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

// Instruction memory belongs to the GPU integration layer, not to a core.
module SimInstMem(
  input wire ren,
  input wire [31:0] addr,
  output wire [31:0] rdata
);
  SimDpiMem mem(
    .ren(ren), .wen(1'b0), .mask(8'b0),
    .addr(addr), .wdata(32'b0), .rdata(rdata)
  );
endmodule

// Data memory is integrated above the core and preserves byte-write masks.
module SimDataMem(
  input wire ren,
  input wire wen,
  input wire [3:0] mask,
  input wire [31:0] addr,
  input wire [31:0] wdata,
  output wire [31:0] rdata
);
  SimDpiMem mem(
    .ren(ren), .wen(wen), .mask({4'b0, mask}),
    .addr(addr), .wdata(wdata), .rdata(rdata)
  );
endmodule
