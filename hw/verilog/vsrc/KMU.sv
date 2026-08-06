module GPUKmu #(
  parameter integer NUM_CORES=2,
  parameter integer CB=(NUM_CORES<=1)?1:$clog2(NUM_CORES)
) (
  input wire clock, input wire reset,
  DcrIf.slave dcr,
  input wire launch,
  KmuIf.master kmu_if,
  output wire busy
);
  reg running;
  reg [CB-1:0] rr_core;
  reg selected_valid;
  reg [CB-1:0] selected_core;
  integer i, candidate;

  always @(*) begin
    kmu_if.cta_valid = 0;
    selected_valid = 0;
    selected_core = 0;
    for (i=0; i<NUM_CORES; i=i+1) begin
      candidate = (int'(rr_core) + i) % NUM_CORES;
      if (!selected_valid && kmu_if.core_ready[candidate]) begin
        selected_valid = 1;
        selected_core = candidate[CB-1:0];
      end
    end
    if (running && selected_valid) kmu_if.cta_valid[selected_core] = 1'b1;
  end

  wire cta_fire = running && selected_valid && kmu_if.core_ready[selected_core];
  wire last_x = kmu_if.block_idx_x + 1 == kmu_if.grid_dim_x;
  wire last_y = kmu_if.block_idx_y + 1 == kmu_if.grid_dim_y;
  wire last_z = kmu_if.block_idx_z + 1 == kmu_if.grid_dim_z;

  always @(posedge clock) begin
    if (reset) begin
      kmu_if.startup_pc <= 32'h81000000; kmu_if.args_addr <= 0; kmu_if.args_size <= 0;
      kmu_if.block_dim_x <= 1; kmu_if.block_dim_y <= 1; kmu_if.block_dim_z <= 1;
      kmu_if.grid_dim_x <= 1; kmu_if.grid_dim_y <= 1; kmu_if.grid_dim_z <= 1;
      kmu_if.block_size <= 1; kmu_if.block_idx_x <= 0; kmu_if.block_idx_y <= 0; kmu_if.block_idx_z <= 0;
      running <= 0; rr_core <= 0;
    end else begin
      if (dcr.valid) begin
        case (dcr.addr)
          12'h010: kmu_if.startup_pc <= dcr.data;
          12'h011: kmu_if.args_addr <= dcr.data;
          12'h012: kmu_if.args_size <= dcr.data;
          12'h013: kmu_if.block_dim_x <= dcr.data;
          12'h014: kmu_if.block_dim_y <= dcr.data;
          12'h015: kmu_if.block_dim_z <= dcr.data;
          12'h016: kmu_if.grid_dim_x <= dcr.data;
          12'h017: kmu_if.grid_dim_y <= dcr.data;
          12'h018: kmu_if.grid_dim_z <= dcr.data;
          12'h019: kmu_if.block_size <= dcr.data;
          default: ;
        endcase
      end
      if (launch) begin
        kmu_if.block_idx_x <= 0; kmu_if.block_idx_y <= 0; kmu_if.block_idx_z <= 0;
        running <= kmu_if.block_size != 0 && kmu_if.grid_dim_x != 0 && kmu_if.grid_dim_y != 0 && kmu_if.grid_dim_z != 0;
      end else if (cta_fire) begin
        rr_core <= (selected_core == CB'(NUM_CORES-1)) ? CB'(0) : selected_core + 1'b1;
        if (last_x) begin
          kmu_if.block_idx_x <= 0;
          if (last_y) begin
            kmu_if.block_idx_y <= 0;
            if (last_z) begin kmu_if.block_idx_z <= 0; running <= 0; end
            else kmu_if.block_idx_z <= kmu_if.block_idx_z + 1;
          end else kmu_if.block_idx_y <= kmu_if.block_idx_y + 1;
        end else kmu_if.block_idx_x <= kmu_if.block_idx_x + 1;
      end
    end
  end

  assign busy = running || (|kmu_if.core_busy);
`ifdef CONFIG_DIFFTEST
  wire [20*32-1:0] diff_state;
  assign diff_state[0*32 +: 32] = kmu_if.startup_pc;
  assign diff_state[1*32 +: 32] = kmu_if.args_addr;
  assign diff_state[2*32 +: 32] = kmu_if.args_size;
  assign diff_state[3*32 +: 32] = kmu_if.block_dim_x;
  assign diff_state[4*32 +: 32] = kmu_if.block_dim_y;
  assign diff_state[5*32 +: 32] = kmu_if.block_dim_z;
  assign diff_state[6*32 +: 32] = kmu_if.grid_dim_x;
  assign diff_state[7*32 +: 32] = kmu_if.grid_dim_y;
  assign diff_state[8*32 +: 32] = kmu_if.grid_dim_z;
  assign diff_state[9*32 +: 32] = kmu_if.block_size;
  assign diff_state[10*32 +: 32] = kmu_if.block_idx_x;
  assign diff_state[11*32 +: 32] = kmu_if.block_idx_y;
  assign diff_state[12*32 +: 32] = kmu_if.block_idx_z;
  assign diff_state[13*32 +: 32] = {31'b0, running};
  assign diff_state[14*32 +: 32] = 32'(rr_core);
  assign diff_state[15*32 +: 32] = 32'(kmu_if.cta_valid);
  assign diff_state[16*32 +: 32] = 32'(kmu_if.core_ready);
  assign diff_state[17*32 +: 32] = 32'(kmu_if.core_busy);
  assign diff_state[18*32 +: 32] = {31'b0, busy};
  assign diff_state[19*32 +: 32] = 32'(selected_core) | (32'(selected_valid) << 31);
  DpiGpuStateBB #(.WORDS(20), .BASE(3)) diff_bridge(.state(diff_state));
`endif
  wire _unused = &{1'b0, kmu_if.args_size};
endmodule
