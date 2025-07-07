#pragma once

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/util/IntervalMap.h"

#include "netlist/DirectedGraph.hpp"

namespace slang::netlist {

enum class NodeKind {
  None = 0,
  PortDeclaration,
  VariableDeclaration,
  VariableReference,
  VariableAlias,
  Assignment,
  Conditional,
  Case,
  Join,
};

class NetlistNode;
class NetlistEdge;

/// Represent a node in the netlist, corresponding to a variable or an
/// operation.
class NetlistNode : public Node<NetlistNode, NetlistEdge> {
public:
  size_t ID;
  NodeKind kind;

  NetlistNode(NodeKind kind) : ID(++nextID), kind(kind) {};

  ~NetlistNode() override = default;

  template <typename T> auto as() -> T & {
    SLANG_ASSERT(T::isKind(kind));
    return *(static_cast<T *>(this));
  }

  template <typename T> auto as() const -> const T & {
    SLANG_ASSERT(T::isKind(kind));
    return const_cast<T &>(this->as<T>());
  }

private:
  static size_t nextID;
};

/// A class representing a dependency between two variables in the netlist.
class NetlistEdge : public DirectedEdge<NetlistNode, NetlistEdge> {
public:
  bool disabled{false};

  NetlistEdge(NetlistNode &sourceNode, NetlistNode &targetNode)
      : DirectedEdge(sourceNode, targetNode) {}

  void disable() { disabled = true; }
};

class PortDeclaration : public NetlistNode {
public:
  PortDeclaration(ast::Symbol const &symbol)
      : NetlistNode(NodeKind::PortDeclaration), symbol(symbol) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::PortDeclaration;
  }

  ast::Symbol const &symbol;
};

class VariableDeclaration : public NetlistNode {
public:
  VariableDeclaration(ast::Symbol const &symbol)
      : NetlistNode(NodeKind::VariableDeclaration), symbol(symbol) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::VariableDeclaration;
  }

  ast::Symbol const &symbol;
};

class VariableAlias : public NetlistNode {
public:
  VariableAlias(ast::Symbol const &symbol)
      : NetlistNode(NodeKind::VariableAlias), symbol(symbol) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::VariableAlias;
  }

  ast::Symbol const &symbol;
};

class VariableReference : public NetlistNode {
public:
  ast::ValueSymbol const &symbol;
  analysis::ValueDriver const &driver;
  std::pair<uint64_t, uint64_t> bounds;

  VariableReference(ast::ValueSymbol const &symbol,
                    analysis::ValueDriver const &driver,
                    std::pair<uint64_t, uint64_t> bounds)
      : NetlistNode(NodeKind::VariableReference), symbol(symbol),
        driver(driver), bounds(bounds) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::VariableReference;
  }
};

class Assignment : public NetlistNode {
public:
  Assignment() : NetlistNode(NodeKind::Assignment) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Assignment;
  }
};

class Conditional : public NetlistNode {
public:
  Conditional() : NetlistNode(NodeKind::Conditional) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Conditional;
  }
};

class Case : public NetlistNode {
public:
  Case() : NetlistNode(NodeKind::Case) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Case;
  }
};

class Join : public NetlistNode {
public:
  Join() : NetlistNode(NodeKind::Join) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Join;
  }
};

/// Represent the netlist connectivity of an elaborated design.
class NetlistGraph : public DirectedGraph<NetlistNode, NetlistEdge> {

  using SymbolDriverMap = IntervalMap<uint64_t, NetlistNode *, 8>;
  BumpAllocator allocator;
  SymbolDriverMap::allocator_type mapAllocator;

  // Map graph nodes to the intervals of symbols.
  std::map<ast::ValueSymbol const *, SymbolDriverMap> symbolMap;

public:
  NetlistGraph() : mapAllocator(allocator) {}

  /// Add a node to the graph that represents a bit range of a variable.
  auto addVariable(ast::ValueSymbol const &symbol,
                   analysis::ValueDriver const &driver,
                   std::pair<uint64_t, uint64_t> bounds) -> NetlistNode & {
    auto &node =
        addNode(std::make_unique<VariableReference>(symbol, driver, bounds));
    symbolMap[&symbol].insert(bounds, &node, mapAllocator);
    return node;
  }

  /// Lookup a variable node in the graph by its ValueSymbol and
  /// exact bounds. Return null if a match is not found.
  auto lookupVariable(ast::ValueSymbol const &symbol,
                      std::pair<uint64_t, uint64_t> bounds) -> NetlistNode * {
    if (symbolMap.contains(&symbol)) {
      auto &map = symbolMap[&symbol];
      for (auto it = map.find(bounds); it != map.end(); it++) {
        if (it.bounds() == bounds) {
          return *it;
        }
      }
    }
    return nullptr;
  }
};

} // namespace slang::netlist
