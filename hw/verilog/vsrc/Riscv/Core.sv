// Independent SystemVerilog implementation of the compact OpenGPU RV32E SIMT
// core.  Every context has a PC and 16 registers; an instruction is issued to
// all non-halted lanes in a warp that share the selected PC.
module GPUCore #(
  parameter integer CORE_ID = 0, WARPS = 2, THREADS = 2,
  parameter [31:0] RESET_PC = 32'h8100_0000
) (
  input wire clock, input wire reset, input wire enable,
  output wire done, output wire [WARPS-1:0] active_warps,
  output reg [((WARPS <= 1) ? 1 : $clog2(WARPS))-1:0] issue_warp,
  output reg [THREADS-1:0] issue_mask
);
  localparam CTX = WARPS * THREADS;
  localparam WB = (WARPS <= 1) ? 1 : $clog2(WARPS);
  localparam TB = (THREADS <= 1) ? 1 : $clog2(THREADS);
  // Keep the DPI request asserted for a full cycle, then consume its response
  // on the following cycle.  This matches the request/response timing used by
  // the Chisel memory adapter and avoids sampling a stale combinational DPI
  // read when consecutive lane accesses are issued.
  localparam SCHED=0, FETCH=1, EXEC=2, MEM_REQ=3, MEM_RESP=4;
  reg [2:0] state;
  reg [WB-1:0] rr_warp;
  reg [TB-1:0] mem_thread;
  reg [31:0] pcs [0:CTX-1];
  reg halted [0:CTX-1];
  reg [31:0] gprs [0:CTX*16-1];
  reg [31:0] issue_pc, issue_inst;
  reg [31:0] mem_rdata;
  reg [31:0] imem_addr, dmem_addr, dmem_wdata;
  reg [3:0] dmem_mask;
  reg dmem_ren, dmem_wen;
  wire [31:0] imem_rdata, dmem_rdata;
  integer i, t, w, c, base, selected, selected_t, lane, rbase;
  reg found;
  reg [31:0] a, b, result, next_pc, imm_i, imm_s, imm_b, imm_u, imm_j;
  reg [6:0] opcode, funct7;
  reg [2:0] funct3;
  reg [4:0] rd, rs1, rs2;
  reg take, write_rd, supported, is_load, is_store, is_ebreak, is_mhartid;
  reg uses_rd, uses_rs1, uses_rs2;
  reg [31:0] addr, shifted, load_data;
  wire [CTX-1:0] halt_bits;

  SimDpiMem imem(.ren(state == FETCH), .wen(1'b0), .mask(8'b0),
    .addr(imem_addr), .wdata(32'b0), .rdata(imem_rdata));
  SimDpiMem dmem(.ren(dmem_ren), .wen(dmem_wen), .mask({4'b0,dmem_mask}),
    .addr(dmem_addr), .wdata(dmem_wdata), .rdata(dmem_rdata));
  generate
    for (genvar gh=0; gh<CTX; gh=gh+1) begin : G_HALT
      assign halt_bits[gh] = halted[gh];
    end
    genvar gw;
    for (gw=0; gw<WARPS; gw=gw+1) begin : G_ACTIVE
      wire all_halted;
      if (THREADS == 1) assign all_halted = halted[gw];
      else begin
        wire [THREADS-1:0] hs;
        for (genvar gt=0; gt<THREADS; gt=gt+1) assign hs[gt] = halted[gw*THREADS+gt];
        assign all_halted = &hs;
      end
      assign active_warps[gw] = !all_halted;
    end
  endgenerate
  assign done = &halt_bits;

  always @(*) begin
    imem_addr = issue_pc;
    dmem_ren = 1'b0; dmem_wen = 1'b0; dmem_addr = 0; dmem_wdata = 0; dmem_mask = 0;
    // The DPI block is combinational.  Preserve the request address through
    // MEM_RESP so the response is never evaluated against the default zero
    // address at the clock edge.
    if (state == MEM_REQ || state == MEM_RESP) begin
      c = int'(issue_warp) * THREADS + int'(mem_thread);
      rbase = c * 16;
      addr = gprs[rbase + int'(rs1[3:0])] + (is_load ? imm_i : imm_s);
      dmem_addr = {addr[31:2], 2'b00};
      dmem_ren = is_load && issue_mask[mem_thread];
      dmem_wen = is_store && state == MEM_REQ && issue_mask[mem_thread];
      case (funct3)
        3'b000: dmem_mask = 4'b0001 << addr[1:0];
        3'b001: dmem_mask = 4'b0011 << addr[1:0];
        default: dmem_mask = 4'b1111;
      endcase
      dmem_wdata = gprs[rbase + int'(rs2[3:0])] << (addr[1:0] * 8);
    end
  end

  always @(posedge clock) begin
    if (reset || !enable) begin
      state <= SCHED; rr_warp <= 0; issue_warp <= 0; issue_mask <= 0;
      issue_pc <= RESET_PC; issue_inst <= 0; mem_thread <= 0; mem_rdata <= 0;
      for (i=0; i<CTX; i=i+1) begin pcs[i] <= RESET_PC; halted[i] <= 0; end
      for (i=0; i<CTX*16; i=i+1) gprs[i] <= 0;
    end else begin
      case (state)
        SCHED: begin
          found = 0; selected = 0;
          for (i=0; i<WARPS; i=i+1) begin
            w = (int'(rr_warp) + i) % WARPS;
            if (!found && active_warps[w]) begin selected = w; found = 1; end
          end
          if (found) begin
            selected_t = 0;
            for (t=0; t<THREADS; t=t+1)
              if (!halted[selected*THREADS+t] && selected_t == 0) selected_t = t;
            issue_warp <= selected[WB-1:0];
            issue_pc <= pcs[selected*THREADS+selected_t];
            for (t=0; t<THREADS; t=t+1)
              issue_mask[t] <= !halted[selected*THREADS+t] &&
                               pcs[selected*THREADS+t] == pcs[selected*THREADS+selected_t];
            state <= FETCH;
          end
        end
        FETCH: begin issue_inst <= imem_rdata; state <= EXEC; end
        EXEC: begin
          opcode = issue_inst[6:0]; rd = issue_inst[11:7]; funct3 = issue_inst[14:12];
          rs1 = issue_inst[19:15]; rs2 = issue_inst[24:20]; funct7 = issue_inst[31:25];
          imm_i = {{20{issue_inst[31]}},issue_inst[31:20]};
          imm_s = {{20{issue_inst[31]}},issue_inst[31:25],issue_inst[11:7]};
          imm_b = {{19{issue_inst[31]}},issue_inst[31],issue_inst[7],issue_inst[30:25],issue_inst[11:8],1'b0};
          imm_u = {issue_inst[31:12],12'b0};
          imm_j = {{11{issue_inst[31]}},issue_inst[31],issue_inst[19:12],issue_inst[20],issue_inst[30:21],1'b0};
          is_load = opcode == 7'b0000011; is_store = opcode == 7'b0100011;
          is_ebreak = issue_inst == 32'h00100073;
          is_mhartid = opcode == 7'b1110011 && funct3 == 3'b010 && rs1 == 0 && issue_inst[31:20] == 12'hf14;
          uses_rd = opcode==7'b0110111 || opcode==7'b0010111 || opcode==7'b0010011 ||
                    opcode==7'b0110011 || opcode==7'b0000011 || opcode==7'b1101111 ||
                    opcode==7'b1100111 || opcode==7'b1110011;
          uses_rs1 = opcode==7'b0010011 || opcode==7'b0110011 || opcode==7'b0000011 ||
                     opcode==7'b0100011 || opcode==7'b1100011 || opcode==7'b1100111;
          uses_rs2 = opcode==7'b0110011 || opcode==7'b0100011 || opcode==7'b1100011;
          if (is_load || is_store) begin mem_thread <= 0; state <= MEM_REQ; end
          else begin
            for (t=0; t<THREADS; t=t+1) if (issue_mask[t]) begin
              c = int'(issue_warp)*THREADS+t; base = c*16; a = gprs[base+int'(rs1[3:0])]; b = gprs[base+int'(rs2[3:0])];
              result = 0; next_pc = issue_pc+4; write_rd = 1; supported = 1; take = 0;
              case (opcode)
                7'b0110111: result=imm_u;
                7'b0010111: result=issue_pc+imm_u;
                7'b0010011: case(funct3)
                  0: result=a+imm_i; 2: result={31'b0, ($signed(a)<$signed(imm_i))}; 3: result={31'b0, (a<imm_i)};
                  4: result=a^imm_i; 6: result=a|imm_i; 7: result=a&imm_i; 1: result=a<<issue_inst[24:20];
                  5: result=issue_inst[30] ? $signed(a)>>>issue_inst[24:20] : a>>issue_inst[24:20]; default: supported=0;
                endcase
                7'b0110011: case(funct3)
                  0: result=funct7==7'b0100000 ? a-b:a+b; 1: result=a<<b[4:0]; 2: result={31'b0, ($signed(a)<$signed(b))};
                  3: result={31'b0, (a<b)}; 4: result=a^b; 5: result=funct7==7'b0100000 ? $signed(a)>>>b[4:0]:a>>b[4:0];
                  6: result=a|b; 7: result=a&b; default: supported=0;
                endcase
                7'b1100011: begin write_rd=0; case(funct3)
                  0: take=(a==b); 1: take=(a!=b); 4: take=($signed(a)<$signed(b)); 5: take=($signed(a)>=$signed(b));
                  6: take=(a<b); 7: take=(a>=b); default: supported=0; endcase next_pc=take ? issue_pc+imm_b:issue_pc+4; end
                7'b1101111: begin result=issue_pc+4; next_pc=issue_pc+imm_j; end
                7'b1100111: begin result=issue_pc+4; next_pc=(a+imm_i)&32'hfffffffe; end
                7'b1110011: begin write_rd=0; if (is_ebreak) halted[c] <= 1; else if (is_mhartid) begin write_rd=1; result=CORE_ID*WARPS*THREADS+issue_warp*THREADS+t; end else supported=0; end
                default: supported=0;
              endcase
              if (!supported || (uses_rd && rd[4]) || (uses_rs1 && rs1[4]) || (uses_rs2 && rs2[4])) halted[c] <= 1;
              else if (!is_ebreak) begin pcs[c] <= next_pc; if (write_rd && rd != 0) gprs[base+int'(rd[3:0])] <= result; end
              gprs[base] <= 0;
            end
            rr_warp <= (int'(issue_warp) == WARPS-1) ? 0 : issue_warp+1; state <= SCHED;
          end
        end
        MEM_REQ: begin
          state <= MEM_RESP;
        end
        MEM_RESP: begin
          c = int'(issue_warp)*THREADS+int'(mem_thread); base=c*16;
          if (issue_mask[mem_thread]) begin
            addr = gprs[base+int'(rs1[3:0])] + (is_load ? imm_i : imm_s); shifted=dmem_rdata >> (addr[1:0]*8);
            case(funct3)
              0: load_data={{24{shifted[7]}},shifted[7:0]}; 1: load_data={{16{shifted[15]}},shifted[15:0]};
              2: load_data=dmem_rdata; 4: load_data={24'b0,shifted[7:0]}; 5: load_data={16'b0,shifted[15:0]}; default: load_data=0;
            endcase
            if (is_load && rd != 0) gprs[base+int'(rd[3:0])] <= load_data;
            pcs[c] <= issue_pc+4; gprs[base] <= 0;
          end
          if (int'(mem_thread) == THREADS-1) begin rr_warp <= (int'(issue_warp) == WARPS-1)?0:issue_warp+1; state <= SCHED; end
          else begin mem_thread <= mem_thread+1; state <= MEM_REQ; end
        end
      endcase
    end
  end
endmodule
