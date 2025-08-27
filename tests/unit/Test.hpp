#include "netlist/IntervalMapUtils.hpp"
#include "netlist/NetlistDot.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistVisitor.hpp"
#include "netlist/PathFinder.hpp"
#include <catch2/catch_test_macros.hpp>

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/FormatBuffer.h"

using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;
using namespace slang::netlist;

using namespace slang::analysis;

std::string report(const Diagnostics &diags);

/// A test fixture for netlist tests that sets up a compilation and analysis
/// manager.
struct NetlistTest {

  Compilation compilation;
  AnalysisManager analysisManager;
  NetlistGraph netlist;

  NetlistTest(std::string const &text) {

    auto tree = SyntaxTree::fromText(text);
    compilation.addSyntaxTree(tree);
    auto diags = compilation.getAllDiagnostics();

    if (!std::ranges::all_of(diags,
                             [](auto &diag) { return !diag.isError(); })) {
      FAIL_CHECK(report(diags));
    }

    compilation.freeze();

    auto design = analysisManager.analyze(compilation);

    NetlistVisitor visitor(compilation, analysisManager, netlist);
    compilation.getRoot().visit(visitor);
    netlist.finalize();
  }

  auto renderDot() const -> std::string {
    FormatBuffer buffer;
    NetlistDot::render(netlist, buffer);
    return buffer.str();
  }

  auto findPath(const std::string &startName,
                const std::string &endName) const {
    auto *start = netlist.lookup(startName);
    auto *end = netlist.lookup(endName);
    if (!start || !end) {
      return NetlistPath();
    }
    PathFinder pathFinder(netlist);
    return pathFinder.find(*start, *end);
  }

  auto pathExists(const std::string &startName,
                  const std::string &endName) const -> bool {
    auto path = findPath(startName, endName);
    return !path.empty();
  }
};
