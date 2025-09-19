#pragma once

#include "netlist/DirectedGraph.hpp"

#include "slang/ast/Symbol.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"

namespace slang::netlist {

class NetlistEdge;

enum class NodeKind {
  None = 0,
  Port,
  Modport,
  Assignment,
  Conditional,
  Case,
  Merge,
  State,
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

class Modport : public NetlistNode {
public:
  Modport() : NetlistNode(NodeKind::Modport) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Modport;
  }
};

class Assignment : public NetlistNode {
public:
  ast::AssignmentExpression const &expr;

  Assignment(ast::AssignmentExpression const &expr)
      : NetlistNode(NodeKind::Assignment), expr(expr) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Assignment;
  }
};

class Conditional : public NetlistNode {
public:
  ast::ConditionalStatement const &stmt;

  Conditional(ast::ConditionalStatement const &stmt)
      : NetlistNode(NodeKind::Conditional), stmt(stmt) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Conditional;
  }
};

class Case : public NetlistNode {
public:
  ast::CaseStatement const &stmt;

  Case(ast::CaseStatement const &stmt)
      : NetlistNode(NodeKind::Case), stmt(stmt) {}

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

class State : public NetlistNode {
public:
  ast::ValueSymbol const *symbol{nullptr};
  std::pair<uint64_t, uint64_t> bounds;

  State(ast::ValueSymbol const *symbol, std::pair<uint64_t, uint64_t> bounds)
      : NetlistNode(NodeKind::State), symbol(symbol), bounds(bounds) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::State;
  }
};

} // namespace slang::netlist
