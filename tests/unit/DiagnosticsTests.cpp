#include "Test.hpp"

#include "netlist/NetlistDiagnostics.hpp"

static auto makeCompilation() {
  auto tree = SyntaxTree::fromText(R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)");
  Compilation compilation;
  compilation.addSyntaxTree(tree);
  compilation.getAllDiagnostics();
  return compilation;
}

TEST_CASE("NetlistDiagnostics construction", "[Diagnostics]") {
  auto compilation = makeCompilation();
  NetlistDiagnostics diags(compilation);
  CHECK(diags.getString().empty());
}

TEST_CASE("NetlistDiagnostics issue InputPort", "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *sym = root.lookupName("m.a");
  REQUIRE(sym);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);
  Diagnostic diag(slang::diag::InputPort, sym->location);
  diag << sym->name;
  diags.issue(diag);

  auto result = diags.getString();
  CHECK(result.find("input port") != std::string::npos);
}

TEST_CASE("NetlistDiagnostics issue Value", "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *sym = root.lookupName("m.b");
  REQUIRE(sym);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);
  Diagnostic diag(slang::diag::Value, sym->location);
  diag << sym->name;
  diags.issue(diag);

  auto result = diags.getString();
  CHECK(result.find("value") != std::string::npos);
}

TEST_CASE("NetlistDiagnostics issue Assignment", "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *sym = root.lookupName("m.a");
  REQUIRE(sym);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);
  Diagnostic diag(slang::diag::Assignment, sym->location);
  diags.issue(diag);

  auto result = diags.getString();
  CHECK(result.find("assignment") != std::string::npos);
}

TEST_CASE("NetlistDiagnostics clear resets output", "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *sym = root.lookupName("m.a");
  REQUIRE(sym);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);
  Diagnostic diag(slang::diag::InputPort, sym->location);
  diag << sym->name;
  diags.issue(diag);
  CHECK_FALSE(diags.getString().empty());

  diags.clear();
  CHECK(diags.getString().empty());
}

TEST_CASE("NetlistDiagnostics multiple diagnostics accumulate",
          "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *symA = root.lookupName("m.a");
  auto *symB = root.lookupName("m.b");
  REQUIRE(symA);
  REQUIRE(symB);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);

  Diagnostic d1(slang::diag::InputPort, symA->location);
  d1 << symA->name;
  diags.issue(d1);

  Diagnostic d2(slang::diag::OutputPort, symB->location);
  d2 << symB->name;
  diags.issue(d2);

  auto result = diags.getString();
  CHECK(result.find("input port") != std::string::npos);
  CHECK(result.find("output port") != std::string::npos);
}

TEST_CASE("NetlistDiagnostics no ANSI colours when disabled", "[Diagnostics]") {
  auto compilation = makeCompilation();
  auto &root = compilation.getRoot();
  root.visit(VisitAll{});

  auto *sym = root.lookupName("m.a");
  REQUIRE(sym);

  NetlistDiagnostics diags(compilation, /*showColours=*/false);
  Diagnostic diag(slang::diag::InputPort, sym->location);
  diag << sym->name;
  diags.issue(diag);

  auto result = diags.getString();
  CHECK(result.find("\033[") == std::string::npos);
  CHECK(result.find("\x1b[") == std::string::npos);
}
