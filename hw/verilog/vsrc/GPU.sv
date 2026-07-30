// SystemVerilog backend entry point.  Its hierarchy matches the Scala
// backends: GPU -> Riscv/Core + Peripheral/Mem + Util/DPI.
module GPUTop #(
  parameter integer NUM_CORES=2, NUM_WARPS=2, NUM_THREADS=2
) (
  input wire clock, input wire reset, input wire io_gpu_launch,
  output wire io_gpu_busy, output wire [NUM_CORES-1:0] io_gpu_done,
  output wire [NUM_CORES*NUM_WARPS-1:0] io_gpu_active_warps,
  output wire [NUM_CORES*((NUM_WARPS<=1)?1:$clog2(NUM_WARPS))-1:0] io_gpu_issue_warp,
  output wire [NUM_CORES*NUM_THREADS-1:0] io_gpu_issue_mask
);
  genvar i;
  generate for (i=0;i<NUM_CORES;i=i+1) begin: G_CORE
    GPUCore #(.CORE_ID(i),.WARPS(NUM_WARPS),.THREADS(NUM_THREADS)) core(
      .clock(clock),.reset(reset),.enable(io_gpu_launch),.done(io_gpu_done[i]),
      .active_warps(io_gpu_active_warps[i*NUM_WARPS +: NUM_WARPS]),
      .issue_warp(io_gpu_issue_warp[i*((NUM_WARPS<=1)?1:$clog2(NUM_WARPS)) +: ((NUM_WARPS<=1)?1:$clog2(NUM_WARPS))]),
      .issue_mask(io_gpu_issue_mask[i*NUM_THREADS +: NUM_THREADS]));
  end endgenerate
  assign io_gpu_busy = io_gpu_launch && !(&io_gpu_done);
endmodule
