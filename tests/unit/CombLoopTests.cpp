#include "Test.hpp"
#include "netlist/CombLoops.hpp"

TEST_CASE("A simple combinational loop", "[CombLoop]") {
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

TEST_CASE("No combinational loop with a single posedge DFF path",
          "[CombLoop]") {
  // Test that a DFF in a closed path prevents the loop from being counted
  // as combinational.
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
  CHECK(cycles.empty());
}

TEST_CASE("No combinational loop with multiple edges DFF path", "[CombLoop]") {
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
  CHECK(cycles.empty());
}

TEST_CASE("A combinational loop with a combinational event list",
          "[CombLoop]") {
  // Test that a sensitivity list with a non-edge signal in the
  // sensitivity list is detected as a combinational loop.
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

TEST_CASE("No combinational loop with self assignment", "[CombLoop]") {
  // Test that a self assignment does not create a combinational loop.
  auto const &tree = (R"(
module m();
  wire [10:0] w;
  assign w[0] = w[3];
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();
  CHECK(cycles.empty());
}

TEST_CASE("No combinational loop with inout port", "[CombLoop]") {
  // Test that a self assignment does not create a combinational loop.
  auto const &tree = (R"(
module t(wire w);
endmodule
module m(input w);
  t tt(.w(w));
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();
  CHECK(cycles.empty());
}

TEST_CASE("No combinational loop with sequential assignments", "[CombLoop]") {
  // Test that sequential assignments do not create a combinational loop.
  auto const &tree = (R"(
module aes_key_mem1(input wire key);
  reg key_mem_new;
  always_comb
    begin: round_key_gen
      key_mem_new = key;
      key_mem_new = key;
     end
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();
  CHECK(cycles.empty());
}

TEST_CASE("No combinational loop in expression", "[CombLoop]") {
  // Test that a variable appearing in an expression does not create a
  // combinational loop.
  auto const &tree = (R"(
module m();
   int apb_xx_paddr;
   assign psel_s5 = apb_xx_paddr>=1 && apb_xx_paddr <=6;
endmodule
)");
  const NetlistTest test(tree);
  CombLoops combLoops(test.graph);
  auto cycles = combLoops.getAllLoops();
  CHECK(cycles.empty());
}
