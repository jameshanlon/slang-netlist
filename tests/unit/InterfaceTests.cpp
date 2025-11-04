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
  modport m(input .foo({a, b}));
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
  // FIXME: these paths are not valid and are due to incorrect resolution of
  // concatenations in modport connection expressions.
  CHECK(test.pathExists("m.i.a", "m.b"));
  CHECK(test.pathExists("m.i.b", "m.a"));
}
