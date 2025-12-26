#include "Test.hpp"

TEST_CASE("Edge annotation", "[Netlist]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  logic [12:8] t;
  assign t = a;
  assign b = t[9:8];
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N1 -> N3 [label="a[7:0]"]
  N3 -> N4 [label="t[1:0]"]
  N4 -> N2 [label="b[7:0]"]
}
)");
}
