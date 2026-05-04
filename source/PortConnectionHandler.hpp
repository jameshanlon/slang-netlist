#pragma once

#include "BitSliceList.hpp"
#include "CutRegistry.hpp"

#include "slang/ast/EvalContext.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"

namespace slang::netlist {

class NetlistBuilder;

/// Handles port-connection wiring for the NetlistBuilder. Owns the
/// per-build state needed by the bit-aligned port path: the slice
/// allocator and the cut registry that propagates concat-shaped
/// actuals' bit boundaries down to the formal ports' internal symbols.
///
/// All graph mutation (node creation, edge insertion, driver merging)
/// is delegated back to the builder, which grants this class friend
/// access for the narrow set of private hooks it needs.
class PortConnectionHandler {
public:
  explicit PortConnectionHandler(NetlistBuilder &builder) : builder(builder) {}

  /// Create port nodes for @p symbol, splitting per registered cuts
  /// when `propCutsAcrossPorts` is enabled.
  void materializePortNodes(ast::PortSymbol const &symbol);

  /// Record cut hints from @p instance's port connections onto the
  /// formal ports' internal symbols. Must run before the body of @p
  /// instance is visited so port-node materialization sees the cuts.
  void recordCutsFromPortConnections(ast::InstanceSymbol const &instance);

  /// Drive a single port connection. Picks between the bit-aligned
  /// path (`options.resolveAssignBits`) and the legacy whole-port LSP
  /// walk based on the connection's shape and types.
  void handlePortConnection(ast::Symbol const &containingSymbol,
                            ast::PortConnection const &portConnection);

  /// Cut registry used to propagate concat-shaped actuals' bit
  /// boundaries down into the formal ports' internal symbols. Exposed
  /// so DataFlowAnalysis can split LSPs at the same cut points when
  /// `propCutsAcrossPorts` is enabled.
  auto getCutRegistry() const -> CutRegistry const & { return cutRegistry; }

private:
  /// Legacy whole-port LSP walk for a port connection. Used both when
  /// `--resolve-assign-bits` is off and as a fallback when the
  /// bit-aligned path can't build compatible-width slicelists for the
  /// two sides.
  void handlePortConnectionLegacy(ast::PortSymbol const &port,
                                  ast::Expression const &expr, bool isOutput,
                                  ast::EvalContext &evalCtx);

  /// Build a slicelist for the formal side of a port connection. Each
  /// netlist Port node belonging to @p symbol becomes a PortNode source
  /// covering the bits it drives.
  auto buildPortSliceList(ast::PortSymbol const &symbol) -> BitSliceList;

  /// Drive one aligned segment of a port connection. Each segment spans
  /// exactly one port node (by construction of the formal slicelist),
  /// so the segment's bits feed into or out of that single port node.
  void drivePortSegment(Segment const &seg, bool isOutput,
                        ast::EvalContext &evalCtx);

  NetlistBuilder &builder;

  /// Allocator reused across BitSliceList::build() invocations on the
  /// port-connection path.
  BumpAllocator sliceAllocator;

  /// Cut hints propagated from concat-shaped port-connection actuals
  /// to the formal ports' internal symbols. Drives port-node and
  /// internal-assignment splitting.
  CutRegistry cutRegistry;
};

} // namespace slang::netlist
