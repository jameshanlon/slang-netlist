#pragma once

#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/expressions/Operator.h"

#include <optional>

namespace slang::netlist {

struct DataFlowAnalysis;

/// The netlist counterpart of slang's binary operator @p op.
auto mapBinaryOperator(ast::BinaryOperator op) -> OperationKind;

/// The netlist counterpart of slang's unary operator @p op, or nullopt
/// for the increment and decrement forms, which mutate their operand and
/// are left opaque.
auto mapUnaryOperator(ast::UnaryOperator op) -> std::optional<OperationKind>;

/// Expands binary, unary and conditional expressions into Operation
/// nodes. Each operator node is wired into the analysis's current node
/// and then becomes the current node for the duration of its operands,
/// so leaf references attach to the innermost enclosing operator without
/// any change to the reference handlers.
class OperationLowering {
public:
  explicit OperationLowering(DataFlowAnalysis &dfa) : dfa(dfa) {}

  /// Visit @p expr as an R-value. An expandable operator becomes an
  /// Operation node whose operands are visited recursively; anything
  /// else is handed to the ordinary expression visitor.
  void visitOperand(ast::Expression const &expr);

private:
  DataFlowAnalysis &dfa;

  /// The netlist operator @p expr denotes, or nullopt when @p expr is
  /// not one of the expanded kinds.
  static auto classify(ast::Expression const &expr)
      -> std::optional<OperationKind>;
};

} // namespace slang::netlist
