#include "Test.hpp"

TEST_CASE("Interface with modports", "[NetlistInterface]") {
  auto &tree = (R"(
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
  NetlistTest test(tree);
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

TEST_CASE("Interface with a modport connection expression",
          "[NetlistInterface]") {
  auto &tree = (R"(
interface I;
  logic a, b;
  modport m(input .foo({a, b}));
endinterface

module n(I.m i);
  assign x = i.foo[0];
endmodule

module top;
  I i();
  assign i.a = 1;
  assign i.b = 1;
  n n1(i);
endmodule
)");
  NetlistTest test(tree);
  // FIXME
  CHECK_FALSE(test.renderDot() == R"(digraph {
digraph {
}
)");
}
