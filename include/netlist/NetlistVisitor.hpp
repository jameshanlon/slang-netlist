#pragma once

#include "netlist/DataFlowAnalysis.hpp"
#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace slang::netlist {

/// Visitor for building the netlist graph from the AST.
struct NetlistVisitor : public ast::ASTVisitor<NetlistVisitor,
                                               /*VisitStatements=*/false,
                                               /*VisitExpressions=*/true,
                                               /*VisitBad=*/false,
                                               /*VisitCanonical=*/true> {
  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  NetlistGraph &graph;

  static std::string getLSPName(const ast::ValueSymbol &symbol,
                                const analysis::ValueDriver &driver);

  /// Determine the edge type to apply within a procedural
  /// block.
  static ast::EdgeKind
  determineEdgeKind(ast::ProceduralBlockSymbol const &symbol);

public:
  explicit NetlistVisitor(ast::Compilation &compilation,
                          analysis::AnalysisManager &analysisManager,
                          NetlistGraph &graph);

  void handle(const ast::PortSymbol &symbol);
  void handle(const ast::ModportPortSymbol &symbol);
  void handle(const ast::InstanceSymbol &symbol);
  void handle(const ast::ProceduralBlockSymbol &symbol);
  void handle(const ast::ContinuousAssignSymbol &symbol);
  void handle(const ast::GenerateBlockSymbol &symbol);

private:
  template <typename T> void visitMembers(const T &symbol) {
    for (auto &member : symbol.members())
      member.visit(*this);
  }
};

} // namespace slang::netlist
