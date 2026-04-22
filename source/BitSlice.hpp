#pragma once

#include "slang/util/SmallVector.h"

#include <cstdint>

namespace slang::ast {
class Expression;
class ValuePath;
} // namespace slang::ast

namespace slang::netlist {

class NetlistNode;

/// One contributing source for a bit slice.
///
/// A slice from a concatenation or replication of static values has exactly
/// one source; a slice coming from a conditional-operator whose arms have
/// been re-segmented onto a common cut-point grid has multiple sources
/// (one per arm, plus an opaque source carrying the condition expression).
struct BitSliceSource {
  enum class Kind {
    /// The source is an LSP expression (a NamedValue / HierarchicalValue /
    /// select / member-access path that resolves to a ValueSymbol root).
    Lsp,
    /// The source is constant-zero (zero-extension) or the sign bit
    /// (sign-extension) of a widening conversion. Produces no
    /// dependency edges.
    Padding,
    /// The source is an arbitrary expression (arithmetic, call, streaming
    /// concat, non-constant select, or a conditional operator's condition).
    /// Any LSPs inside fan into the full bit range of the slice owning
    /// this source.
    Opaque,
    /// The source is a pre-existing netlist Port node. Used on the formal
    /// side of port connections so that port bit layout aligns with the
    /// actual expression's slicelist.
    PortNode,
  };

  Kind kind = Kind::Opaque;
  const ast::ValuePath *path = nullptr;        // Lsp
  const ast::Expression *opaqueExpr = nullptr; // Opaque
  NetlistNode *portNode = nullptr;             // PortNode
  bool padIsSignExtension = false;             // Padding

  static auto makeLsp(const ast::ValuePath &path) -> BitSliceSource {
    BitSliceSource s;
    s.kind = Kind::Lsp;
    s.path = &path;
    return s;
  }

  static auto makeOpaque(const ast::Expression &expr) -> BitSliceSource {
    BitSliceSource s;
    s.kind = Kind::Opaque;
    s.opaqueExpr = &expr;
    return s;
  }

  static auto makePadding(bool isSignExtension) -> BitSliceSource {
    BitSliceSource s;
    s.kind = Kind::Padding;
    s.padIsSignExtension = isSignExtension;
    return s;
  }

  static auto makePortNode(NetlistNode &node) -> BitSliceSource {
    BitSliceSource s;
    s.kind = Kind::PortNode;
    s.portNode = &node;
    return s;
  }
};

/// A half-open bit range `[concatLo, concatHi)` within a concatenated value,
/// carrying one or more contributing sources.
struct BitSlice {
  uint64_t concatLo;
  uint64_t concatHi;
  SmallVector<BitSliceSource, 2> sources;

  auto width() const -> uint64_t { return concatHi - concatLo; }
};

} // namespace slang::netlist
