#include "Test.hpp"

#include "netlist/TextLocation.hpp"
#include "netlist/Utilities.hpp"

TEST_CASE("TextLocation with FileTable", "[TextLocation]") {
  FileTable fileTable;
  auto idx = fileTable.addFile("test.sv");
  TextLocation loc(idx, 10, 5);
  CHECK(loc.toString(fileTable) == "test.sv:10:5");
  CHECK_FALSE(loc.empty());
}

TEST_CASE("TextLocation default is empty", "[TextLocation]") {
  FileTable fileTable;
  TextLocation loc;
  CHECK(loc.empty());
  CHECK(loc.toString(fileTable) == "?");
}

TEST_CASE("FileTable deduplicates filenames", "[TextLocation]") {
  FileTable fileTable;
  auto idx1 = fileTable.addFile("foo.sv");
  auto idx2 = fileTable.addFile("bar.sv");
  auto idx3 = fileTable.addFile("foo.sv");
  CHECK(idx1 == idx3);
  CHECK(idx1 != idx2);
  CHECK(fileTable.size() == 2);
  CHECK(fileTable.getFilename(idx1) == "foo.sv");
  CHECK(fileTable.getFilename(idx2) == "bar.sv");
}

TEST_CASE("TextLocation hasSourceLocation", "[TextLocation]") {
  FileTable fileTable;
  auto idx = fileTable.addFile("test.sv");

  // Default-constructed has no source location.
  TextLocation empty;
  CHECK_FALSE(empty.hasSourceLocation());

  // 3-arg constructor has no source location.
  TextLocation noSrc(idx, 1, 1);
  CHECK_FALSE(noSrc.hasSourceLocation());

  // 4-arg constructor with NoLocation has no source location.
  TextLocation noSrc2(idx, 1, 1, SourceLocation::NoLocation);
  CHECK_FALSE(noSrc2.hasSourceLocation());
}

TEST_CASE("TextLocation preserves SourceLocation", "[TextLocation]") {
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

  FileTable fileTable;
  auto &sm = *compilation.getSourceManager();
  auto srcLoc = sym->location;
  auto fileIdx = fileTable.addFile(sm.getFileName(srcLoc));
  TextLocation loc(fileIdx, sm.getLineNumber(srcLoc),
                   sm.getColumnNumber(srcLoc), srcLoc);

  CHECK(loc.hasSourceLocation());
  CHECK(loc.sourceLocation == srcLoc);
  CHECK_FALSE(loc.empty());
  CHECK(loc.toString(fileTable).find(":") != std::string::npos);
}

TEST_CASE("Netlist nodes have valid TextLocations", "[TextLocation]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);

  // FileTable should be populated from the source file.
  CHECK(test.graph.fileTable.size() > 0);

  // Port nodes should have non-empty locations with source locations.
  bool foundPort = false;
  for (auto const &node : test.graph.filterNodes(NodeKind::Port)) {
    auto const &port = node->as<Port>();
    CHECK_FALSE(port.location.empty());
    CHECK(port.location.hasSourceLocation());
    CHECK(port.location.line > 0);
    CHECK(port.location.column > 0);
    foundPort = true;
  }
  CHECK(foundPort);

  // Assignment nodes should have non-empty locations.
  bool foundAssign = false;
  for (auto const &node : test.graph.filterNodes(NodeKind::Assignment)) {
    auto const &assign = node->as<Assignment>();
    CHECK_FALSE(assign.location.empty());
    CHECK(assign.location.hasSourceLocation());
    foundAssign = true;
  }
  CHECK(foundAssign);
}

TEST_CASE("Edge SymbolReference has valid TextLocation", "[TextLocation]") {
  auto const &tree = R"(
module m(input logic a, output logic b);
  assign b = a;
endmodule
)";
  NetlistTest test(tree);

  // Find an edge with a symbol reference and check its location.
  bool foundEdge = false;
  for (auto const &node : test.graph) {
    for (auto it = node->begin(); it != node->end(); ++it) {
      auto &edge = **it;
      if (!edge.symbol.empty()) {
        CHECK_FALSE(edge.symbol.location.empty());
        CHECK(edge.symbol.location.hasSourceLocation());
        auto locStr = edge.symbol.location.toString(test.graph.fileTable);
        CHECK(locStr != "?");
        CHECK(locStr.find(":") != std::string::npos);
        foundEdge = true;
        break;
      }
    }
    if (foundEdge) {
      break;
    }
  }
  CHECK(foundEdge);
}
