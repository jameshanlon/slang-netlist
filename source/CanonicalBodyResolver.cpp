#include "CanonicalBodyResolver.hpp"

#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"

namespace slang::netlist {

auto CanonicalBodyResolver::getCanonicalValueSymbol(
    ast::ValueSymbol const &symbol) -> ast::ValueSymbol const & {
  if (auto it = valueCache.find(&symbol); it != valueCache.end()) {
    return *it->second;
  }
  // Resolving the containing body's canonical fans out to populate
  // valueCache for every value pair under that body, so most subsequent
  // lookups are O(1) hash hits.
  if (auto const *scope = symbol.getParentScope()) {
    if (auto const *body = scope->getContainingInstance()) {
      getCanonicalBody(*body);
      if (auto it = valueCache.find(&symbol); it != valueCache.end()) {
        return *it->second;
      }
    }
  }
  // Either the symbol has no enclosing body or no redirect was found
  // — memoize identity so the slow path runs at most once per symbol.
  valueCache.emplace(&symbol, &symbol);
  return symbol;
}

auto CanonicalBodyResolver::getCanonicalBody(
    ast::InstanceBodySymbol const &body) -> ast::InstanceBodySymbol const & {
  if (auto it = bodyCache.find(&body); it != bodyCache.end()) {
    return *it->second;
  }

  // Walk up looking for an anchor: an enclosing body whose canonical
  // we already know, either because slang set it via setCanonicalBody
  // (the outermost non-canonical instance) or because we cached it on
  // a previous query.
  ast::InstanceBodySymbol const *cur = &body;
  ast::InstanceBodySymbol const *anchor = nullptr;
  ast::InstanceBodySymbol const *anchorCanonical = nullptr;
  while (cur != nullptr) {
    if (auto it = bodyCache.find(cur); it != bodyCache.end()) {
      anchor = cur;
      anchorCanonical = it->second;
      break;
    }
    if (cur->parentInstance != nullptr) {
      auto const *direct = cur->parentInstance->getCanonicalBody();
      if (direct != nullptr && direct != cur) {
        anchor = cur;
        anchorCanonical = direct;
        break;
      }
    }
    auto const *parentScope = cur->parentInstance != nullptr
                                  ? cur->parentInstance->getParentScope()
                                  : nullptr;
    if (parentScope == nullptr) {
      break;
    }
    cur = parentScope->getContainingInstance();
  }

  if (anchor == nullptr || anchorCanonical == anchor) {
    // No redirect along the chain; body is canonical.
    bodyCache.emplace(&body, &body);
    return body;
  }

  // Pair anchor with its canonical, then traverse the subtree to
  // register every nested body and value pair. After this, the lookup
  // for `body` should hit the cache.
  bodyCache.emplace(anchor, anchorCanonical);
  populatePairedBodies(*anchor, *anchorCanonical);

  if (auto it = bodyCache.find(&body); it != bodyCache.end()) {
    return *it->second;
  }
  // Defensive: structural mismatch between anchor and its canonical
  // (shouldn't happen given slang's cache-key invariants, but fall
  // back to identity rather than asserting).
  bodyCache.emplace(&body, &body);
  return body;
}

void CanonicalBodyResolver::populatePairedBodies(ast::Scope const &local,
                                                 ast::Scope const &canonical) {
  auto localIt = local.members().begin();
  auto localEnd = local.members().end();
  auto canonIt = canonical.members().begin();
  auto canonEnd = canonical.members().end();
  for (; localIt != localEnd && canonIt != canonEnd; ++localIt, ++canonIt) {
    if (localIt->isValue() && canonIt->isValue()) {
      valueCache.emplace(&localIt->as<ast::ValueSymbol>(),
                         &canonIt->as<ast::ValueSymbol>());
      continue;
    }
    if (localIt->kind == ast::SymbolKind::Instance &&
        canonIt->kind == ast::SymbolKind::Instance) {
      auto const &li = localIt->as<ast::InstanceSymbol>();
      auto const &ci = canonIt->as<ast::InstanceSymbol>();
      // ci may itself be non-canonical: when the parent body contains
      // multiple named child instances of the same submodule, slang
      // collapses them onto a single canonical leaf body and points
      // each via setCanonicalBody. Pair the local body with that final
      // canonical so analysis-time drivers (registered only on the true
      // canonical) are reachable through any non-canonical hierarchical
      // path to a sibling.
      auto const *ciCanon = ci.getCanonicalBody();
      auto const &ciBody = ciCanon != nullptr ? *ciCanon : ci.body;
      if (&li.body != &ciBody) {
        bodyCache.emplace(&li.body, &ciBody);
        populatePairedBodies(li.body, ciBody);
      } else {
        bodyCache.emplace(&li.body, &li.body);
      }
      continue;
    }
    if (localIt->kind == canonIt->kind) {
      if (localIt->kind == ast::SymbolKind::GenerateBlock) {
        populatePairedBodies(localIt->as<ast::GenerateBlockSymbol>(),
                             canonIt->as<ast::GenerateBlockSymbol>());
      } else if (localIt->kind == ast::SymbolKind::GenerateBlockArray) {
        populatePairedBodies(localIt->as<ast::GenerateBlockArraySymbol>(),
                             canonIt->as<ast::GenerateBlockArraySymbol>());
      } else if (localIt->kind == ast::SymbolKind::InstanceArray) {
        // Array of instances (e.g. `sub u[4](...)`); recurse so each
        // element's instance body is paired with its canonical.
        populatePairedBodies(localIt->as<ast::InstanceArraySymbol>(),
                             canonIt->as<ast::InstanceArraySymbol>());
      }
    }
  }
}

} // namespace slang::netlist
