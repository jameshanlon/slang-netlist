#include "Test.hpp"

TEST_CASE("Empty module") {
  auto &tree = (R"(
module m();
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.netlist.numNodes() == 0);
  CHECK(test.netlist.numEdges() == 0);
}

TEST_CASE("Passthrough module") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  NetlistTest test(tree);
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

TEST_CASE("Module with out-of-order dependencies") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  logic temp;
  assign b = temp;
  assign temp = a;
endmodule
)");
  NetlistTest test(tree);
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

TEST_CASE("If statement with else branch assigning constants") {
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
  N7 [label="Merge"]
  N1 -> N3 [label="a[0:0]"]
  N3 -> N4
  N3 -> N5
  N4 -> N6 [label="b[0:0]"]
  N4 -> N7
  N5 -> N6 [label="b[0:0]"]
  N5 -> N7
  N6 -> N2 [label="b[0:0]"]
}
)");
}

TEST_CASE("If statement with else branch assigning variables") {
  auto &tree = (R"(
module foo(input logic a, input logic b, input logic c, output logic d);
  always_comb
    if (a) begin
      d = b;
    end else begin
      d = c;
    end
endmodule
)");
  NetlistTest test(tree);
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
  N9 [label="Merge"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N6 [label="b[0:0]"]
  N3 -> N7 [label="c[0:0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8 [label="d[0:0]"]
  N6 -> N9
  N7 -> N8 [label="d[0:0]"]
  N7 -> N9
  N8 -> N4 [label="d[0:0]"]
}
)");
}

TEST_CASE("Ternary operator in continuous assignment") {
  auto &tree = (R"(
module mux(input logic a, input logic b, input logic ctrl, output logic c);
  assign c = ctrl ? a : b;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="In port ctrl"]
  N4 [label="Out port c"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N1 -> N5 [label="a[0:0]"]
  N2 -> N5 [label="b[0:0]"]
  N3 -> N5 [label="ctrl[0:0]"]
  N5 -> N6 [label="c[0:0]"]
  N6 -> N4 [label="c[0:0]"]
}
)");
}

TEST_CASE("Four-way case statement") {
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
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Case"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Merge"]
  N7 [label="Merge"]
  N8 [label="Assignment"]
  N9 [label="Merge"]
  N10 [label="Merge"]
  N11 [label="Assignment"]
  N12 [label="Merge"]
  N13 [label="Merge"]
  N1 -> N3 [label="a[1:0]"]
  N3 -> N4
  N3 -> N5
  N3 -> N8
  N3 -> N11
  N4 -> N6 [label="b[0:0]"]
  N4 -> N7
  N5 -> N6 [label="b[0:0]"]
  N5 -> N7
  N6 -> N9 [label="b[0:0]"]
  N7 -> N10
  N8 -> N9 [label="b[0:0]"]
  N8 -> N10
  N9 -> N12 [label="b[0:0]"]
  N10 -> N13
  N11 -> N12 [label="b[0:0]"]
  N11 -> N13
  N12 -> N2 [label="b[0:0]"]
}
)");
}

TEST_CASE("Module instance with connections to the top ports") {
  auto &tree = (R"(
module foo(input logic x, input logic y, output logic z);
  assign z = x | y;
endmodule

module top(input logic a, input logic b, output logic c);
  foo u_mux (
    .x(a),
    .y(b),
    .z(c)
  );
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port c"]
  N4 [label="In port x"]
  N5 [label="In port y"]
  N6 [label="Out port z"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N1 -> N4 [label="a[0:0]"]
  N2 -> N5 [label="b[0:0]"]
  N4 -> N7 [label="x[0:0]"]
  N5 -> N7 [label="y[0:0]"]
  N6 -> N8
  N7 -> N6 [label="z[0:0]"]
  N8 -> N3 [label="c[0:0]"]
}
)");
}

TEST_CASE("Module with a chain of assignments through a procedural loop") {
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
  N9 [label="Merge"]
  N1 -> N4 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
  N4 -> N5 [label="p[0:0]"]
  N4 -> N8 [label="p[0:0]"]
  N4 -> N9
  N5 -> N6 [label="p[1:1]"]
  N6 -> N7 [label="p[2:2]"]
  N7 -> N9
  N7 -> N3 [label="p[3:3]"]
}
)");
}

TEST_CASE("Module with a chain of assignments through a procedural loop with "
          "an inner conditional") {
  auto &tree = (R"(
module foo(input logic a, output logic b);
  localparam N=4;
  logic p [N-1:0];
  always_comb
    for (int i=0; i<N; i++)
      if (i==0)
        p[0] = a;
      else
        p[i] = p[i-1];
  assign b = p[N-1];
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N9 [label="Assignment"]
  N10 [label="Assignment"]
  N11 [label="Assignment"]
  N1 -> N3 [label="a[0:0]"]
  N3 -> N6 [label="p[3:3]"]
  N6 -> N8 [label="p[2:2]"]
  N8 -> N10 [label="p[1:1]"]
  N10 -> N11 [label="p[0:0]"]
  N11 -> N2 [label="b[0:0]"]
}
)");
}
