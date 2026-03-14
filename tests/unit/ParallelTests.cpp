#include "Test.hpp"

/// Helper to build the netlist in parallel mode.
static NetlistTest parallelTest(std::string const &tree) {
  return NetlistTest(tree, /*parallel=*/true);
}

TEST_CASE("Parallel: continuous assignments", "[Parallel]") {
  auto const &tree = R"(
module m(input logic [7:0] a, b, output logic [7:0] x, y);
  assign x = a;
  assign y = b;
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.b", "m.x"));
}

TEST_CASE("Parallel: always_comb blocking assignments", "[Parallel]") {
  auto const &tree = R"(
module m(input logic [7:0] a, b, output logic [7:0] x, y);
  always_comb begin
    x = a;
    y = b;
  end
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.b", "m.x"));
}

TEST_CASE("Parallel: if/else merge", "[Parallel]") {
  auto const &tree = R"(
module m(input logic cond,
         input logic [7:0] a, b,
         output logic [7:0] x);
  always_comb begin
    if (cond)
      x = a;
    else
      x = b;
  end
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.getDrivers("m.x", {7, 0}).size() == 2);
}

TEST_CASE("Parallel: multiple procedural blocks", "[Parallel]") {
  auto const &tree = R"(
module m(input logic [7:0] a, b, c,
         output logic [7:0] x, y, z);
  always_comb x = a;
  always_comb y = b;
  always_comb z = c;
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK(test.pathExists("m.c", "m.z"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
  CHECK_FALSE(test.pathExists("m.b", "m.z"));
}

TEST_CASE("Parallel: mixed procedural and continuous", "[Parallel]") {
  auto const &tree = R"(
module m(input logic [7:0] a, b, output logic [7:0] x, y);
  assign x = a;
  always_comb y = b;
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.y"));
  CHECK_FALSE(test.pathExists("m.a", "m.y"));
}

TEST_CASE("Parallel: always_ff non-blocking", "[Parallel]") {
  auto const &tree = R"(
module m(input logic clk,
         input logic [7:0] d,
         output logic [7:0] q);
  always_ff @(posedge clk)
    q <= d;
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.d", "m.q"));
}

TEST_CASE("Parallel: bit-range splitting with merge", "[Parallel]") {
  auto const &tree = R"(
module m(input logic cond,
         input logic [7:0] a, b,
         output logic [7:0] x);
  always_comb begin
    if (cond)
      x[7:4] = a[3:0];
    else
      x[7:0] = b;
  end
endmodule
)";
  auto test = parallelTest(tree);
  // Bits [7:4] have 2 drivers (a and b), bits [3:0] have 1 driver (b).
  CHECK(test.getDrivers("m.x", {7, 4}).size() == 2);
  CHECK(test.getDrivers("m.x", {3, 0}).size() == 1);
}

TEST_CASE("Parallel: case statement", "[Parallel]") {
  auto const &tree = R"(
module m(input logic [1:0] sel,
         input logic [7:0] a, b, c,
         output logic [7:0] x);
  always_comb begin
    case (sel)
      0: x = a;
      1: x = b;
      default: x = c;
    endcase
  end
endmodule
)";
  auto test = parallelTest(tree);
  CHECK(test.pathExists("m.a", "m.x"));
  CHECK(test.pathExists("m.b", "m.x"));
  CHECK(test.pathExists("m.c", "m.x"));
  CHECK(test.getDrivers("m.x", {7, 0}).size() == 3);
}

TEST_CASE("Parallel: results match sequential", "[Parallel]") {
  auto const &tree = R"(
module m(input logic cond,
         input logic [7:0] a, b, c,
         output logic [7:0] x, y);
  assign x = a;
  always_comb begin
    if (cond)
      y = b;
    else
      y = c;
  end
endmodule
)";
  NetlistTest seq(tree, /*parallel=*/false);
  NetlistTest par(tree, /*parallel=*/true);

  // Same graph structure.
  CHECK(seq.graph.numNodes() == par.graph.numNodes());
  CHECK(seq.graph.numEdges() == par.graph.numEdges());

  // Same path reachability.
  CHECK(seq.pathExists("m.a", "m.x") == par.pathExists("m.a", "m.x"));
  CHECK(seq.pathExists("m.b", "m.y") == par.pathExists("m.b", "m.y"));
  CHECK(seq.pathExists("m.c", "m.y") == par.pathExists("m.c", "m.y"));
  CHECK(seq.pathExists("m.a", "m.y") == par.pathExists("m.a", "m.y"));

  // Same driver counts.
  CHECK(seq.getDrivers("m.y", {7, 0}).size() ==
        par.getDrivers("m.y", {7, 0}).size());
}
