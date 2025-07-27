#pragma once

#include "netlist/DirectedGraph.hpp"
#include "slang/ast/Symbol.h"

namespace slang::netlist {

class NetlistEdge;

enum class NodeKind {
  None = 0,
  Port,
  Assignment,
  Conditional,
  Case,
  Merge,
};

/// Represent a node in the netlist, corresponding to a variable or an
/// operation.
class NetlistNode : public Node<NetlistNode, NetlistEdge> {
  friend class NetlistGraph;

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
    return const_cast<T &>(*(static_cast<const T *>(this)));
  }

private:
  static size_t nextID;
};

class Port : public NetlistNode {
public:
  ast::ArgumentDirection direction;
  ast::Symbol const *internalSymbol;

  Port(ast::ArgumentDirection direction, ast::Symbol const *internalSymbol)
      : NetlistNode(NodeKind::Port), direction(direction),
        internalSymbol(internalSymbol) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Port;
  }

  auto isInput() const { return direction == ast::ArgumentDirection::In; }
  auto isOutput() const { return direction == ast::ArgumentDirection::Out; }
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

class Merge : public NetlistNode {
public:
  Merge() : NetlistNode(NodeKind::Merge) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Merge;
  }
};

} // namespace slang::netlist
