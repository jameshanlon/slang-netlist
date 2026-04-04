#include "Test.hpp"

TEST_CASE("Chain of assignments through a procedural loop", "[Loop]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
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
  N1 -> N4 [label="a[0]"]
  N3 -> N2 [label="b[0]"]
  N4 -> N5 [label="p[0]"]
  N4 -> N8
  N5 -> N6 [label="p[1]"]
  N6 -> N7 [label="p[2]"]
  N7 -> N8
  N7 -> N3 [label="p[3]"]
}
)");
}

TEST_CASE("Nested for loops", "[Loop]") {
  auto const &tree = R"(
module m(input logic [3:0] a, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 2; j++)
        t[i*2+j] = a[i*2+j];
  end
  assign b = t;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
}

TEST_CASE("Loop body depends on external signal", "[Loop]") {
  auto const &tree = R"(
module m(input logic [3:0] a, input logic sel, output logic [3:0] b);
  logic [3:0] t;
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      if (sel)
        t[i] = a[i];
      else
        t[i] = 1'b0;
    end
  end
  assign b = t;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.pathExists("m.sel", "m.b"));
}

TEST_CASE("While loop with assignments", "[Loop]") {
  // Known limitation: while loops are not unrolled by the data flow analysis,
  // so assignments inside the loop body are not tracked as dependencies.
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  logic [7:0] t;
  always_comb begin
    int i;
    i = 0;
    t = '0;
    while (i < 8) begin
      t[i] = a[i];
      i = i + 1;
    end
  end
  assign b = t;
endmodule
)";
  const NetlistTest test(tree);
  CHECK(!test.pathExists("m.a", "m.b"));
}
