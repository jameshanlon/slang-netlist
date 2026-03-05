#pragma once

#include <string>

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"

#include "slang/ast/SemanticFacts.h"
#include "slang/text/SourceLocation.h"

namespace slang::netlist {

class NetlistNode;

/// Extracted identity of an AST symbol, decoupled from the live AST.
struct SymbolReference {
  std::string name;
  std::string hierarchicalPath;
  SourceLocation location;

  SymbolReference() = default;
  SymbolReference(std::string name, std::string hierarchicalPath,
                  SourceLocation location)
      : name(std::move(name)), hierarchicalPath(std::move(hierarchicalPath)),
        location(location) {}

  auto empty() const -> bool { return name.empty(); }
};

/// A class representing a dependency between two nodes in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  ast::EdgeKind edgeKind{ast::EdgeKind::None};
  SymbolReference symbol;
  DriverBitRange bounds;
  bool disabled{false};

  NetlistEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}

  auto setEdgeKind(ast::EdgeKind kind) { this->edgeKind = kind; }

  auto setVariable(SymbolReference sym, DriverBitRange bounds) {
    this->symbol = std::move(sym);
    this->bounds = bounds;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
