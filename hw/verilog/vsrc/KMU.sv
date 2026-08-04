module GPUKmu #(
  parameter integer NUM_CORES=2,
  parameter integer CB=(NUM_CORES<=1)?1:$clog2(NUM_CORES)
) (
  input wire clock, input wire reset,
  input wire dcr_valid, input wire [11:0] dcr_addr,
  input wire [31:0] dcr_data, input wire launch,
  input wire [NUM_CORES-1:0] core_ready,
  input wire [NUM_CORES-1:0] core_busy,
  output reg [NUM_CORES-1:0] cta_valid,
  output wire busy,
  output reg [31:0] startup_pc, args_addr, args_size,
  output reg [31:0] block_idx_x, block_idx_y, block_idx_z,
  output reg [31:0] block_dim_x, block_dim_y, block_dim_z,
  output reg [31:0] grid_dim_x, grid_dim_y, grid_dim_z,
  output reg [31:0] block_size
);
  reg running;
  reg [CB-1:0] rr_core;
  reg selected_valid;
  reg [CB-1:0] selected_core;
  integer i, candidate;

  always @(*) begin
    cta_valid = 0;
    selected_valid = 0;
    selected_core = 0;
    for (i=0; i<NUM_CORES; i=i+1) begin
      candidate = (int'(rr_core) + i) % NUM_CORES;
      if (!selected_valid && core_ready[candidate]) begin
        selected_valid = 1;
        selected_core = candidate[CB-1:0];
      end
    end
    if (running && selected_valid) cta_valid[selected_core] = 1'b1;
  end

  wire cta_fire = running && selected_valid && core_ready[selected_core];
  wire last_x = block_idx_x + 1 == grid_dim_x;
  wire last_y = block_idx_y + 1 == grid_dim_y;
  wire last_z = block_idx_z + 1 == grid_dim_z;

  always @(posedge clock) begin
    if (reset) begin
      startup_pc <= 32'h81000000; args_addr <= 0; args_size <= 0;
      block_dim_x <= 1; block_dim_y <= 1; block_dim_z <= 1;
      grid_dim_x <= 1; grid_dim_y <= 1; grid_dim_z <= 1;
      block_size <= 1; block_idx_x <= 0; block_idx_y <= 0; block_idx_z <= 0;
      running <= 0; rr_core <= 0;
    end else begin
      if (dcr_valid) begin
        case (dcr_addr)
          12'h010: startup_pc <= dcr_data;
          12'h011: args_addr <= dcr_data;
          12'h012: args_size <= dcr_data;
          12'h013: block_dim_x <= dcr_data;
          12'h014: block_dim_y <= dcr_data;
          12'h015: block_dim_z <= dcr_data;
          12'h016: grid_dim_x <= dcr_data;
          12'h017: grid_dim_y <= dcr_data;
          12'h018: grid_dim_z <= dcr_data;
          12'h019: block_size <= dcr_data;
          default: ;
        endcase
      end
      if (launch) begin
        block_idx_x <= 0; block_idx_y <= 0; block_idx_z <= 0;
        running <= block_size != 0 && grid_dim_x != 0 && grid_dim_y != 0 && grid_dim_z != 0;
      end else if (cta_fire) begin
        rr_core <= (selected_core == CB'(NUM_CORES-1)) ? CB'(0) : selected_core + 1'b1;
        if (last_x) begin
          block_idx_x <= 0;
          if (last_y) begin
            block_idx_y <= 0;
            if (last_z) begin block_idx_z <= 0; running <= 0; end
            else block_idx_z <= block_idx_z + 1;
          end else block_idx_y <= block_idx_y + 1;
        end else block_idx_x <= block_idx_x + 1;
      end
    end
  end

  assign busy = running || (|core_busy);
  wire _unused = &{1'b0, args_size};
endmodule
