module GPUTop #(
  parameter integer NUM_CORES=2, NUM_WARPS=4, NUM_THREADS=4
) (
  input wire clock, input wire reset,
  input wire io_dcr_valid,
  input wire [11:0] io_dcr_addr,
  input wire [31:0] io_dcr_data,
  input wire io_gpu_launch,
  output wire io_gpu_busy,
  output wire io_gpu_fault,
  output wire [NUM_CORES-1:0] io_gpu_done,
  output wire [NUM_CORES*NUM_WARPS-1:0] io_gpu_active_warps,
  output wire [NUM_CORES*((NUM_WARPS<=1)?1:$clog2(NUM_WARPS))-1:0] io_gpu_issue_warp,
  output wire [NUM_CORES*NUM_THREADS-1:0] io_gpu_issue_mask
);
  wire [NUM_CORES-1:0] core_fault;
  wire kmu_busy;
  DcrIf dcr_if();
  KmuIf #(.NUM_CORES(NUM_CORES)) kmu_if();

  assign dcr_if.valid = io_dcr_valid;
  assign dcr_if.addr = io_dcr_addr;
  assign dcr_if.data = io_dcr_data;

  GPUKmu #(.NUM_CORES(NUM_CORES)) kmu (
    .clock(clock), .reset(reset),
    .dcr(dcr_if), .kmu_if(kmu_if),
    .launch(io_gpu_launch), .busy(kmu_busy));

  genvar i;
  generate for (i=0;i<NUM_CORES;i=i+1) begin: cores
    wire imem_ren;
    wire [31:0] imem_addr, imem_rdata;
    wire dmem_ren, dmem_wen;
    wire [3:0] dmem_mask;
    wire [31:0] dmem_addr, dmem_wdata, dmem_rdata;
    SimInstMem imem(
      .ren(imem_ren), .addr(imem_addr), .rdata(imem_rdata));
    SimDataMem dmem(
      .ren(dmem_ren), .wen(dmem_wen), .mask(dmem_mask),
      .addr(dmem_addr), .wdata(dmem_wdata), .rdata(dmem_rdata));
    GPUCore #(.CORE_ID(i),.WARPS(NUM_WARPS),.THREADS(NUM_THREADS)) core(
      .clock(clock), .reset(reset),
      .kmu_if(kmu_if),
      .imem_ren(imem_ren), .imem_addr(imem_addr), .imem_rdata(imem_rdata),
      .dmem_ren(dmem_ren), .dmem_wen(dmem_wen), .dmem_mask(dmem_mask),
      .dmem_addr(dmem_addr), .dmem_wdata(dmem_wdata), .dmem_rdata(dmem_rdata),
      .fault(core_fault[i]), .done(io_gpu_done[i]),
      .active_warps(io_gpu_active_warps[i*NUM_WARPS +: NUM_WARPS]),
      .issue_warp(io_gpu_issue_warp[i*((NUM_WARPS<=1)?1:$clog2(NUM_WARPS)) +: ((NUM_WARPS<=1)?1:$clog2(NUM_WARPS))]),
      .issue_mask(io_gpu_issue_mask[i*NUM_THREADS +: NUM_THREADS]));
  end endgenerate

  assign io_gpu_busy = kmu_busy;
  assign io_gpu_fault = |core_fault;
endmodule
