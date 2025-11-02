#include "Test.hpp"

TEST_CASE("If statement with else branch assigning constants",
          "[Conditionals]") {
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
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
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic c, output logic d);
  always_comb
    if (a) begin
      d = b;
    end else begin
      d = c;
    end
endmodule
)");
  const NetlistTest test(tree);
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
  auto const &tree = (R"(
module m(input logic a, input logic b, input logic ctrl, output logic c);
  assign c = ctrl ? a : b;
endmodule
)");
  const NetlistTest test(tree);
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
  auto const &tree = (R"(
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
  const NetlistTest test(tree);
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

TEST_CASE("Variable is not assigned on all control paths", "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, output logic y);
  logic t;
  always_comb begin
    if (a) t = 1;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // a should be a valid path to y.
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Unreachable assignment is ignored in data flow analysis",
          "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    if (0) t = a;
    else   t = b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // Only b should be a valid path to y, a should not.
  CHECK(!test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Merge two control paths assigning to different parts of a vector",
          "[Conditional]") {
  auto const &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         output logic x,
         output logic y);
  logic [1:0] t;
  always_comb
    if (a) begin
      t[0] = b;
    end else begin
      t[1] = c;
    end
  assign x =  t[0];
  assign y =  t[1];
endmodule
  )");
  const NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.y"));
}

TEST_CASE("Merge two control paths assigning to the same part of a vector",
          "[Conditional]") {
  auto const &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         output logic x);
  logic [1:0] t;
  always_comb
    if (a) begin
      t[1] = b;
    end else begin
      t[1] = c;
    end
  assign x =  t[1];
endmodule
  )");
  const NetlistTest test(tree);
  // Both b and c should be valid paths to x.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.x"));
}

TEST_CASE("Merge two control paths assigning to overlapping of a vector",
          "[Netlist]") {
  auto const &tree = (R"(
module m(input logic a,
         input logic b,
         input logic c,
         input logic d,
         output logic x,
         output logic y,
         output logic z);
  logic [2:0] t;
  always_comb
    if (a) begin
      t[0] = d;
      t[1] = b;
    end else begin
      t[1] = c;
      t[2] = d;
    end
  assign x =  t[0];
  assign y =  t[1];
  assign z =  t[2];
endmodule
  )");
  const NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.d", "m.z"));
}

TEST_CASE("Nested conditionals assigning variables", "[Netlist]") {
  // Test that the variables in multiple nested levels of conditions are
  // correctly added as dependencies of the output variable.
  auto const &tree = R"(
 module m(input a, input b, input c, input sel_a, input sel_b, output reg f);
  always @(*) begin
    if (sel_a == 1'b0) begin
      if (sel_b == 1'b0)
        f = a;
      else
        f = b;
    end else begin
      f = c;
    end
  end
 endmodule
)";
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.f"));
  CHECK(test.pathExists("m.b", "m.f"));
  CHECK(test.pathExists("m.c", "m.f"));
  CHECK(test.pathExists("m.sel_a", "m.f"));
  CHECK(test.pathExists("m.sel_b", "m.f"));
}
