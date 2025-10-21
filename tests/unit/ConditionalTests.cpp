#include "Test.hpp"

TEST_CASE("If statement with else branch assigning constants",
          "[Conditionals]") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  always_comb begin
    if (a) begin
      b = 1;
    end else begin
      b = 0;
    end
  end
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Conditional"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N1 -> N3 [label="a[0:0]"]
  N3 -> N4
  N3 -> N5
  N4 -> N6
  N4 -> N2 [label="b[0:0]"]
  N5 -> N6
  N5 -> N2 [label="b[0:0]"]
}
)");
}

TEST_CASE("If statement with else branch assigning variables",
          "[Conditionals]") {
  auto &tree = (R"(
module m(input logic a, input logic b, input logic c, output logic d);
  always_comb
    if (a) begin
      d = b;
    end else begin
      d = c;
    end
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.d"));
  CHECK(test.pathExists("m.b", "m.d"));
  CHECK(test.pathExists("m.c", "m.d"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="In port c"]
  N4 [label="Out port d"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N6 [label="b[0:0]"]
  N3 -> N7 [label="c[0:0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N6 -> N4 [label="d[0:0]"]
  N7 -> N8
  N7 -> N4 [label="d[0:0]"]
}
)");
}

TEST_CASE("Ternary operator in continuous assignment", "[Conditionals]") {
  auto &tree = (R"(
module m(input logic a, input logic b, input logic ctrl, output logic c);
  assign c = ctrl ? a : b;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.c"));
  CHECK(test.pathExists("m.ctrl", "m.c"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="In port ctrl"]
  N4 [label="Out port c"]
  N5 [label="Assignment"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N5 [label="b[0:0]"]
  N3 -> N5 [label="ctrl[0:0]"]
  N5 -> N4 [label="c[0:0]"]
}
)");
}

TEST_CASE("Four-way case statement", "[Conditionals]") {
  auto &tree = (R"(
module m(input logic [1:0] a, output logic b);
  always_comb
    case (a)
      2'b00: b = 0;
      2'b01: b = 1;
      2'b10: b = 2;
      2'b11: b = 3;
    endcase
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Case"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="Assignment"]
  N10 [label="Merge"]
  N1 -> N3 [label="a[1:0]"]
  N3 -> N4
  N3 -> N5
  N3 -> N7
  N3 -> N9
  N4 -> N6
  N4 -> N2 [label="b[0:0]"]
  N5 -> N6
  N5 -> N2 [label="b[0:0]"]
  N6 -> N8
  N7 -> N8
  N7 -> N2 [label="b[0:0]"]
  N8 -> N10
  N9 -> N10
  N9 -> N2 [label="b[0:0]"]
}
)");
}
