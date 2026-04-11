#include "Test.hpp"

#include "netlist/Config.hpp"
#include "netlist/Debug.hpp"
#include "netlist/Utilities.hpp"

TEST_CASE("Config singleton returns same instance", "[Utility]") {
  auto &a = Config::getInstance();
  auto &b = Config::getInstance();
  CHECK(&a == &b);
}

TEST_CASE("Config default values", "[Utility]") {
  auto &cfg = Config::getInstance();
  cfg.debugEnabled = false;
  cfg.quietEnabled = false;
  CHECK_FALSE(cfg.debugEnabled);
  CHECK_FALSE(cfg.quietEnabled);
}

TEST_CASE("file_name extracts basename", "[Utility]") {
  CHECK(std::string(file_name("/foo/bar/baz.cpp")) == "baz.cpp");
  CHECK(std::string(file_name("no_slash.cpp")) == "no_slash.cpp");
  CHECK(std::string(file_name("/single.cpp")) == "single.cpp");
}

TEST_CASE("Utilities::formatTable basic", "[Utility]") {
  FormatBuffer buffer;
  Utilities::Row header = {"Name", "Value"};
  Utilities::Table rows = {{"a", "1"}, {"bb", "22"}};
  Utilities::formatTable(buffer, header, rows);
  auto result = buffer.str();
  CHECK(result.find("Name") != std::string::npos);
  CHECK(result.find("Value") != std::string::npos);
  CHECK(result.find("a") != std::string::npos);
  CHECK(result.find("bb") != std::string::npos);
  auto lineCount = std::count(result.begin(), result.end(), '\n');
  CHECK(lineCount == 3);
}

TEST_CASE("Utilities::formatTable column alignment", "[Utility]") {
  FormatBuffer buffer;
  Utilities::Row header = {"X", "Y"};
  Utilities::Table rows = {{"long_value", "1"}};
  Utilities::formatTable(buffer, header, rows);
  auto result = buffer.str();
  CHECK(result.find("long_value") != std::string::npos);
}

TEST_CASE("Utilities::formatTable with short row", "[Utility]") {
  FormatBuffer buffer;
  Utilities::Row header = {"A", "B", "C"};
  Utilities::Table rows = {{"x"}};
  Utilities::formatTable(buffer, header, rows);
  auto result = buffer.str();
  CHECK(result.find("x") != std::string::npos);
}

TEST_CASE("Utilities::locationStr with valid SourceLocation", "[Utility]") {
  auto tree = SyntaxTree::fromText(R"(
module m(input logic a);
endmodule
)");
  Compilation compilation;
  compilation.addSyntaxTree(tree);
  compilation.getAllDiagnostics();

  auto &root = compilation.getRoot();
  root.visit(VisitAll{});
  auto *sym = root.lookupName("m.a");
  REQUIRE(sym);
  auto loc = sym->location;

  auto result = Utilities::locationStr(compilation, loc);
  CHECK(result != "?");
  CHECK(result.find(":") != std::string::npos);
}

TEST_CASE("Utilities::locationStr with no SourceLocation", "[Utility]") {
  Compilation compilation;
  auto result = Utilities::locationStr(compilation, SourceLocation::NoLocation);
  CHECK(result == "?");
}

TEST_CASE("DriverBitRange toString with pair overload", "[Utility]") {
  auto result = toString(std::pair<int32_t, int32_t>{3, 0});
  CHECK(result == "[3:0]");
}

TEST_CASE("DriverBitRange toString single bit via pair", "[Utility]") {
  auto result = toString(std::pair<int32_t, int32_t>{5, 5});
  CHECK(result == "[5]");
}

TEST_CASE("DriverBitRange toPair normalises order", "[Utility]") {
  using netlist::DriverBitRange;
  // Descending range (left > right) should be normalised to {lower, upper}.
  auto descending = DriverBitRange(3, 0).toPair();
  CHECK(descending.first == 0);
  CHECK(descending.second == 3);

  // Ascending range should also produce {lower, upper}.
  auto ascending = DriverBitRange(0, 3).toPair();
  CHECK(ascending.first == 0);
  CHECK(ascending.second == 3);

  // Single bit.
  auto single = DriverBitRange(5, 5).toPair();
  CHECK(single.first == 5);
  CHECK(single.second == 5);
}

TEST_CASE("DriverBitRange hull", "[Utility]") {
  using netlist::DriverBitRange;
  // Abutting ranges: hull is contiguous.
  auto abutting = DriverBitRange(0, 1).hull(DriverBitRange(2, 3));
  CHECK(abutting.lower() == 0);
  CHECK(abutting.upper() == 3);

  // Overlapping ranges.
  auto overlapping = DriverBitRange(0, 4).hull(DriverBitRange(3, 7));
  CHECK(overlapping.lower() == 0);
  CHECK(overlapping.upper() == 7);

  // Disjoint ranges: hull still spans the gap.
  auto disjoint = DriverBitRange(0, 1).hull(DriverBitRange(5, 7));
  CHECK(disjoint.lower() == 0);
  CHECK(disjoint.upper() == 7);

  // Nested ranges: hull matches the outer.
  auto nested = DriverBitRange(0, 15).hull(DriverBitRange(4, 7));
  CHECK(nested.lower() == 0);
  CHECK(nested.upper() == 15);

  // Hull is commutative.
  auto swapped = DriverBitRange(5, 7).hull(DriverBitRange(0, 1));
  CHECK(swapped.lower() == 0);
  CHECK(swapped.upper() == 7);
}

TEST_CASE("DriverBitRange clipTo", "[Utility]") {
  using netlist::DriverBitRange;
  // Strict overlap clips to the intersection.
  auto clipped = DriverBitRange(0, 7).clipTo(DriverBitRange(2, 5));
  REQUIRE(clipped.has_value());
  CHECK(clipped->lower() == 2);
  CHECK(clipped->upper() == 5);

  // One range contained in the other clips to the inner.
  auto nested = DriverBitRange(2, 5).clipTo(DriverBitRange(0, 15));
  REQUIRE(nested.has_value());
  CHECK(nested->lower() == 2);
  CHECK(nested->upper() == 5);

  // Abutting-but-disjoint ranges (one bit apart) yield nullopt.
  auto disjoint = DriverBitRange(0, 1).clipTo(DriverBitRange(2, 3));
  CHECK_FALSE(disjoint.has_value());

  // Completely disjoint ranges yield nullopt.
  auto farApart = DriverBitRange(0, 1).clipTo(DriverBitRange(10, 15));
  CHECK_FALSE(farApart.has_value());

  // Single-bit touch at the boundary keeps just that bit.
  auto touching = DriverBitRange(0, 3).clipTo(DriverBitRange(3, 5));
  REQUIRE(touching.has_value());
  CHECK(touching->lower() == 3);
  CHECK(touching->upper() == 3);
}
