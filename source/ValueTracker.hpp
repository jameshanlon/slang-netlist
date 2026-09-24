#pragma once

#include "DriverMap.hpp"

#include "netlist/NetlistNode.hpp"

#include "slang/ast/Expression.h"
#include "slang/ast/symbols/ValueSymbol.h"

#include "slang/util/ConcurrentMap.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace slang::netlist {

/// Heap-stable allocator pair for a single ValueTracker driver slot.
/// The PoolAllocator holds a reference to the co-located BumpAllocator,
/// so instances must not be moved after construction (enforced by
/// unique_ptr storage).
struct SlotAllocator {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc;
  SlotAllocator() : alloc(ba) {}
  SlotAllocator(SlotAllocator const &) = delete;
  SlotAllocator &operator=(SlotAllocator const &) = delete;
};

/// Per-value symbol ValueDriverMaps.
///
/// Indexed by a tracker's slot number. Under SlotGrowth::Amortised the
/// vector runs ahead of the number of live slots, so its size is a bound on
/// the slot numbers it can hold, not a count of the symbols in it.
using ValueDrivers = std::vector<DriverMap>;

/// How a ValueTracker grows the vectors indexed by its slot numbers.
enum class SlotGrowth {
  /// Grow to fit. Keeps a vector's size equal to the number of live slots,
  /// which the per-DFA analysis states rely on, and keeps the per-branch
  /// clones in DataFlowAnalysis::copyState as small as possible.
  Exact,
  /// Grow by at least double, so that growth, which needs exclusive access
  /// to vectors every other thread is reading, is not paid once for every
  /// symbol in the design.
  Amortised
};

/// Track drivers for value symbols.
///
/// Each value symbol encountered in the AST has an interval map where each
/// interval is a range that is driven by one or more statements in the design.
/// Intervals are non-overlapping, each interval maps to a list of DriverInfo
/// objects, and adjacent intervals have different driver lists.
///
/// Note that a ValueDrivers variable is not a member of this class because it
/// is stored in the analysis state during the DataFlowAnalysis pass.
class ValueTracker {

  BumpAllocator allocator;
  DriverMap::AllocatorType mapAllocator;

  // Reader-writer lock protecting the drivers vector from concurrent
  // resize.
  mutable std::shared_mutex driversMutex;

  // Per-slot mutexes, one per drivers[i].
  std::vector<std::unique_ptr<std::mutex>> slotMutexes;

  // Per-slot allocators for IntervalMap insert/erase, one per drivers[i].
  // Built on first use, since a grown-into slot may never be written.
  std::vector<std::unique_ptr<SlotAllocator>> slotAllocators;

  // Map value symbols to indexes in vectors of ValueDriverMaps.
  concurrent_map<const ast::ValueSymbol *, uint32_t> valueToSlot;

  // The reverse mapping of slot indexes to value symbols.
  concurrent_map<uint32_t, const ast::ValueSymbol *> slotToValue;

  // Atomic counter for allocating slot indexes.
  std::atomic<uint32_t> nextSlot{0};

  SlotGrowth growth;

  /// Size to grow a slot-indexed vector to so that @p index becomes valid.
  auto grownSize(size_t current, uint32_t index) const -> size_t {
    auto required = static_cast<size_t>(index) + 1;
    return growth == SlotGrowth::Amortised ? std::max(required, current * 2)
                                           : required;
  }

public:
  /// Construct a tracker. @p growth has no default because the wrong choice
  /// is silent: see SlotGrowth.
  explicit ValueTracker(SlotGrowth growth)
      : mapAllocator(allocator), growth(growth) {}

  /// Visit all symbol-to-slot mappings.
  template <typename F> void visitAll(F &&fn) const {
    slotToValue.cvisit_all(
        [&fn](const auto &pair) { fn(pair.second, pair.first); });
  }

  /// Get a symbol by its slot index.
  auto getSymbol(uint32_t slot) const -> const ast::ValueSymbol * {
    const ast::ValueSymbol *result = nullptr;
    slotToValue.cvisit(slot, [&](const auto &pair) { result = pair.second; });
    SLANG_ASSERT(result != nullptr);
    return result;
  }

  /// Get the slot index for a symbol, if it exists.
  auto getSlot(ast::ValueSymbol const &symbol) -> std::optional<uint32_t> {
    std::optional<uint32_t> result;
    valueToSlot.cvisit(&symbol,
                       [&](const auto &pair) { result = pair.second; });
    return result;
  }

  /// Add a driver for the specified value symbol. This overwrites any existing
  /// drivers for the specified bit range.
  void addDrivers(ValueDrivers &drivers, ast::ValueSymbol const &symbol,
                  DriverBitRange bounds, DriverList const &driverList,
                  bool merge = false);

  /// Return a list of all the drivers for the given value symbol and bit range.
  /// If there are no drivers, the returned list will be empty.
  auto getDrivers(ValueDrivers const &drivers, ast::ValueSymbol const &symbol,
                  DriverBitRange bounds) const -> DriverList;

  /// Invoke @p fn once per driver interval of @p symbol that overlaps @p
  /// bounds, with the interval's own bounds and its associated driver list.
  /// Used by callers that need per-interval precision rather than the flat
  /// set returned by getDrivers.
  template <typename F>
  void forEachDriverInterval(ValueDrivers const &drivers,
                             ast::ValueSymbol const &symbol,
                             DriverBitRange bounds, F &&fn) const {
    valueToSlot.cvisit(&symbol, [&](const auto &pair) {
      auto index = pair.second;
      if (index >= drivers.size()) {
        return;
      }
      auto const &map = drivers[index];
      for (auto it = map.find(bounds); it != map.end(); ++it) {
        auto itBounds = it.bounds();
        fn(DriverBitRange{itBounds.first, itBounds.second},
           map.getDriverList(*it));
      }
    });
  }

  /// Dump the current driver map for all value symbols for debugging output.
  static auto dumpDrivers(ast::ValueSymbol const &symbol, DriverMap &driverMap)
      -> std::string;

  /// Return the IntervalMap allocator. Only safe for single-threaded
  /// per-DFA ValueTracker instances, not the shared NetlistBuilder tracker.
  auto getAllocator() -> DriverMap::AllocatorType & { return mapAllocator; }
};

} // namespace slang::netlist
