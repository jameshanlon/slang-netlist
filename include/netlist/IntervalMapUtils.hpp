#pragma once

#include "slang/util/BumpAllocator.h"
#include "slang/util/IntervalMap.h"

namespace slang::netlist {

struct IntervalMapUtils {

  /// Construct the difference between two IntervalMaps.
  template <typename TKey, typename TValue, uint32_t N>
  static IntervalMap<TKey, TValue, N>
  difference(IntervalMap<TKey, TValue, N> const &first,
             IntervalMap<TKey, TValue, N> const &second,
             IntervalMap<TKey, TValue, N>::allocator_type &alloc) {

    IntervalMap<TKey, TValue, N> result;

    auto lit = first.begin();
    auto rit = second.begin();
    auto lend = first.end();
    auto rend = second.end();

    while (lit != lend && rit != rend) {
      auto lkey = lit.bounds();
      auto rkey = rit.bounds();
      if (lkey.second < rkey.first) {
        result.unionWith(lkey.first, lkey.second, *lit, alloc);
        ++lit;
      } else if (rkey.second < lkey.first) {
        ++rit;
      } else if (lkey.second < rkey.second) {
        result.unionWith(lkey.first, rkey.first, *lit, alloc);
        ++lit;
      } else {
        result.unionWith(rkey.second, lkey.second, *lit, alloc);
        ++rit;
      }
    }

    return result;
  }
};

} // namespace slang::netlist
