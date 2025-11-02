#pragma once

#include <utility>

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverMap.hpp"

#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

namespace slang::netlist {

class NetlistEdge;

enum class NodeKind {
  None = 0,
  Port,
  Variable,
  Assignment,
  Conditional,
  Case,
  Merge,
  State,
};

/// Represent a node in the netlist, corresponding to a variable or an
/// operation.
class NetlistNode : public Node<NetlistNode, NetlistEdge> {
  friend class NetlistBuilder;

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
  ast::PortSymbol const &symbol;
  DriverBitRange bounds;

  Port(ast::PortSymbol const &symbol, DriverBitRange bounds)
      : NetlistNode(NodeKind::Port), symbol(symbol), bounds(std::move(bounds)) {
  }

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Port;
  }

  auto isInput() const {
    return symbol.direction == ast::ArgumentDirection::In;
  }
  auto isOutput() const {
    return symbol.direction == ast::ArgumentDirection::Out;
  }
};

class Variable : public NetlistNode {
public:
  ast::VariableSymbol const &symbol;
  DriverBitRange bounds;

  Variable(ast::VariableSymbol const &symbol, DriverBitRange bounds)
      : NetlistNode(NodeKind::Variable), symbol(symbol),
        bounds(std::move(bounds)) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Variable;
  }
};

class State : public NetlistNode {
public:
  ast::ValueSymbol const &symbol;
  DriverBitRange bounds;

  State(ast::ValueSymbol const &symbol, DriverBitRange bounds)
      : NetlistNode(NodeKind::State), symbol(symbol),
        bounds(std::move(bounds)) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::State;
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

} // namespace slang::netlist
