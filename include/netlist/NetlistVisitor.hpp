#pragma once

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/LSPUtilities.h"

#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"
#include "netlist/ProceduralAnalysis.hpp"
#include <slang/ast/symbols/ValueSymbol.h>

namespace slang::netlist {

struct NetlistVisitor : public ast::ASTVisitor<NetlistVisitor, false, true> {
  ast::Compilation &compilation;
  analysis::AnalysisManager &analysisManager;
  NetlistGraph &graph;

  static std::string getLSPName(const ast::ValueSymbol &symbol,
                                const analysis::ValueDriver &driver) {
    FormatBuffer buf;
    ast::EvalContext evalContext(symbol);
    ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
    return buf.str();
  }

public:
  explicit NetlistVisitor(ast::Compilation &compilation,
                          analysis::AnalysisManager &analysisManager,
                          NetlistGraph &graph)
      : compilation(compilation), analysisManager(analysisManager),
        graph(graph) {}

  void handle(const ast::PortSymbol &symbol) {
    DEBUG_PRINT("PortSymbol {}\n", symbol.name);
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[driver, bitRange] : drivers) {
      DEBUG_PRINT("  Driven by {} [{}:{}]\n", toString(driver->kind),
                  bitRange.first, bitRange.second);
    }
  }

  void handle(const ast::ValueSymbol &symbol) {
    DEBUG_PRINT("ValueSymbol {}\n", symbol.name);
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[driver, bitRange] : drivers) {
      DEBUG_PRINT("  Driven by {} [{}:{}]\n", toString(driver->kind),
                  bitRange.first, bitRange.second);

      // Add a variable node to the graph for this symbol driver.
      // auto &node = graph.addVariable(symbol, *driver, bitRange);

      // if (driver->kind == analysis::DriverKind::Continuous) {
      //   DEBUG_PRINT("  Continuous driver {}\n", getLSPName(symbol, *driver));
      // }
    }
  }

  void handle(const ast::ProceduralBlockSymbol &symbol) {
    DEBUG_PRINT("ProceduralBlock\n");
    ProceduralAnalysis dfa(analysisManager, symbol, graph);
    dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  }

  void handle(const ast::ContinuousAssignSymbol &symbol) {
    DEBUG_PRINT("ContinuousAssign\n");
    ProceduralAnalysis dfa(analysisManager, symbol, graph);
    dfa.run(symbol.getAssignment());
  }
};

} // namespace slang::netlist
