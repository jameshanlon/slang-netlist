#include "Test.hpp"

TEST_CASE("Passthrough module") {
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
  auto *start = graph.lookup("a");
  auto *end = graph.lookup("b");
  CHECK(pathFinder.find(*start, *end).size() == 3);
}
