#include "Test.hpp"

TEST_CASE("Empty module", "[Netlist]") {
  auto &tree = (R"(
module m();
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.graph.numNodes() == 0);
  CHECK(test.graph.numEdges() == 0);
}

TEST_CASE("Passthrough module", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N1 -> N3 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
}
)");
}

TEST_CASE("Module with out-of-order dependencies", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  logic temp;
  assign b = temp;
  assign temp = a;
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
  N1 -> N4 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
  N4 -> N3 [label="temp[0:0]"]
}
)");
}

TEST_CASE("Chain of dependencies through procedural and continuous assignments",
          "[Netlist]") {
  auto &tree = R"(
module m(input logic i_value, output logic o_value);
  logic a, b, c, d, e;
  assign a = i_value;
  always_comb begin
    b = a;
    c = b;
    d = c;
  end
  assign e = d;
  assign o_value = e;
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N3 [label="i_value[0:0]"]
  N3 -> N4 [label="a[0:0]"]
  N4 -> N5 [label="b[0:0]"]
  N5 -> N6 [label="c[0:0]"]
  N6 -> N7 [label="d[0:0]"]
  N7 -> N8 [label="e[0:0]"]
  N8 -> N2 [label="o_value[0:0]"]
}
)");
}

TEST_CASE("Chain of dependencies through a packed array", "[Netlist]") {
  auto &tree = R"(
module m(input logic i_value, output logic o_value);
  logic [4:0] x;
  assign x[0] = i_value;
  always_comb begin
    x[1] = x[0];
    x[2] = x[1];
    x[3] = x[2];
  end
  assign x[4] = x[3];
  assign o_value = x[4];
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N3 [label="i_value[0:0]"]
  N3 -> N4 [label="x[0:0]"]
  N4 -> N5 [label="x[1:1]"]
  N5 -> N6 [label="x[2:2]"]
  N6 -> N7 [label="x[3:3]"]
  N7 -> N8 [label="x[4:4]"]
  N8 -> N2 [label="o_value[0:0]"]
}
)");
}

TEST_CASE("Passthrough two signals via ranges in a shared vector",
          "[Netlist]") {
  auto tree = R"(
module m(
  input  logic [1:0] i_value_a,
  input  logic [1:0] i_value_b,
  output logic [1:0] o_value_a,
  output logic [1:0] o_value_b);
  logic [3:0] foo;
  assign foo[1:0] = i_value_a;
  assign foo[3:2] = i_value_b;
  assign o_value_a = foo[1:0];
  assign o_value_b = foo[3:2];
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N5 [label="i_value_a[1:0]"]
  N2 -> N6 [label="i_value_b[1:0]"]
  N5 -> N7 [label="foo[1:0]"]
  N6 -> N8 [label="foo[3:2]"]
  N7 -> N3 [label="o_value_a[1:0]"]
  N8 -> N4 [label="o_value_b[1:0]"]
}
)");
}

TEST_CASE("Passthrough two signals via a shared struct", "[Netlist]") {
  auto &tree = R"(
module m(
  input logic i_value_a,
  input logic i_value_b,
  output logic o_value_a,
  output logic o_value_b);
  struct packed {
    logic a;
    logic b;
  } foo;
  assign foo.a = i_value_a;
  assign foo.b = i_value_b;
  assign o_value_a = foo.a;
  assign o_value_b = foo.b;
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N5 [label="i_value_a[0:0]"]
  N2 -> N6 [label="i_value_b[0:0]"]
  N5 -> N7 [label="foo[1:1]"]
  N6 -> N8 [label="foo[0:0]"]
  N7 -> N3 [label="o_value_a[0:0]"]
  N8 -> N4 [label="o_value_b[0:0]"]
}
)");
}

TEST_CASE("Passthrough two signals via a shared union", "[Netlist]") {
  auto &tree = R"(
module m(input logic i_value_a,
         input logic i_value_b,
         output logic o_value_a,
         output logic o_value_b,
         output logic o_value_c);
  union packed {
    logic [1:0] a;
    logic [1:0] b;
  } foo;
  assign foo.a[0] = i_value_a;
  assign foo.b[1] = i_value_b;
  assign o_value_a = foo.a[0];
  assign o_value_b = foo.b[1];
  assign o_value_c = foo.b[0]; // Overlapping with a in union.
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value_a", "m.o_value_a"));
  CHECK(test.pathExists("m.i_value_b", "m.o_value_b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value_a"]
  N2 [label="In port i_value_b"]
  N3 [label="Out port o_value_a"]
  N4 [label="Out port o_value_b"]
  N5 [label="Out port o_value_c"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N9 [label="Assignment"]
  N10 [label="Assignment"]
  N1 -> N6 [label="i_value_a[0:0]"]
  N2 -> N7 [label="i_value_b[0:0]"]
  N6 -> N8 [label="foo[0:0]"]
  N6 -> N10 [label="foo[0:0]"]
  N7 -> N9 [label="foo[1:1]"]
  N8 -> N3 [label="o_value_a[0:0]"]
  N9 -> N4 [label="o_value_b[0:0]"]
  N10 -> N5 [label="o_value_c[0:0]"]
}
)");
}

TEST_CASE("Chain of assignments through a procedural loop", "[Netlist]") {
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

TEST_CASE("Chain of dependencies though continuous assignments", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  logic [2:0] pipe;
  assign pipe[0] = a;
  assign pipe[1] = pipe[0];
  assign pipe[2] = pipe[1];
  assign b = pipe[2];
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
  N1 -> N3 [label="a[0:0]"]
  N3 -> N4 [label="pipe[0:0]"]
  N4 -> N5 [label="pipe[1:1]"]
  N5 -> N6 [label="pipe[2:2]"]
  N6 -> N2 [label="b[0:0]"]
}
)");
}

TEST_CASE("Procedural statement with internal and external r-values",
          "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic c);
  logic [2:0] p;
  assign p[1] = b;
  always_comb begin
    p[0] = a;
    p[2] = p[0] + p[1];
  end
  assign c = p[2];
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.c"));
  CHECK(test.pathExists("m.b", "m.c"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port c"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N4 [label="b[0:0]"]
  N4 -> N6 [label="p[1:1]"]
  N5 -> N6 [label="p[0:0]"]
  N6 -> N7 [label="p[2:2]"]
  N7 -> N3 [label="c[0:0]"]
}
)");
}

TEST_CASE("Non-blocking assignment effect", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic z);
  logic [3:0] t;
  always_comb begin
    z <= a & t; // t defined by the blocking assignment.
    t = a & b;
  end
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.z"));
  CHECK(test.pathExists("m.b", "m.z"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port z"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N1 -> N4 [label="a[0:0]"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N5 [label="b[0:0]"]
  N4 -> N3 [label="z[0:0]"]
  N5 -> N4 [label="t[3:0]"]
}
)");
}

TEST_CASE("Merge two control paths assigning to different parts of a vector",
          "[Netlist]") {
  auto &tree = (R"(
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
  NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.y"));
}

TEST_CASE("Merge two control paths assigning to the same part of a vector",
          "[Netlist]") {
  auto &tree = (R"(
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
  NetlistTest test(tree);
  // Both b and c should be valid paths to x.
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.x"));
}

TEST_CASE("Merge two control paths assigning to overlapping of a vector",
          "[Netlist]") {
  auto &tree = (R"(
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
  NetlistTest test(tree);
  // Both b and c should be valid paths to y.
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.y"));
  CHECK(test.pathExists("m.d", "m.z"));
}

TEST_CASE("Unreachable assignment is ignored in data flow analysis",
          "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    if (0) t = a;
    else   t = b;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // Only b should be a valid path to y, a should not.
  CHECK(!test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Sequential (blocking) assignment overwrites previous value",
          "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    t = a;
    t = b;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // Only b should be a valid path to y, a should not.
  CHECK(!test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Non-blocking assignment defers update until end of block",
          "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    t <= a;
    t <= b;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // Both a and b should be valid paths to y (last assignment wins, but both are
  // drivers).
  CHECK((test.pathExists("m.a", "m.y") || test.pathExists("m.b", "m.y")));
}

TEST_CASE("Variable is not assigned on all control paths", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, output logic y);
  logic t;
  always_comb begin
    if (a) t = 1;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // a should be a valid path to y.
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Assign to different slices of a vector", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic [1:0] y);
  logic [1:0] t;
  always_comb begin
    t[0] = a;
    t[1] = b;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // Both a and b should be valid paths to y.
  CHECK(test.pathExists("m.a", "m.y"));
  CHECK(test.pathExists("m.b", "m.y"));
}

TEST_CASE("Overlapping assignments to same variable", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic [1:0] y);
  logic [1:0] t;
  always_comb begin
    t[1:0] = a;
    t[0] = b;
  end
  assign y = t;
endmodule
  )");
  NetlistTest test(tree);
  // b should be the only driver for t[0], and a for t[1].
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Chained assignments", "[Netlist]") {
  auto &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t, u;
  always_comb begin
    t = a;
    u = t;
  end
  assign y = u;
endmodule
  )");
  NetlistTest test(tree);
  // a should be a valid path to y through t and u.
  CHECK(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Nested conditionals assigning variables", "[Netlist]") {
  // Test that the variables in multiple nested levels of conditions are
  // correctly added as dependencies of the output variable.
  auto &tree = R"(
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
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.f"));
  CHECK(test.pathExists("m.b", "m.f"));
  CHECK(test.pathExists("m.c", "m.f"));
  CHECK(test.pathExists("m.sel_a", "m.f"));
  CHECK(test.pathExists("m.sel_b", "m.f"));
}
