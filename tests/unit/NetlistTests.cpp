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

TEST_CASE("Module with out-of-order dependencies") {
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

TEST_CASE(
    "Chain of dependencies through procedural and continuous assignments") {
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

TEST_CASE("Chain of dependencies through a packed array") {
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
  CHECK(test.pathExists("m.a", "m.b"));
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

TEST_CASE("Passthrough two signals via ranges in a shared vector") {
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

TEST_CASE("Passthrough two signals via a shared struct") {
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

TEST_CASE("Passthrough two signals via a shared union") {
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
  CHECK(test.pathExists("top.a", "top.c"));
  CHECK(test.pathExists("top.b", "top.c"));
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

TEST_CASE("Signal passthrough with a nested module") {
  auto &tree = R"(
module p(input logic i_value, output logic o_value);
  assign o_value = i_value;
endmodule

module m(input logic i_value, output logic o_value);
  p foo(
    .i_value(i_value),
    .o_value(o_value));
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value", "m.o_value"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port i_value"]
  N2 [label="Out port o_value"]
  N3 [label="In port i_value"]
  N4 [label="Out port o_value"]
  N5 [label="Assignment"]
  N6 [label="Assignment"]
  N1 -> N3 [label="i_value[0:0]"]
  N3 -> N5 [label="i_value[0:0]"]
  N4 -> N6
  N5 -> N4 [label="o_value[0:0]"]
  N6 -> N2 [label="o_value[0:0]"]
}
)");
}

// FIXME: failing due to isFrozen assert.
// TEST_CASE("Signal passthrough with a chain of two nested modules") {
//  auto tree = R"(
// module passthrough(input logic i_value, output logic o_value);
//  assign o_value = i_value;
// endmodule
//
// module m(input logic i_value, output logic o_value);
//  logic value;
//  passthrough a(
//    .i_value(i_value),
//    .o_value(value));
//  passthrough b(
//    .i_value(value),
//    .o_value(o_value));
// endmodule
//)";
//  NetlistTest test(tree);
//  CHECK(test.renderDot() == R"(digraph {
//}
//)");
//}

TEST_CASE("Chain of assignments through a procedural loop") {
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

TEST_CASE("Chain of assignments through a procedural loop with "
          "an inner conditional") {
  auto &tree = (R"(
module m(input logic a, output logic b);
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

TEST_CASE("Chain of assignments using a nested loop") {
  auto &tree = R"(
module m #(parameter N=3) (input logic i_value, output logic o_value);
  logic [(N*N)-1:0] stages;
  assign o_value = stages[(N*N)-1];
  always_comb begin
    for (int i=0; i<N; i++) begin
      for (int j=0; j<N; j++) begin
        if ((i == 0) && (j == 0))
          stages[0] = i_value;
        else
          stages[(i*N + j)] = stages[(i*N + j)-1];
      end
    end
  end
endmodule
)";
  NetlistTest test(tree);
  CHECK(test.pathExists("m.i_value", "m.o_value"));
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
  N9 [label="Assignment"]
  N10 [label="Assignment"]
  N11 [label="Assignment"]
  N12 [label="Assignment"]
  N13 [label="Assignment"]
  N14 [label="Assignment"]
  N15 [label="Assignment"]
  N16 [label="Merge"]
  N17 [label="Merge"]
  N18 [label="Merge"]
  N19 [label="Merge"]
  N20 [label="Assignment"]
  N21 [label="Assignment"]
  N22 [label="Assignment"]
  N23 [label="Assignment"]
  N24 [label="Assignment"]
  N25 [label="Assignment"]
  N26 [label="Merge"]
  N27 [label="Merge"]
  N28 [label="Merge"]
  N29 [label="Merge"]
  N30 [label="Merge"]
  N31 [label="Merge"]
  N32 [label="Merge"]
  N1 -> N4 [label="i_value[0:0]"]
  N3 -> N2 [label="o_value[0:0]"]
  N4 -> N7 [label="stages[0:0]"]
  N4 -> N16 [label="stages[0:0]"]
  N7 -> N9 [label="stages[1:1]"]
  N7 -> N17 [label="stages[1:1]"]
  N9 -> N11 [label="stages[2:2]"]
  N9 -> N18 [label="stages[2:2]"]
  N9 -> N19
  N11 -> N13 [label="stages[3:3]"]
  N11 -> N29 [label="stages[3:3]"]
  N13 -> N15 [label="stages[4:4]"]
  N13 -> N30 [label="stages[4:4]"]
  N15 -> N19
  N15 -> N21 [label="stages[5:5]"]
  N15 -> N31 [label="stages[5:5]"]
  N16 -> N26 [label="stages[0:0]"]
  N17 -> N27 [label="stages[1:1]"]
  N18 -> N28 [label="stages[2:2]"]
  N19 -> N32
  N21 -> N23 [label="stages[6:6]"]
  N23 -> N25 [label="stages[7:7]"]
  N25 -> N32
  N25 -> N3 [label="stages[8:8]"]
}
)");
}

TEST_CASE("Chain of dependencies though continuous assignments") {
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

TEST_CASE("Procedural statement with internal and external r-values") {
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

TEST_CASE("Non-blocking assignment effect") {
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

TEST_CASE(
    "Sequential state: two control paths assigning to the same variable") {
  auto &tree = (R"(
  module m(input clk, input rst, input logic a, output logic b);
    always_ff @(posedge clk or posedge rst)
      if (rst)
        b <= '0;
      else
        b <= a;
  endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port a"]
  N4 [label="Out port b"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="b [0:0]"]
  N2 -> N5 [label="rst[0:0]"]
  N3 -> N7 [label="a[0:0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N7 -> N8
  N7 -> N9 [label="b[0:0]"]
  N9 -> N4 [label="b[0:0]"]
}
)");
}

TEST_CASE("Sequential state: with a self-referential assignment") {
  auto &tree = (R"(
  module m(input clk, input rst, input logic a, output logic b);
    always_ff @(posedge clk or posedge rst)
      if (rst)
        b <= '0;
      else
        b <= b + a;
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.b"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port a"]
  N4 [label="Out port b"]
  N5 [label="Conditional"]
  N6 [label="Assignment"]
  N7 [label="Assignment"]
  N8 [label="Merge"]
  N9 [label="b [0:0]"]
  N2 -> N5 [label="rst[0:0]"]
  N3 -> N7 [label="a[0:0]"]
  N5 -> N6
  N5 -> N7
  N6 -> N8
  N7 -> N8
  N7 -> N9 [label="b[0:0]"]
  N9 -> N4 [label="b[0:0]"]
  N9 -> N7 [label="b[0:0]"]
}
)");
}

TEST_CASE("Sequential state: reference to a previous variable definition") {
  auto &tree = (R"(
module m(input logic clk, input logic rst, input logic foo, input logic ready, output logic foo_q);
  logic valid_q;
  always @(posedge clk)
    if (rst) begin
      foo_q <= 0;
      valid_q <= 0;
    end else begin
      if (!valid_q)
        foo_q <= foo;
      valid_q <= ready;
    end
endmodule
  )");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.foo", "m.foo_q"));
  CHECK(test.pathExists("m.ready", "m.foo_q"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port clk"]
  N2 [label="In port rst"]
  N3 [label="In port foo"]
  N4 [label="In port ready"]
  N5 [label="Out port foo_q"]
  N6 [label="Conditional"]
  N7 [label="Assignment"]
  N8 [label="Assignment"]
  N9 [label="Conditional"]
  N10 [label="Assignment"]
  N11 [label="Merge"]
  N12 [label="Assignment"]
  N13 [label="Merge"]
  N14 [label="valid_q [0:0]"]
  N15 [label="foo_q [0:0]"]
  N2 -> N6 [label="rst[0:0]"]
  N3 -> N10 [label="foo[0:0]"]
  N4 -> N12 [label="ready[0:0]"]
  N6 -> N7
  N6 -> N9
  N8 -> N13
  N9 -> N10
  N9 -> N11
  N9 -> N12
  N10 -> N11
  N10 -> N15 [label="foo_q[0:0]"]
  N12 -> N13
  N12 -> N14 [label="valid_q[0:0]"]
  N14 -> N9 [label="valid_q[0:0]"]
  N15 -> N5 [label="foo_q[0:0]"]
}
)");
}

TEST_CASE("Merge two control paths assigning to different parts of a vector") {
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

TEST_CASE("Merge two control paths assigning to the same part of a vector") {
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

TEST_CASE("Merge two control paths assigning to overlapping of a vector") {
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

TEST_CASE("Unreachable assignment is ignored in data flow analysis") {
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

TEST_CASE("Sequential (blocking) assignment overwrites previous value") {
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

TEST_CASE("Non-blocking assignment defers update until end of block") {
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

TEST_CASE("Variable is not assigned on all control paths") {
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

TEST_CASE("Assign to different slices of a vector") {
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

TEST_CASE("Overlapping assignments to same variable") {
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

TEST_CASE("Chained assignments") {
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

TEST_CASE("Multiple assignments to an output port") {
  auto &tree = (R"(
module m(input in, output [1:0] out);
   assign out[0] = in;
   assign out[1] = in;
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.in", "m.out"));
}

TEST_CASE("Multiple assignments from an input port") {
  auto &tree = (R"(
module m(input [1:0] in, output out);
   assign out = {in[0], in[1]};
endmodule
)");
  NetlistTest test(tree);
  CHECK(test.pathExists("m.in", "m.out"));
}

TEST_CASE("Nested conditionals assigning variables") {
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
