#include "Test.hpp"

TEST_CASE("Chain of assignments through a procedural loop", "[Loop]") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  localparam N=4;
  logic [N-1:0] p;
  assign b = p[N-1];
  always_comb begin
    p[0] = a;
    for (int i=0; i<N-1; i++)
      p[i+1] = p[i];
  end
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N1 -> N4 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
  N4 -> N5 [label="p[0:0]"]
  N4 -> N8
  N5 -> N6 [label="p[1:1]"]
  N6 -> N7 [label="p[2:2]"]
  N7 -> N8
  N7 -> N3 [label="p[3:3]"]
}
)");
}
