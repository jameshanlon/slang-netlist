#pragma once

namespace slang::netlist {

/// Information about a driver of a particular range of a symbol.
struct DriverInfo {
  NetlistNode *node;
  const ast::Expression *lsp;
};

/// A list of drivers for a particular range of a symbol.
struct DriverList {
  std::vector<DriverInfo> drivers;
  DriverList() = default;
  DriverList(std::initializer_list<DriverInfo> init) : drivers(init) {}
  auto begin() const { return drivers.begin(); }
  auto end() const { return drivers.end(); }
  auto size() const { return drivers.size(); }
  auto empty() const { return drivers.empty(); }
  void push_back(const DriverInfo &info) { drivers.push_back(info); }
  void append(const DriverInfo *begin, const DriverInfo *end) {
    drivers.insert(drivers.end(), begin, end);
  }
  void clear() { drivers.clear(); }
};

/// An identifier held by the interval map corresponding to the
/// separately-allocated driver list.
using DriverListHandle = uint32_t;

/// A range over which a symbol is driven.
using DriverBitRange = std::pair<uint32_t, uint32_t>;

class DriverMap {

  /// Map driven ranges of a particular symbol to driver list indexes.
  IntervalMap<uint32_t, DriverListHandle, 8>;
};

} // namespace slang::netlist
