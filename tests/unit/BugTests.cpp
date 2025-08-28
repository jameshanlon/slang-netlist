#include "Test.hpp"

TEST_CASE("Slang #792: bus expression in ports") {
  auto &tree = (R"(
module test (input [1:0] in_i,
             output [1:0] out_o);
  wire [1:0] in_s;
  assign in_s = in_i;
  nop i_nop(
    .in_i(in_s[1:0]), // ok: in_s, in_i, {in_i[1], in_i[0]}
    .out_o(out_o)
 );
endmodule

module nop (input [1:0]  in_i,
            output [1:0] out_o);
   // individual bits access; ok: out_o = in_i;
   assign out_o[0] = in_i[0];
   assign out_o[1] = in_i[1];
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("test.in_i", "test.out_o"));
}

TEST_CASE("Slang #985: conditional generate blocks") {
  // One branch of the generate conditional is uninstantiated.
  auto &tree = (R"(
module top #(parameter X=0)(input logic a, input logic b, output logic out);
  generate
    if (X) begin
      assign out = a;
    end else begin
      assign out = b;
    end
  endgenerate
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("top.b", "top.out"));
}

TEST_CASE("Slang #919: empty port hookup") {
  auto &tree = (R"(
module foo (input logic i_in);
endmodule

module top ();
  foo u_foo(.i_in());
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.netlist.numNodes() == 1);
}

TEST_CASE("Slang #993: multiple blocking assignments of same variable in "
          "always_comb") {
  auto &tree = (R"(
module t2 (input clk, output reg [31:0] nq);
  reg [31:0] n;
  always_comb begin
    n = nq;
    n = n + 1;
  end
  always_ff @(posedge clk)
    nq <= n;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="Out port nq"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="nq [0:31]"]
  N4 -> N4 [label="n[31:0]"]
  N4 -> N5 [label="n[31:0]"]
  N5 -> N6 [label="nq[31:0]"]
  N6 -> N2 [label="nq[31:0]"]
  N6 -> N3 [label="nq[31:0]"]
}
)");
}

TEST_CASE("Slang #1005: ignore concurrent assertions") {
  // Test that we handle timing events inside concurrent assertions.
  auto &tree = (R"(
module t33 #(
  parameter MODE = 3'd0
) (
  input wire  clk,
  input wire [15:0]l,
  input wire [15:0]s,
  input wire [15:0]c,
  input wire  [1:0]b,
  input wire       a
);
  reg   [15:0] c_n;
  always @(s or l or c)
  begin : c_inc
    c_n = c + (l ^ s);
  end

  property test_prop;
    @(posedge clk) disable iff (MODE != 3'd0)
    !($isunknown({a,b,c})) &
      a & (b == 2'b01)
      |-> (c_n[15:12] == c[15:12]);
  endproperty
  tp_inst: assert property (test_prop) else
        $error("prop error");
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.netlist.numNodes() > 0);
}

TEST_CASE("Slang #1007: variable declarations in procedural blocks") {
  auto &tree = (R"(
module m;
  reg [3:0] x;
  reg [15:0] v;
  always @(v)
  begin
    integer i;
    x = '0;
    for (i = 0; i <= 15; i = i + 1)
      if (v[i] == 1'b0)
        x = i[3:0];
  end
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.netlist.numNodes() > 0);
}

TEST_CASE("Slang #1124: net initialisers") {
  auto &tree = (R"(
module t;
  reg a, b;
  wire c;
  initial begin
    a <= 1;
    b <= a;
  end
  assign c = a;
  wire d = a;
  wire e = d;
endmodule
)");
  NetlistTest test(tree);
  CHECK(!test.pathExists("t.a", "t.d"));
  CHECK(!test.pathExists("t.d", "t.e"));
}

TEST_CASE("Slang #1281: hierarchical reference processing") {
  auto &tree = (R"(
module top();
  initial begin
    m2.c = 1'b0;
  end
  m1 m2();
endmodule

module m1();
  reg c;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.netlist.numNodes() > 0);
}
