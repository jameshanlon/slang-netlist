#include "Test.hpp"

TEST_CASE("Path through passthrough module") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  NetlistTest test(tree);
  PathFinder pathFinder(test.netlist);
  auto *start = test.netlist.lookup("m.a");
  auto *end = test.netlist.lookup("m.b");
  CHECK(pathFinder.find(*start, *end).size() == 3);
}
