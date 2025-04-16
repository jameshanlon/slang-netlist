module foo(input logic a, input logic b, input logic c, output logic d);
  always_comb
    if (a) begin 
      d = b;
    end else begin
      d = c;
    end
endmodule
