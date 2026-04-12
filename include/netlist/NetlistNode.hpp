#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/TextLocation.hpp"

#include "slang/ast/SemanticFacts.h"

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

  NetlistNode(NodeKind kind)
      : ID(nextID.fetch_add(1, std::memory_order_relaxed)), kind(kind) {};

  ~NetlistNode() override = default;

  template <typename T> auto as() -> T & {
    SLANG_ASSERT(T::isKind(kind));
    return *(static_cast<T *>(this));
  }

  template <typename T> auto as() const -> const T & {
    SLANG_ASSERT(T::isKind(kind));
    return const_cast<T &>(*(static_cast<const T *>(this)));
  }

  /// Get the hierarchical path of this node, if it has one.
  auto getHierarchicalPath() const -> std::optional<std::string_view>;

  /// Get the bit range bounds of this node, if it has them.
  auto getBounds() const -> std::optional<DriverBitRange>;

  /// Get the source location of this node, if it has one.
  auto getLocation() const -> std::optional<TextLocation>;

private:
  static std::atomic<size_t> nextID;
};

class Port : public NetlistNode {
public:
  std::string name;
  std::string hierarchicalPath;
  TextLocation location;
  ast::ArgumentDirection direction;
  DriverBitRange bounds;

  Port(std::string name, std::string hierarchicalPath, TextLocation location,
       ast::ArgumentDirection direction, DriverBitRange bounds)
      : NetlistNode(NodeKind::Port), name(std::move(name)),
        hierarchicalPath(std::move(hierarchicalPath)), location(location),
        direction(direction), bounds(std::move(bounds)) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Port;
  }

  auto isInput() const { return direction == ast::ArgumentDirection::In; }
  auto isOutput() const { return direction == ast::ArgumentDirection::Out; }
};

class Variable : public NetlistNode {
public:
  std::string name;
  std::string hierarchicalPath;
  TextLocation location;
  DriverBitRange bounds;

  Variable(std::string name, std::string hierarchicalPath,
           TextLocation location, DriverBitRange bounds)
      : NetlistNode(NodeKind::Variable), name(std::move(name)),
        hierarchicalPath(std::move(hierarchicalPath)), location(location),
        bounds(std::move(bounds)) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Variable;
  }
};

class State : public NetlistNode {
public:
  std::string name;
  std::string hierarchicalPath;
  TextLocation location;
  DriverBitRange bounds;

  State(std::string name, std::string hierarchicalPath, TextLocation location,
        DriverBitRange bounds)
      : NetlistNode(NodeKind::State), name(std::move(name)),
        hierarchicalPath(std::move(hierarchicalPath)), location(location),
        bounds(std::move(bounds)) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::State;
  }
};

class Assignment : public NetlistNode {
public:
  TextLocation location;

  Assignment(TextLocation location)
      : NetlistNode(NodeKind::Assignment), location(location) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Assignment;
  }
};

class Conditional : public NetlistNode {
public:
  TextLocation location;

  Conditional(TextLocation location)
      : NetlistNode(NodeKind::Conditional), location(location) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Conditional;
  }
};

class Case : public NetlistNode {
public:
  TextLocation location;

  Case(TextLocation location)
      : NetlistNode(NodeKind::Case), location(location) {}

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

inline auto NetlistNode::getHierarchicalPath() const
    -> std::optional<std::string_view> {
  switch (kind) {
  case NodeKind::Port:
    return as<Port>().hierarchicalPath;
  case NodeKind::Variable:
    return as<Variable>().hierarchicalPath;
  case NodeKind::State:
    return as<State>().hierarchicalPath;
  default:
    return std::nullopt;
  }
}

inline auto NetlistNode::getBounds() const -> std::optional<DriverBitRange> {
  switch (kind) {
  case NodeKind::Port:
    return as<Port>().bounds;
  case NodeKind::Variable:
    return as<Variable>().bounds;
  case NodeKind::State:
    return as<State>().bounds;
  default:
    return std::nullopt;
  }
}

inline auto NetlistNode::getLocation() const -> std::optional<TextLocation> {
  switch (kind) {
  case NodeKind::Port:
    return as<Port>().location;
  case NodeKind::Variable:
    return as<Variable>().location;
  case NodeKind::State:
    return as<State>().location;
  case NodeKind::Assignment:
    return as<Assignment>().location;
  case NodeKind::Conditional:
    return as<Conditional>().location;
  case NodeKind::Case:
    return as<Case>().location;
  default:
    return std::nullopt;
  }
}

} // namespace slang::netlist
