#pragma once

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"

#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/ProceduralAnalysis.hpp"
#include <slang/ast/symbols/ValueSymbol.h>

namespace slang::netlist {

struct NetlistVisitor : public ast::ASTVisitor<NetlistVisitor, false, true> {
  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  NetlistGraph &graph;

public:
  explicit NetlistVisitor(ast::Compilation &compilation,
                          analysis::AnalysisManager &analysisManager,
                          NetlistGraph &graph)
      : compilation(compilation), analysisManager(analysisManager),
        graph(graph) {}

  void handle(const ast::ValueSymbol &symbol) {
    DEBUG_PRINT("ValueSymbol {}\n", symbol.name);
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[valueSymbol, bitRange] : drivers) {
      DEBUG_PRINT("Driven by {} [{}:{}]\n", toString(valueSymbol->kind),
                  bitRange.first, bitRange.second);
    }
  }

  void handle(const ast::ProceduralBlockSymbol &symbol) {
    DEBUG_PRINT("ProceduralBlock\n");
    ProceduralAnalysis dfa(symbol, graph);
    dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  }
};

} // namespace slang::netlist
