#pragma once

#include "slang/util/Util.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace slang::netlist {

/// A class to manage objects of type T that are external to an interval map.
///
/// This is due to interval maps requiring their values to be trivially
/// copyable, which is not the case for vectors or other STL containers. This
/// class provides a simple handle-based interface to allocate, access, and free
/// objects of type T, where the handle is simply an index into a vector.
template <typename T> class ExternalManager {
public:
  using Handle = std::uint32_t;

  ExternalManager() = default;
  ExternalManager &operator=(ExternalManager &&) = default;
  ~ExternalManager() = default;

  /// Deep-copy constructor
  ExternalManager(const ExternalManager &other) { copyFrom(other); }

  /// Deep-copy assignment.
  ExternalManager &operator=(const ExternalManager &other) {
    if (this != &other) {
      ExternalManager tmp(other);
      swap(tmp);
    }
    return *this;
  }

  /// Create a new T, forwarding args to T's constructor. Returns a trivial
  /// handle (index). Note: std::bad_alloc may be thrown by std::make_unique;
  /// this is not asserted away.
  template <typename... Args> [[nodiscard]] Handle allocate(Args &&...args) {
    if (!freeList.empty()) {
      auto index = freeList.front();
      freeList.pop();
      slots[index] = std::make_unique<T>(std::forward<Args>(args)...);
      return index;
    }
    slots.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    return static_cast<Handle>(slots.size() - 1);
  }

  /// Const access to the T referenced by the specified handle.
  const T &get(Handle handle) const {
    SLANG_ASSERT(handle < slots.size() && "get: handle index out of range");
    const auto &ptr = slots[handle];
    assert(ptr && "get: invalid or freed handle");
    return *ptr;
  }

  /// Non-const access to the T referenced by the specified handle.
  T &get(Handle handle) {
    SLANG_ASSERT(handle < slots.size() && "handle index out of range");
    auto &ptr = slots[handle];
    SLANG_ASSERT(ptr && "get: invalid or freed handle");
    return *ptr;
  }

  /// Free the T referenced by the specified handle.
  void erase(Handle handle) {
    assert(handle < slots.size() && "handle index out of range");
    auto &ptr = slots[handle];
    assert(ptr && "free: invalid or already-freed handle");
    ptr.reset();
    freeList.push(handle);
  }

  /// Check whether a handle is valid.
  [[nodiscard]] bool valid(Handle handle) const {
    return handle < slots.size() && slots[handle] != nullptr;
  }

  /// Return a deep copy
  [[nodiscard]] ExternalManager clone() const { return ExternalManager(*this); }

  /// Swap with another manager (noexcept)
  void swap(ExternalManager &other) {
    slots.swap(other.slots);
    freeList.swap(other.freeList);
  }

private:
  /// Helper to deep-copy from other into *this.
  void copyFrom(const ExternalManager &other) {

    // Create temporaries first for strong exception safety.
    std::vector<std::unique_ptr<T>> newSlots;
    newSlots.reserve(other.slots.size());

    // For each slot, if other has object, copy it; otherwise leave nullptr.
    for (const auto &up : other.slots) {
      if (up) {
        // Copy-construct T into a new unique_ptr.
        newSlots.push_back(std::make_unique<T>(*up)); // may throw
      } else {
        newSlots.push_back(nullptr);
      }
    }

    // Copy freeList.
    auto newFreeList = other.freeList;

    // Commit.
    slots.swap(newSlots);
    freeList.swap(newFreeList);
  }

  std::vector<std::unique_ptr<T>> slots;
  std::queue<Handle> freeList;
};

} // namespace slang::netlist
