#include <catch2/catch_test_macros.hpp>

#include "slang/analysis/AbstractFlowAnalysis.h"
#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

#include "netlist/NetlistGraph.hpp"
#include "netlist/NetlistVisitor.hpp"
#include "netlist/PathFinder.hpp"

using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;
using namespace slang::netlist;

using namespace slang::analysis;

std::string report(const Diagnostics &diags);

inline auto createNetlist(std::string const &text, Compilation &compilation,
                          AnalysisManager &analysisManager,
                          NetlistGraph &netlist) {

  auto tree = SyntaxTree::fromText(text);
  compilation.addSyntaxTree(tree);
  auto diags = compilation.getAllDiagnostics();

  if (!std::ranges::all_of(diags, [](auto &diag) { return !diag.isError(); })) {
    FAIL_CHECK(report(diags));
  }

  compilation.freeze();

  auto design = analysisManager.analyze(compilation);

  NetlistVisitor visitor(compilation, analysisManager, netlist);
  compilation.getRoot().visit(visitor);
}
