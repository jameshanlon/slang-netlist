#include "Test.hpp"

#include "OperationLowering.hpp"

TEST_CASE("Binary operators map onto their netlist counterparts",
          "[Operation]") {
  CHECK(mapBinaryOperator(ast::BinaryOperator::Add) == OperationKind::Add);
  CHECK(mapBinaryOperator(ast::BinaryOperator::BinaryAnd) ==
        OperationKind::BitwiseAnd);
  CHECK(mapBinaryOperator(ast::BinaryOperator::LogicalAnd) ==
        OperationKind::LogicalAnd);
  CHECK(mapBinaryOperator(ast::BinaryOperator::ArithmeticShiftRight) ==
        OperationKind::ArithmeticShiftRight);
  CHECK(mapBinaryOperator(ast::BinaryOperator::WildcardEquality) ==
        OperationKind::WildcardEquality);
}

TEST_CASE("Unary operators map onto their netlist counterparts",
          "[Operation]") {
  CHECK(mapUnaryOperator(ast::UnaryOperator::Minus) ==
        OperationKind::UnaryMinus);
  CHECK(mapUnaryOperator(ast::UnaryOperator::BitwiseNot) ==
        OperationKind::BitwiseNot);
  CHECK(mapUnaryOperator(ast::UnaryOperator::BitwiseAnd) ==
        OperationKind::ReductionAnd);
  CHECK(mapUnaryOperator(ast::UnaryOperator::LogicalNot) ==
        OperationKind::LogicalNot);
}

TEST_CASE("Increment and decrement operators are not expanded", "[Operation]") {
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Preincrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Predecrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Postincrement).has_value());
  CHECK(!mapUnaryOperator(ast::UnaryOperator::Postdecrement).has_value());
}
