#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "netlist/DirectedGraph.hpp"
#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistEdge.hpp"
#include "netlist/TextLocation.hpp"

#include "slang/ast/SemanticFacts.h"
#include "slang/numeric/ConstantValue.h"

namespace slang::netlist {

enum class NodeKind {
  None = 0,
  Port,
  Variable,
  Assignment,
  Conditional,
  Case,
  Merge,
  State,
  Constant,
  Operation,
};

/// A SystemVerilog operator, named independently of slang's own operator
/// enums so that the serialised graph format stays stable across slang
/// upgrades.
enum class OperationKind {
  // Unary.
  UnaryPlus,
  UnaryMinus,
  BitwiseNot,
  LogicalNot,
  ReductionAnd,
  ReductionOr,
  ReductionXor,
  ReductionNand,
  ReductionNor,
  ReductionXnor,
  // Arithmetic.
  Add,
  Subtract,
  Multiply,
  Divide,
  Mod,
  Power,
  // Bitwise.
  BitwiseAnd,
  BitwiseOr,
  BitwiseXor,
  BitwiseXnor,
  // Equality.
  Equality,
  Inequality,
  CaseEquality,
  CaseInequality,
  WildcardEquality,
  WildcardInequality,
  // Relational.
  GreaterThan,
  GreaterThanEqual,
  LessThan,
  LessThanEqual,
  // Logical.
  LogicalAnd,
  LogicalOr,
  LogicalImplication,
  LogicalEquivalence,
  // Shift.
  LogicalShiftLeft,
  LogicalShiftRight,
  ArithmeticShiftLeft,
  ArithmeticShiftRight,
  // Selection.
  Conditional,
};

/// The stable identifier for @p kind, as used in the serialised format.
auto toString(OperationKind kind) -> std::string_view;

/// The SystemVerilog operator token for @p kind, for use in labels and
/// path reports.
auto toSymbol(OperationKind kind) -> std::string_view;

/// The operation kind named by @p str, or nullopt if it names none.
auto operationKindFromString(std::string_view str)
    -> std::optional<OperationKind>;

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

  virtual auto getHierarchicalPath() const -> std::optional<std::string_view> {
    return std::nullopt;
  }

  virtual auto getBounds() const -> std::optional<DriverBitRange> {
    return std::nullopt;
  }

  virtual auto getLocation() const -> std::optional<TextLocation> {
    return std::nullopt;
  }

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

  /// Return true if any other node drives this port.
  auto isDriven() const -> bool { return inDegree() > 0; }

  auto getHierarchicalPath() const -> std::optional<std::string_view> override {
    return hierarchicalPath;
  }

  auto getBounds() const -> std::optional<DriverBitRange> override {
    return bounds;
  }

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
  }
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

  auto getHierarchicalPath() const -> std::optional<std::string_view> override {
    return hierarchicalPath;
  }

  auto getBounds() const -> std::optional<DriverBitRange> override {
    return bounds;
  }

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
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

  auto getHierarchicalPath() const -> std::optional<std::string_view> override {
    return hierarchicalPath;
  }

  auto getBounds() const -> std::optional<DriverBitRange> override {
    return bounds;
  }

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
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

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
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

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
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

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
  }
};

class Merge : public NetlistNode {
public:
  Merge() : NetlistNode(NodeKind::Merge) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Merge;
  }
};

/// A constant-value driver. Sources of edges that originate from literal or
/// constant-foldable expressions, including zero-extension padding bits.
class Constant : public NetlistNode {
public:
  ConstantValue value;
  uint64_t width;
  TextLocation location;

  Constant(ConstantValue value, uint64_t width, TextLocation location)
      : NetlistNode(NodeKind::Constant), value(std::move(value)), width(width),
        location(location) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Constant;
  }

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
  }
};

/// An operator applied within an expression. Names the operator and the
/// type of its result; it carries no operand values and cannot be
/// evaluated. Operators sit upstream of the Assignment node for the
/// segment they belong to.
class Operation : public NetlistNode {
public:
  OperationKind op;
  uint64_t width;
  bool isSigned;
  TextLocation location;

  Operation(OperationKind op, uint64_t width, bool isSigned,
            TextLocation location)
      : NetlistNode(NodeKind::Operation), op(op), width(width),
        isSigned(isSigned), location(location) {}

  static auto isKind(NodeKind otherKind) -> bool {
    return otherKind == NodeKind::Operation;
  }

  auto getLocation() const -> std::optional<TextLocation> override {
    return location;
  }
};

} // namespace slang::netlist
