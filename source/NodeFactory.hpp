#pragma once

#include "BitSlice.hpp"

#include "netlist/DriverBitRange.hpp"
#include "netlist/NetlistNode.hpp"
#include "netlist/TextLocation.hpp"

#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/numeric/ConstantValue.h"

namespace slang::netlist {

class NetlistBuilder;
struct Segment;

/// Centralizes creation of netlist nodes. Each method allocates the
/// appropriate node, registers it with the builder's `NetlistGraph`,
/// and (for the value-bearing kinds) records the (symbol, bounds) →
/// node mapping in the builder's `VariableTracker`. All location and
/// SymbolReference materialization goes through the builder's
/// thread-local-cached helpers (`toTextLocation`, `toSymbolRef`).
class NodeFactory {
public:
  explicit NodeFactory(NetlistBuilder &builder) : builder(builder) {}

  /// Create a port node and register it under @p symbol's bounds.
  auto createPort(ast::PortSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create a variable node and register it under @p symbol's bounds.
  auto createVariable(ast::VariableSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create a state node (sequential persistent value) and register it
  /// under @p symbol's bounds.
  auto createState(ast::ValueSymbol const &symbol, DriverBitRange bounds)
      -> NetlistNode &;

  /// Create an assignment node.
  auto createAssignment(ast::AssignmentExpression const &expr) -> NetlistNode &;

  /// Create a constant-driver node.
  auto createConstant(ConstantValue value, uint64_t width,
                      TextLocation location) -> NetlistNode &;

  /// Materialize a Constant node for the bits a
  /// `BitSliceSource::Kind::Constant` source contributes to one aligned
  /// segment. Slices @p src's value down to the segment's bit range when
  /// wider, derives the node location from the source's recorded
  /// expression (falling back to @p fallbackLoc for synthetic constants
  /// like zero-extension padding), and registers the node in the graph.
  /// Caller is responsible for adding edges out of it.
  auto createConstantForSegment(BitSliceSource const &src, Segment const &seg,
                                TextLocation fallbackLoc) -> NetlistNode &;

  /// Create a conditional node.
  auto createConditional(ast::ConditionalStatement const &stmt)
      -> NetlistNode &;

  /// Create a case node.
  auto createCase(ast::CaseStatement const &stmt) -> NetlistNode &;

private:
  NetlistBuilder &builder;
};

} // namespace slang::netlist
