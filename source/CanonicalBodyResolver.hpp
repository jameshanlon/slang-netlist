#pragma once

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/util/FlatMap.h"

namespace slang::netlist {

/// Resolves AST symbols to their canonical counterparts. Slang's
/// AnalysisManager stores drivers only against canonical instance bodies, so
/// when querying drivers for symbols inside a non-canonical body we must
/// first redirect to the corresponding canonical symbol.
///
/// Slang sets a canonical pointer on the outermost non-canonical instance
/// only; nested instances are paired structurally by walking up to find an
/// anchor (a body whose canonical we already know) and lockstep-traversing
/// it with its canonical to populate every paired body and value symbol
/// below it. Results are memoized.
class CanonicalBodyResolver {
public:
  /// Return the value symbol that the AnalysisManager stores drivers
  /// against for @p symbol. For a symbol inside a non-canonical instance
  /// body that is the corresponding member of the canonical body;
  /// otherwise it is @p symbol itself.
  auto getCanonicalValueSymbol(ast::ValueSymbol const &symbol)
      -> ast::ValueSymbol const &;

  /// Return the canonical instance body for @p body. An entry mapping a
  /// body to itself means it is canonical (no redirect).
  auto getCanonicalBody(ast::InstanceBodySymbol const &body)
      -> ast::InstanceBodySymbol const &;

private:
  /// Walk @p local and @p canonical in lockstep, registering paired
  /// value symbols and instance bodies in the caches. Recurses through
  /// generate blocks and child instance bodies. Positional matching is
  /// sound because slang's instance-cache key requires identical content
  /// (same parameters, ports, and members in the same order) before
  /// linking a canonical body.
  void populatePairedBodies(ast::Scope const &local,
                            ast::Scope const &canonical);

  /// A symbol from a non-canonical instance body maps to the corresponding
  /// symbol in the canonical body; every other symbol maps to itself.
  flat_hash_map<ast::ValueSymbol const *, ast::ValueSymbol const *> valueCache;

  /// Maps each instance body to its canonical counterpart.
  flat_hash_map<ast::InstanceBodySymbol const *,
                ast::InstanceBodySymbol const *>
      bodyCache;
};

} // namespace slang::netlist
