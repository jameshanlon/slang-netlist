#pragma once

#include "netlist/DataFlowAnalysis.hpp"
#include "netlist/Debug.hpp"
#include "netlist/NetlistGraph.hpp"
#include <slang/ast/symbols/ValueSymbol.h>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/LSPUtilities.h"

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
                                const analysis::ValueDriver &driver) {
    FormatBuffer buf;
    ast::EvalContext evalContext(symbol);
    ast::LSPUtilities::stringifyLSP(*driver.prefixExpression, evalContext, buf);
    return buf.str();
  }

  /// Determine the egde type to apply within a procedrual
  /// block.
  static ast::EdgeKind
  determineEdgeKind(ast::ProceduralBlockSymbol const &symbol) {
    ast::EdgeKind result = ast::EdgeKind::None;
    if (symbol.procedureKind == ast::ProceduralBlockKind::AlwaysFF ||
        symbol.procedureKind == ast::ProceduralBlockKind::Always) {
      if (symbol.getBody().kind == ast::StatementKind::Block) {
        auto &block = symbol.getBody().as<ast::BlockStatement>();
        if (block.blockKind == ast::StatementBlockKind::Sequential &&
            block.body.kind == ast::StatementKind::ConcurrentAssertion) {
          return result;
        }
      }
      auto tck = symbol.getBody().as<ast::TimedStatement>().timing.kind;
      if (tck == ast::TimingControlKind::SignalEvent) {
        result = symbol.getBody()
                     .as<ast::TimedStatement>()
                     .timing.as<ast::SignalEventControl>()
                     .edge;
      } else if (tck == ast::TimingControlKind::EventList) {
        auto &events = symbol.getBody()
                           .as<ast::TimedStatement>()
                           .timing.as<ast::EventListControl>()
                           .events;
        // We need to decide if this has the potential for combinatorial loops
        // The most strict test is if for any unique signal on the event list
        // only one edge (pos or neg) appears e.g. "@(posedge x or negedge x)"
        // is potentially combinatorial. At the moment we'll settle for no
        // signal having "None" edge.
        for (auto const &e : events) {
          result = e->as<ast::SignalEventControl>().edge;
          if (result == ast::EdgeKind::None) {
            break;
          }
        }
        // If we got here, edgeKind is not "None" which is all we care about.
      }
    }
    return result;
  }

public:
  explicit NetlistVisitor(ast::Compilation &compilation,
                          analysis::AnalysisManager &analysisManager,
                          NetlistGraph &graph)
      : compilation(compilation), analysisManager(analysisManager),
        graph(graph) {}

  void handle(const ast::PortSymbol &symbol) {
    DEBUG_PRINT("PortSymbol {}\n", symbol.name);
    graph.addPort(symbol);
  }

  void handle(const ast::ValueSymbol &symbol) {
    DEBUG_PRINT("ValueSymbol {}\n", symbol.name);
    auto drivers = analysisManager.getDrivers(symbol);
    for (auto &[driver, bounds] : drivers) {
      DEBUG_PRINT("  Driven by {} [{}:{}] prefix={}\n", toString(driver->kind),
                  bounds.first, bounds.second, getLSPName(symbol, *driver));

      if (driver->isInputPort()) {
        DEBUG_PRINT("  driven by input port\n");
        graph.connectInputPort(symbol, bounds);
      } else if (driver->flags.has(analysis::DriverFlags::OutputPort)) {
        DEBUG_PRINT("  driving output port\n");
        // Connection of output ports is handled in the port connection
        // code of the instance symbol visitor below.
      }
    }
  }

  void handle(const ast::InstanceSymbol &symbol) {
    DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);
    if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
      DEBUG_PRINT("(Uninstantiated)\n");
      return;
    }

    symbol.body.visit(*this);

    // Handle port connections.
    for (auto portConnection : symbol.getPortConnections()) {
      if (portConnection->port.kind == ast::SymbolKind::Port) {
        auto &port = portConnection->port.as<ast::PortSymbol>();
        auto direction = portConnection->port.as<ast::PortSymbol>().direction;
        auto *internalSymbol = port.internalSymbol;

        if (direction == ast::ArgumentDirection::In) {
          DEBUG_PRINT("Input port int sym={}\n", internalSymbol->name);
        } else if (direction == ast::ArgumentDirection::Out) {
          DEBUG_PRINT("Output port int sym={}\n", internalSymbol->name);
        } else {
          DEBUG_PRINT("Unhandled port connection type\n");
        }

        if (portConnection->getExpression() == nullptr) {
          // Empty port hookup so skip.
          continue;
        }

        // Lookup the port node in the graph by the internal symbol.
        // Run the DFA to hookup values to or from the port node
        // depending on its direction.
        auto node = graph.getPort(port.internalSymbol);
        DataFlowAnalysis dfa(analysisManager, symbol, graph, *node);
        dfa.run(*portConnection->getExpression());
        graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions);

        // Special handling for output ports to create a dependency
        // between the port netlist node and the assignment of the port
        // to the connection expression. The DFA produces an assignment
        // node, so connect to that via the final DFA state.
        if (direction == ast::ArgumentDirection::Out) {
          SLANG_ASSERT(dfa.getState().node);
          auto &edge = graph.addEdge(**graph.getPort(port.internalSymbol),
                                     *dfa.getState().node);
        }
      } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
        DEBUG_PRINT("Unhandled interface port connection\n");
      } else {
        SLANG_UNREACHABLE;
      }
    }
  }

  void handle(const ast::ProceduralBlockSymbol &symbol) {
    DEBUG_PRINT("ProceduralBlock\n");
    auto edgeKind = determineEdgeKind(symbol);
    DataFlowAnalysis dfa(analysisManager, symbol, graph);
    dfa.run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
    dfa.finalize();
    graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions, edgeKind);
  }

  void handle(const ast::ContinuousAssignSymbol &symbol) {
    DEBUG_PRINT("ContinuousAssign\n");
    DataFlowAnalysis dfa(analysisManager, symbol, graph);
    dfa.run(symbol.getAssignment());
    graph.mergeDrivers(dfa.symbolToSlot, dfa.getState().definitions,
                       ast::EdgeKind::None);
  }

  void handle(const ast::GenerateBlockSymbol &symbol) {
    // Skip uninstantiated generate blocks.
    if (!symbol.isUninstantiated) {
      visitMembers(symbol);
    }
  }

private:
  template <typename T> void visitMembers(const T &symbol) {
    for (auto &member : symbol.members())
      member.visit(*this);
  }
};

} // namespace slang::netlist
