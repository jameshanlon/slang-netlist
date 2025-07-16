#pragma once

#include "netlist/DirectedGraph.hpp"
#include "slang/ast/Symbol.h"

namespace slang::netlist {

class NetlistEdge;

enum class NodeKind {
  None = 0,
  Input,
  VariableDeclaration,
  VariableReference,
  Assignment,
  Conditional,
  Case,
  Join,
  Merge,
  Meet,
  Split,
};

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

class Input : public NetlistNode {
public:
  ast::Symbol const &symbol;

  Input(ast::Symbol const &symbol)
      : NetlistNode(NodeKind::Input), symbol(symbol) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Input;
  }
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

class VariableReference : public NetlistNode {
public:
  ast::ValueSymbol const &symbol;
  std::pair<uint64_t, uint64_t> bounds;

  VariableReference(ast::ValueSymbol const &symbol,
                    std::pair<uint64_t, uint64_t> bounds)
      : NetlistNode(NodeKind::VariableReference), symbol(symbol),
        bounds(bounds) {}

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

class Merge : public NetlistNode {
public:
  Merge() : NetlistNode(NodeKind::Merge) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Merge;
  }
};

class Meet : public NetlistNode {
public:
  Meet() : NetlistNode(NodeKind::Meet) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Meet;
  }
};

class Split : public NetlistNode {
public:
  Split() : NetlistNode(NodeKind::Split) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Split;
  }
};

} // namespace slang::netlist
