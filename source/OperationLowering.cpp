#include "OperationLowering.hpp"

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

} // namespace slang::netlist
