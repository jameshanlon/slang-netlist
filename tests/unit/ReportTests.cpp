#include "Test.hpp"

#include "netlist/ReportDrivers.hpp"
#include "netlist/ReportPorts.hpp"
#include "netlist/ReportVariables.hpp"

TEST_CASE("ReportPorts basic", "[Report]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  CHECK(result.find("Direction") != std::string::npos);
  CHECK(result.find("Name") != std::string::npos);
  CHECK(result.find("In") != std::string::npos);
  CHECK(result.find("Out") != std::string::npos);
}

TEST_CASE("ReportPorts multiple ports", "[Report]") {
  auto const &tree = R"(
module m(input logic a, input logic b, output logic c, inout logic d);
  assign c = a;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  CHECK(result.find("m.a") != std::string::npos);
  CHECK(result.find("m.b") != std::string::npos);
  CHECK(result.find("m.c") != std::string::npos);
  CHECK(result.find("m.d") != std::string::npos);
}

TEST_CASE("ReportVariables basic", "[Report]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  logic t;
  always_comb begin
    t = a;
    b = t;
  end
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportVariables reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  CHECK(result.find("Name") != std::string::npos);
  CHECK(result.find("Location") != std::string::npos);
  CHECK(result.find("m.t") != std::string::npos);
}

TEST_CASE("ReportDrivers basic continuous", "[Report]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportDrivers reporter(test.compilation, test.analysisManager);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  CHECK(result.find("Value") != std::string::npos);
  CHECK(result.find("Driver") != std::string::npos);
  CHECK(result.find("Type") != std::string::npos);
  CHECK(result.find("cont") != std::string::npos);
}

TEST_CASE("ReportDrivers procedural", "[Report]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  always_comb begin
    b = a;
  end
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportDrivers reporter(test.compilation, test.analysisManager);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  CHECK(result.find("proc") != std::string::npos);
}

TEST_CASE("ReportPorts empty module", "[Report]") {
  auto const &tree = R"(
module m;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  // Should have header but no data rows.
  CHECK(result.find("Direction") != std::string::npos);
  auto lineCount = std::count(result.begin(), result.end(), '\n');
  CHECK(lineCount == 1);
}
