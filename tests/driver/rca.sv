module rca
  #(parameter p_width = 8)
  (input  logic               i_clk,
   input  logic               i_rst,
   input  logic [p_width-1:0] i_op0,
   input  logic [p_width-1:0] i_op1,
   output logic [p_width-1:0] o_sum,
   output logic               o_co);

  logic [p_width-1:0] carry;
  logic [p_width-1:0] sum;
  logic [p_width-1:0] sum_q;
  logic               co_q;

  assign carry[0] = 1'b0;
  assign {o_co, o_sum} = {co_q, sum_q};

  for (genvar i = 0; i < p_width - 1; i++) begin
    assign {carry[i+1], sum[i]} = i_op0[i] + i_op1[i] + carry[i];
  end

  always_ff @(posedge i_clk or posedge i_rst)
    if (i_rst) begin
      sum_q <= {p_width{1'b0}};
      co_q  <= 1'b0;
    end else begin
      sum_q <= sum;
      co_q  <= carry[p_width-1];
    end

endmodule
