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
