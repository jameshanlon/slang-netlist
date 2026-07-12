#include "Test.hpp"

#include "report/ReportDrivers.hpp"
#include "report/ReportPorts.hpp"
#include "report/ReportVariables.hpp"

#include "slang/text/Json.h"

#include <nlohmann/json.hpp>

using namespace slang::report;
using slang::JsonWriter;
using json = nlohmann::json;

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

  netlist::FormatBuffer buffer;
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

  netlist::FormatBuffer buffer;
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
  ReportVariables reporter(test.compilation, test.analysisManager);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  netlist::FormatBuffer buffer;
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

  netlist::FormatBuffer buffer;
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

  netlist::FormatBuffer buffer;
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

  netlist::FormatBuffer buffer;
  reporter.report(buffer);
  auto result = buffer.str();

  // Should have header but no data rows.
  CHECK(result.find("Direction") != std::string::npos);
  auto lineCount = std::count(result.begin(), result.end(), '\n');
  CHECK(lineCount == 1);
}

//===----------------------------------------------------------------------===//
// JSON output paths
//===----------------------------------------------------------------------===//

TEST_CASE("ReportPorts JSON basic shape", "[Report][JSON]") {
  auto const &tree = R"(
module m(input logic a, output logic [3:0] b);
  assign b = '0;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  REQUIRE(data.is_array());
  REQUIRE(data.size() == 2);

  CHECK(data[0]["name"] == "m.a");
  CHECK(data[0]["direction"] == "In");
  CHECK(data[0]["width"] == 1);
  CHECK(data[0]["netType"] == "wire");
  CHECK(data[0]["location"].is_string());

  CHECK(data[1]["name"] == "m.b");
  CHECK(data[1]["direction"] == "Out");
  CHECK(data[1]["width"] == 4);
}

TEST_CASE("ReportPorts JSON empty module is empty array", "[Report][JSON]") {
  auto const &tree = R"(
module m;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  REQUIRE(data.is_array());
  CHECK(data.empty());
}

TEST_CASE("ReportPorts JSON inout direction", "[Report][JSON]") {
  auto const &tree = R"(
module m(inout wire w);
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportPorts reporter(test.compilation);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  REQUIRE(data.size() == 1);
  CHECK(data[0]["direction"] == "InOut");
  CHECK(data[0]["netType"] == "wire");
}

TEST_CASE("ReportVariables JSON enriched fields", "[Report][JSON]") {
  auto const &tree = R"(
module m;
  logic [7:0] v;
  logic       s;
  assign v = '0;
  assign s = 1'b0;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportVariables reporter(test.compilation, test.analysisManager);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  REQUIRE(data.is_array());

  auto findByName = [&](std::string const &name) -> json {
    for (auto const &v : data) {
      if (v["name"] == name) {
        return v;
      }
    }
    return {};
  };

  auto v = findByName("m.v");
  REQUIRE_FALSE(v.is_null());
  CHECK(v["type"] == "logic[7:0]");
  CHECK(v["width"] == 8);
  CHECK(v["kind"] == "var");
  CHECK(v["drivers"] == 1);

  auto s = findByName("m.s");
  REQUIRE_FALSE(s.is_null());
  CHECK(s["type"] == "logic");
  CHECK(s["width"] == 1);
  CHECK(s["drivers"] == 1);
}

TEST_CASE("ReportVariables JSON net symbol kind", "[Report][JSON]") {
  auto const &tree = R"(
module m;
  wire w;
endmodule
)";
  NetlistTest test(tree);

  test.compilation.unfreeze();
  ReportVariables reporter(test.compilation, test.analysisManager);
  test.compilation.getRoot().visit(reporter);
  test.compilation.freeze();

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  bool sawWire = false;
  for (auto const &v : data) {
    if (v["name"] == "m.w") {
      sawWire = true;
      CHECK(v["kind"] == "wire");
    }
  }
  CHECK(sawWire);
}

TEST_CASE("ReportDrivers JSON nested continuous", "[Report][JSON]") {
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

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  REQUIRE(data.is_array());

  // Each entry should expose value/location/drivers, and drivers is a
  // (possibly empty) array of objects with range/driver/kind/location.
  for (auto const &v : data) {
    CHECK(v.contains("value"));
    CHECK(v.contains("location"));
    REQUIRE(v["drivers"].is_array());
    for (auto const &d : v["drivers"]) {
      CHECK(d.contains("range"));
      CHECK(d.contains("driver"));
      CHECK(d.contains("kind"));
      CHECK(d.contains("location"));
    }
  }

  // The output port should have exactly one continuous driver.
  for (auto const &v : data) {
    if (v["value"] == "m.b") {
      REQUIRE(v["drivers"].size() == 1);
      CHECK(v["drivers"][0]["kind"] == "cont");
      CHECK(v["drivers"][0]["range"] == "[0]");
    }
  }
}

TEST_CASE("ReportDrivers JSON procedural kind", "[Report][JSON]") {
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

  JsonWriter writer;
  reporter.report(writer);
  auto data = json::parse(writer.view());

  bool sawProc = false;
  for (auto const &v : data) {
    if (v["value"] == "m.b") {
      REQUIRE(v["drivers"].size() == 1);
      CHECK(v["drivers"][0]["kind"] == "proc");
      sawProc = true;
    }
  }
  CHECK(sawProc);
}
