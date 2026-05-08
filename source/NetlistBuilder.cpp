#include "DataFlowAnalysis.hpp"

#include "netlist/NetlistBuilder.hpp"
#include "netlist/PendingRValue.hpp"
#include "netlist/Utilities.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/HierarchicalReference.h"
#include "slang/ast/LSPUtilities.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"

#include <BS_thread_pool.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace slang::netlist {

namespace {

/// Get the driver bit range for a given node, if it has one.
auto getNodeBounds(NetlistNode const &node) -> std::optional<DriverBitRange> {
  switch (node.kind) {
  case NodeKind::Port:
    return node.as<Port>().bounds;
  case NodeKind::Variable:
    return node.as<Variable>().bounds;
  case NodeKind::State:
    return node.as<State>().bounds;
  default:
    return std::nullopt;
  }
}

auto getFullTypeBounds(ast::Type const &type) -> std::optional<DriverBitRange> {
  auto width = type.getBitWidth();
  if (width == 0 ||
      width > static_cast<decltype(width)>(std::numeric_limits<int32_t>::max())) {
    return std::nullopt;
  }
  return DriverBitRange{0, static_cast<int32_t>(width - 1)};
}

struct LocationKey {
  uint32_t fileIndex{FileTable::NoFile};
  size_t line{0};
  size_t column{0};

  auto operator==(LocationKey const &other) const -> bool {
    return fileIndex == other.fileIndex && line == other.line &&
           column == other.column;
  }
};

struct LocationKeyHash {
  auto operator()(LocationKey const &key) const -> size_t {
    auto hash = std::hash<uint32_t>{}(key.fileIndex);
    hash ^= std::hash<size_t>{}(key.line + 0x9e3779b9 + (hash << 6) +
                                (hash >> 2));
    hash ^= std::hash<size_t>{}(key.column + 0x9e3779b9 + (hash << 6) +
                                (hash >> 2));
    return hash;
  }
};

struct EdgePredicateKey {
  LocationKey assignment;
  std::string signal;

  auto operator==(EdgePredicateKey const &other) const -> bool {
    return assignment == other.assignment && signal == other.signal;
  }
};

struct EdgePredicateKeyHash {
  auto operator()(EdgePredicateKey const &key) const -> size_t {
    auto hash = LocationKeyHash{}(key.assignment);
    hash ^= std::hash<std::string>{}(key.signal) + 0x9e3779b9 + (hash << 6) +
            (hash >> 2);
    return hash;
  }
};

using AssignmentPredicateMap =
    std::unordered_map<LocationKey, std::vector<BranchPredicate>,
                       LocationKeyHash>;
using EdgePredicateMap =
    std::unordered_map<EdgePredicateKey, BranchPredicate, EdgePredicateKeyHash>;

auto locationKey(TextLocation const &loc) -> std::optional<LocationKey> {
  if (loc.empty()) {
    return std::nullopt;
  }
  return LocationKey{
      .fileIndex = loc.fileIndex,
      .line = loc.line,
      .column = loc.column,
  };
}

auto branchTextLocation(FileTable &fileTable, SourceManager const &sourceManager,
                        SourceLocation loc) -> TextLocation {
  if (loc.buffer() == SourceLocation::NoLocation.buffer()) {
    return {};
  }
  auto fileIndex = fileTable.addFile(sourceManager.getFileName(loc));
  return {fileIndex, sourceManager.getLineNumber(loc),
          sourceManager.getColumnNumber(loc), loc};
}

auto referencedValueSymbol(ast::Expression const &expression)
    -> std::optional<std::pair<ast::ValueSymbol const *, ast::Expression const *>> {
  auto const *expr = &expression.unwrapImplicitConversions();
  auto const *symbol = expr->getSymbolReference(/*allowPacked=*/true);
  if (symbol == nullptr || !ast::ValueSymbol::isKind(symbol->kind)) {
    return std::nullopt;
  }
  return std::pair{&symbol->as<ast::ValueSymbol>(), expr};
}

auto selectedBoundsForExpression(ast::ValueSymbol const &value,
                                 ast::Expression const &expr)
    -> std::optional<DriverBitRange> {
  ast::EvalContext evalContext(value);
  auto bounds = ast::LSPUtilities::getBounds(expr, evalContext, value.getType());
  if (!bounds) {
    return std::nullopt;
  }

  auto selected = DriverBitRange(*bounds);
  auto full = getFullTypeBounds(value.getType());
  if (full && selected.lower() == full->lower() &&
      selected.upper() == full->upper()) {
    return std::nullopt;
  }
  return selected;
}

auto invertedPredicateKind(std::string_view kind) -> std::optional<std::string> {
  if (kind == "truthy") {
    return "falsy";
  }
  if (kind == "falsy") {
    return "truthy";
  }
  if (kind == "equals") {
    return "not_equals";
  }
  if (kind == "not_equals") {
    return "equals";
  }
  if (kind == "in") {
    return "not_in";
  }
  if (kind == "not_in") {
    return "in";
  }
  return std::nullopt;
}

auto expressionText(ast::Expression const &expr) -> std::string {
  if (expr.syntax != nullptr) {
    return expr.syntax->toString();
  }
  return {};
}

auto dedupeValues(std::vector<std::string> const &values)
    -> std::vector<std::string> {
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  for (auto const &value : values) {
    if (seen.insert(value).second) {
      result.push_back(value);
    }
  }
  return result;
}

auto normalizeConstantExpression(ast::Expression const &expr)
    -> std::optional<std::string> {
  auto const *constant = expr.getConstant();
  if (constant == nullptr || !constant->isInteger()) {
    return std::nullopt;
  }

  auto width = expr.type->getBitWidth();
  auto value = constant->integer();
  if (width > 0 && value.getBitWidth() != width) {
    value = value.resize(width);
  }

  auto bits = value.toString(LiteralBase::Binary, /*includeBase=*/false);
  bits.erase(std::remove(bits.begin(), bits.end(), '_'), bits.end());
  std::transform(bits.begin(), bits.end(), bits.begin(), [](unsigned char ch) {
    ch = static_cast<unsigned char>(std::tolower(ch));
    return ch == '?' ? 'z' : static_cast<char>(ch);
  });

  if (bits.empty()) {
    return std::nullopt;
  }
  if (width > 0 && bits.size() < width) {
    bits.insert(bits.begin(), width - bits.size(), '0');
  } else if (width > 0 && bits.size() > width) {
    bits = bits.substr(bits.size() - width);
  }
  return bits;
}

auto normalizeWildcardLiteralText(ast::Expression const &expr)
    -> std::optional<std::string> {
  auto text = expressionText(expr);
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
               return std::isspace(ch) != 0;
             }),
             text.end());

  auto quote = text.find('\'');
  if (quote == std::string::npos || quote + 1 >= text.size()) {
    return std::nullopt;
  }

  uint64_t width = 0;
  if (quote > 0) {
    for (size_t i = 0; i < quote; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
        return std::nullopt;
      }
      width = width * 10 + static_cast<uint64_t>(text[i] - '0');
    }
  }

  auto baseIndex = quote + 1;
  if (baseIndex < text.size() &&
      (text[baseIndex] == 's' || text[baseIndex] == 'S')) {
    baseIndex++;
  }
  if (baseIndex >= text.size() ||
      (text[baseIndex] != 'b' && text[baseIndex] != 'B')) {
    return std::nullopt;
  }

  std::string bits;
  for (size_t i = baseIndex + 1; i < text.size(); ++i) {
    auto ch =
        static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    if (ch == '_') {
      continue;
    }
    if (ch == '?') {
      ch = 'z';
    }
    if (ch != '0' && ch != '1' && ch != 'x' && ch != 'z') {
      return std::nullopt;
    }
    bits.push_back(ch);
  }

  if (bits.empty()) {
    return std::nullopt;
  }
  if (width > 0 && bits.size() < width) {
    bits.insert(bits.begin(), width - bits.size(), '0');
  } else if (width > 0 && bits.size() > width) {
    bits = bits.substr(bits.size() - width);
  }
  return bits;
}

auto normalizeWildcardCasePattern(
    ast::Expression const &expr, ast::CaseStatementCondition condition)
    -> std::optional<BranchWildcardPattern> {
  auto bits = normalizeConstantExpression(expr);
  if (!bits) {
    bits = normalizeWildcardLiteralText(expr);
  }
  if (!bits) {
    return std::nullopt;
  }

  auto xIsWildcard = condition == ast::CaseStatementCondition::WildcardXOrZ;
  BranchWildcardPattern pattern;
  pattern.value.reserve(bits->size());
  pattern.mask.reserve(bits->size());

  for (auto ch : *bits) {
    if (ch == '0' || ch == '1') {
      pattern.value.push_back(ch);
      pattern.mask.push_back('1');
    } else if (ch == 'z' || (xIsWildcard && ch == 'x')) {
      pattern.value.push_back('0');
      pattern.mask.push_back('0');
    } else {
      return std::nullopt;
    }
  }

  return pattern;
}

auto comparisonPredicateKind(ast::BinaryOperator op)
    -> std::optional<std::string> {
  switch (op) {
  case ast::BinaryOperator::Equality:
  case ast::BinaryOperator::CaseEquality:
  case ast::BinaryOperator::WildcardEquality:
    return "equals";
  case ast::BinaryOperator::Inequality:
  case ast::BinaryOperator::CaseInequality:
  case ast::BinaryOperator::WildcardInequality:
    return "not_equals";
  default:
    return std::nullopt;
  }
}

auto compoundPredicate(std::string op, std::vector<BranchPredicate> terms)
    -> BranchPredicate {
  if (terms.size() == 1) {
    return std::move(terms.front());
  }
  return BranchPredicate::compound(std::move(op), std::move(terms));
}

auto combinePredicates(BranchPredicate const &left,
                       BranchPredicate const &right,
                       std::string op = "and") -> BranchPredicate {
  std::vector<BranchPredicate> terms;
  terms.push_back(left);
  terms.push_back(right);
  return compoundPredicate(std::move(op), std::move(terms));
}

auto invertPredicate(BranchPredicate const &predicate)
    -> std::optional<BranchPredicate> {
  if (predicate.op.empty()) {
    auto inverted = invertedPredicateKind(predicate.kind);
    if (!inverted) {
      return std::nullopt;
    }
    auto result = predicate;
    result.kind = std::move(*inverted);
    return result;
  }

  if (predicate.op != "and" && predicate.op != "or") {
    return std::nullopt;
  }

  std::vector<BranchPredicate> invertedTerms;
  invertedTerms.reserve(predicate.terms.size());
  for (auto const &term : predicate.terms) {
    auto inverted = invertPredicate(term);
    if (!inverted) {
      return std::nullopt;
    }
    invertedTerms.push_back(std::move(*inverted));
  }

  return compoundPredicate(predicate.op == "and" ? "or" : "and",
                           std::move(invertedTerms));
}

auto predicateForSignal(ast::ValueSymbol const &value, ast::Expression const &expr,
                        std::string kind, FileTable &fileTable,
                        SourceManager const &sourceManager)
    -> std::optional<BranchPredicate> {
  auto signal = value.getHierarchicalPath();
  if (signal.empty()) {
    return std::nullopt;
  }
  auto predicate = BranchPredicate::leaf(std::move(kind), std::move(signal),
                                         value.getType().getBitWidth());
  predicate.bounds = selectedBoundsForExpression(value, expr);
  predicate.source =
      branchTextLocation(fileTable, sourceManager, expr.sourceRange.start());
  predicate.expression = expressionText(expr);
  return predicate;
}

auto predicateForBinaryComparison(ast::BinaryExpression const &expr,
                                  FileTable &fileTable,
                                  SourceManager const &sourceManager)
    -> std::optional<BranchPredicate> {
  auto kind = comparisonPredicateKind(expr.op);
  if (!kind) {
    return std::nullopt;
  }

  auto leftSignal = referencedValueSymbol(expr.left());
  auto rightSignal = referencedValueSymbol(expr.right());
  ast::ValueSymbol const *value = nullptr;
  ast::Expression const *constantExpr = nullptr;

  if (leftSignal && !rightSignal) {
    value = leftSignal->first;
    constantExpr = &expr.right();
  } else if (rightSignal && !leftSignal) {
    value = rightSignal->first;
    constantExpr = &expr.left();
  } else {
    return std::nullopt;
  }

  auto normalized = normalizeConstantExpression(*constantExpr);
  if (!normalized) {
    return std::nullopt;
  }

  auto signal = value->getHierarchicalPath();
  if (signal.empty()) {
    return std::nullopt;
  }

  auto predicate = BranchPredicate::leaf(std::move(*kind), std::move(signal),
                                         value->getType().getBitWidth());
  predicate.bounds = selectedBoundsForExpression(*value,
                                                 leftSignal ? *leftSignal->second
                                                            : *rightSignal->second);
  predicate.values = {*normalized};
  predicate.source =
      branchTextLocation(fileTable, sourceManager, expr.sourceRange.start());
  predicate.expression = expressionText(expr);
  return predicate;
}

auto truthyPredicateForCondition(ast::Expression const &condition,
                                 FileTable &fileTable,
                                 SourceManager const &sourceManager)
    -> std::optional<BranchPredicate> {
  auto const *expr = &condition.unwrapImplicitConversions();

  if (auto const *unary = expr->as_if<ast::UnaryExpression>();
      unary != nullptr && unary->op == ast::UnaryOperator::LogicalNot) {
    auto operandPredicate =
        truthyPredicateForCondition(unary->operand(), fileTable, sourceManager);
    if (!operandPredicate) {
      return std::nullopt;
    }
    return invertPredicate(*operandPredicate);
  }

  if (auto const *binary = expr->as_if<ast::BinaryExpression>()) {
    if (binary->op == ast::BinaryOperator::LogicalAnd ||
        binary->op == ast::BinaryOperator::LogicalOr) {
      auto left =
          truthyPredicateForCondition(binary->left(), fileTable, sourceManager);
      auto right =
          truthyPredicateForCondition(binary->right(), fileTable, sourceManager);
      if (!left || !right) {
        return std::nullopt;
      }
      std::vector<BranchPredicate> terms;
      terms.push_back(std::move(*left));
      terms.push_back(std::move(*right));
      return compoundPredicate(binary->op == ast::BinaryOperator::LogicalAnd
                                   ? "and"
                                   : "or",
                               std::move(terms));
    }

    return predicateForBinaryComparison(*binary, fileTable, sourceManager);
  }

  auto ref = referencedValueSymbol(*expr);
  if (!ref) {
    return std::nullopt;
  }
  return predicateForSignal(*ref->first, *ref->second, "truthy", fileTable,
                            sourceManager);
}

auto predicateForBranch(ast::Expression const &condition,
                        std::string_view branchKind, FileTable &fileTable,
                        SourceManager const &sourceManager)
    -> std::optional<BranchPredicate> {
  auto predicate = truthyPredicateForCondition(condition, fileTable, sourceManager);
  if (!predicate || branchKind == "truthy") {
    return predicate;
  }
  if (branchKind == "falsy") {
    return invertPredicate(*predicate);
  }
  return std::nullopt;
}

void collectPredicateSignals(BranchPredicate const &predicate,
                             std::vector<std::string> &signals) {
  if (!predicate.signal.empty()) {
    signals.push_back(predicate.signal);
  }
  for (auto const &term : predicate.terms) {
    collectPredicateSignals(term, signals);
  }
}

auto pathSegments(std::string_view path) -> std::vector<std::string_view> {
  std::vector<std::string_view> result;
  size_t start = 0;
  while (start <= path.size()) {
    auto dot = path.find('.', start);
    auto end = dot == std::string_view::npos ? path.size() : dot;
    if (end > start) {
      result.push_back(path.substr(start, end - start));
    }
    if (dot == std::string_view::npos) {
      break;
    }
    start = dot + 1;
  }
  return result;
}

auto commonPathPrefixSegments(std::string_view left, std::string_view right)
    -> size_t {
  auto leftSegments = pathSegments(left);
  auto rightSegments = pathSegments(right);
  auto count = std::min(leftSegments.size(), rightSegments.size());
  size_t common = 0;
  while (common < count && leftSegments[common] == rightSegments[common]) {
    common++;
  }
  return common;
}

auto assignmentSignalPaths(Assignment const &assignment)
    -> std::vector<std::string> {
  auto const &node = static_cast<NetlistNode const &>(assignment);
  std::set<std::string> signals;
  for (auto const &edge : node.getInEdges()) {
    if (!edge->symbol.empty()) {
      signals.insert(edge->symbol.hierarchicalPath);
    }
  }
  for (auto const &edge : node.getOutEdges()) {
    if (!edge->symbol.empty()) {
      signals.insert(edge->symbol.hierarchicalPath);
    }
  }
  return {signals.begin(), signals.end()};
}

auto parentScope(std::string_view path) -> std::optional<std::string> {
  auto dot = path.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return std::nullopt;
  }
  return std::string(path.substr(0, dot));
}

auto leafName(std::string_view path) -> std::string_view {
  auto dot = path.rfind('.');
  return dot == std::string_view::npos ? path : path.substr(dot + 1);
}

auto scopeSegmentCount(std::string_view scope) -> size_t {
  return pathSegments(scope).size();
}

auto assignmentScopes(std::vector<std::string> const &assignmentSignals)
    -> std::vector<std::string> {
  std::set<std::string> scopes;
  for (auto const &signal : assignmentSignals) {
    if (auto scope = parentScope(signal)) {
      scopes.insert(std::move(*scope));
    }
  }
  return {scopes.begin(), scopes.end()};
}

auto remapSignalToAssignmentScope(
    std::string const &signal,
    std::vector<std::string> const &assignmentSignals) -> std::string {
  auto signalScope = parentScope(signal);
  if (!signalScope) {
    return signal;
  }

  auto scopes = assignmentScopes(assignmentSignals);
  if (scopes.empty()) {
    return signal;
  }

  for (auto const &scope : scopes) {
    if (scope == *signalScope) {
      return signal;
    }
  }

  std::string const *bestScope = nullptr;
  size_t bestScore = 0;
  size_t bestSegmentCount = std::numeric_limits<size_t>::max();
  for (auto const &scope : scopes) {
    auto score = commonPathPrefixSegments(*signalScope, scope);
    auto segmentCount = scopeSegmentCount(scope);
    if (score > bestScore ||
        (score == bestScore && score > 0 &&
         segmentCount < bestSegmentCount)) {
      bestScore = score;
      bestSegmentCount = segmentCount;
      bestScope = &scope;
    }
  }

  if (bestScope == nullptr || bestScore == 0) {
    return signal;
  }
  return *bestScope + "." + std::string(leafName(signal));
}

auto remapPredicateToAssignmentScope(
    BranchPredicate predicate,
    std::vector<std::string> const &assignmentSignals) -> BranchPredicate {
  if (!predicate.signal.empty()) {
    predicate.signal =
        remapSignalToAssignmentScope(predicate.signal, assignmentSignals);
  }
  for (auto &term : predicate.terms) {
    term = remapPredicateToAssignmentScope(std::move(term), assignmentSignals);
  }
  return predicate;
}

auto predicateAssignmentScore(BranchPredicate const &predicate,
                              std::vector<std::string> const &assignmentSignals)
    -> size_t {
  std::vector<std::string> predicateSignals;
  collectPredicateSignals(predicate, predicateSignals);

  size_t best = 0;
  for (auto const &predicateSignal : predicateSignals) {
    for (auto const &assignmentSignal : assignmentSignals) {
      best = std::max(best,
                      commonPathPrefixSegments(predicateSignal,
                                               assignmentSignal));
    }
  }
  return best;
}

auto selectAssignmentPredicate(
    Assignment const &assignment,
    std::vector<BranchPredicate> const &candidates)
    -> BranchPredicate const * {
  if (candidates.empty()) {
    return nullptr;
  }
  if (candidates.size() == 1) {
    return &candidates.front();
  }

  auto assignmentSignals = assignmentSignalPaths(assignment);
  if (assignmentSignals.empty()) {
    return nullptr;
  }

  BranchPredicate const *bestPredicate = nullptr;
  size_t bestScore = 0;
  bool tied = false;
  for (auto const &candidate : candidates) {
    auto score = predicateAssignmentScore(candidate, assignmentSignals);
    if (score > bestScore) {
      bestScore = score;
      bestPredicate = &candidate;
      tied = false;
    } else if (score == bestScore && score > 0) {
      tied = true;
    }
  }

  return bestScore > 0 && !tied ? bestPredicate : nullptr;
}

auto remapPredicateForAssignment(Assignment const &assignment,
                                 BranchPredicate predicate) -> BranchPredicate {
  return remapPredicateToAssignmentScope(std::move(predicate),
                                         assignmentSignalPaths(assignment));
}

auto selectEdgePredicateForAssignment(
    EdgePredicateMap const &edgePredicates, LocationKey const &assignmentKey,
    NetlistEdge const &edge, Assignment const &assignment)
    -> std::optional<BranchPredicate> {
  EdgePredicateKey exactKey{
      .assignment = assignmentKey,
      .signal = edge.symbol.hierarchicalPath,
  };
  auto exact = edgePredicates.find(exactKey);
  if (exact != edgePredicates.end()) {
    return remapPredicateForAssignment(assignment, exact->second);
  }

  auto assignmentSignals = assignmentSignalPaths(assignment);
  for (auto const &[key, predicate] : edgePredicates) {
    if (!(key.assignment == assignmentKey)) {
      continue;
    }
    auto remappedSignal =
        remapSignalToAssignmentScope(key.signal, assignmentSignals);
    if (remappedSignal == edge.symbol.hierarchicalPath) {
      return remapPredicateToAssignmentScope(predicate, assignmentSignals);
    }
  }
  return std::nullopt;
}

struct BranchPredicateCollector
    : public ast::ASTVisitor<BranchPredicateCollector,
                             /*VisitStatements=*/false,
                             /*VisitExpressions=*/false,
                             /*VisitBad=*/false,
                             /*VisitCanonical=*/false> {
  FileTable &fileTable;
  SourceManager const &sourceManager;
  AssignmentPredicateMap assignmentPredicates;
  EdgePredicateMap edgePredicates;
  std::vector<BranchPredicate> activePredicates;

  BranchPredicateCollector(FileTable &fileTable,
                           SourceManager const &sourceManager)
      : fileTable(fileTable), sourceManager(sourceManager) {}

  auto toTextLocation(SourceLocation loc) -> TextLocation {
    if (loc.buffer() == SourceLocation::NoLocation.buffer()) {
      return {};
    }
    auto fileIndex = fileTable.addFile(sourceManager.getFileName(loc));
    return {fileIndex, sourceManager.getLineNumber(loc),
            sourceManager.getColumnNumber(loc), loc};
  }

  void handle(ast::ProceduralBlockSymbol const &symbol) {
    walkStatement(symbol.getBody());
  }

  void handle(ast::ContinuousAssignSymbol const &symbol) {
    if (auto const *assignment =
            symbol.getAssignment().as_if<ast::AssignmentExpression>()) {
      recordAssignment(*assignment);
    }
  }

  void withPredicate(BranchPredicate const &predicate,
                     ast::Statement const &stmt) {
    activePredicates.push_back(predicate);
    walkStatement(stmt);
    activePredicates.pop_back();
  }

  void recordAssignment(ast::AssignmentExpression const &expr) {
    auto key = locationKey(toTextLocation(expr.sourceRange.start()));

    if (key && !expr.isLValueArg()) {
      recordTernaryPredicates(expr.right(), *key, std::nullopt);
    }

    if (!key || activePredicates.empty()) {
      return;
    }

    assignmentPredicates[*key].push_back(
        compoundPredicate("and", activePredicates));
  }

  void recordBranchExpressionSignals(
      ast::Expression const &expr, LocationKey const &assignmentKey,
      BranchPredicate const &predicate) {
    auto const *unwrapped = &expr.unwrapImplicitConversions();
    if (unwrapped->as_if<ast::ConditionalExpression>() != nullptr) {
      recordTernaryPredicates(*unwrapped, assignmentKey, predicate);
      return;
    }

    std::set<std::string> signals;
    expr.visitSymbolReferences([&](ast::Expression const &,
                                   ast::Symbol const &symbol) {
      if (!ast::ValueSymbol::isKind(symbol.kind)) {
        return;
      }
      auto signal = symbol.as<ast::ValueSymbol>().getHierarchicalPath();
      if (!signal.empty()) {
        signals.insert(std::move(signal));
      }
    });

    for (auto const &signal : signals) {
      EdgePredicateKey key{.assignment = assignmentKey, .signal = signal};
      auto found = edgePredicates.find(key);
      if (found == edgePredicates.end()) {
        edgePredicates.emplace(std::move(key), predicate);
      } else {
        found->second = combinePredicates(found->second, predicate, "or");
      }
    }
  }

  void recordTernaryPredicates(
      ast::Expression const &expr, LocationKey const &assignmentKey,
      std::optional<BranchPredicate> const &contextPredicate) {
    auto const *unwrapped = &expr.unwrapImplicitConversions();
    auto const *conditional = unwrapped->as_if<ast::ConditionalExpression>();
    if (conditional == nullptr || conditional->conditions.size() != 1 ||
        conditional->conditions.front().pattern != nullptr) {
      return;
    }

    auto const &condition = *conditional->conditions.front().expr;
    auto truePredicate =
        predicateForBranch(condition, "truthy", fileTable, sourceManager);
    auto falsePredicate =
        predicateForBranch(condition, "falsy", fileTable, sourceManager);

    auto withContext = [&](BranchPredicate const &predicate) {
      return contextPredicate ? combinePredicates(*contextPredicate, predicate)
                              : predicate;
    };

    if (truePredicate) {
      recordBranchExpressionSignals(conditional->left(), assignmentKey,
                                    withContext(*truePredicate));
    }
    if (falsePredicate) {
      recordBranchExpressionSignals(conditional->right(), assignmentKey,
                                    withContext(*falsePredicate));
    }
  }

  void walkConditional(ast::ConditionalStatement const &stmt) {
    if (stmt.conditions.size() != 1 ||
        stmt.conditions.front().pattern != nullptr) {
      walkStatement(stmt.ifTrue);
      if (stmt.ifFalse != nullptr) {
        walkStatement(*stmt.ifFalse);
      }
      return;
    }

    auto const &condition = *stmt.conditions.front().expr;
    auto truePredicate =
        predicateForBranch(condition, "truthy", fileTable, sourceManager);
    auto falsePredicate =
        predicateForBranch(condition, "falsy", fileTable, sourceManager);

    if (truePredicate) {
      withPredicate(*truePredicate, stmt.ifTrue);
    } else {
      walkStatement(stmt.ifTrue);
    }

    if (stmt.ifFalse != nullptr) {
      if (falsePredicate) {
        withPredicate(*falsePredicate, *stmt.ifFalse);
      } else {
        walkStatement(*stmt.ifFalse);
      }
    }
  }

  void walkCase(ast::CaseStatement const &stmt) {
    auto selector = referencedValueSymbol(stmt.expr);
    auto selectorWidth = selector ? selector->first->getType().getBitWidth() : 0;
    auto wildcardCase =
        stmt.condition == ast::CaseStatementCondition::WildcardJustZ ||
        stmt.condition == ast::CaseStatementCondition::WildcardXOrZ;
    std::vector<std::string> explicitValues;
    std::vector<BranchWildcardPattern> explicitPatterns;
    auto allItemsSupported = selector.has_value();

    for (auto const &item : stmt.items) {
      std::vector<std::string> itemValues;
      std::vector<BranchWildcardPattern> itemPatterns;
      for (auto const *expr : item.expressions) {
        if (expr == nullptr) {
          allItemsSupported = false;
          itemValues.clear();
          itemPatterns.clear();
          break;
        }
        if (wildcardCase) {
          auto pattern = normalizeWildcardCasePattern(*expr, stmt.condition);
          if (!pattern) {
            allItemsSupported = false;
            itemPatterns.clear();
            break;
          }
          itemPatterns.push_back(std::move(*pattern));
        } else {
          auto normalized = normalizeConstantExpression(*expr);
          if (!normalized) {
            allItemsSupported = false;
            itemValues.clear();
            break;
          }
          itemValues.push_back(*normalized);
        }
      }

      if (selector && wildcardCase && !itemPatterns.empty()) {
        std::vector<BranchPredicate> predicates;
        predicates.reserve(itemPatterns.size());
        for (auto const &pattern : itemPatterns) {
          auto predicate = BranchPredicate::leaf(
              "casez_match", selector->first->getHierarchicalPath(),
              selectorWidth);
          predicate.bounds =
              selectedBoundsForExpression(*selector->first, *selector->second);
          predicate.value = pattern.value;
          predicate.mask = pattern.mask;
          predicate.source = branchTextLocation(
              fileTable, sourceManager,
              item.expressions.front()->sourceRange.start());
          predicate.expression = expressionText(*item.expressions.front());
          predicates.push_back(std::move(predicate));
        }
        auto itemPredicate = compoundPredicate("or", std::move(predicates));
        if (!explicitPatterns.empty()) {
          auto previousPredicate = BranchPredicate::leaf(
              "casez_default", selector->first->getHierarchicalPath(),
              selectorWidth);
          previousPredicate.bounds =
              selectedBoundsForExpression(*selector->first, *selector->second);
          previousPredicate.excluded = explicitPatterns;
          previousPredicate.source = branchTextLocation(
              fileTable, sourceManager, stmt.sourceRange.start());
          previousPredicate.expression = expressionText(stmt.expr);
          itemPredicate = compoundPredicate(
              "and", {std::move(previousPredicate), std::move(itemPredicate)});
        }
        withPredicate(itemPredicate, *item.stmt);
        explicitPatterns.insert(explicitPatterns.end(), itemPatterns.begin(),
                                itemPatterns.end());
      } else if (selector && !itemValues.empty()) {
        explicitValues.insert(explicitValues.end(), itemValues.begin(),
                              itemValues.end());
        auto predicate =
            BranchPredicate::leaf("in", selector->first->getHierarchicalPath(),
                                  selectorWidth);
        predicate.bounds =
            selectedBoundsForExpression(*selector->first, *selector->second);
        predicate.values = itemValues;
        predicate.source = branchTextLocation(
            fileTable, sourceManager, item.expressions.front()->sourceRange.start());
        predicate.expression = expressionText(*item.expressions.front());
        withPredicate(predicate, *item.stmt);
      } else {
        walkStatement(*item.stmt);
      }
    }

    if (stmt.defaultCase == nullptr) {
      return;
    }

    if (selector && wildcardCase && !explicitPatterns.empty() &&
        allItemsSupported) {
      auto predicate = BranchPredicate::leaf(
          "casez_default", selector->first->getHierarchicalPath(),
          selectorWidth);
      predicate.bounds =
          selectedBoundsForExpression(*selector->first, *selector->second);
      predicate.excluded = explicitPatterns;
      predicate.source =
          branchTextLocation(fileTable, sourceManager, stmt.sourceRange.start());
      predicate.expression = expressionText(stmt.expr);
      withPredicate(predicate, *stmt.defaultCase);
    } else if (selector && !explicitValues.empty() && allItemsSupported) {
      auto predicate =
          BranchPredicate::leaf("not_in", selector->first->getHierarchicalPath(),
                                selectorWidth);
      predicate.bounds =
          selectedBoundsForExpression(*selector->first, *selector->second);
      predicate.values = dedupeValues(explicitValues);
      predicate.source =
          branchTextLocation(fileTable, sourceManager, stmt.sourceRange.start());
      predicate.expression = expressionText(stmt.expr);
      withPredicate(predicate, *stmt.defaultCase);
    } else {
      walkStatement(*stmt.defaultCase);
    }
  }

  void walkStatement(ast::Statement const &stmt) {
    switch (stmt.kind) {
    case ast::StatementKind::List:
      for (auto const *child : stmt.as<ast::StatementList>().list) {
        if (child != nullptr) {
          walkStatement(*child);
        }
      }
      break;
    case ast::StatementKind::Block:
      walkStatement(stmt.as<ast::BlockStatement>().body);
      break;
    case ast::StatementKind::Timed:
      walkStatement(stmt.as<ast::TimedStatement>().stmt);
      break;
    case ast::StatementKind::Conditional:
      walkConditional(stmt.as<ast::ConditionalStatement>());
      break;
    case ast::StatementKind::Case:
      walkCase(stmt.as<ast::CaseStatement>());
      break;
    case ast::StatementKind::ExpressionStatement: {
      auto const &expr = stmt.as<ast::ExpressionStatement>().expr;
      if (auto const *assignment = expr.as_if<ast::AssignmentExpression>()) {
        recordAssignment(*assignment);
      }
      break;
    }
    default:
      break;
    }
  }
};

/// Thread-local pointer to the deferred work buffer for the current parallel
/// task. nullptr when running sequentially.
thread_local DeferredGraphWork *threadLocalDeferredWork = nullptr;

} // namespace

NetlistBuilder::NetlistBuilder(ast::Compilation &compilation,
                               analysis::AnalysisManager &analysisManager,
                               NetlistGraph &graph,
                               bool materializeInternalVariables)
    : compilation(compilation), analysisManager(analysisManager), graph(graph),
      materializeInternalVariables(materializeInternalVariables) {
  NetlistNode::nextID.store(1, std::memory_order_relaxed);
}

auto NetlistBuilder::toTextLocation(SourceLocation loc) const -> TextLocation {
  if (loc.buffer() == SourceLocation::NoLocation.buffer()) {
    return {};
  }
  auto &sm = *compilation.getSourceManager();
  auto fileIdx = graph.fileTable.addFile(sm.getFileName(loc));
  return {fileIdx, sm.getLineNumber(loc), sm.getColumnNumber(loc), loc};
}

auto NetlistBuilder::toSymbolRef(ast::Symbol const &sym) const
    -> SymbolReference {
  return {std::string(sym.name), std::string(sym.getHierarchicalPath()),
          toTextLocation(sym.location)};
}

auto NetlistBuilder::createAssignment(ast::AssignmentExpression const &expr,
                                      AssignmentType assignmentType,
                                      bool isBlocking) -> NetlistNode & {
  auto node = std::make_unique<Assignment>(
      toTextLocation(expr.sourceRange.start()), assignmentType, isBlocking);
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createConditional(ast::ConditionalStatement const &stmt)
    -> NetlistNode & {
  auto node =
      std::make_unique<Conditional>(toTextLocation(stmt.sourceRange.start()));
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

auto NetlistBuilder::createCase(ast::CaseStatement const &stmt)
    -> NetlistNode & {
  auto node = std::make_unique<Case>(toTextLocation(stmt.sourceRange.start()));
  if (threadLocalDeferredWork) {
    return threadLocalDeferredWork->addNode(std::move(node));
  }
  return graph.addNode(std::move(node));
}

void NetlistBuilder::build(const ast::Symbol &root, bool parallel,
                           unsigned numThreads) {
  buildRoot = &root;

  // Phase 1: Visit the AST sequentially to create ports, variables, and
  // instance structure. Procedural blocks and continuous assignments are
  // deferred.
  collectingPhase = true;
  root.visit(*this);
  collectingPhase = false;

  // Phase 2: Dispatch deferred DFA work items.
  if (parallel) {
    BS::thread_pool pool(numThreads);
    std::mutex exceptionMutex;
    std::exception_ptr pendingException;
    std::vector<DeferredGraphWork> allWork(deferredBlocks.size());

    for (size_t i = 0; i < deferredBlocks.size(); ++i) {
      pool.detach_task([this, &block = deferredBlocks[i], &work = allWork[i],
                        &exceptionMutex, &pendingException] {
        threadLocalDeferredWork = &work;
        SLANG_TRY {
          if (block.isProcedural) {
            handleProceduralBlock(
                block.symbol->as<ast::ProceduralBlockSymbol>());
          } else {
            handleContinuousAssign(
                block.symbol->as<ast::ContinuousAssignSymbol>());
          }
        }
        SLANG_CATCH(const std::exception &) {
          std::lock_guard<std::mutex> lock(exceptionMutex);
          if (!pendingException) {
            pendingException = std::current_exception();
          }
        }
        threadLocalDeferredWork = nullptr;
      });
    }

    pool.wait();

    if (pendingException) {
      std::rethrow_exception(pendingException);
    }

    drainDeferredWork(allWork);
  } else {
    for (auto &block : deferredBlocks) {
      if (block.isProcedural) {
        handleProceduralBlock(block.symbol->as<ast::ProceduralBlockSymbol>());
      } else {
        handleContinuousAssign(block.symbol->as<ast::ContinuousAssignSymbol>());
      }
    }
  }

  deferredBlocks.clear();
}

/// Drain thread-local buffers into the shared graph after all parallel
/// tasks have completed. Must be called single-threaded (after
/// pool.wait()) so that no synchronisation is needed.
void NetlistBuilder::drainDeferredWork(
    std::vector<DeferredGraphWork> &allWork) {
  for (auto &work : allWork) {
    // Move deferred nodes into the shared graph.
    for (auto &node : work.nodes) {
      graph.addNode(std::move(node));
    }
    // Replay deferred edge creation, annotating with symbol/bounds
    // where applicable.
    for (auto &e : work.edges) {
      auto &edge = e.source->addEdge(*e.target);
      if (!e.symbol.empty()) {
        edge.setVariable(std::move(e.symbol), e.bounds);
        edge.setEdgeKind(e.edgeKind);
      }
    }
    // Collect pending R-values for processPendingRvalues() in finalize().
    for (auto &pr : work.pendingRValues) {
      pendingRValues.push_back(std::move(pr));
    }
    // Run deferred mergeDrivers calls that write to the shared driverMap.
    for (auto &fn : work.deferredMerges) {
      fn();
    }
  }
}

void NetlistBuilder::finalize() {
  processPendingRvalues();
  if (buildRoot != nullptr) {
    populateBranchPredicates(*buildRoot);
  }
}

void NetlistBuilder::populateBranchPredicates(ast::Symbol const &root) {
  auto *sourceManager = compilation.getSourceManager();
  if (sourceManager == nullptr) {
    return;
  }

  BranchPredicateCollector collector(graph.fileTable, *sourceManager);
  root.visit(collector);

  for (auto const &node : graph.filterNodes(NodeKind::Assignment)) {
    auto &assignment = node->as<Assignment>();
    assignment.branchPredicate.reset();
    auto key = locationKey(assignment.location);
    if (!key) {
      continue;
    }
    auto found = collector.assignmentPredicates.find(*key);
    if (found != collector.assignmentPredicates.end()) {
      if (auto const *predicate =
              selectAssignmentPredicate(assignment, found->second)) {
        assignment.branchPredicate =
            remapPredicateForAssignment(assignment, *predicate);
      }
    }
  }

  for (auto const &node : graph) {
    for (auto const &edge : node->getOutEdges()) {
      edge->branchPredicate.reset();
      if (edge->symbol.empty()) {
        continue;
      }

      auto const &target = edge->getTargetNode();
      if (target.kind != NodeKind::Assignment) {
        continue;
      }

      auto key = locationKey(target.as<Assignment>().location);
      if (!key) {
        continue;
      }

      if (auto predicate = selectEdgePredicateForAssignment(
              collector.edgePredicates, *key, *edge,
              target.as<Assignment>())) {
        edge->branchPredicate = std::move(*predicate);
      }
    }
  }
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target) {
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->edges.push_back(
        {&source, &target, {}, {}, ast::EdgeKind::None});
    return;
  }
  graph.addEdge(source, target);
}

void NetlistBuilder::addDependency(NetlistNode &source, NetlistNode &target,
                                   SymbolReference symbol,
                                   DriverBitRange bounds,
                                   ast::EdgeKind edgeKind) {

  // Retrieve the bounds of the driving node, if any.
  auto nodeBounds = getNodeBounds(source);

  // By default, use the specified bounds for the edge.
  auto edgeBounds = bounds;

  // If the source node has specific bounds, intersect them with the specified
  // bounds to determine the actual driven range.
  if (nodeBounds.has_value() && bounds.overlaps(ConstantRange(*nodeBounds))) {
    auto newRange = bounds.intersect(ConstantRange(*nodeBounds));
    edgeBounds = {newRange.lower(), newRange.upper()};
  }

  DEBUG_PRINT("New edge {} from node {} to node {} via {}{}\n",
              toString(edgeKind), source.ID, target.ID, symbol.hierarchicalPath,
              toString(edgeBounds));

  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->edges.push_back(
        {&source, &target, std::move(symbol), edgeBounds, edgeKind});
  } else {
    auto &edge = graph.addEdge(source, target);
    edge.setVariable(std::move(symbol), edgeBounds);
    edge.setEdgeKind(edgeKind);
  }
}

auto NetlistBuilder::getLSPName(ast::ValueSymbol const &symbol,
                                analysis::ValueDriver const &driver)
    -> std::string {
  FormatBuffer buf;
  ast::EvalContext evalContext(symbol);
  ast::LSPUtilities::stringifyLSP(*driver.lsp, evalContext, buf);
  return buf.str();
}

auto NetlistBuilder::determineEdgeKind(ast::ProceduralBlockSymbol const &symbol)
    -> ast::EdgeKind {
  ast::EdgeKind result = ast::EdgeKind::None;

  if (symbol.procedureKind == ast::ProceduralBlockKind::AlwaysFF ||
      symbol.procedureKind == ast::ProceduralBlockKind::Always) {

    if (symbol.getBody().kind == ast::StatementKind::Block) {
      auto const &block = symbol.getBody().as<ast::BlockStatement>();

      if (block.blockKind == ast::StatementBlockKind::Sequential &&
          block.body.kind == ast::StatementKind::ConcurrentAssertion) {
        return result;
      }
    }

    auto tck = symbol.getBody().as<ast::TimedStatement>().timing.kind;

    if (tck == ast::TimingControlKind::SignalEvent) {
      result = symbol.getBody()
                   .as<ast::TimedStatement>()
                   .timing.as<ast::SignalEventControl>()
                   .edge;

    } else if (tck == ast::TimingControlKind::EventList) {

      auto const &events = symbol.getBody()
                               .as<ast::TimedStatement>()
                               .timing.as<ast::EventListControl>()
                               .events;

      // We need to decide if this has the potential for combinational loops
      // The most strict test is if for any unique signal on the event list
      // only one edge (pos or neg) appears e.g. "@(posedge x or negedge x)"
      // is potentially combinational. At the moment we'll settle for no
      // signal having "None" edge.

      for (auto const &e : events) {
        result = e->as<ast::SignalEventControl>().edge;
        if (result == ast::EdgeKind::None) {
          break;
        }
      }

      // If we got here, edgeKind is not "None" which is all we care about.
    }
  }

  return result;
}

void NetlistBuilder::_resolveInterfaceRef(
    BumpAllocator &alloc, std::vector<InterfaceVarBounds> &result,
    ast::EvalContext &evalCtx, ast::ModportPortSymbol const &symbol,
    ast::Expression const &prefixExpr) {

  DEBUG_PRINT("Resolving interface references for symbol {} {} loc={}\n",
              toString(symbol.kind), symbol.name,
              Utilities::locationStr(compilation, symbol.location));

  // Visit all LSPs in the connection expression.
  ast::LSPUtilities::expandIndirectLSPs(
      alloc, prefixExpr, evalCtx,
      [&](const ast::ValueSymbol &symbol, const ast::Expression &lsp,
          bool /*isLValue*/) -> void {
        // Get the bounds of the LSP.
        auto bounds =
            ast::LSPUtilities::getBounds(lsp, evalCtx, symbol.getType());
        if (!bounds) {
          return;
        }

        DEBUG_PRINT("Resolved LSP in modport connection expression: {} {} "
                    "bounds={} loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds),
                    Utilities::locationStr(compilation, symbol.location));

        if (symbol.kind == ast::SymbolKind::Variable) {
          // This is an interface variable, so add it to the result.
          result.emplace_back(symbol.as<ast::VariableSymbol>(),
                              DriverBitRange(*bounds));

        } else if (symbol.kind == ast::SymbolKind::ModportPort) {
          // Recurse to follow a nested modport connection.
          _resolveInterfaceRef(alloc, result, evalCtx,
                               symbol.as<ast::ModportPortSymbol>(), lsp);
        } else {
          // The symbol is not an interface variable or modport port — it is
          // likely a parameter or genvar used as an array index in the access
          // expression.  LSPVisitor visits both the array value and the
          // selector, so index symbols reach this callback.  They are not
          // interface signals and should be ignored.
          DEBUG_PRINT("Ignoring non-interface symbol of kind {}\n",
                      toString(symbol.kind));
        }
      });
}

auto NetlistBuilder::resolveInterfaceRef(ast::EvalContext &evalCtx,
                                         ast::ModportPortSymbol const &symbol,
                                         ast::Expression const &lsp)
    -> std::vector<InterfaceVarBounds> {

  // This method translates references to modport ports found in
  // in expressions via their connection expressions, to follow modport
  // connections back to the base interface. The underlying interface variable
  // symbol and its access bounds can then be resolved, allowing inputs to be
  // matched with outputs and vice versa.

  BumpAllocator alloc;
  std::vector<InterfaceVarBounds> result;
  _resolveInterfaceRef(alloc, result, evalCtx, symbol, lsp);
  return result;
}

auto NetlistBuilder::createPort(ast::PortSymbol const &symbol,
                                DriverBitRange bounds) -> NetlistNode & {
  SLANG_ASSERT(symbol.internalSymbol != nullptr);
  auto ref = toSymbolRef(*symbol.internalSymbol);
  auto &node = graph.addNode(std::make_unique<Port>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      symbol.direction, bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createVariable(ast::VariableSymbol const &symbol,
                                    DriverBitRange bounds) -> NetlistNode & {
  auto ref = toSymbolRef(symbol);
  auto &node = graph.addNode(std::make_unique<Variable>(
      std::move(ref.name), std::move(ref.hierarchicalPath), ref.location,
      bounds));
  variables.insert(symbol, bounds, node);
  return node;
}

auto NetlistBuilder::createState(ast::ValueSymbol const &symbol,
                                 DriverBitRange bounds) -> NetlistNode & {
  auto symRef = toSymbolRef(symbol);
  auto node = std::make_unique<State>(std::move(symRef.name),
                                      std::move(symRef.hierarchicalPath),
                                      symRef.location, bounds);
  auto &ref = threadLocalDeferredWork
                  ? threadLocalDeferredWork->addNode(std::move(node))
                  : graph.addNode(std::move(node));
  variables.insert(symbol, bounds, ref);
  return ref;
}

void NetlistBuilder::addDriversToNode(DriverList const &drivers,
                                      NetlistNode &node, SymbolReference symbol,
                                      DriverBitRange bounds) {
  for (auto driver : drivers) {
    if (driver.node != nullptr) {
      addDependency(*driver.node, node, symbol, bounds);
    }
  }
}

auto NetlistBuilder::merge(NetlistNode &a, NetlistNode &b) -> NetlistNode & {
  if (a.ID == b.ID) {
    return a;
  }

  auto mergeNode = std::make_unique<Merge>();
  auto &node = threadLocalDeferredWork
                   ? threadLocalDeferredWork->addNode(std::move(mergeNode))
                   : graph.addNode(std::move(mergeNode));
  addDependency(a, node);
  addDependency(b, node);
  return node;
}

void NetlistBuilder::addRvalue(ast::EvalContext &evalCtx,
                               ast::ValueSymbol const &symbol,
                               ast::Expression const &lsp,
                               DriverBitRange bounds, NetlistNode *node) {

  // For rvalues that are via a modport port, resolve the interface variables
  // they are driven from and add dependencies from each interface variable to
  // the node where the rvalue occurs.
  if (symbol.kind == ast::SymbolKind::ModportPort) {
    for (auto &var : resolveInterfaceRef(
             evalCtx, symbol.as<ast::ModportPortSymbol>(), lsp)) {
      if (auto *varNode = getVariable(var.symbol, var.bounds)) {
        addDependency(*varNode, *node, toSymbolRef(symbol), bounds);
      }
    }
    return;
  }

  // Add to the pending list to be processed later.
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->pendingRValues.emplace_back(&symbol, &lsp, bounds,
                                                         node);
  } else {
    pendingRValues.emplace_back(&symbol, &lsp, bounds, node);
  }
}

void NetlistBuilder::processPendingRvalues() {
  for (auto &pending : pendingRValues) {
    DEBUG_PRINT("Processing pending R-value {}{}\n", pending.symbol->name,
                toString(pending.bounds));

    if (pending.node != nullptr) {

      auto symRef = toSymbolRef(*pending.symbol);

      auto driverList =
          driverMap.getDrivers(drivers, *pending.symbol, pending.bounds);

      // If there is a materialized variable/state node matching this rvalue,
      // keep that node in the trace but also bridge any driver-map sources into
      // it. This matters for aggregate rvalues: a full aggregate node may exist
      // for lookup/display, while its procedural drivers are tracked on element
      // or slice ranges in the driver map.
      if (auto *stateNode = getVariable(*pending.symbol, pending.bounds)) {
        std::unordered_set<LocationKey, LocationKeyHash> bridgedAssignments;
        for (auto const &source : driverList) {
          if (source.node != nullptr && source.node->ID != stateNode->ID &&
              source.node->kind != NodeKind::Port) {
            if (source.node->kind == NodeKind::Assignment) {
              auto key = locationKey(source.node->as<Assignment>().location);
              if (key && !bridgedAssignments.insert(*key).second) {
                continue;
              }
            }
            addDependency(*source.node, *stateNode, symRef, pending.bounds);
          }
        }
        addDependency(*stateNode, *pending.node, symRef, pending.bounds);
        continue;
      }

      // Otherwise, find drivers of the pending R-value, and for each one add
      // edges from the driver to the R-value.
      for (auto const &source : driverList) {
        if (source.node != nullptr) {
          addDependency(*source.node, *pending.node, symRef, pending.bounds);
        }
      }
    }
  }
  pendingRValues.clear();
}

void NetlistBuilder::hookupOutputPort(ast::ValueSymbol const &symbol,
                                      DriverBitRange bounds,
                                      DriverList const &driverList,
                                      ast::EdgeKind edgeKind) {

  // If there is an output port associated with this symbol, then add a
  // dependency from the driver to the port.
  if (auto const *portBackRef = symbol.getFirstPortBackref()) {

    if (portBackRef->getNextBackreference() != nullptr) {
      DEBUG_PRINT("Ignoring symbol with multiple port back refs");
      return;
    }

    // Lookup the port node in the graph.
    const ast::PortSymbol *portSymbol = portBackRef->port;
    if (auto *portNode = getVariable(*portSymbol, bounds)) {

      // Connect the drivers to the port node(s).
      auto symRef = toSymbolRef(symbol);
      for (auto const &driver : driverList) {
        addDependency(*driver.node, *portNode, symRef, bounds, edgeKind);
      }
    }
  }
}

void NetlistBuilder::mergeDrivers(ast::EvalContext &evalCtx,
                                  ValueTracker const &valueTracker,
                                  ValueDrivers const &valueDrivers,
                                  ast::EdgeKind edgeKind) {
  DEBUG_PRINT("Merging procedural drivers\n");

  valueTracker.visitAll([&](const ast::ValueSymbol *symbol, uint32_t index) {
    DEBUG_PRINT("Symbol {} at index={}\n", symbol->name, index);

    if (index >= valueDrivers.size()) {
      // No drivers for this symbol so we don't need to do anything.
      return;
    }

    if (valueDrivers[index].empty()) {
      // No drivers for this symbol so we don't need to do anything.
      return;
    }

    // Merge all of the driver intervals for the symbol into the global map.
    for (auto it = valueDrivers[index].begin(); it != valueDrivers[index].end();
         it++) {

      DEBUG_PRINT("Merging driver interval {}\n", toString(it.bounds()));

      auto const &driverList = valueDrivers[index].getDriverList(*it);
      auto const &valueSymbol = symbol->as<ast::ValueSymbol>();

      if (edgeKind == ast::EdgeKind::None) {

        // Combinational edge, so just add the interval with the driving
        // node(s).
        mergeDrivers(*symbol, it.bounds(), driverList);

        hookupOutputPort(valueSymbol, it.bounds(), driverList);

      } else {

        // Sequential edge, so the procedural drivers act on a stateful
        // variable which is represented by a node in the graph. We create
        // this node, add edges from the procedural drivers to it, and then
        // add the state node as the new driver for the range.

        auto &stateNode = createState(valueSymbol, it.bounds());

        auto symRef = toSymbolRef(*symbol);
        for (auto const &driver : driverList) {
          addDependency(*driver.node, stateNode, symRef, it.bounds(), edgeKind);
        }

        hookupOutputPort(valueSymbol, it.bounds(),
                         {{.node = &stateNode, .lsp = nullptr}}, edgeKind);
      }

      auto symRef = toSymbolRef(*symbol);
      for (auto const &driver : driverList) {

        if (symbol->kind == ast::SymbolKind::ModportPort) {
          // Resolve the interface variables that are driven by a modport port
          // symbol. Add a dependency from the driver to each of the interface
          // variable nodes.
          for (auto &var : resolveInterfaceRef(
                   evalCtx, symbol->as<ast::ModportPortSymbol>(),
                   *driver.lsp)) {
            if (auto *varNode = getVariable(var.symbol, var.bounds)) {
              addDependency(*driver.node, *varNode, symRef, var.bounds);
            }
          }
        } else if (symbol->kind == ast::SymbolKind::Variable) {
          // Check if variable symbols have a node defined for the current
          // bounds. Eg when interface members are assigned to directly.
          if (auto *varNode =
                  getVariable(symbol->as<ast::VariableSymbol>(), it.bounds())) {
            auto varBounds = getNodeBounds(*varNode);
            SLANG_ASSERT(varBounds.has_value());
            addDependency(*driver.node, *varNode, symRef, *varBounds);
          }
        }
      }
    }
  });
}

void NetlistBuilder::handlePortConnection(
    ast::Symbol const &containingSymbol,
    ast::PortConnection const &portConnection) {

  auto const &port = portConnection.port.as<ast::PortSymbol>();
  auto const *expr = portConnection.getExpression();

  if (expr == nullptr || expr->bad()) {
    // Empty port hookup so skip.
    return;
  }

  ast::EvalContext evalCtx(containingSymbol);

  // Remove the assignment from output port connection expressions.
  bool isOutput{false};
  if (expr->kind == ast::ExpressionKind::Assignment) {
    expr = &expr->as<ast::AssignmentExpression>().left();
    isOutput = true;
  }

  auto portNodes = getVariable(port);
  DEBUG_PRINT("Port {} has {} nodes\n", port.name, portNodes.size());

  // Visit all LSPs in the connection expression.
  ast::LSPUtilities::visitLSPs(
      *expr, evalCtx,
      [&](const ast::ValueSymbol &symbol, const ast::Expression &lsp,
          bool /*isLValue*/) -> void {
        // Get the bounds of the LSP.
        auto bounds =
            ast::LSPUtilities::getBounds(lsp, evalCtx, symbol.getType());
        if (!bounds) {
          return;
        }

        DEBUG_PRINT("Resolved LSP in port connection expression: {} {} "
                    "bounds={}, loc={}\n",
                    toString(symbol.kind), symbol.name, toString(*bounds),
                    Utilities::locationStr(compilation, symbol.location));

        for (auto *node : portNodes) {
          auto driverBounds = DriverBitRange(*bounds);
          if (isOutput) {
            // If lvalue, then the port defines symbol with bounds.
            // FIXME: *Merge* the driver there is currently no way to tell what
            // bounds the lsp occupies within the port type and to drive
            // appropriately.
            mergeDrivers(symbol, driverBounds, {DriverInfo(node, &lsp)});
            hookupOutputPort(symbol, driverBounds, {DriverInfo(node, nullptr)});
          } else {
            // If rvalue, then the port is driven by symbol with bounds.
            addRvalue(evalCtx, symbol, lsp, driverBounds, node);
          }
        }
      });
}

void NetlistBuilder::handle(ast::PortSymbol const &symbol) {
  DEBUG_PRINT("PortSymbol {}\n", symbol.name);

  if (symbol.internalSymbol != nullptr && symbol.internalSymbol->isValue()) {
    auto const &valueSymbol = symbol.internalSymbol->as<ast::ValueSymbol>();
    auto drivers = analysisManager.getDrivers(valueSymbol);
    bool createdNode = false;
    for (auto &[driver, bounds] : drivers) {

      DEBUG_PRINT("{} driven by prefix={}\n", toString(bounds),
                  getLSPName(valueSymbol, *driver));

      // Add a port node for the driven range, and add a driver entry for it.
      // Note that the driver key is a PortSymbol, rather than a ValueSymbol.
      auto &node = createPort(symbol, DriverBitRange(bounds));
      createdNode = true;

      // If the driver is an input port, then create a dependency to the
      // internal symbol (ValueSymbol).
      if (driver->isInputPort()) {
        addDriver(valueSymbol, nullptr, DriverBitRange(bounds), &node);
      }
    }

    if (!createdNode && materializeInternalVariables) {
      auto bounds = getFullTypeBounds(valueSymbol.getType());
      if (!bounds) {
        return;
      }

      auto &node = createPort(symbol, *bounds);
      if (symbol.direction == ast::ArgumentDirection::In ||
          symbol.direction == ast::ArgumentDirection::InOut) {
        addDriver(valueSymbol, nullptr, *bounds, &node);
      }
    }
  }
}

void NetlistBuilder::handle(ast::VariableSymbol const &symbol) {
  bool isInterfaceVariable = false;

  if (auto const *scope = symbol.getParentScope()) {
    auto const *container = scope->getContainingInstance();
    isInterfaceVariable = container != nullptr &&
                          container->parentInstance != nullptr &&
                          container->parentInstance->isInterface();
  }

  bool shouldMaterializeInternal = materializeInternalVariables;
  if (shouldMaterializeInternal && symbol.getFirstPortBackref() != nullptr) {
    shouldMaterializeInternal = false;
  }

  if (!isInterfaceVariable && !shouldMaterializeInternal) {
    return;
  }

  auto shouldCreateNode = [&](analysis::ValueDriver const &driver) {
    if (isInterfaceVariable) {
      return true;
    }

    if (!shouldMaterializeInternal) {
      return false;
    }

    if (driver.kind != analysis::DriverKind::Procedural ||
        driver.containingSymbol->kind != ast::SymbolKind::ProceduralBlock) {
      return true;
    }

    auto edgeKind = determineEdgeKind(
        driver.containingSymbol->as<ast::ProceduralBlockSymbol>());
    return edgeKind == ast::EdgeKind::None;
  };

  auto drivers = analysisManager.getDrivers(symbol);
  bool sawDriver = false;
  bool createdNode = false;
  for (auto &[driver, bounds] : drivers) {
    sawDriver = true;
    if (!shouldCreateNode(*driver)) {
      continue;
    }

    DEBUG_PRINT("{} variable {}\n",
                isInterfaceVariable ? "Interface" : "Internal", symbol.name);
    DEBUG_PRINT("[{}:{}] driven by prefix={}\n", bounds.first, bounds.second,
                getLSPName(symbol, *driver));

    // Create a variable node for the driven range so it can act as a graph
    // lookup endpoint and preserve intermediate values in traces.
    createVariable(symbol, DriverBitRange(bounds));
    createdNode = true;
  }

  if (!sawDriver && !createdNode && shouldMaterializeInternal) {
    auto bounds = getFullTypeBounds(symbol.getType());
    if (bounds) {
      createVariable(symbol, *bounds);
    }
  }
}

void NetlistBuilder::handle(ast::InstanceSymbol const &symbol) {
  DEBUG_PRINT("InstanceSymbol {}\n", symbol.name);

  if (symbol.body.flags.has(ast::InstanceFlags::Uninstantiated)) {
    return;
  }

  symbol.body.visit(*this);

  // Handle port connections.
  for (auto const *portConnection : symbol.getPortConnections()) {

    if (portConnection->port.kind == ast::SymbolKind::Port) {
      handlePortConnection(symbol, *portConnection);
    } else if (portConnection->port.kind == ast::SymbolKind::InterfacePort) {
      // Interfaces are handled via ModportPorts.
    } else {
      SLANG_UNREACHABLE;
    }
  }
}

void NetlistBuilder::handle(ast::ProceduralBlockSymbol const &symbol) {
  if (collectingPhase) {
    deferredBlocks.push_back({&symbol, /*isProcedural=*/true});
    return;
  }
  handleProceduralBlock(symbol);
}

void NetlistBuilder::handle(ast::ContinuousAssignSymbol const &symbol) {
  if (collectingPhase) {
    deferredBlocks.push_back({&symbol, /*isProcedural=*/false});
    return;
  }
  handleContinuousAssign(symbol);
}

void NetlistBuilder::handleProceduralBlock(
    ast::ProceduralBlockSymbol const &symbol) {
  DEBUG_PRINT("ProceduralBlock\n");
  auto edgeKind = determineEdgeKind(symbol);
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.as<ast::ProceduralBlockSymbol>().getBody());
  dfa->finalize();
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->deferredMerges.push_back([this, dfa, edgeKind]() {
      mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                   dfa->getState().valueDrivers, edgeKind);
    });
  } else {
    mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                 dfa->getState().valueDrivers, edgeKind);
  }
}

void NetlistBuilder::handleContinuousAssign(
    ast::ContinuousAssignSymbol const &symbol) {
  DEBUG_PRINT("ContinuousAssign\n");
  auto dfa = std::make_shared<DataFlowAnalysis>(analysisManager, symbol, *this);
  dfa->run(symbol.getAssignment());
  if (threadLocalDeferredWork) {
    threadLocalDeferredWork->deferredMerges.push_back([this, dfa]() {
      mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                   dfa->getState().valueDrivers, ast::EdgeKind::None);
    });
  } else {
    mergeDrivers(dfa->getEvalContext(), dfa->valueTracker,
                 dfa->getState().valueDrivers, ast::EdgeKind::None);
  }
}

void NetlistBuilder::handle(ast::GenerateBlockSymbol const &symbol) {
  if (!symbol.isUninstantiated) {
    visitMembers(symbol);
  }
}

} // namespace slang::netlist
