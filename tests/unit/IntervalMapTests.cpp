#include "Test.hpp"

#include "IntervalMapUtils.hpp"

TEST_CASE("IntervalMap: difference", "[IntervalMap]") {
  IntervalMap<int64_t, int64_t> left, right;
  BumpAllocator ba;
  IntervalMap<int64_t, int64_t>::allocator_type alloc(ba);

  // [[0, 2], [5, 10], [13, 23], [24, 25]]
  left.unionWith({0, 2}, 1, alloc);
  left.unionWith({5, 10}, 2, alloc);
  left.unionWith({13, 23}, 3, alloc);
  left.unionWith({24, 25}, 4, alloc);

  // [[1, 5], [8, 12], [15, 18], [20, 24]]
  right.unionWith({1, 5}, 1, alloc);
  right.unionWith({8, 12}, 2, alloc);
  right.unionWith({15, 18}, 3, alloc);
  right.unionWith({20, 24}, 4, alloc);

  auto difference = IntervalMapUtils::difference(left, right, alloc);

  std::vector<std::pair<int64_t, int64_t>> result;
  for (auto it = difference.begin(); it != difference.end(); it++) {
    result.push_back(it.bounds());
  }

  std::vector<std::pair<int64_t, int64_t>> expected = {
      {0, 0}, {6, 7}, {13, 14}, {19, 19}, {25, 25}};

  CHECK(std::ranges::equal(result, expected));
}

TEST_CASE("IntervalMap: difference with empty map", "[IntervalMap]") {
  IntervalMap<int64_t, int64_t> left, right;
  BumpAllocator ba;
  IntervalMap<int64_t, int64_t>::allocator_type alloc(ba);

  // [[0, 2], [5, 10]]
  left.unionWith({0, 2}, 1, alloc);
  left.unionWith({5, 10}, 2, alloc);

  // Empty right map

  auto difference = IntervalMapUtils::difference(left, right, alloc);

  std::vector<std::pair<int64_t, int64_t>> result;
  for (auto it = difference.begin(); it != difference.end(); it++) {
    result.push_back(it.bounds());
  }

  std::vector<std::pair<int64_t, int64_t>> expected = {{0, 2}, {5, 10}};

  CHECK(std::ranges::equal(result, expected));
}
