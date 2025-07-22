#include "Test.hpp"

TEST_CASE("Path through passthrough module") {
  auto &tree = (R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  Compilation compilation;
  AnalysisManager analysisManager;
  NetlistGraph netlist;
  createNetlist(tree, compilation, analysisManager, netlist);
  PathFinder pathFinder(netlist);
  auto *start = netlist.lookup("m.a");
  auto *end = netlist.lookup("m.b");
  CHECK(pathFinder.find(*start, *end).size() == 3);
}
