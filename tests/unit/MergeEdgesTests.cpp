#include "Test.hpp"

#include <utility>
#include <vector>

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

namespace {

using Range = std::pair<int32_t, int32_t>;

/// A graph holding two variables and one interned symbol, with edges added
/// directly rather than through the builder so that the order in which
/// ranges arrive can be controlled exactly.
struct MergeTest {
  NetlistGraph graph;
  NetlistNode &a = addVariable("a");
  NetlistNode &b = addVariable("b");
  SymbolReference const *symbol = intern("a");

  auto addVariable(std::string_view name) -> NetlistNode & {
    return graph.addNode(
        std::make_unique<Variable>(std::string(name), std::string(name),
                                   TextLocation{}, DriverBitRange{0, 31}));
  }

  auto intern(std::string_view name) -> SymbolReference const * {
    return graph.symbolTable.intern(name, name, TextLocation{});
  }

  void addEdge(NetlistNode &source, NetlistNode &target,
               SymbolReference const *edgeSymbol, Range range,
               ast::EdgeKind edgeKind = ast::EdgeKind::None) {
    auto &edge = source.addNewEdge(target);
    edge.setVariable(edgeSymbol, DriverBitRange{range.first, range.second});
    edge.setEdgeKind(edgeKind);
  }

  void addEdge(Range range) { addEdge(a, b, symbol, range); }

  /// Ranges carried by the outgoing edges of @p node, in ascending order.
  auto outEdgeRanges(NetlistNode const &node) const -> std::vector<Range> {
    std::vector<Range> result;
    for (auto const &edge : node.getOutEdges()) {
      result.push_back(edge->bounds.toPair());
    }
    std::ranges::sort(result);
    return result;
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

TEST_CASE("Merge edges: contiguous ranges collapse whatever the arrival order",
          "[MergeEdges]") {
  std::vector<Range> arrival{{0, 3}, {4, 7}, {8, 11}, {16, 19}};
  std::vector<Range> const expected{{0, 11}, {16, 19}};

  do {
    MergeTest test;
    for (auto range : arrival) {
      test.addEdge(range);
    }

    test.graph.mergeParallelEdges();

    CHECK(test.outEdgeRanges(test.a) == expected);
    CHECK(test.b.inDegree() == expected.size());
  } while (std::ranges::next_permutation(arrival).found);
}

TEST_CASE("Merge edges: overlapping ranges collapse", "[MergeEdges]") {
  MergeTest test;
  test.addEdge({4, 11});
  test.addEdge({0, 7});

  test.graph.mergeParallelEdges();

  CHECK(test.outEdgeRanges(test.a) == std::vector<Range>{{0, 11}});
}

TEST_CASE("Merge edges: edges differing in more than range are kept apart",
          "[MergeEdges]") {
  MergeTest test;

  SECTION("distinct symbols") {
    test.addEdge(test.a, test.b, test.symbol, {0, 3});
    test.addEdge(test.a, test.b, test.intern("b"), {4, 7});
  }

  SECTION("distinct edge kinds") {
    test.addEdge(test.a, test.b, test.symbol, {0, 3}, ast::EdgeKind::None);
    test.addEdge(test.a, test.b, test.symbol, {4, 7}, ast::EdgeKind::PosEdge);
  }

  SECTION("unannotated edge alongside an annotated one") {
    test.a.addNewEdge(test.b);
    test.addEdge({0, 3});
    test.addEdge({4, 7});
  }

  test.graph.mergeParallelEdges();

  CHECK(test.graph.numEdges() == 2);
  CHECK(test.b.inDegree() == 2);
}

TEST_CASE("Merge edges: distinct targets are kept apart", "[MergeEdges]") {
  MergeTest test;
  auto &c = test.addVariable("c");
  test.addEdge(test.a, test.b, test.symbol, {0, 3});
  test.addEdge(test.a, c, test.symbol, {4, 7});

  test.graph.mergeParallelEdges();

  CHECK(test.graph.numEdges() == 2);
  CHECK(test.b.inDegree() == 1);
  CHECK(c.inDegree() == 1);
}
