#pragma once

#include <string>

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"

#include "slang/ast/SemanticFacts.h"
#include "slang/text/SourceLocation.h"

namespace slang::netlist {

class NetlistNode;

/// A class representing a dependency between two nodes in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  ast::EdgeKind edgeKind{ast::EdgeKind::None};
  std::string symbolName;
  std::string symbolHierarchicalPath;
  SourceLocation symbolLocation;
  DriverBitRange bounds;
  bool disabled{false};

  NetlistEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}

  auto setEdgeKind(ast::EdgeKind kind) { this->edgeKind = kind; }

  auto setVariable(std::string_view name, std::string_view path,
                   SourceLocation loc, DriverBitRange bounds) {
    this->symbolName = name;
    this->symbolHierarchicalPath = path;
    this->symbolLocation = loc;
    this->bounds = bounds;
  }

  void disable() { disabled = true; }
};

} // namespace slang::netlist
