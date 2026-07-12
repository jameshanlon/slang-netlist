#include "netlist/BuilderOptions.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/PathFinder.hpp"
#include "netlist/VisitAll.hpp"

#include "common/FormatBuffer.hpp"
#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/internal/catch_context.hpp>
#include <cstdlib>
#include <fstream>

using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;
using namespace slang::netlist;

using namespace slang::analysis;

std::string report(const Diagnostics &diags);

/// A test fixture for netlist tests that manages a compilation, analysis
/// manager, and graph.
struct NetlistTest {

  Compilation compilation;
  AnalysisManager analysisManager;
  NetlistGraph graph;

  NetlistTest(std::string const &text, bool parallel = false,
              size_t parallelRValueThreshold = 1000)
      : NetlistTest(text, BuilderOptions{.parallel = parallel,
                                         .parallelRValueThreshold =
                                             parallelRValueThreshold}) {}

  NetlistTest(std::string const &text, BuilderOptions options) {
    auto tree = SyntaxTree::fromText(text);
    compilation.addSyntaxTree(tree);
    auto diags = compilation.getAllDiagnostics();
    if (!std::ranges::all_of(diags,
                             [](auto &diag) { return !diag.isError(); })) {
      FAIL_CHECK(report(diags));
    }
    VisitAll va;
    compilation.getRoot().visit(va);
    compilation.freeze();
    analysisManager.analyze(compilation);
    graph.build(compilation, analysisManager, options);

#ifdef RENDER_UNITTEST_DOT
    std::string testName =
        Catch::getCurrentContext().getResultCapture()->getCurrentTestName();
    renderDotAndPdf(sanitizeFilename(testName));
#endif
  }

  auto renderDot() const -> std::string {
    netlist::FormatBuffer buffer;
    NetlistDot::render(graph, buffer);
    return buffer.str();
  }

  auto findPath(const std::string &startName,
                const std::string &endName) const {
    auto *start = graph.lookup(startName);
    auto *end = graph.lookup(endName);
    if (!start || !end) {
      return NetlistPath();
    }
    PathFinder pathFinder;
    return pathFinder.find(*start, *end);
  }

  auto pathExists(const std::string &startName,
                  const std::string &endName) const -> bool {
    auto path = findPath(startName, endName);
    return !path.empty();
  }

  auto findCombPath(const std::string &startName,
                    const std::string &endName) const {
    auto *start = graph.lookup(startName);
    auto *end = graph.lookup(endName);
    if (!start || !end) {
      return NetlistPath();
    }
    PathFinder pathFinder;
    return pathFinder.findComb(*start, *end);
  }

  auto combPathExists(const std::string &startName,
                      const std::string &endName) const -> bool {
    auto path = findCombPath(startName, endName);
    return !path.empty();
  }

  auto getDrivers(std::string const &symbolName, netlist::DriverBitRange bounds)
      -> std::vector<netlist::NetlistNode *> {
    return graph.getDrivers(symbolName, bounds);
  }

  auto getBitDrivers(std::string const &symbolName,
                     netlist::DriverBitRange bounds)
      -> std::vector<netlist::NetlistGraph::BitDriver> {
    return graph.getBitDrivers(symbolName, bounds);
  }

  /// Whether any driver of the given symbol bit-range carries the given
  /// hierarchical path. Useful for asserting that a specific LSP either
  /// does or does not drive a particular bit.
  auto hasDriverNamed(std::string const &symbolName,
                      netlist::DriverBitRange bounds,
                      std::string_view driverPath) -> bool {
    auto drivers = graph.getDrivers(symbolName, bounds);
    return std::any_of(drivers.begin(), drivers.end(), [&](auto *n) {
      auto p = n->getHierarchicalPath();
      return p && *p == driverPath;
    });
  }

  /// Sanitize a test name to be a valid filename by replacing non-alphanumeric
  /// characters with hyphens.
  static inline std::string sanitizeFilename(const std::string &name) {
    std::string result = name;
    for (char &c : result) {
      if (!std::isalnum(c)) {
        c = '-';
      }
    }
    return result;
  }

  /// Render a netlist dotfile for a test case and generate a PDF using
  /// Graphviz.
  void renderDotAndPdf(const std::string &testName) {
    std::string dot = renderDot();
    std::string dotFile = testName + ".dot";
    std::string pdfFile = testName + ".pdf";
    // Write dotfile to disk
    std::ofstream ofs(dotFile);
    ofs << dot;
    ofs.close();
    // Run Graphviz dot command
    DEBUG_PRINT("Generating dot file: {}\n", dotFile);
    std::string cmd = "dot -Tpdf -o " + pdfFile + " " + dotFile;
    std::system(cmd.c_str());
  }
};
