#include "netlist/DriverBitRange.hpp"
#include "netlist/DriverMap.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace slang;
using namespace slang::netlist;

TEST_CASE("DriverMap empty", "[DriverMap]") {
  DriverMap dm;
  CHECK(dm.empty());
}

TEST_CASE("DriverMap allocate and retrieve driver list", "[DriverMap]") {
  DriverMap dm;
  auto handle = dm.newDriverList();
  CHECK(dm.validHandle(handle));
  CHECK(dm.getDriverList(handle).empty());
}

TEST_CASE("DriverMap add driver list with contents", "[DriverMap]") {
  DriverMap dm;
  DriverList list;
  list.insert(DriverInfo{reinterpret_cast<NetlistNode *>(1), nullptr});
  list.insert(DriverInfo{reinterpret_cast<NetlistNode *>(2), nullptr});

  auto handle = dm.addDriverList(list);
  CHECK(dm.validHandle(handle));
  CHECK(dm.getDriverList(handle).size() == 2);
}

TEST_CASE("DriverMap insert interval and find", "[DriverMap]") {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc(ba);
  DriverMap dm;

  auto handle = dm.newDriverList();
  dm.insert(DriverBitRange(7, 0), handle, alloc);
  CHECK_FALSE(dm.empty());

  auto it = dm.find(DriverBitRange(7, 0));
  CHECK(it.valid());
}

TEST_CASE("DriverMap multiple non-overlapping intervals", "[DriverMap]") {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc(ba);
  DriverMap dm;

  auto h1 = dm.newDriverList();
  auto h2 = dm.newDriverList();
  dm.insert(DriverBitRange(3, 0), h1, alloc);
  dm.insert(DriverBitRange(7, 4), h2, alloc);

  auto it1 = dm.find(DriverBitRange(3, 0));
  CHECK(it1.valid());
  auto it2 = dm.find(DriverBitRange(7, 4));
  CHECK(it2.valid());
}

TEST_CASE("DriverMap erase handle", "[DriverMap]") {
  DriverMap dm;
  auto handle = dm.newDriverList();
  CHECK(dm.validHandle(handle));
  dm.erase(handle);
  CHECK_FALSE(dm.validHandle(handle));
}

TEST_CASE("DriverMap erase interval", "[DriverMap]") {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc(ba);
  DriverMap dm;

  auto handle = dm.newDriverList();
  dm.insert(DriverBitRange(7, 0), handle, alloc);
  CHECK_FALSE(dm.empty());

  auto it = dm.find(DriverBitRange(7, 0));
  dm.erase(it, alloc);
  CHECK(dm.empty());
}

TEST_CASE("DriverMap clone preserves content", "[DriverMap]") {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc(ba);
  DriverMap dm;

  DriverList list;
  list.insert(DriverInfo{reinterpret_cast<NetlistNode *>(1), nullptr});
  auto handle = dm.addDriverList(list);
  dm.insert(DriverBitRange(3, 0), handle, alloc);

  auto cloned = dm.clone(alloc);
  CHECK_FALSE(cloned.empty());

  auto it = cloned.find(DriverBitRange(3, 0));
  CHECK(it.valid());

  auto clonedHandle = *it;
  CHECK(cloned.getDriverList(clonedHandle).size() == 1);
}

TEST_CASE("DriverMap clone is independent", "[DriverMap]") {
  BumpAllocator ba;
  DriverMap::AllocatorType alloc(ba);
  DriverMap dm;

  auto handle = dm.newDriverList();
  dm.insert(DriverBitRange(7, 0), handle, alloc);

  auto cloned = dm.clone(alloc);

  // Erase from original; clone should be unaffected.
  auto it = dm.find(DriverBitRange(7, 0));
  dm.erase(it, alloc);
  CHECK(dm.empty());
  CHECK_FALSE(cloned.empty());
}
