#include "OperationLowering.hpp"

#include "DataFlowAnalysis.hpp"
#include "NetlistBuilder.hpp"

#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/types/Type.h"

namespace slang::netlist {

auto mapBinaryOperator(ast::BinaryOperator op) -> OperationKind {
  // No default: a slang upgrade that adds an operator must be handled
  // explicitly rather than silently misclassified.
  switch (op) {
  case ast::BinaryOperator::Add:
    return OperationKind::Add;
  case ast::BinaryOperator::Subtract:
    return OperationKind::Subtract;
  case ast::BinaryOperator::Multiply:
    return OperationKind::Multiply;
  case ast::BinaryOperator::Divide:
    return OperationKind::Divide;
  case ast::BinaryOperator::Mod:
    return OperationKind::Mod;
  case ast::BinaryOperator::BinaryAnd:
    return OperationKind::BitwiseAnd;
  case ast::BinaryOperator::BinaryOr:
    return OperationKind::BitwiseOr;
  case ast::BinaryOperator::BinaryXor:
    return OperationKind::BitwiseXor;
  case ast::BinaryOperator::BinaryXnor:
    return OperationKind::BitwiseXnor;
  case ast::BinaryOperator::Equality:
    return OperationKind::Equality;
  case ast::BinaryOperator::Inequality:
    return OperationKind::Inequality;
  case ast::BinaryOperator::CaseEquality:
    return OperationKind::CaseEquality;
  case ast::BinaryOperator::CaseInequality:
    return OperationKind::CaseInequality;
  case ast::BinaryOperator::GreaterThanEqual:
    return OperationKind::GreaterThanEqual;
  case ast::BinaryOperator::GreaterThan:
    return OperationKind::GreaterThan;
  case ast::BinaryOperator::LessThanEqual:
    return OperationKind::LessThanEqual;
  case ast::BinaryOperator::LessThan:
    return OperationKind::LessThan;
  case ast::BinaryOperator::WildcardEquality:
    return OperationKind::WildcardEquality;
  case ast::BinaryOperator::WildcardInequality:
    return OperationKind::WildcardInequality;
  case ast::BinaryOperator::LogicalAnd:
    return OperationKind::LogicalAnd;
  case ast::BinaryOperator::LogicalOr:
    return OperationKind::LogicalOr;
  case ast::BinaryOperator::LogicalImplication:
    return OperationKind::LogicalImplication;
  case ast::BinaryOperator::LogicalEquivalence:
    return OperationKind::LogicalEquivalence;
  case ast::BinaryOperator::LogicalShiftLeft:
    return OperationKind::LogicalShiftLeft;
  case ast::BinaryOperator::LogicalShiftRight:
    return OperationKind::LogicalShiftRight;
  case ast::BinaryOperator::ArithmeticShiftLeft:
    return OperationKind::ArithmeticShiftLeft;
  case ast::BinaryOperator::ArithmeticShiftRight:
    return OperationKind::ArithmeticShiftRight;
  case ast::BinaryOperator::Power:
    return OperationKind::Power;
  }
  SLANG_UNREACHABLE;
}

auto mapUnaryOperator(ast::UnaryOperator op) -> std::optional<OperationKind> {
  switch (op) {
  case ast::UnaryOperator::Plus:
    return OperationKind::UnaryPlus;
  case ast::UnaryOperator::Minus:
    return OperationKind::UnaryMinus;
  case ast::UnaryOperator::BitwiseNot:
    return OperationKind::BitwiseNot;
  case ast::UnaryOperator::BitwiseAnd:
    return OperationKind::ReductionAnd;
  case ast::UnaryOperator::BitwiseOr:
    return OperationKind::ReductionOr;
  case ast::UnaryOperator::BitwiseXor:
    return OperationKind::ReductionXor;
  case ast::UnaryOperator::BitwiseNand:
    return OperationKind::ReductionNand;
  case ast::UnaryOperator::BitwiseNor:
    return OperationKind::ReductionNor;
  case ast::UnaryOperator::BitwiseXnor:
    return OperationKind::ReductionXnor;
  case ast::UnaryOperator::LogicalNot:
    return OperationKind::LogicalNot;
  case ast::UnaryOperator::Preincrement:
  case ast::UnaryOperator::Predecrement:
  case ast::UnaryOperator::Postincrement:
  case ast::UnaryOperator::Postdecrement:
    // These mutate their operand; leave them opaque.
    return std::nullopt;
  }
  SLANG_UNREACHABLE;
}

auto OperationLowering::classify(ast::Expression const &expr)
    -> std::optional<OperationKind> {
  // Only integral results have a meaningful width and signedness to
  // record, so everything else stays opaque.
  if (!expr.type->isIntegral()) {
    return std::nullopt;
  }

  switch (expr.kind) {
  case ast::ExpressionKind::BinaryOp:
    return mapBinaryOperator(expr.as<ast::BinaryExpression>().op);
  case ast::ExpressionKind::UnaryOp:
    return mapUnaryOperator(expr.as<ast::UnaryExpression>().op);
  case ast::ExpressionKind::ConditionalOp: {
    auto const &cond = expr.as<ast::ConditionalExpression>();
    // Mirror the restriction BitSliceList applies: a single condition
    // bearing no pattern, so the predicate is the only extra operand.
    if (cond.conditions.size() != 1 || cond.conditions[0].pattern != nullptr) {
      return std::nullopt;
    }
    return OperationKind::Conditional;
  }
  default:
    return std::nullopt;
  }
}

void OperationLowering::visitOperand(ast::Expression const &expr) {
  auto kind = classify(expr);
  auto *previous = dfa.getState().node;

  // With no current node there is nothing to attach an operator to;
  // references fall back to the pending R-value queue as before.
  if (!kind.has_value() || previous == nullptr) {
    dfa.visit(expr);
    return;
  }

  auto &node = dfa.builder.nodeFactory.createOperation(
      *kind, expr.type->getBitWidth(), expr.type->isSigned(),
      dfa.builder.toTextLocation(expr.sourceRange.start()));

  // Edges run producer to consumer, so the operator drives the node it
  // was reached from. Set the current node directly rather than through
  // updateNode: the enclosing condition already feeds the Assignment,
  // and re-attaching it here would invent a dependency per operand.
  dfa.builder.addDependency(node, *previous);
  dfa.getState().node = &node;

  switch (expr.kind) {
  case ast::ExpressionKind::BinaryOp: {
    auto const &binary = expr.as<ast::BinaryExpression>();
    visitOperand(binary.left());
    visitOperand(binary.right());
    break;
  }
  case ast::ExpressionKind::UnaryOp:
    visitOperand(expr.as<ast::UnaryExpression>().operand());
    break;
  case ast::ExpressionKind::ConditionalOp: {
    auto const &cond = expr.as<ast::ConditionalExpression>();
    visitOperand(*cond.conditions[0].expr);
    visitOperand(cond.left());
    visitOperand(cond.right());
    break;
  }
  default:
    SLANG_UNREACHABLE;
  }

  dfa.getState().node = previous;
}

} // namespace slang::netlist
