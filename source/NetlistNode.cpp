#include "netlist/NetlistNode.hpp"

#include <atomic>
#include <cstddef>

std::atomic<size_t> slang::netlist::NetlistNode::nextID{1};

namespace slang::netlist {

namespace {

struct OperationName {
  OperationKind kind;
  std::string_view name;
  std::string_view symbol;
};

// Exhaustive table over OperationKind; toString, toSymbol and
// operationKindFromString all read from it.
constexpr OperationName operationNames[] = {
    {OperationKind::UnaryPlus, "UnaryPlus", "+"},
    {OperationKind::UnaryMinus, "UnaryMinus", "-"},
    {OperationKind::BitwiseNot, "BitwiseNot", "~"},
    {OperationKind::LogicalNot, "LogicalNot", "!"},
    {OperationKind::ReductionAnd, "ReductionAnd", "&"},
    {OperationKind::ReductionOr, "ReductionOr", "|"},
    {OperationKind::ReductionXor, "ReductionXor", "^"},
    {OperationKind::ReductionNand, "ReductionNand", "~&"},
    {OperationKind::ReductionNor, "ReductionNor", "~|"},
    {OperationKind::ReductionXnor, "ReductionXnor", "~^"},
    {OperationKind::Add, "Add", "+"},
    {OperationKind::Subtract, "Subtract", "-"},
    {OperationKind::Multiply, "Multiply", "*"},
    {OperationKind::Divide, "Divide", "/"},
    {OperationKind::Mod, "Mod", "%"},
    {OperationKind::Power, "Power", "**"},
    {OperationKind::BitwiseAnd, "BitwiseAnd", "&"},
    {OperationKind::BitwiseOr, "BitwiseOr", "|"},
    {OperationKind::BitwiseXor, "BitwiseXor", "^"},
    {OperationKind::BitwiseXnor, "BitwiseXnor", "~^"},
    {OperationKind::Equality, "Equality", "=="},
    {OperationKind::Inequality, "Inequality", "!="},
    {OperationKind::CaseEquality, "CaseEquality", "==="},
    {OperationKind::CaseInequality, "CaseInequality", "!=="},
    {OperationKind::WildcardEquality, "WildcardEquality", "==?"},
    {OperationKind::WildcardInequality, "WildcardInequality", "!=?"},
    {OperationKind::GreaterThan, "GreaterThan", ">"},
    {OperationKind::GreaterThanEqual, "GreaterThanEqual", ">="},
    {OperationKind::LessThan, "LessThan", "<"},
    {OperationKind::LessThanEqual, "LessThanEqual", "<="},
    {OperationKind::LogicalAnd, "LogicalAnd", "&&"},
    {OperationKind::LogicalOr, "LogicalOr", "||"},
    {OperationKind::LogicalImplication, "LogicalImplication", "->"},
    {OperationKind::LogicalEquivalence, "LogicalEquivalence", "<->"},
    {OperationKind::LogicalShiftLeft, "LogicalShiftLeft", "<<"},
    {OperationKind::LogicalShiftRight, "LogicalShiftRight", ">>"},
    {OperationKind::ArithmeticShiftLeft, "ArithmeticShiftLeft", "<<<"},
    {OperationKind::ArithmeticShiftRight, "ArithmeticShiftRight", ">>>"},
    {OperationKind::Conditional, "Conditional", "?:"},
};

} // namespace

auto toString(OperationKind kind) -> std::string_view {
  for (auto const &entry : operationNames) {
    if (entry.kind == kind) {
      return entry.name;
    }
  }
  SLANG_UNREACHABLE;
}

auto toSymbol(OperationKind kind) -> std::string_view {
  for (auto const &entry : operationNames) {
    if (entry.kind == kind) {
      return entry.symbol;
    }
  }
  SLANG_UNREACHABLE;
}

auto operationKindFromString(std::string_view str)
    -> std::optional<OperationKind> {
  for (auto const &entry : operationNames) {
    if (entry.name == str) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

} // namespace slang::netlist
