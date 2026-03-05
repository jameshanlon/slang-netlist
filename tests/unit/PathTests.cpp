#include "Test.hpp"

TEST_CASE("NetlistPath constructor from node list", "[Path]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  CHECK_FALSE(path.empty());
  CHECK(path.size() > 0);

  // Test the NodeListType constructor.
  NetlistPath::NodeListType nodeList;
  for (size_t i = 0; i < path.size(); ++i) {
    nodeList.push_back(path[i]);
  }
  NetlistPath pathCopy(nodeList);
  CHECK(pathCopy.size() == path.size());
}

TEST_CASE("NetlistPath add pointer overload", "[Path]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  REQUIRE(path.size() >= 1);

  NetlistPath newPath;
  auto *firstNode = const_cast<NetlistNode *>(path[0]);
  newPath.add(firstNode);
  CHECK(newPath.size() == 1);
}

TEST_CASE("NetlistPath front and back", "[Path]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  REQUIRE(path.size() >= 2);
  auto *first = path.front();
  auto *last = path.back();
  CHECK(first != nullptr);
  CHECK(last != nullptr);
  CHECK(first != last);
}

TEST_CASE("NetlistPath iteration and indexing", "[Path]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  REQUIRE(path.size() >= 2);

  CHECK(path[0] == path.front());
  CHECK(path[path.size() - 1] == path.back());

  // Test const iteration.
  const auto &constPath = path;
  size_t count = 0;
  for (auto it = constPath.begin(); it != constPath.end(); ++it) {
    CHECK(*it != nullptr);
    count++;
  }
  CHECK(count == path.size());
}

TEST_CASE("NetlistPath clear", "[Path]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  const NetlistTest test(tree);
  auto path = test.findPath("m.a", "m.b");
  CHECK_FALSE(path.empty());
  path.clear();
  CHECK(path.empty());
  CHECK(path.size() == 0);
}
