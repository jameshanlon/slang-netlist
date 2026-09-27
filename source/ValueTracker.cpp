#include "ValueTracker.hpp"

#include "netlist/Debug.hpp"

#include "common/FormatBuffer.hpp"
#include "slang/util/IntervalMap.h"
#include "slang/util/Util.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

using namespace slang::netlist;

namespace {

/// Whether a driver is the stand-in for a value's declared storage.
auto isPlaceholder(DriverInfo const &driver) -> bool {
  return driver.node != nullptr && driver.node->placeholder;
}

/// Drop the declaration placeholder from every interval overlapping @p
/// bounds, giving each one a driver list of its own first.
///
/// Splitting an interval leaves both halves sharing a single list, so
/// erasing the placeholder in place would also strip it from ranges the
/// caller never named. Rewrites are collected before being applied so the
/// map is not mutated while it is being walked.
void stripPlaceholders(DriverMap &driverMap, DriverBitRange bounds,
                       DriverMap::AllocatorType &alloc) {
  std::vector<std::pair<std::pair<int32_t, int32_t>, DriverMap::Handle>>
      rewrites;
  for (auto it = driverMap.find(bounds); it != driverMap.end(); ++it) {
    auto const &list = driverMap.getDriverList(*it);
    if (std::ranges::any_of(list, isPlaceholder)) {
      auto handle = driverMap.addDriverList(list);
      std::erase_if(driverMap.getDriverList(handle), isPlaceholder);
      rewrites.emplace_back(it.bounds(), handle);
    }
  }

  for (auto const &[range, handle] : rewrites) {
    DriverBitRange rewritten{range.first, range.second};
    for (auto it = driverMap.find(rewritten); it != driverMap.end(); ++it) {
      if (it.bounds() == range) {
        driverMap.erase(it, alloc);
        break;
      }
    }
    driverMap.insert(rewritten, handle, alloc);
  }
}

} // namespace

void ValueTracker::addDrivers(ValueDrivers &drivers,
                              ast::ValueSymbol const &symbol,
                              DriverBitRange bounds,
                              DriverList const &driverList, DriverUpdate mode) {

  bool const replace = mode == DriverUpdate::Replace;

  // Fold the incoming drivers into an existing list.
  auto combine = [&](DriverList &target) {
    target.insert(driverList.begin(), driverList.end());
  };

  // Allocate or look up the slot for this symbol (lock-free).
  uint32_t index;

  // Fast path: check if symbol already has a slot.
  bool found = valueToSlot.cvisit(
      &symbol, [&index](const auto &pair) { index = pair.second; });

  if (!found) {
    // Slow path: allocate a new slot. Pre-allocate atomically so the value
    // emplaced into valueToSlot is immediately correct.
    uint32_t candidate = nextSlot.fetch_add(1, std::memory_order_relaxed);
    bool inserted = valueToSlot.try_emplace_or_cvisit(
        &symbol, candidate,
        [&index](const auto &pair) { index = pair.second; });
    if (inserted) {
      index = candidate;
      slotToValue.emplace(index, &symbol);
    }
    // If !inserted, another thread won the race — index was set by cvisit
    // and the candidate slot is wasted but harmless.
  }

  // Resize vectors if necessary (double-checked locking).
  // The shared lock is held for the remainder of the function so that a
  // concurrent resize cannot invalidate the drivers[index] reference.
  //
  // The external drivers vector and member vectors (slotMutexes,
  // slotAllocators) are resized independently: drivers may vary per
  // caller (e.g. different AnalysisState instances in mergeStates), so
  // we must never shrink the member vectors to match a smaller drivers.
  std::shared_lock readLock(driversMutex);
  if (index >= drivers.size() || index >= slotAllocators.size()) {
    readLock.unlock();
    std::unique_lock writeLock(driversMutex);
    if (index >= drivers.size()) {
      drivers.resize(grownSize(drivers.size(), index));
    }
    if (index >= slotAllocators.size()) {
      auto oldSize = slotAllocators.size();
      auto newSize = grownSize(oldSize, index);
      slotMutexes.resize(newSize);
      slotAllocators.resize(newSize);
      for (size_t i = oldSize; i < newSize; ++i) {
        slotMutexes[i] = std::make_unique<std::mutex>();
      }
    }
    writeLock.unlock();
    readLock.lock();
  }

  // Acquire the per-slot lock. The shared driversMutex remains held for
  // the rest of the function to prevent vector reallocation.
  std::lock_guard slotLock(*slotMutexes[index]);
  if (!slotAllocators[index]) {
    slotAllocators[index] = std::make_unique<SlotAllocator>();
  }
  auto &slotAlloc = slotAllocators[index]->alloc;

  // Normalize to ascending order so that IntervalMap insertions and the
  // bounds-adjustment arithmetic below (bounds.left = ...) work correctly
  // regardless of how the SV range was declared ([hi:lo] vs [lo:hi]).
  bounds = DriverBitRange{bounds.lower(), bounds.upper()};

  DEBUG_PRINT("Add driver range {} for symbol={}, index={}: \n",
              toString(bounds), symbol.name, index);

  auto &driverMap = drivers[index];

  // A superseding driver stands in for the declaration placeholder, so
  // drop that first and then fold the driver in as an ordinary merge.
  if (mode == DriverUpdate::Supersede) {
    stripPlaceholders(driverMap, bounds, slotAlloc);
  }

  // Build the list for a range that @p handle currently covers: the
  // incoming drivers on their own when replacing, otherwise those drivers
  // combined with the ones already recorded there.
  auto derive = [&](DriverMap::Handle handle) {
    auto newHandle =
        replace ? driverMap.newDriverList()
                : driverMap.addDriverList(driverMap.getDriverList(handle));
    combine(driverMap.getDriverList(newHandle));
    return newHandle;
  };

  for (auto it = driverMap.find(bounds); it != driverMap.end();) {
    DEBUG_PRINT("Examining existing definition {}\n", toString(it.bounds()));

    auto itBounds = it.bounds();
    auto existingHandle = *it;

    // Matching intervals: add driver to existing entry.
    if (ConstantRange(itBounds) == bounds) {
      auto &existingDrivers = driverMap.getDriverList(existingHandle);
      if (replace) {
        existingDrivers.clear();
        existingDrivers.insert(driverList.begin(), driverList.end());
        DEBUG_PRINT("Replaced existing definition\n");
      } else {
        combine(existingDrivers);
        DEBUG_PRINT("Added to existing definition\n");
      }
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // An existing entry completely contains the new bounds, so split the
    // existing entry to create an interval for the new driver.
    //  Existing entry:    [---------------]
    //  New bounds:           [-------]
    if (ConstantRange(itBounds).contains(bounds)) {
      driverMap.erase(it, slotAlloc);

      // Left part.
      if (itBounds.first < bounds.lower()) {
        auto newBounds = DriverBitRange{itBounds.first, bounds.lower() - 1};
        driverMap.insert(newBounds, existingHandle, slotAlloc);
        DEBUG_PRINT("Split left {}\n", toString(newBounds));
      }

      // Right part.
      if (itBounds.second > bounds.upper()) {
        auto newBounds = DriverBitRange{bounds.upper() + 1, itBounds.second};
        driverMap.insert(newBounds, existingHandle, slotAlloc);
        DEBUG_PRINT("Split right {}\n", toString(newBounds));
      }

      // Middle part (with new driver).
      DEBUG_PRINT("Inserting new definition {}\n", toString(bounds));
      driverMap.insert(bounds, derive(existingHandle), slotAlloc);

      // No more intervals to compare against.
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // The new bounds completely contains an existing entry.
    // Replace: delete that entry.
    // Otherwise: add new driver to that entry.
    //   Existing entry:    [-------]
    //   New bounds:     [---------------]
    if (bounds.contains(ConstantRange(itBounds))) {

      if (replace) {
        driverMap.erase(it, slotAlloc);
        // Split intervals may share a handle (e.g. both halves of a split
        // entry point to the same DriverList), so only free it if still live.
        if (driverMap.validHandle(existingHandle)) {
          driverMap.erase(existingHandle);
        }
        it = driverMap.find(bounds);
        DEBUG_PRINT("Erased existing definition\n");
        continue;
      }

      // Add the new driver to the existing entry and extend the interval
      // down to the start of the new bounds.
      combine(driverMap.getDriverList(*it));
      DEBUG_PRINT("Merged with existing definition\n");

      // Left part.
      if (itBounds.first > bounds.lower()) {
        auto newHandle = driverMap.addDriverList(driverList);
        auto newBounds = DriverBitRange{bounds.lower(), itBounds.first - 1};
        driverMap.insert(newBounds, newHandle, slotAlloc);
        DEBUG_PRINT("Split left {}\n", toString(newBounds));
      }

      // Adjust the bounds to continue searching for overlaps.
      bounds.left = itBounds.second + 1;
      if (bounds.left > bounds.right) {
        // Range fully consumed; nothing left to insert.
        DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
        return;
      }
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry left-overlaps new bounds.
    //   Existing entry:  [-------]
    //   New bounds:           [-------]
    if (itBounds.first <= bounds.lower() && itBounds.second >= bounds.lower()) {
      driverMap.erase(it, slotAlloc);

      // Left part.
      SLANG_ASSERT(itBounds.first < bounds.lower());
      auto newBounds = DriverBitRange{itBounds.first, bounds.lower() - 1};
      driverMap.insert(newBounds, existingHandle, slotAlloc);
      DEBUG_PRINT("Split left {}\n", toString(newBounds));

      // Overlapping part (with new driver).
      auto overlapBounds = DriverBitRange{bounds.lower(), itBounds.second};
      driverMap.insert(overlapBounds, derive(existingHandle), slotAlloc);
      DEBUG_PRINT("Inserting new definition {}\n", toString(overlapBounds));

      // Adjust the bounds to continue searching for overlaps.
      bounds.left = itBounds.second + 1;
      if (bounds.left > bounds.right) {
        // Range fully consumed; nothing left to insert.
        DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
        return;
      }
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry right-overlaps new bounds.
    //   Existing entry:         [-------]
    //   New bounds:        [-------]
    if (itBounds.first <= bounds.upper() && itBounds.second >= bounds.upper()) {
      driverMap.erase(it, slotAlloc);

      auto leftHandle = driverMap.addDriverList(driverList);

      if (replace) {
        // Left part (new drivers).
        auto leftBounds = bounds;
        driverMap.insert(leftBounds, leftHandle, slotAlloc);
        DEBUG_PRINT("Inserting new definition {}\n", toString(leftBounds));

      } else {

        // Left part (new drivers).
        auto leftBounds = DriverBitRange{bounds.lower(), itBounds.first - 1};
        driverMap.insert(leftBounds, leftHandle, slotAlloc);
        DEBUG_PRINT("Inserting new definition {}\n", toString(leftBounds));

        // Middle part (existing + new drivers).
        auto middleBounds = DriverBitRange{itBounds.first, bounds.upper()};
        driverMap.insert(middleBounds, derive(existingHandle), slotAlloc);
        DEBUG_PRINT("Inserting new definition {}\n", toString(middleBounds));
      }

      // Right part (existing drivers).
      SLANG_ASSERT(itBounds.second > bounds.upper());
      auto newBounds = DriverBitRange{bounds.upper() + 1, itBounds.second};
      driverMap.insert(newBounds, existingHandle, slotAlloc);
      DEBUG_PRINT("Split right {}\n", toString(newBounds));

      // No more overlaps possible, so exit here.
      return;
    }

    // Skip interval.
    ++it;
  }

  // Insert the new driver interval (or what remains of it).
  auto newHandle = driverMap.addDriverList(driverList);
  driverMap.insert(bounds, newHandle, slotAlloc);
  DEBUG_PRINT("Inserting new definition {}\n", toString(bounds));

  // Dump the driver map for debugging.
  DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
}

auto ValueTracker::getDrivers(ValueDrivers const &drivers,
                              ast::ValueSymbol const &symbol,
                              DriverBitRange bounds) const -> DriverList {
  DriverList result;
  valueToSlot.cvisit(&symbol, [&](const auto &pair) {
    auto index = pair.second;
    SLANG_ASSERT(drivers.size() > index);
    auto const &map = drivers[index];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      // If the driver interval contains the requested bounds, eg:
      //   Driver: |-------|
      //   Requested:   |---|
      if (ConstantRange(it.bounds()).contains(bounds)) {
        auto drivers = map.getDriverList(*it);
        result.insert(drivers.begin(), drivers.end());
        continue;
      }
      // If the driver contributes to the requested range, eg:
      //   Driver:      |---|
      //   Requested: |-------|
      if (bounds.contains(ConstantRange(it.bounds()))) {
        auto drivers = map.getDriverList(*it);
        result.insert(drivers.begin(), drivers.end());
        continue;
      }
      DEBUG_PRINT("Partial overlap driver retrieval not implemented\n");
    }
  });
  return result;
}

auto ValueTracker::dumpDrivers(ast::ValueSymbol const &symbol,
                               DriverMap &driverMap) -> std::string {
  FormatBuffer out;
  out.format("Driver map for symbol {}:\n", symbol.name);
  for (auto it = driverMap.begin(); it != driverMap.end(); it++) {
    out.format("{} {} drivers\n", toString(it.bounds()),
               driverMap.getDriverList(*it).size());
  }
  return out.str();
}
