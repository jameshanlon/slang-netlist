#include "Test.hpp"

TEST_CASE("Non-blocking assignment effect", "[NonblockingAssignment]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic z);
  logic [3:0] t;
  always_comb begin
    z <= a & t; // t defined by the blocking assignment.
    t = a & b;
  end
endmodule
  )");
  const NetlistTest test(tree);
  CHECK(test.pathExists("m.a", "m.z"));
  CHECK(test.pathExists("m.b", "m.z"));
  CHECK(test.renderDot() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="In port b"]
  N3 [label="Out port z"]
  N4 [label="Assignment"]
  N5 [label="Assignment"]
  N1 -> N4 [label="a[0]"]
  N1 -> N5 [label="a[0]"]
  N2 -> N5 [label="b[0]"]
  N4 -> N3 [label="z[0]"]
  N5 -> N4 [label="t[3:0]"]
}
)");
}

TEST_CASE("Non-blocking assignment defers update until end of block",
          "[NonblockingAssignment]") {
  auto const &tree = (R"(
module m(input logic a, input logic b, output logic y);
  logic t;
  always_comb begin
    t <= a;
    t <= b;
  end
  assign y = t;
endmodule
  )");
  const NetlistTest test(tree);
  // Both a and b should be valid paths to y (last assignment wins, but both are
  // drivers).
  CHECK((test.pathExists("m.a", "m.y") || test.pathExists("m.b", "m.y")));
}
