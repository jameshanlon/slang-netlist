#include "Test.hpp"

TEST_CASE("Interface with modports", "[Interface]") {
  auto const &tree = (R"(
interface I;
    logic l;
    modport mst ( output l );
    modport slv ( input l );
endinterface

module m(I.slv i);
    logic x;
    assign x = i.l;
endmodule

module n(I.mst i);
    assign i.l = 1 ;
endmodule

module top;
    I i();
    m u_m(i);
    n u_n(i);
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="Variable l"]
  N2 [label="Assignment"]
  N3 [label="Assignment"]
  N1 -> N2 [label="l[0:0]"]
  N3 -> N1 [label="l[0:0]"]
}
)");
}

TEST_CASE("Interface with a modport connection expression", "[Interface]") {
  auto const &tree = (R"(
interface I;
  logic a;
  logic b;
  modport m(input .foo({b, a}));
endinterface

module foo(I.m i, output logic x);
  assign x = i.foo[0];
endmodule

module bar(I.m i, output logic x);
  assign x = i.foo[1];
endmodule

module m(output logic a, output logic b);
  I i();
  assign i.a = 1;
  assign i.b = 1;
  foo a1(i, a);
  bar b1(i, b);
endmodule
)");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.i.a", "m.a"));
  CHECK(test.pathExists("m.i.b", "m.b"));
  CHECK(!test.pathExists("m.i.a", "m.b"));
  CHECK(!test.pathExists("m.i.b", "m.a"));
}

TEST_CASE("Slang #855: instance with an interface", "[Interface]") {
  auto &tree = R"(
interface my_if();
  logic [31:0] a;
  logic [31:0] b;
  logic [31:0] sum;
  logic        co;
  modport test (
    input  a,
    input  b,
    output sum,
    output co
  );
endinterface

module adder(my_if.test i);
  logic [31:0] sum;
  logic co;
  assign {co, sum} = i.a + i.b;
  assign i.sum = sum;
  assign i.co = co;
endmodule

module m();
  my_if i ();
  adder adder0 (i);
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.graph.numNodes() > 0);
}

TEST_CASE("Slang #855: interface array", "[Interface]") {
  auto &tree = R"(
interface if_foo();
  logic [31:0] a;
  modport produce (output a);
  modport consume (input a);
endinterface

module produce(if_foo.produce i, input logic [31:0] x);
  assign i.a = x;
endmodule

module consume(if_foo.consume i, output logic [31:0] x);
  assign x = i.a;
endmodule

module m(input logic [31:0] in, output logic [31:0] out);
  if_foo i [2] [3] ();
  produce p (i[0][0], in);
  consume c (i[0][0], out);
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.in", "m.out"));
}
