interface DcrIf;
  logic valid;
  logic [11:0] addr;
  logic [31:0] data;

  modport master(output valid, addr, data);
  modport slave(input valid, addr, data);
endinterface
