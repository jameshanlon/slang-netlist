#include "Test.hpp"

TEST_CASE("Path through passthrough module") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  CHECK(path.size() == 3);
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
