interface KmuIf #(
  parameter integer NUM_CORES = 2
);
  logic [NUM_CORES-1:0] core_ready;
  logic [NUM_CORES-1:0] core_busy;
  logic [NUM_CORES-1:0] cta_valid;
  logic [31:0] startup_pc;
  logic [31:0] args_addr;
  logic [31:0] args_size;
  logic [31:0] block_idx_x, block_idx_y, block_idx_z;
  logic [31:0] block_dim_x, block_dim_y, block_dim_z;
  logic [31:0] grid_dim_x, grid_dim_y, grid_dim_z;
  logic [31:0] block_size;

  modport master(
    input core_ready, core_busy,
    output cta_valid, startup_pc, args_addr, args_size,
      block_idx_x, block_idx_y, block_idx_z,
      block_dim_x, block_dim_y, block_dim_z,
      grid_dim_x, grid_dim_y, grid_dim_z, block_size
  );
  modport slave(
    output core_ready, core_busy,
    input cta_valid, startup_pc, args_addr, args_size,
      block_idx_x, block_idx_y, block_idx_z,
      block_dim_x, block_dim_y, block_dim_z,
      grid_dim_x, grid_dim_y, grid_dim_z, block_size
  );
endinterface
