#pragma once

#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"

namespace slang::netlist {

/// Utility class for working with IntervalMaps.
struct IntervalMapUtils {

  template <typename TKey, typename TValue, uint32_t N>
  static void
  subtractSingle(IntervalMap<TKey, TValue, N> &result,
                 std::pair<TKey, TKey> const &interval, TValue const &value,
                 IntervalMap<TKey, TValue, N> const &second,
                 IntervalMap<TKey, TValue, N>::allocator_type &alloc) {
    TKey start = interval.first;
    TKey end = interval.second;
    TKey current = start;

    for (auto it = second.begin(); it != second.end(); ++it) {
      auto rbounds = it.bounds();

      if (rbounds.second < current) {
        // Right interval is before the current interval.
        continue;
      }

      if (rbounds.first > end) {
        // Right interval is after the current interval.
        continue;
      }

      if (rbounds.first >= current) {
        // Right interval overlaps with the current interval.
        result.unionWith(current, std::min(end, rbounds.first - 1), value,
                         alloc);
      }

      // Move current to the end of the right interval.
      current = std::max(current, rbounds.second + 1);

      if (current >= end) {
        // If current has reached or exceeded the end of the interval,
        // break.
        break;
      }
    }

    if (current <= end) {
      result.unionWith(current, end, value, alloc);
    }
  }

  /// Construct the difference between two IntervalMaps.
  template <typename TKey, typename TValue, uint32_t N>
  static IntervalMap<TKey, TValue, N>
  difference(IntervalMap<TKey, TValue, N> const &first,
             IntervalMap<TKey, TValue, N> const &second,
             IntervalMap<TKey, TValue, N>::allocator_type &alloc) {

    if (second.empty()) {
      // If the second map is empty, return the first map.
      return first.clone(alloc);
    }

    IntervalMap<TKey, TValue, N> result;

    for (auto it = first.begin(); it != first.end(); ++it) {
      subtractSingle(result, it.bounds(), *it, second, alloc);
    }

    return result;
  }
};

} // namespace slang::netlist
