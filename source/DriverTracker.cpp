#include "netlist/DriverTracker.hpp"
#include "netlist/Debug.hpp"

using namespace slang::netlist;

void DriverTracker::addDriver(SymbolDrivers &drivers, ast::Symbol const &symbol,
                              ast::Expression const *lsp, DriverBitRange bounds,
                              NetlistNode *node) {

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

  DEBUG_PRINT("Add driver [{}:{}] for symbol={} index={}: \n", bounds.first,
              bounds.second, symbol.name, index);

  auto &driverMap = drivers[index];

  for (auto it = driverMap.find(bounds); it != driverMap.end();) {
    DEBUG_PRINT("Examining existing definition: [{}:{}]\n", it.bounds().first,
                it.bounds().second);

    //    auto itBounds = it.bounds();
    //    auto drivers = *it;
    //
    //    // An existing entry completely contains the new bounds, so split the
    //    // existing entry to create an interval for the new driver.
    //    //  Existing entry:    [---------------]
    //    //  New bounds:           [-------]
    //    if (ConstantRange(itBounds).contains(ConstantRange(bounds))) {
    //      driverMap.erase(it, mapAllocator);
    //
    //      // Left part.
    //      if (itBounds.first < bounds.first) {
    //        driverMap.insert({itBounds.first, bounds.first - 1}, drivers,
    //                         mapAllocator);
    //        DEBUG_PRINT("Split left [{}:{}]\n", itBounds.first, bounds.first -
    //        1);
    //      }
    //
    //      // Right part.
    //      if (itBounds.second > bounds.second) {
    //        driverMap.insert({bounds.second + 1, itBounds.second}, drivers,
    //                         mapAllocator);
    //        DEBUG_PRINT("Split right [{}:{}]\n", bounds.second + 1,
    //                    itBounds.second);
    //      }
    //
    //      // Middle part (with new driver).
    //      driverMap.insert(bounds, {DriverInfo(node, &lsp)}, mapAllocator);
    //      DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
    //                  bounds.second);
    //
    //      // No more intervals to compare against.
    //      return;
    //    }
    //
    //    // The new bounds completely contains an existing entry, so delete
    //    that
    //    // entry.
    //    //   Existing entry:    [-------]
    //    //   New bounds:     [---------------]
    //    if (ConstantRange(bounds).contains(ConstantRange(itBounds))) {
    //      driverMap.erase(it, mapAllocator);
    //      it = driverMap.find(bounds);
    //      DEBUG_PRINT("Erased existing definition\n");
    //      continue;
    //    }
    //
    //    // Existing entry left-overlaps new bounds.
    //    //   Existing entry:  [-------]
    //    //   New bounds:            [-------]
    //    if (itBounds.first <= bounds.first && itBounds.second >= bounds.first)
    //    {
    //      driverMap.erase(it, mapAllocator);
    //
    //      // Left part.
    //      driverMap.insert({itBounds.first, bounds.first - 1}, drivers,
    //                       mapAllocator);
    //      DEBUG_PRINT("Split left [{}:{}]\n", itBounds.first, bounds.first -
    //      1);
    //
    //      // Overlapping part (with new driver).
    //      driverMap.insert({bounds.first, itBounds.second},
    //                       {DriverInfo(node, &lsp)}, mapAllocator);
    //      DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
    //                  itBounds.second);
    //
    //      // Adjust the bounds to continue searching for overlaps.
    //      bounds.first = itBounds.second + 1;
    //      it = driverMap.find(bounds);
    //      continue;
    //    }
    //
    //    // Existing entry right-overlaps new bounds.
    //    //   Existing entry:          [-------]
    //    //   New bounds:        [-------]
    //    if (itBounds.first <= bounds.second && itBounds.second >=
    //    bounds.second) {
    //      driverMap.erase(it, mapAllocator);
    //
    //      // Right part of existing entry.
    //      driverMap.insert({bounds.second + 1, itBounds.second}, drivers,
    //                       mapAllocator);
    //      DEBUG_PRINT("Split right [{}:{}]\n", bounds.second + 1,
    //      itBounds.second);
    //
    //      // Left part (with new driver).
    //      driverMap.insert({bounds.first, bounds.second}, {DriverInfo(node,
    //      &lsp)},
    //                       mapAllocator);
    //      DEBUG_PRINT("Inserting new definition: [{}:{}]\n", bounds.first,
    //                  bounds.second);
    //
    //      // No more overlaps possible.
    //      return;
    //    }

    // Skip interval.
    ++it;
  }
}

void DriverTracker::mergeDriver(SymbolDrivers &drivers,
                                ast::Symbol const &symbol,
                                ast::Expression const *lsp,
                                DriverBitRange bounds, NetlistNode *node) {
  // TODO
}

auto DriverTracker::getDrivers(SymbolDrivers &drivers,
                               ast::Symbol const &symbol, DriverBitRange bounds)
    -> DriverList {
  DriverList result;
  if (symbolToSlot.contains(&symbol)) {
    SLANG_ASSERT(drivers.size() > symbolToSlot[&symbol]);
    auto &map = drivers[symbolToSlot[&symbol]];
    for (auto it = map.find(bounds); it != map.end(); it++) {
      if (ConstantRange(bounds).contains(ConstantRange(it.bounds()))) {
        // Add the drivers from this interval to the result.
        auto drivers = map.getDriverList(*it);
        result.insert(result.end(), drivers.begin(), drivers.end());
      }
    }
  }
  return result;
}

auto DriverTracker::dumpDrivers(ast::Symbol const &symbol, DriverMap &driverMap)
    -> std::string {
  FormatBuffer out;
  out.format("Driver map for symbol {}:\n", symbol.name);
  for (auto it = driverMap.begin(); it != driverMap.end(); it++) {
    out.format("[{}:{}] {} drivers\n", it.bounds().first, it.bounds().second,
               driverMap.getDriverList(*it).size());
  }
  return out.str();
}
