// RV32E SIMT core. A warp owns one PC and active mask; lanes own register data.
module GPUCore #(
  parameter integer CORE_ID=0, WARPS=4, THREADS=4, STACK_DEPTH=8
) (
  input wire clock, input wire reset,
  KmuIf.slave kmu_if,
  output wire imem_ren, output wire [31:0] imem_addr,
  input wire [31:0] imem_rdata,
  output reg dmem_ren, output reg dmem_wen,
  output reg [3:0] dmem_mask,
  output reg [31:0] dmem_addr, output reg [31:0] dmem_wdata,
  input wire [31:0] dmem_rdata,
  output wire fault, output wire done,
  output wire [WARPS-1:0] active_warps,
  output reg [((WARPS<=1)?1:$clog2(WARPS))-1:0] issue_warp,
  output reg [THREADS-1:0] issue_mask
);
  localparam CTX=WARPS*THREADS;
  localparam WB=(WARPS<=1)?1:$clog2(WARPS);
  localparam TB=(THREADS<=1)?1:$clog2(THREADS);
  localparam SCHED=0, FETCH=1, EXEC=2, MEM_REQ=3, MEM_RESP=4;

  reg [2:0] state;
  reg cta_active, fault_r;
  reg [WB-1:0] rr_warp;
  reg [31:0] warp_pc [0:WARPS-1];
  reg [THREADS-1:0] warp_mask [0:WARPS-1];
  reg warp_valid [0:WARPS-1];
  reg [3:0] stack_sp [0:WARPS-1];
  reg [31:0] stack_pc [0:WARPS*STACK_DEPTH-1];
  reg [THREADS-1:0] stack_mask [0:WARPS*STACK_DEPTH-1];
  reg [31:0] gprs [0:CTX*16-1];
  reg [31:0] thread_x [0:CTX-1], thread_y [0:CTX-1], thread_z [0:CTX-1];
  reg [31:0] global_id [0:CTX-1];
  reg [31:0] block_x, block_y, block_z, dim_x, dim_y, dim_z;
  reg [31:0] grid_x, grid_y, grid_z, args_addr;
  reg [31:0] issue_pc, issue_inst;
  reg [TB-1:0] mem_thread;

  wire cta_valid = kmu_if.cta_valid[CORE_ID];
  wire cta_ready;
  wire [31:0] cta_startup_pc = kmu_if.startup_pc;
  wire [31:0] cta_args_addr = kmu_if.args_addr;
  wire [31:0] cta_block_idx_x = kmu_if.block_idx_x;
  wire [31:0] cta_block_idx_y = kmu_if.block_idx_y;
  wire [31:0] cta_block_idx_z = kmu_if.block_idx_z;
  wire [31:0] cta_block_dim_x = kmu_if.block_dim_x;
  wire [31:0] cta_block_dim_y = kmu_if.block_dim_y;
  wire [31:0] cta_block_dim_z = kmu_if.block_dim_z;
  wire [31:0] cta_grid_dim_x = kmu_if.grid_dim_x;
  wire [31:0] cta_grid_dim_y = kmu_if.grid_dim_y;
  wire [31:0] cta_grid_dim_z = kmu_if.grid_dim_z;
  wire [31:0] cta_block_size = kmu_if.block_size;

  integer i,t,w,c,base,rbase,selected,candidate,local_id,block_linear;
  reg found, supported, uses_rd, uses_rs1, uses_rs2, write_rd;
  reg [6:0] opcode, funct7;
  reg [2:0] funct3;
  reg [4:0] rd,rs1,rs2;
  reg [11:0] csr_addr;
  reg [31:0] a,b,result,next_pc,imm_i,imm_s,imm_b,imm_u,imm_j;
  reg [31:0] addr,shifted,load_data,first_target,lane_target;
  reg [THREADS-1:0] taken_mask, fall_mask;
  reg take, target_set, divergent_target;

  assign done=!cta_active;
  assign fault=fault_r;
  assign cta_ready=!cta_active && state==SCHED;
  assign kmu_if.core_ready[CORE_ID]=cta_ready;
  assign kmu_if.core_busy[CORE_ID]=cta_active;
  assign imem_ren=state==FETCH;
  assign imem_addr=issue_pc;
  generate for (genvar aw=0;aw<WARPS;aw=aw+1)
    assign active_warps[aw]=warp_valid[aw];
  endgenerate

  always @(*) begin
    dmem_ren=0; dmem_wen=0; dmem_addr=0; dmem_wdata=0; dmem_mask=0;
    if (state==MEM_REQ || state==MEM_RESP) begin
      c=int'(issue_warp)*THREADS+int'(mem_thread); rbase=c*16;
      addr=gprs[rbase+int'(rs1[3:0])] + (opcode==7'b0000011 ? imm_i : imm_s);
      dmem_addr={addr[31:2],2'b0};
      dmem_ren=(opcode==7'b0000011) && issue_mask[mem_thread];
      dmem_wen=(opcode==7'b0100011) && state==MEM_REQ && issue_mask[mem_thread];
      case(funct3)
        3'b000:dmem_mask=4'b0001<<addr[1:0];
        3'b001:dmem_mask=4'b0011<<addr[1:0];
        default:dmem_mask=4'b1111;
      endcase
      dmem_wdata=gprs[rbase+int'(rs2[3:0])]<<(addr[1:0]*8);
    end
  end

  task automatic advance_warp;
    begin
      rr_warp <= (issue_warp==WB'(WARPS-1))?WB'(0):issue_warp+1'b1;
      state <= SCHED;
    end
  endtask

  always @(posedge clock) begin
    if (reset) begin
      state<=SCHED; cta_active<=0; fault_r<=0; rr_warp<=0;
      issue_warp<=0; issue_mask<=0; issue_pc<=0; issue_inst<=0; mem_thread<=0;
      block_x<=0;block_y<=0;block_z<=0;dim_x<=1;dim_y<=1;dim_z<=1;
      grid_x<=1;grid_y<=1;grid_z<=1;args_addr<=0;
      for(i=0;i<WARPS;i=i+1) begin
        warp_pc[i]<=0;warp_mask[i]<=0;warp_valid[i]<=0;stack_sp[i]<=0;
      end
      for(i=0;i<CTX*16;i=i+1) gprs[i]<=0;
      for(i=0;i<CTX;i=i+1) begin thread_x[i]<=0;thread_y[i]<=0;thread_z[i]<=0;global_id[i]<=0;end
    end else if (cta_valid && cta_ready) begin
      cta_active<=1; fault_r<=0; state<=SCHED; rr_warp<=0;
      block_x<=cta_block_idx_x;block_y<=cta_block_idx_y;block_z<=cta_block_idx_z;
      dim_x<=cta_block_dim_x;dim_y<=cta_block_dim_y;dim_z<=cta_block_dim_z;
      grid_x<=cta_grid_dim_x;grid_y<=cta_grid_dim_y;grid_z<=cta_grid_dim_z;
      args_addr<=cta_args_addr;
      block_linear=((cta_block_idx_z*cta_grid_dim_y)+cta_block_idx_y)*cta_grid_dim_x+cta_block_idx_x;
      for(w=0;w<WARPS;w=w+1) begin
        warp_pc[w]<=cta_startup_pc;stack_sp[w]<=0;
        for(t=0;t<THREADS;t=t+1)
          warp_mask[w][t] <= (w*THREADS+t)<cta_block_size;
        warp_valid[w] <= (w*THREADS)<cta_block_size;
      end
      for(i=0;i<CTX;i=i+1) begin
        local_id=i;
        thread_x[i]<=local_id%cta_block_dim_x;
        thread_y[i]<=(local_id/cta_block_dim_x)%cta_block_dim_y;
        thread_z[i]<=local_id/(cta_block_dim_x*cta_block_dim_y);
        global_id[i]<=block_linear*cta_block_size+local_id;
      end
      for(i=0;i<CTX*16;i=i+1) gprs[i]<=0;
    end else if (cta_active) begin
      case(state)
        SCHED: begin
          found=0;selected=0;
          for(i=0;i<WARPS;i=i+1) begin
            candidate=(int'(rr_warp)+i)%WARPS;
            if(!found&&warp_valid[candidate])begin found=1;selected=candidate;end
          end
          if(found)begin
            issue_warp<=selected[WB-1:0];issue_mask<=warp_mask[selected];
            issue_pc<=warp_pc[selected];state<=FETCH;
          end else cta_active<=0;
        end
        FETCH: begin issue_inst<=imem_rdata;state<=EXEC;end
        EXEC: begin
          opcode=issue_inst[6:0];rd=issue_inst[11:7];funct3=issue_inst[14:12];
          rs1=issue_inst[19:15];rs2=issue_inst[24:20];funct7=issue_inst[31:25];csr_addr=issue_inst[31:20];
          imm_i={{20{issue_inst[31]}},issue_inst[31:20]};
          imm_s={{20{issue_inst[31]}},issue_inst[31:25],issue_inst[11:7]};
          imm_b={{19{issue_inst[31]}},issue_inst[31],issue_inst[7],issue_inst[30:25],issue_inst[11:8],1'b0};
          imm_u={issue_inst[31:12],12'b0};
          imm_j={{11{issue_inst[31]}},issue_inst[31],issue_inst[19:12],issue_inst[20],issue_inst[30:21],1'b0};
          uses_rd=opcode==7'b0110111||opcode==7'b0010111||opcode==7'b0010011||opcode==7'b0110011||opcode==7'b0000011||opcode==7'b1101111||opcode==7'b1100111||opcode==7'b1110011;
          uses_rs1=opcode==7'b0010011||opcode==7'b0110011||opcode==7'b0000011||opcode==7'b0100011||opcode==7'b1100011||opcode==7'b1100111;
          uses_rs2=opcode==7'b0110011||opcode==7'b0100011||opcode==7'b1100011;
          if((uses_rd&&rd[4])||(uses_rs1&&rs1[4])||(uses_rs2&&rs2[4]))begin
            fault_r<=1;cta_active<=0;state<=SCHED;
          end else if(opcode==7'b0000011||opcode==7'b0100011)begin mem_thread<=0;state<=MEM_REQ;
          end else if(issue_inst==32'h00100073)begin
            if(stack_sp[issue_warp]!=0)begin
              stack_sp[issue_warp]<=stack_sp[issue_warp]-1'b1;
              warp_pc[issue_warp]<=stack_pc[issue_warp*STACK_DEPTH+stack_sp[issue_warp]-1'b1];
              warp_mask[issue_warp]<=stack_mask[issue_warp*STACK_DEPTH+stack_sp[issue_warp]-1'b1];
            end else warp_valid[issue_warp]<=0;
            advance_warp();
          end else if(opcode==7'b1100011)begin
            taken_mask=0;fall_mask=0;supported=1;
            for(t=0;t<THREADS;t=t+1)if(issue_mask[t])begin
              base=(int'(issue_warp)*THREADS+t)*16;a=gprs[base+int'(rs1[3:0])];b=gprs[base+int'(rs2[3:0])];take=0;
              case(funct3) 0:take=a==b;1:take=a!=b;4:take=$signed(a)<$signed(b);5:take=$signed(a)>=$signed(b);6:take=a<b;7:take=a>=b;default:supported=0;endcase
              if(take)taken_mask[t]=1;else fall_mask[t]=1;
            end
            if(!supported)begin fault_r<=1;cta_active<=0;end
            else if(taken_mask!=0&&fall_mask!=0)begin
              if(stack_sp[issue_warp]>=STACK_DEPTH)begin fault_r<=1;cta_active<=0;end
              else begin
                stack_pc[issue_warp*STACK_DEPTH+stack_sp[issue_warp]]<=issue_pc+imm_b;
                stack_mask[issue_warp*STACK_DEPTH+stack_sp[issue_warp]]<=taken_mask;
                stack_sp[issue_warp]<=stack_sp[issue_warp]+1'b1;
                warp_pc[issue_warp]<=issue_pc+4;warp_mask[issue_warp]<=fall_mask;
              end
            end else if(taken_mask!=0)begin warp_pc[issue_warp]<=issue_pc+imm_b;warp_mask[issue_warp]<=taken_mask;end
            else begin warp_pc[issue_warp]<=issue_pc+4;warp_mask[issue_warp]<=fall_mask;end
            advance_warp();
          end else begin
            supported=1;target_set=0;divergent_target=0;first_target=0;
            for(t=0;t<THREADS;t=t+1)if(issue_mask[t])begin
              c=int'(issue_warp)*THREADS+t;base=c*16;a=gprs[base+int'(rs1[3:0])];b=gprs[base+int'(rs2[3:0])];
              result=0;next_pc=issue_pc+4;write_rd=uses_rd;
              case(opcode)
                7'b0110111:result=imm_u;
                7'b0010111:result=issue_pc+imm_u;
                7'b0010011:case(funct3)0:result=a+imm_i;2:result={31'b0,$signed(a)<$signed(imm_i)};3:result={31'b0,a<imm_i};4:result=a^imm_i;6:result=a|imm_i;7:result=a&imm_i;1:result=a<<issue_inst[24:20];5:result=issue_inst[30]?$signed(a)>>>issue_inst[24:20]:a>>issue_inst[24:20];default:supported=0;endcase
                7'b0110011:case(funct3)0:result=(funct7==7'b0100000) ? a-b : a+b;1:result=a<<b[4:0];2:result={31'b0,$signed(a)<$signed(b)};3:result={31'b0,a<b};4:result=a^b;5:result=(funct7==7'b0100000) ? $signed(a)>>>b[4:0] : a>>b[4:0];6:result=a|b;7:result=a&b;default:supported=0;endcase
                7'b1101111:begin result=issue_pc+4;next_pc=issue_pc+imm_j;end
                7'b1100111:begin result=issue_pc+4;lane_target=(a+imm_i)&32'hfffffffe;next_pc=lane_target;if(!target_set)begin first_target=lane_target;target_set=1;end else if(first_target!=lane_target)divergent_target=1;end
                7'b1110011:begin
                  write_rd=1;
                  if(funct3!=3'b010||rs1!=0)supported=0;
                  else case(csr_addr)
                    12'hf14:result=global_id[c];12'hcc0:result=thread_x[c];12'hcc1:result=thread_y[c];12'hcc2:result=thread_z[c];
                    12'hcc3:result=block_x;12'hcc4:result=block_y;12'hcc5:result=block_z;
                    12'hcc6:result=dim_x;12'hcc7:result=dim_y;12'hcc8:result=dim_z;
                    12'hcc9:result=grid_x;12'hcca:result=grid_y;12'hccb:result=grid_z;
                    12'hccc:result=args_addr;12'hccd:result=CORE_ID*WARPS*THREADS+int'(issue_warp)*THREADS+t;
                    12'hcce:result=32'(issue_warp);12'hccf:result=t;default:supported=0;
                  endcase
                end
                default:supported=0;
              endcase
              if(write_rd&&rd!=0)gprs[base+int'(rd[3:0])]<=result;
              gprs[base]<=0;
            end
            if(!supported||divergent_target)begin fault_r<=1;cta_active<=0;end
            else warp_pc[issue_warp]<=next_pc;
            advance_warp();
          end
        end
        MEM_REQ: begin
          if(!issue_mask[mem_thread])begin
            if(mem_thread==TB'(THREADS-1))begin warp_pc[issue_warp]<=issue_pc+4;advance_warp();end
            else mem_thread<=mem_thread+1'b1;
          end else state<=MEM_RESP;
        end
        MEM_RESP: begin
          c=int'(issue_warp)*THREADS+int'(mem_thread);base=c*16;
          addr=gprs[base+int'(rs1[3:0])] + ((opcode==7'b0000011) ? imm_i : imm_s);shifted=dmem_rdata>>(addr[1:0]*8);
          case(funct3)0:load_data={{24{shifted[7]}},shifted[7:0]};1:load_data={{16{shifted[15]}},shifted[15:0]};2:load_data=dmem_rdata;4:load_data={24'b0,shifted[7:0]};5:load_data={16'b0,shifted[15:0]};default:load_data=0;endcase
          if(opcode==7'b0000011&&rd!=0)gprs[base+int'(rd[3:0])]<=load_data;
          gprs[base]<=0;
          if(mem_thread==TB'(THREADS-1))begin warp_pc[issue_warp]<=issue_pc+4;advance_warp();end
          else begin mem_thread<=mem_thread+1'b1;state<=MEM_REQ;end
        end
      endcase
    end
  end
endmodule
