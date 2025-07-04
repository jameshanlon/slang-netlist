#include "Test.hpp"

TEST_CASE("Empty module") {
  auto &tree = (R"(
module m();
endmodule
)");
  Compilation compilation;
  AnalysisManager analysisManager;
  NetlistGraph netlist;
  createNetlist(tree, compilation, analysisManager, netlist);
  CHECK(netlist.numNodes() == 0);
  CHECK(netlist.numEdges() == 0);
}
