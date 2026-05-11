#include "PortConnectionHandler.hpp"

#include "BitSliceList.hpp"
#include "NetlistBuilder.hpp"

#include "common/Utilities.hpp"
#include "netlist/Debug.hpp"

#include "slang/ast/ValuePath.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/util/SmallVector.h"

namespace slang::netlist {

namespace {

/// Append the bit-offset cuts implied by @p expr's structure to
/// @p cuts. LSP-shaped operands also contribute any cuts already
/// registered against their root symbol, propagating cuts down the
/// hierarchy. Endpoints (0, width) are excluded.
void collectActualCuts(ast::Expression const &expr, ast::EvalContext &evalCtx,
                       CutRegistry const &registry, uint64_t baseOffset,
                       std::vector<uint64_t> &cuts) {
  using namespace ast;
  switch (expr.kind) {
  case ExpressionKind::Concatenation: {
    auto const &concat = expr.as<ConcatenationExpression>();
    auto const &operands = concat.operands();
    uint64_t offset = baseOffset;
    // LSB first; LRM lists concat operands MSB-first.
    for (auto it = operands.rbegin(); it != operands.rend(); ++it) {
      auto w = (*it)->type->getSelectableWidth();
      collectActualCuts(**it, evalCtx, registry, offset, cuts);
      offset += w;
      if (offset != baseOffset + expr.type->getSelectableWidth()) {
        cuts.push_back(offset);
      }
    }
    break;
  }
  case ExpressionKind::Replication: {
    auto const &rep = expr.as<ReplicationExpression>();
    auto countConst = rep.count().eval(evalCtx);
    if (!countConst.isInteger()) {
      break;
    }
    auto maybeCount = countConst.integer().as<int64_t>();
    if (!maybeCount || *maybeCount <= 0) {
      break;
    }
    auto unitWidth = rep.concat().type->getSelectableWidth();
    for (int64_t i = 0; i < *maybeCount; ++i) {
      collectActualCuts(rep.concat(), evalCtx, registry,
                        baseOffset + i * unitWidth, cuts);
      uint64_t boundary = baseOffset + (i + 1) * unitWidth;
      if (boundary != baseOffset + expr.type->getSelectableWidth()) {
        cuts.push_back(boundary);
      }
    }
    break;
  }
  case ExpressionKind::Conversion: {
    auto const &conv = expr.as<ConversionExpression>();
    auto const &inner = conv.operand();
    auto outerWidth = expr.type->getSelectableWidth();
    auto innerWidth = inner.type->getSelectableWidth();
    if (outerWidth == innerWidth) {
      collectActualCuts(inner, evalCtx, registry, baseOffset, cuts);
    } else if (outerWidth > innerWidth) {
      collectActualCuts(inner, evalCtx, registry, baseOffset, cuts);
      // Boundary between operand bits and zero/sign-ext padding.
      cuts.push_back(baseOffset + innerWidth);
    }
    break;
  }
  case ExpressionKind::NamedValue:
  case ExpressionKind::HierarchicalValue:
  case ExpressionKind::ElementSelect:
  case ExpressionKind::RangeSelect:
  case ExpressionKind::MemberAccess: {
    // Pull in any cuts already registered against the LSP's root, so
    // they propagate down to the next port boundary.
    ast::ValuePath path(expr, evalCtx);
    auto const *root = path.rootSymbol();
    if (root == nullptr) {
      break;
    }
    auto const *hints = registry.cutsFor(*root);
    if (hints == nullptr) {
      break;
    }
    uint64_t lspLo = static_cast<uint64_t>(path.lspBounds.first);
    uint64_t lspHi = static_cast<uint64_t>(path.lspBounds.second) + 1;
    auto first = std::upper_bound(hints->begin(), hints->end(), lspLo);
    auto last = std::lower_bound(hints->begin(), hints->end(), lspHi);
    for (auto it = first; it != last; ++it) {
      cuts.push_back(baseOffset + (*it - lspLo));
    }
    break;
  }
  default:
    break;
  }
}

} // namespace

void PortConnectionHandler::materializePortNodes(
    ast::PortSymbol const &symbol) {
  if (symbol.internalSymbol == nullptr || !symbol.internalSymbol->isValue()) {
    return;
  }
  auto const &valueSymbol = symbol.internalSymbol->as<ast::ValueSymbol>();
  // AnalysisManager stores drivers against canonical bodies only;
  // redirect lookups for non-canonical bodies to their canonical
  // counterpart so getDrivers returns the right set.
  auto const &driverQuerySymbol =
      builder.canonicalResolver.getCanonicalValueSymbol(valueSymbol);
  auto drivers = builder.analysisManager.getDrivers(driverQuerySymbol);

  // No hints (or feature off) ⇒ one node per driver.
  std::vector<uint64_t> const *hints = nullptr;
  if (builder.options.propCutsAcrossPorts) {
    hints = cutRegistry.cutsFor(valueSymbol);
  }

  for (auto const *driver : drivers) {
    auto bounds = driver->getBounds();
    DEBUG_PRINT("{} driven by prefix={}\n", toString(bounds),
                NetlistBuilder::getDriverPathName(valueSymbol, *driver));

    // Split the driver's range at every cut strictly inside it.
    SmallVector<DriverBitRange, 2> segments;
    if (hints == nullptr) {
      segments.push_back(DriverBitRange(bounds));
    } else {
      uint64_t lo = static_cast<uint64_t>(bounds.first);
      uint64_t hi = static_cast<uint64_t>(bounds.second) + 1;
      auto first = std::upper_bound(hints->begin(), hints->end(), lo);
      auto last = std::lower_bound(hints->begin(), hints->end(), hi);
      uint64_t prev = lo;
      for (auto it = first; it != last; ++it) {
        segments.push_back(DriverBitRange{static_cast<int32_t>(prev),
                                          static_cast<int32_t>(*it - 1)});
        prev = *it;
      }
      segments.push_back(DriverBitRange{static_cast<int32_t>(prev),
                                        static_cast<int32_t>(hi - 1)});
    }
    for (auto seg : segments) {
      auto &node = builder.nodeFactory.createPort(symbol, seg);
      if (driver->isInputPort()) {
        builder.addDriver(valueSymbol, nullptr, seg, &node);
      }
    }
  }
}

void PortConnectionHandler::recordCutsFromPortConnections(
    ast::InstanceSymbol const &instance) {
  for (auto const *portConnection : instance.getPortConnections()) {
    if (portConnection->port.kind != ast::SymbolKind::Port) {
      continue;
    }
    auto const &port = portConnection->port.as<ast::PortSymbol>();
    auto const *expr = portConnection->getExpression();
    if (expr == nullptr || expr->bad()) {
      continue;
    }
    if (port.internalSymbol == nullptr || !port.internalSymbol->isValue()) {
      continue;
    }
    if (expr->kind == ast::ExpressionKind::Assignment) {
      expr = &expr->as<ast::AssignmentExpression>().left();
    }
    if (!port.getType().isIntegral() || !expr->type->isIntegral() ||
        port.getType().getSelectableWidth() !=
            expr->type->getSelectableWidth()) {
      continue;
    }

    ast::EvalContext evalCtx(instance);
    std::vector<uint64_t> cuts;
    collectActualCuts(*expr, evalCtx, cutRegistry, /*baseOffset=*/0, cuts);
    if (cuts.empty()) {
      continue;
    }
    cutRegistry.addCuts(port.internalSymbol->as<ast::ValueSymbol>(),
                        std::move(cuts));
  }
}

void PortConnectionHandler::handlePortConnection(
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

  if (!builder.options.resolveAssignBits) {
    handlePortConnectionLegacy(port, *expr, isOutput, evalCtx);
    return;
  }

  // Bit-aligned decomposition only makes sense for integral port/actual
  // types whose selectable width is well-defined. Fall back cheaply for
  // unpacked aggregates, strings, etc.
  if (!port.getType().isIntegral() || !expr->type->isIntegral()) {
    handlePortConnectionLegacy(port, *expr, isOutput, evalCtx);
    return;
  }

  // Bit-aligned path: decompose both sides into slicelists and drive
  // one aligned segment at a time.
  auto portList = buildPortSliceList(port);
  // A zero-width port has no driver edges to record, so there's nothing
  // to do here — including no LSP traversal of `expr`, which would be
  // the legacy-walk side effect. No zero-width port today has observable
  // connectivity, so that omission is a no-op.
  if (portList.width() == 0) {
    return;
  }

  auto actualList = BitSliceList::build(*expr, evalCtx, sliceAllocator);
  // Slang type-checking is lenient enough that the port and the
  // connection expression can have different selectable widths (e.g. an
  // instance-array port implicitly sliced per instance, or a packed enum
  // coerced to a 1-bit logic). Fall back to the legacy whole-port LSP
  // walk in that case; it is bit-imprecise but safe.
  if (portList.width() != actualList.width()) {
    handlePortConnectionLegacy(port, *expr, isOutput, evalCtx);
    return;
  }

  for (auto const &seg : alignSegments(portList, actualList)) {
    drivePortSegment(seg, isOutput, evalCtx);
  }
}

void PortConnectionHandler::handlePortConnectionLegacy(
    ast::PortSymbol const &port, ast::Expression const &expr, bool isOutput,
    ast::EvalContext &evalCtx) {
  auto portNodes = builder.getVariable(port);
  DEBUG_PRINT("Port {} has {} nodes\n", port.name, portNodes.size());

  // Visit all LSPs in the connection expression.
  ast::ValuePath::visitPaths(
      expr, evalCtx, [&](const ast::ValuePath &path) -> void {
        if (path.empty() || !path.lsp) {
          return;
        }
        auto const *rootSymbol = path.rootSymbol();
        if (rootSymbol == nullptr) {
          return;
        }
        auto const &symbol = *rootSymbol;
        auto const &lsp = *path.lsp;

        DEBUG_PRINT(
            "Resolved LSP in port connection expression: {} {} "
            "bounds={}, loc={}\n",
            toString(symbol.kind), symbol.name, toString(path.lspBounds),
            Utilities::locationStr(builder.compilation, symbol.location));

        for (auto *node : portNodes) {
          auto driverBounds = DriverBitRange(path.lspBounds);
          if (isOutput) {
            // If lvalue, then the port defines symbol with bounds.
            // FIXME: *Merge* the driver — there is currently no way to
            // tell what bounds the LSP occupies within the port type and
            // to drive appropriately.
            builder.mergeDrivers(symbol, driverBounds,
                                 {DriverInfo(node, &lsp)});
            builder.hookupOutputPort(symbol, driverBounds,
                                     {DriverInfo(node, nullptr)});
          } else {
            // If rvalue, then the port is driven by symbol with bounds.
            builder.addRvalue(evalCtx, symbol, lsp, driverBounds, node);
          }
        }
      });
}

auto PortConnectionHandler::buildPortSliceList(ast::PortSymbol const &symbol)
    -> BitSliceList {
  // Port nodes are created per (driver, bit-range) pair by handle(PortSymbol)
  // and their bit ranges can differ from node to node. An inout port, for
  // example, may register one full-width node for the input-side driver plus
  // N per-bit nodes for the output-side drivers. Build the slicelist over
  // the cut-point grid formed by every node's lower/upper bounds; each
  // segment carries as PortNode sources every node whose bounds fully
  // contain it. Segments with no covering node are left as an Opaque slice
  // referencing a dummy expression — but in practice `handle(PortSymbol)`
  // always registers nodes for the full port type, so such gaps don't occur.
  auto nodes = builder.getVariable(symbol);

  uint64_t fullWidth = symbol.getType().getSelectableWidth();

  BitSliceList list;
  if (fullWidth == 0) {
    return list;
  }

  std::vector<uint64_t> cuts;
  cuts.reserve(nodes.size() * 2 + 2);
  cuts.push_back(0);
  cuts.push_back(fullWidth);
  for (auto *node : nodes) {
    auto bounds = node->getBounds();
    // Nodes registered without bounds are filtered here rather than asserted
    // on: the contract is shaped by callers and a missing-bounds node should
    // degrade gracefully to "this node doesn't cover any range" rather than
    // crash.
    if (!bounds) {
      continue;
    }
    cuts.push_back(static_cast<uint64_t>(bounds->lower()));
    cuts.push_back(static_cast<uint64_t>(bounds->upper()) + 1);
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    uint64_t segLo = cuts[i];
    uint64_t segHi = cuts[i + 1];
    BitSlice slice{segLo, segHi, {}};
    for (auto *node : nodes) {
      auto bounds = node->getBounds();
      if (!bounds) {
        continue;
      }
      auto nodeLo = static_cast<uint64_t>(bounds->lower());
      auto nodeHi = static_cast<uint64_t>(bounds->upper()) + 1;
      if (nodeLo <= segLo && segHi <= nodeHi) {
        slice.sources.emplace_back(
            BitSliceSource::makePortNode(*node, segLo, segHi));
      }
    }
    list.pushPaddingSlice(std::move(slice));
  }
  return list;
}

void PortConnectionHandler::drivePortSegment(Segment const &seg, bool isOutput,
                                             ast::EvalContext &evalCtx) {
  // The formal side may have zero, one, or multiple PortNode sources per
  // segment: an unused port-bit range has none, a plain unidirectional port
  // has one, and an inout port has two (the input-side and output-side nodes
  // registered at identical bounds). Drive every formal-side node from each
  // RHS source, matching the legacy path's "for node : portNodes" behavior.
  SmallVector<NetlistNode *, 2> portNodes;
  for (auto const &lhsSrc : seg.lhsSources) {
    if (lhsSrc.kind == BitSliceSource::Kind::PortNode) {
      portNodes.emplace_back(lhsSrc.portNode);
    }
  }
  if (portNodes.empty()) {
    return;
  }

  for (auto const &src : seg.rhsSources) {
    switch (src.kind) {
    case BitSliceSource::Kind::Lsp: {
      auto const &path = *src.path;
      auto const *root = path.rootSymbol();
      if (root == nullptr) {
        break;
      }
      // Map the segment's concat-space range back onto the LSP's own
      // bounds so the driven range is narrow to what this segment
      // actually covers, not the full LSP.
      auto offset = seg.concatLo - src.srcLo;
      auto segWidth = seg.width();
      // alignSegments must never emit zero-width segments; `segWidth - 1`
      // would underflow int32_t.
      SLANG_ASSERT(segWidth > 0);
      auto lo = static_cast<int32_t>(path.lspBounds.first + offset);
      auto hi =
          static_cast<int32_t>(path.lspBounds.first + offset + segWidth - 1);
      DriverBitRange mapped{lo, hi};
      for (auto *portNode : portNodes) {
        if (isOutput) {
          builder.mergeDrivers(*root, mapped, {DriverInfo(portNode, path.lsp)});
          builder.hookupOutputPort(*root, mapped,
                                   {DriverInfo(portNode, nullptr)});
        } else {
          builder.addRvalue(evalCtx, *root, *path.lsp, mapped, portNode);
        }
      }
      break;
    }
    case BitSliceSource::Kind::Opaque: {
      // Fan every LSP inside the opaque expression into each port node
      // via the existing LSP visitor. Bounds are the full LSP range;
      // the opaque fallback is coarse by design.
      //
      // Inout ports register multiple Port nodes at overlapping bit
      // ranges (one per driver), all with direction=InOut, so a
      // segment's LSP sources fan into both input-side and output-side
      // nodes. Matches the legacy behaviour but is bit-imprecise.
      ast::ValuePath::visitPaths(
          *src.opaqueExpr, evalCtx, [&](const ast::ValuePath &path) {
            if (path.empty() || !path.lsp) {
              return;
            }
            auto const *root = path.rootSymbol();
            if (root == nullptr) {
              return;
            }
            for (auto *portNode : portNodes) {
              builder.addRvalue(evalCtx, *root, *path.lsp,
                                DriverBitRange(path.lspBounds), portNode);
            }
          });
      break;
    }
    case BitSliceSource::Kind::Padding:
      // No driver.
      break;
    case BitSliceSource::Kind::Constant: {
      // Only meaningful when driving an input formal from a constant
      // actual; output bindings on a literal actual are rejected by
      // slang elaboration before we get here.
      if (isOutput) {
        break;
      }
      auto &constNode = builder.nodeFactory.createConstantForSegment(
          src, seg, TextLocation{});
      for (auto *portNode : portNodes) {
        builder.addDependency(constNode, *portNode);
      }
      break;
    }
    case BitSliceSource::Kind::PortNode:
      // Only valid on the formal side.
      SLANG_UNREACHABLE;
    }
  }
}

} // namespace slang::netlist
