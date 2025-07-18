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
  FormatBuffer buffer;
  NetlistDot::render(netlist, buffer);
  CHECK(buffer.str() == R"(digraph {
  node [shape=record];
  N1 [label="In port a"]
  N2 [label="Out port b"]
  N3 [label="Assignment"]
  N1 -> N3 [label="a[0:0]"]
  N3 -> N2 [label="b[0:0]"]
}
)");
}
