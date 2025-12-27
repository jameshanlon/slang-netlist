#include "Test.hpp"
#include "netlist/CombLoops.hpp"

TEST_CASE("A simple combinatorial loop") {
  auto const &tree = (R"(
module t(input x, output y);
  assign y = x;
endmodule

module m;
  wire a, b;
  t t(.x(a), .y(b));
  assign a = b;
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();

  CHECK(cycles.size() == 1);
  CHECK(cycles[0].size() == 4);
  CHECK(std::count_if(cycles[0].begin(), cycles[0].end(),
                      [](NetlistNode const *node) {
                        return node->kind == NodeKind::Assignment;
                      }) == 2);
}

TEST_CASE("No combinatorial loop with a single posedge DFF path") {
  // Test that a DFF in a closed path prevents the loop from being counted
  // as combinatorial.
  auto const &tree = (R"(
module t(input clk, input x, output reg z);
  always @(posedge clk)
    z <= x;
endmodule

module m(input clk);
  wire a, b;
  t t(.clk(clk), .x(a), .z(b));
  assign a = b;
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();

  CHECK(cycles.size() == 0);
}

TEST_CASE("No combinatorial loop with multiple edges DFF path") {
  // As previous test, but with both posedge and negedge in the sensitivity
  // list.
  auto const &tree = (R"(
module t(input clk, input rst, input x, output reg z);
  always @(posedge clk or negedge rst)
    if (!rst)
      z <= 1'b0;
    else
      z <= x;
endmodule

module m(input clk, input rst);
  wire a, b;
  t t(.clk(clk), .rst(rst), .x(a), .z(b));
  assign a = b;
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();

  CHECK(cycles.size() == 0);
}

TEST_CASE("A combinatorial loop with a combinatorial event list") {
  // Test that a sensitivity list with a non-edge signal in the
  // sensitivity list is detected as a combinatorial loop.
  auto const &tree = (R"(
module t(input clk, input rst, input x, output reg z);
  always @(posedge clk or x)
    z <= x;
endmodule

module m(input clk, input rst);
  wire a, b;
  t t(.clk(clk), .rst(rst), .x(a), .z(b));
  assign a = b;
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();

  CHECK(cycles.size() == 1);
}
