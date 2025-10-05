#include "netlist/SymbolTracker.hpp"
#include "netlist/Debug.hpp"

using namespace slang::netlist;

void SymbolTracker::addDriver(SymbolDrivers &drivers, ast::Symbol const &symbol,
                              ast::Expression const *lsp, DriverBitRange bounds,
                              NetlistNode *node, bool merge) {

  // Update visited symbols to slots.
  auto [it, inserted] =
      symbolToSlot.try_emplace(&symbol, (uint32_t)symbolToSlot.size());
  auto index = it->second;

  // Resize drivers vector if necessary.
  if (index >= drivers.size()) {
    drivers.resize(index + 1);
  }

  // Resize slotToSymbol vector if necessary.
  if (index >= slotToSymbol.size()) {
    slotToSymbol.resize(index + 1);
    slotToSymbol[index] = &symbol;
  }

  DEBUG_PRINT("Add driver [{}:{}] for symbol={}, index={}: \n", bounds.first,
              bounds.second, symbol.name, index);

  auto &driverMap = drivers[index];

  for (auto it = driverMap.find(bounds); it != driverMap.end();) {
    DEBUG_PRINT("Examining existing definition: [{}:{}]\n", it.bounds().first,
                it.bounds().second);

    auto itBounds = it.bounds();
    auto existingHandle = *it;

    // An existing entry completely contains the new bounds, so split the
    // existing entry to create an interval for the new driver.
    //  Existing entry:    [---------------]
    //  New bounds:           [-------]
    if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
      driverMap.erase(it, mapAllocator);

      // Left part.
      if (itBounds.first < bounds.first) {
        driverMap.insert({itBounds.first, bounds.first - 1}, existingHandle,
                         mapAllocator);
        DEBUG_PRINT("Split left [{}:{}]\n", itBounds.first, bounds.first - 1);
      }

      // Right part.
      if (itBounds.second > bounds.second) {
        driverMap.insert({bounds.second + 1, itBounds.second}, existingHandle,
                         mapAllocator);
        DEBUG_PRINT("Split right [{}:{}]\n", bounds.second + 1,
                    itBounds.second);
      }

      // Middle part (with new driver).
      auto newHandle = driverMap.newDriverList();
      auto &newDrivers = driverMap.getDriverList(newHandle);
      newDrivers.emplace(node, lsp);
      if (merge) {
        // Merge in existing drivers.
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        newDrivers.insert(existingDrivers.begin(), existingDrivers.end());
      }
      driverMap.insert(bounds, newHandle, mapAllocator);
      DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
                  bounds.second);

      // No more intervals to compare against.
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // The new bounds completely contains an existing entry.
    // Non-merge: delete that entry.
    // Merge: add new driver to that entry.
    //   Existing entry:    [-------]
    //   New bounds:     [---------------]
    if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {

      if (!merge) {
        driverMap.erase(it, mapAllocator);
        driverMap.erase(existingHandle);
        it = driverMap.find(bounds);
        DEBUG_PRINT("Erased existing definition\n");
        continue;
      }

      // Merge: add new driver to existing entry and add the new driver interval
      // up to the existing entry.
      auto &existingDrivers = driverMap.getDriverList(*it);
      existingDrivers.emplace(node, lsp);
      DEBUG_PRINT("Merged with existing definition\n");

      // Left part.
      if (itBounds.first > bounds.first) {
        auto newHandle = driverMap.newDriverList();
        auto &newDrivers = driverMap.getDriverList(newHandle);
        newDrivers.emplace(node, lsp);
        driverMap.insert({bounds.first, itBounds.first - 1}, newHandle,
                         mapAllocator);
        DEBUG_PRINT("Split left [{}:{}]\n", bounds.first, itBounds.first - 1);
      }

      // Adjust the bounds to continue searching for overlaps.
      bounds.first = itBounds.second + 1;
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry left-overlaps new bounds.
    //   Existing entry:  [-------]
    //   New bounds:            [-------]
    if (itBounds.first <= bounds.first && itBounds.second >= bounds.first) {
      driverMap.erase(it, mapAllocator);

      // Left part.
      SLANG_ASSERT(itBounds.first < bounds.first);
      driverMap.insert({itBounds.first, bounds.first - 1}, existingHandle,
                       mapAllocator);
      DEBUG_PRINT("Split left [{}:{}]\n", itBounds.first, bounds.first - 1);

      // Overlapping part (with new driver).
      auto newHandle = driverMap.newDriverList();
      driverMap.getDriverList(newHandle).emplace(node, lsp);
      if (merge) {
        // Merge in existing drivers.
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        auto &newDrivers = driverMap.getDriverList(newHandle);
        driverMap.getDriverList(newHandle).insert(existingDrivers.begin(),
                                                  existingDrivers.end());
      }
      driverMap.insert({bounds.first, itBounds.second}, newHandle,
                       mapAllocator);
      DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
                  itBounds.second);

      // Adjust the bounds to continue searching for overlaps.
      bounds.first = itBounds.second + 1;
      it = driverMap.find(bounds);
      continue;
    }

    // Existing entry right-overlaps new bounds.
    //   Existing entry:          [-------]
    //   New bounds:        [-------]
    if (itBounds.first <= bounds.second && itBounds.second >= bounds.second) {
      driverMap.erase(it, mapAllocator);

      auto leftHandle = driverMap.newDriverList();
      driverMap.getDriverList(leftHandle).emplace(node, lsp);

      if (!merge) {
        // Left part (with new driver).
        driverMap.insert({bounds.first, bounds.second}, leftHandle,
                         mapAllocator);
        DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
                    bounds.second);
      } else {

        // Left part (new driver).
        driverMap.insert({bounds.first, itBounds.first}, leftHandle,
                         mapAllocator);
        DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
                    bounds.second);

        // Middle part (existing + new driver).
        auto middleHandle = driverMap.newDriverList();
        auto &middleDrivers = driverMap.getDriverList(middleHandle);
        middleDrivers.emplace(node, lsp);
        auto &existingDrivers = driverMap.getDriverList(existingHandle);
        middleDrivers.insert(existingDrivers.begin(), existingDrivers.end());
        driverMap.insert({bounds.first, itBounds.first}, middleHandle,
                         mapAllocator);
        DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
                    bounds.second);
      }

      // Right part (existing driver).
      SLANG_ASSERT(itBounds.second > bounds.second);
      driverMap.insert({bounds.second + 1, itBounds.second}, existingHandle,
                       mapAllocator);
      DEBUG_PRINT("Split right [{}:{}]\n", bounds.second + 1, itBounds.second);

      // No more overlaps possible.
      DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
      return;
    }

    // Skip interval.
    ++it;
  }

  // Insert the new driver interval (or what remains of it).
  auto newHandle = driverMap.newDriverList();
  driverMap.getDriverList(newHandle).emplace(node, lsp);
  driverMap.insert({bounds.first, bounds.second}, newHandle, mapAllocator);
  DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
              bounds.second);

  DEBUG_PRINT("{}\n", dumpDrivers(symbol, driverMap));
}

void SymbolTracker::mergeDriver(SymbolDrivers &drivers,
                                ast::Symbol const &symbol,
                                ast::Expression const *lsp,
                                DriverBitRange bounds, NetlistNode *node) {
  addDriver(drivers, symbol, lsp, bounds, node, true);
}

void SymbolTracker::mergeDrivers(SymbolDrivers &drivers,
                                 ast::Symbol const &symbol,
                                 DriverBitRange bounds,
                                 DriverList const &driverList) {
  // TODO: optimize by merging the list instead of one at a time.
  for (auto &driver : driverList) {
    mergeDriver(drivers, symbol, driver.lsp, bounds, driver.node);
  }
}

auto SymbolTracker::getDrivers(SymbolDrivers &drivers,
                               ast::Symbol const &symbol, DriverBitRange bounds)
    -> DriverList {
  DriverList result;
  if (symbolToSlot.contains(&symbol)) {
    SLANG_ASSERT(drivers.size() > symbolToSlot[&symbol]);
    auto &map = drivers[symbolToSlot[&symbol]];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      // If the driver interval contains the requested bounds, eg:
      //   Driver: |-------|
      //   Requested:   |---|
      if (ConstantRange(it.bounds()).contains(ConstantRange(bounds))) {
        // Add the drivers from this interval to the result.
        auto drivers = map.getDriverList(*it);
        result.insert(drivers.begin(), drivers.end());
      }
      // If the driver contributes to the requested range, eg:
      //   Driver:      |---|
      //   Requested: |-------|
      if (ConstantRange(bounds).contains(ConstantRange(it.bounds()))) {
        auto drivers = map.getDriverList(*it);
        result.insert(drivers.begin(), drivers.end());
      }
      // TODO: handle partial overlaps?
    }
  }
  return result;
}

auto SymbolTracker::dumpDrivers(ast::Symbol const &symbol, DriverMap &driverMap)
    -> std::string {
  FormatBuffer out;
  out.format("Driver map for symbol {}:\n", symbol.name);
  for (auto it = driverMap.begin(); it != driverMap.end(); it++) {
    out.format("[{}:{}] {} drivers\n", it.bounds().first, it.bounds().second,
               driverMap.getDriverList(*it).size());
  }
  return out.str();
}
