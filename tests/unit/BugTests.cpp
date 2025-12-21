#include "Test.hpp"

TEST_CASE("Slang #792: bus expression in ports", "[Bugs]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.pathExists("test.in_i", "test.out_o"));
}

TEST_CASE("Slang #793: port name collision with unused modules", "[Bugs]") {
  // Test that unused modules are not visited by the netlist builder.
  auto const &tree = R"(
module test (input i1,
             input i2,
             output o1
             );
   cell_a i_cell_a(.d1(i1),
                   .d2(i2),
                   .c(o1));
endmodule

module cell_a(input  d1,
              input  d2,
              output c);
   assign c = d1 + d2;
endmodule

// unused
module cell_b(input  a,
              input  b,
              output z);
   assign z = a || b;
endmodule

// unused
module cell_c(input  a,
              input  b,
              output z);
   assign z = (!a) && b;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("test.i1", "test.o1"));
}

TEST_CASE("Slang #985: conditional generate blocks", "[Bugs]") {
  // One branch of the generate conditional is uninstantiated.
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.pathExists("top.b", "top.out"));
}

TEST_CASE("Slang #919: empty port hookup", "[Bugs]") {
  auto const &tree = (R"(
module foo (input logic i_in);
endmodule

module top ();
  foo u_foo(.i_in());
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() == 1);
}

TEST_CASE("Slang #993: multiple blocking assignments of same variable in "
          "always_comb",
          "[Bugs]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="Out port nq"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="nq [31:0]"]
  N4 -> N4 [label="n[31:0]"]
  N4 -> N5 [label="n[31:0]"]
  N5 -> N6 [label="nq[31:0]"]
  N6 -> N2 [label="nq[31:0]"]
  N6 -> N3 [label="nq[31:0]"]
}
)");
}

TEST_CASE("Slang #1005: ignore concurrent assertions", "[Bugs]") {
  // Test that we handle timing events inside concurrent assertions.
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Slang #1007: variable declarations in procedural blocks", "[Bugs]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Slang #1124: net initialisers", "[Bugs]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(!test.pathExists("t.a", "t.d"));
  CHECK(!test.pathExists("t.d", "t.e"));
}

TEST_CASE("Slang #1281: hierarchical reference processing", "[Bugs]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Issue 18: reduced test case with merging of driver ranges in loops",
          "[Bugs]") {
  auto const &tree = R"(
 module m #(parameter NUM_CONSUMERS = 2, NUM_CHANNELS = 4)(
     input logic [NUM_CONSUMERS-1:0] read_valid,
     input logic i_state [NUM_CHANNELS-1:0],
     output logic o_state [NUM_CHANNELS-1:0]
);
     logic state_next [NUM_CHANNELS-1:0];
     always_comb begin
         state_next = i_state;
         for (int i = 0; i < NUM_CHANNELS; i = i + 1) begin
             for (int j = 0; j < NUM_CONSUMERS; j = j + 1) begin
                 if (read_valid[j]) begin
                     state_next[i] = 1;
                 end
             end
         end
     end
     assign o_state = state_next;
 endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i_state", "m.o_state"));
}
