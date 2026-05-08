#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "netlist/DriverBitRange.hpp"
#include "netlist/TextLocation.hpp"

namespace slang::netlist {

struct BranchWildcardPattern {
  std::string value;
  std::string mask;
};

/// Predicate metadata for a branch-selected assignment or data edge.
///
/// Leaf predicates use kind/signal/bitWidth and optional value fields.
/// Compound predicates use op and terms.
struct BranchPredicate {
  std::string op;
  std::string kind;
  std::string signal;
  uint64_t bitWidth{0};
  std::optional<DriverBitRange> bounds;
  std::vector<std::string> values;
  std::string value;
  std::string mask;
  std::vector<BranchWildcardPattern> excluded;
  TextLocation source;
  std::string expression;
  std::vector<BranchPredicate> terms;

  static BranchPredicate leaf(std::string kind, std::string signal,
                              uint64_t bitWidth = 0) {
    BranchPredicate predicate;
    predicate.kind = std::move(kind);
    predicate.signal = std::move(signal);
    predicate.bitWidth = bitWidth;
    return predicate;
  }

  static BranchPredicate compound(std::string op,
                                  std::vector<BranchPredicate> terms) {
    BranchPredicate predicate;
    predicate.op = std::move(op);
    predicate.terms = std::move(terms);
    return predicate;
  }
};

} // namespace slang::netlist
