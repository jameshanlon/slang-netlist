#include "netlist/ValueTracker.hpp"
#include "netlist/Debug.hpp"

#include "slang/text/FormatBuffer.h"
#include "slang/util/IntervalMap.h"
#include "slang/util/Util.h"

#include <cstddef>

using namespace slang::netlist;

void ValueTracker::addDrivers(ValueDrivers &drivers,
                              ast::ValueSymbol const &symbol,
                              DriverBitRange bounds,
                              DriverList const &driverList, bool merge) {

  // Update visited symbols to slots.
  auto [it, inserted] =
      valueToSlot.try_emplace(&symbol, (uint32_t)valueToSlot.size());
  auto index = it->second;

  // Resize drivers vector if necessary.
  if (index >= drivers.size()) {
    drivers.resize(index + 1);
  }

  // Resize slotToValue vector if necessary.
  if (index >= slotToValue.size()) {
    slotToValue.resize(index + 1);
    slotToValue[index] = &symbol;
  }

  DEBUG_PRINT("Add driver range {} for symbol={}, index={}: \n",
              toString(bounds), symbol.name, index);

  auto &driverMap = drivers[index];

  for (auto it = driverMap.find(bounds); it != driverMap.end();) {
    DEBUG_PRINT("Examining existing definition {}\n", toString(it.bounds()));

    auto itBounds = it.bounds();
    auto existingHandle = *it;

    // Mathing intervals: add driver to existing entry.
    if (ConstantRange(itBounds) == bounds) {
      if (merge) {
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        existingDrivers.insert(driverList.begin(), driverList.end());
        DEBUG_PRINT("Added to existing definition\n");
      } else {
        // Non-merge: replace existing drivers within same interval.
        auto &drivers = driverMap.getDriverList(existingHandle);
        drivers.clear();
        drivers.insert(driverList.begin(), driverList.end());
        DEBUG_PRINT("Replaced existing definition\n");
      }
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // An existing entry completely contains the new bounds, so split the
    // existing entry to create an interval for the new driver.
    //  Existing entry:    [---------------]
    //  New bounds:           [-------]
    if (ConstantRange(itBounds).contains(bounds)) {
      driverMap.erase(it, mapAllocator);

      // Left part.
      if (itBounds.first < bounds.lower()) {
        auto newBounds = DriverBitRange{itBounds.first, bounds.lower() - 1};
        driverMap.insert(newBounds, existingHandle, mapAllocator);
        DEBUG_PRINT("Split left {}\n", toString(newBounds));
      }

      // Right part.
      if (itBounds.second > bounds.upper()) {
        auto newBounds = DriverBitRange{bounds.upper() + 1, itBounds.second};
        driverMap.insert(newBounds, existingHandle, mapAllocator);
        DEBUG_PRINT("Split right {}\n", toString(newBounds));
      }

      // Middle part (with new driver).
      DEBUG_PRINT("Inserting new definition {}\n", toString(bounds));
      if (merge) {
        // Merge in existing drivers.
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        auto newHandle = driverMap.addDriverList(existingDrivers);
        auto &newDrivers = driverMap.getDriverList(newHandle);
        newDrivers.insert(driverList.begin(), driverList.end());
        driverMap.insert(bounds, newHandle, mapAllocator);
      } else {
        // Just add new drivers.
        auto newHandle = driverMap.addDriverList(driverList);
        driverMap.insert(bounds, newHandle, mapAllocator);
      }

      // No more intervals to compare against.
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // The new bounds completely contains an existing entry.
    // Non-merge: delete that entry.
    // Merge: add new driver to that entry.
    //   Existing entry:    [-------]
    //   New bounds:     [---------------]
    if (bounds.contains(ConstantRange(itBounds))) {

      if (!merge) {
        driverMap.erase(it, mapAllocator);
        driverMap.erase(existingHandle);
        it = driverMap.find(bounds);
        DEBUG_PRINT("Erased existing definition\n");
        continue;
      }

      // Merge: add new driver to existing entry and add the new driver
      // interval / up to the existing entry.
      auto &existingDrivers = driverMap.getDriverList(*it);
      existingDrivers.insert(driverList.begin(), driverList.end());
      DEBUG_PRINT("Merged with existing definition\n");

      // Left part.
      if (itBounds.first > bounds.lower()) {
        auto newHandle = driverMap.addDriverList(driverList);
        auto &newDrivers = driverMap.getDriverList(newHandle);
        auto newBounds = DriverBitRange{bounds.lower(), itBounds.first - 1};
        driverMap.insert(newBounds, newHandle, mapAllocator);
        DEBUG_PRINT("Split left {}\n", toString(newBounds));
      }

      // Adjust the bounds to continue searching for overlaps.
      bounds.left = itBounds.second + 1;
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry left-overlaps new bounds.
    //   Existing entry:  [-------]
    //   New bounds:           [-------]
    if (itBounds.first <= bounds.lower() && itBounds.second >= bounds.lower()) {
      driverMap.erase(it, mapAllocator);

      // Left part.
      SLANG_ASSERT(itBounds.first < bounds.lower());
      auto newBounds = DriverBitRange{itBounds.first, bounds.lower() - 1};
      driverMap.insert(newBounds, existingHandle, mapAllocator);
      DEBUG_PRINT("Split left {}\n", toString(newBounds));

      if (!merge) {
        // Right part (with new driver).
        auto newHandle = driverMap.addDriverList(driverList);
        auto newBounds = DriverBitRange{bounds.lower(), itBounds.second};
        driverMap.insert(newBounds, newHandle, mapAllocator);
        DEBUG_PRINT("Inserting new definition {}\n", toString(newBounds));
      } else {
        // Overlapping part (with new driver).
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        auto newHandle = driverMap.addDriverList(existingDrivers);
        auto &newDrivers = driverMap.getDriverList(newHandle);
        newDrivers.insert(driverList.begin(), driverList.end());
        auto newBounds = DriverBitRange{bounds.lower(), itBounds.second};
        driverMap.insert(newBounds, newHandle, mapAllocator);
        DEBUG_PRINT("Inserting new definition {}\n", toString(newBounds));
      }

      // Adjust the bounds to continue searching for overlaps.
      bounds.left = itBounds.second + 1;
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry right-overlaps new bounds.
    //   Existing entry:         [-------]
    //   New bounds:        [-------]
    if (itBounds.first <= bounds.upper() && itBounds.second >= bounds.upper()) {
      driverMap.erase(it, mapAllocator);

      auto leftHandle = driverMap.addDriverList(driverList);

      if (!merge) {
        // Left part (new drivers).
        auto leftBounds = bounds;
        driverMap.insert(leftBounds, leftHandle, mapAllocator);
        DEBUG_PRINT("Inserting new definition {}\n", toString(leftBounds));

      } else {

        // Left part (new drivers).
        auto leftBounds = DriverBitRange{bounds.lower(), itBounds.first - 1};
        driverMap.insert(leftBounds, leftHandle, mapAllocator);
        DEBUG_PRINT("Inserting new definition {}\n", toString(leftBounds));

        // Middle part (existing + new drivers).
        auto middleBounds = DriverBitRange{itBounds.first, bounds.upper()};
        auto existingDrivers = driverMap.getDriverList(existingHandle);
        auto middleHandle = driverMap.addDriverList(existingDrivers);
        auto &middleDrivers = driverMap.getDriverList(middleHandle);
        middleDrivers.insert(driverList.begin(), driverList.end());
        driverMap.insert(middleBounds, middleHandle, mapAllocator);
        DEBUG_PRINT("Inserting new definition {}\n", toString(leftBounds));
      }

      // Right part (existing drivers).
      SLANG_ASSERT(itBounds.second > bounds.upper());
      auto newBounds = DriverBitRange{bounds.upper() + 1, itBounds.second};
      driverMap.insert(newBounds, existingHandle, mapAllocator);
      DEBUG_PRINT("Split right {}\n", toString(newBounds));

      // No more overlaps possible, so exit here.
      return;
    }

    // Skip interval.
    ++it;
  }

  // Insert the new driver interval (or what remains of it).
  auto newHandle = driverMap.addDriverList(driverList);
  driverMap.insert(bounds, newHandle, mapAllocator);
  DEBUG_PRINT("Inserting new definition {}\n", toString(bounds));

  // Dump the driver map for debugging.
  DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
}

auto ValueTracker::getDrivers(ValueDrivers const &drivers,
                              ast::ValueSymbol const &symbol,
                              DriverBitRange bounds) const -> DriverList {
  DriverList result;
  if (valueToSlot.contains(&symbol)) {
    SLANG_ASSERT(drivers.size() > valueToSlot.at(&symbol));
    auto const &map = drivers[valueToSlot.at(&symbol)];
    for (auto it = map.find(bounds); it != map.end(); it++) {

      // If the driver interval contains the requested bounds, eg:
      //   Driver: |-------|
      //   Requested:   |---|
      if (ConstantRange(it.bounds()).contains(bounds)) {
        // Add the drivers from this interval to the result.
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

      // TODO: handle partial overlaps?
      DEBUG_PRINT("Partial overlap driver retrieval not implemented\n");
    }
  }
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
