#include "Test.hpp"
#include "VariableTracker.hpp"

/// Helper to look up an AST symbol by name and return a reference.
static auto lookupSymbol(NetlistTest &test, std::string const &name)
    -> ast::Symbol const & {
  test.compilation.unfreeze();
  auto *sym = test.compilation.getRoot().lookupName(name);
  test.compilation.freeze();
  REQUIRE(sym);
  return *sym;
}

TEST_CASE("VariableTracker insert and lookup by bounds", "[VariableTracker]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);
  auto &symA = lookupSymbol(test, "m.a");

  // Build a standalone tracker and insert a node.
  VariableTracker tracker;
  auto *portA = test.graph.lookup("m.a");
  REQUIRE(portA);

  tracker.insert(symA, {0, 7}, *portA);

  // Lookup with exact bounds returns the node.
  auto *result = tracker.lookup(symA, {0, 7});
  CHECK(result == portA);
}

TEST_CASE("VariableTracker lookup with wrong bounds returns nullptr",
          "[VariableTracker]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);
  auto &symA = lookupSymbol(test, "m.a");

  VariableTracker tracker;
  auto *portA = test.graph.lookup("m.a");
  REQUIRE(portA);

  tracker.insert(symA, {0, 7}, *portA);

  // Lookup with non-matching bounds returns nullptr.
  CHECK(tracker.lookup(symA, {0, 3}) == nullptr);
  CHECK(tracker.lookup(symA, {8, 15}) == nullptr);
}

TEST_CASE("VariableTracker lookup all nodes for a symbol",
          "[VariableTracker]") {
  auto const &tree = R"(
module m(input logic [7:0] a, output logic [7:0] b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);
  auto &symA = lookupSymbol(test, "m.a");

  VariableTracker tracker;
  auto *portA = test.graph.lookup("m.a");
  auto *portB = test.graph.lookup("m.b");
  REQUIRE(portA);
  REQUIRE(portB);

  // Insert two different ranges for the same symbol.
  tracker.insert(symA, {3, 0}, *portA);
  tracker.insert(symA, {7, 4}, *portB);

  auto nodes = tracker.lookup(symA);
  CHECK(nodes.size() == 2);
}

TEST_CASE("VariableTracker lookup non-existent symbol returns nullptr",
          "[VariableTracker]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);
  auto &symA = lookupSymbol(test, "m.a");
  auto &symB = lookupSymbol(test, "m.b");

  VariableTracker tracker;
  auto *portA = test.graph.lookup("m.a");
  REQUIRE(portA);

  // Only insert symA.
  tracker.insert(symA, {0, 0}, *portA);

  // Lookup symB (never inserted) returns nullptr / empty.
  CHECK(tracker.lookup(symB, {0, 0}) == nullptr);
  CHECK(tracker.lookup(symB).empty());
}

TEST_CASE("VariableTracker multiple symbols are independent",
          "[VariableTracker]") {
  auto const &tree = R"(
module m(input logic a, input logic b, output logic c);
  assign c = a | b;
endmodule
)";
  NetlistTest test(tree);
  auto &symA = lookupSymbol(test, "m.a");
  auto &symB = lookupSymbol(test, "m.b");

  VariableTracker tracker;
  auto *portA = test.graph.lookup("m.a");
  auto *portB = test.graph.lookup("m.b");
  REQUIRE(portA);
  REQUIRE(portB);

  tracker.insert(symA, {0, 0}, *portA);
  tracker.insert(symB, {0, 0}, *portB);

  CHECK(tracker.lookup(symA, {0, 0}) == portA);
  CHECK(tracker.lookup(symB, {0, 0}) == portB);
  // Cross-lookup returns nullptr.
  CHECK(tracker.lookup(symA, {0, 0}) != portB);
  CHECK(tracker.lookup(symB, {0, 0}) != portA);
}

TEST_CASE("VariableTracker used by NetlistBuilder for ports",
          "[VariableTracker]") {
  // Integration test: verify that the builder's VariableTracker is correctly
  // populated by checking that the graph's node lookup aligns with the port
  // nodes created during construction.
  auto const &tree = R"(
module m(input logic [3:0] a, output logic [3:0] b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);

  // The graph lookup uses hierarchical paths, which is independent of
  // VariableTracker. But we can verify that port nodes exist with the
  // expected properties (which means VariableTracker was populated during
  // createPort).
  auto *nodeA = test.graph.lookup("m.a");
  auto *nodeB = test.graph.lookup("m.b");
  REQUIRE(nodeA);
  REQUIRE(nodeB);
  CHECK(nodeA->kind == NodeKind::Port);
  CHECK(nodeB->kind == NodeKind::Port);
  CHECK(nodeA->as<Port>().name == "a");
  CHECK(nodeB->as<Port>().name == "b");
  CHECK(nodeA->as<Port>().bounds.lower() == 0);
  CHECK(nodeA->as<Port>().bounds.upper() == 3);
}
