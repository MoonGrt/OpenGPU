module GPUTop #(
  parameter integer NUM_CORES=2, NUM_WARPS=2, NUM_THREADS=2
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
  wire [NUM_CORES-1:0] cta_valid, cta_ready, core_busy, core_fault;
  wire [31:0] startup_pc, args_addr, args_size, block_size;
  wire [31:0] block_idx_x, block_idx_y, block_idx_z;
  wire [31:0] block_dim_x, block_dim_y, block_dim_z;
  wire [31:0] grid_dim_x, grid_dim_y, grid_dim_z;
  wire kmu_busy;

  GPUKmu #(.NUM_CORES(NUM_CORES)) kmu (
    .clock(clock), .reset(reset),
    .dcr_valid(io_dcr_valid), .dcr_addr(io_dcr_addr), .dcr_data(io_dcr_data),
    .launch(io_gpu_launch), .core_ready(cta_ready), .core_busy(core_busy),
    .cta_valid(cta_valid), .busy(kmu_busy),
    .startup_pc(startup_pc), .args_addr(args_addr), .args_size(args_size),
    .block_idx_x(block_idx_x), .block_idx_y(block_idx_y), .block_idx_z(block_idx_z),
    .block_dim_x(block_dim_x), .block_dim_y(block_dim_y), .block_dim_z(block_dim_z),
    .grid_dim_x(grid_dim_x), .grid_dim_y(grid_dim_y), .grid_dim_z(grid_dim_z),
    .block_size(block_size));

  genvar i;
  generate for (i=0;i<NUM_CORES;i=i+1) begin: G_CORE
    GPUCore #(.CORE_ID(i),.WARPS(NUM_WARPS),.THREADS(NUM_THREADS)) core(
      .clock(clock), .reset(reset),
      .cta_valid(cta_valid[i]), .cta_ready(cta_ready[i]),
      .cta_startup_pc(startup_pc), .cta_args_addr(args_addr),
      .cta_block_idx_x(block_idx_x), .cta_block_idx_y(block_idx_y),
      .cta_block_idx_z(block_idx_z), .cta_block_dim_x(block_dim_x),
      .cta_block_dim_y(block_dim_y), .cta_block_dim_z(block_dim_z),
      .cta_grid_dim_x(grid_dim_x), .cta_grid_dim_y(grid_dim_y),
      .cta_grid_dim_z(grid_dim_z), .cta_block_size(block_size),
      .busy(core_busy[i]), .fault(core_fault[i]), .done(io_gpu_done[i]),
      .active_warps(io_gpu_active_warps[i*NUM_WARPS +: NUM_WARPS]),
      .issue_warp(io_gpu_issue_warp[i*((NUM_WARPS<=1)?1:$clog2(NUM_WARPS)) +: ((NUM_WARPS<=1)?1:$clog2(NUM_WARPS))]),
      .issue_mask(io_gpu_issue_mask[i*NUM_THREADS +: NUM_THREADS]));
  end endgenerate

  assign io_gpu_busy = kmu_busy || (|core_busy);
  assign io_gpu_fault = |core_fault;
endmodule
