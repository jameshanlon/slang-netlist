#include "Test.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace {

auto getNames(const std::vector<NetlistNode *> &nodes)
    -> std::set<std::string> {
  std::set<std::string> names;
  for (auto *node : nodes) {
    auto path = node->getHierarchicalPath();
    if (path.has_value()) {
      names.insert(std::string(*path));
    }
  }
  return names;
}

} // namespace

TEST_CASE("Fan-out from an input through combinational logic", "[CombFan]") {
  auto const &tree = R"(
  module m(input logic a, output logic x, output logic y);
    assign x = a;
    assign y = a;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *start = test.graph.lookup("m.a");
  REQUIRE(start);
  auto fanOut = test.graph.getCombFanOut(*start);
  auto names = getNames(fanOut);
  CHECK(names.contains("m.a"));
  CHECK(names.contains("m.x"));
  CHECK(names.contains("m.y"));
}

TEST_CASE("Fan-in to an output through combinational logic", "[CombFan]") {
  auto const &tree = R"(
  module m(input logic a, input logic b, output logic y);
    assign y = a + b;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *end = test.graph.lookup("m.y");
  REQUIRE(end);
  auto fanIn = test.graph.getCombFanIn(*end);
  auto names = getNames(fanIn);
  CHECK(names.contains("m.a"));
  CHECK(names.contains("m.b"));
  CHECK(names.contains("m.y"));
}

TEST_CASE("Fan-out stops at sequential state", "[CombFan]") {
  auto const &tree = R"(
  module m(input clk, input logic a, output logic x, output logic y);
    assign x = a;
    always_ff @(posedge clk)
      y <= a;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *start = test.graph.lookup("m.a");
  REQUIRE(start);
  auto fanOut = test.graph.getCombFanOut(*start);
  auto names = getNames(fanOut);
  CHECK(names.contains("m.x"));
  CHECK_FALSE(names.contains("m.y"));
}

TEST_CASE("Fan-in stops at sequential state", "[CombFan]") {
  auto const &tree = R"(
  module m(input clk, input logic a, input logic b, output logic y);
    logic q;
    always_ff @(posedge clk)
      q <= a;
    assign y = q + b;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *end = test.graph.lookup("m.y");
  REQUIRE(end);
  auto fanIn = test.graph.getCombFanIn(*end);
  auto names = getNames(fanIn);
  CHECK(names.contains("m.b"));
  CHECK(names.contains("m.y"));
  CHECK_FALSE(names.contains("m.a"));
}

TEST_CASE("Fan-out from a single node", "[CombFan]") {
  auto const &tree = R"(
  module m(input logic a, output logic b);
    assign b = a;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *start = test.graph.lookup("m.b");
  REQUIRE(start);
  auto fanOut = test.graph.getCombFanOut(*start);
  CHECK(fanOut.size() == 1);
  CHECK(fanOut[0] == start);
}

TEST_CASE("Fan-in from a single node", "[CombFan]") {
  auto const &tree = R"(
  module m(input logic a, output logic b);
    assign b = a;
  endmodule
  )";
  const NetlistTest test(tree);
  auto *start = test.graph.lookup("m.a");
  REQUIRE(start);
  auto fanIn = test.graph.getCombFanIn(*start);
  CHECK(fanIn.size() == 1);
  CHECK(fanIn[0] == start);
}
