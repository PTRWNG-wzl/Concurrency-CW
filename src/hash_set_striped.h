#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

// ============================================================================
// Striped Hash Set
// ----------------------------------------------------------------------------
//  - Thread-safe via per-bucket locking (finer granularity).
//  - Uses a fixed number of locks, one per bucket, created at initialization.
//  - Automatically resizes when load factor exceeds threshold.
//  - Resize operation locks all buckets.
// ============================================================================
template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : table_(initial_capacity),
        locks_(initial_capacity),  // one lock per bucket
        size_(0) {
    assert(initial_capacity > 0 && "Initial capacity must be > 0");
  }

  // --------------------------------------------------------------------------
  // Inserts an element if it does not already exist.
  // Returns true if the insertion occurred, false if the element was already
  // present.
  // --------------------------------------------------------------------------
  bool Add(T elem) final {
    const size_t h = std::hash<T>{}(elem);
    const size_t lock_index = h % locks_.size();  // lock by hash only
    std::unique_lock<std::mutex> lock(locks_[lock_index]);

    const size_t index = h % table_.size();  // bucket uses current table size
    auto& bucket = table_[index];

    // Check for duplicates
    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;
    }

    // Insert new element
    bucket.push_back(elem);
    ++size_;  // atomic

    // Check load factor and resize if needed
    if (LoadFactor() > kLoadFactorThreshold) {
      lock.unlock();  // release this bucket's lock before resizing
      Resize();  // locks all buckets internally (serialized by resize_mutex_)
    }
    return true;
  }

  // --------------------------------------------------------------------------
  // Removes an element if present.
  // Returns true if removal occurred, false otherwise.
  // --------------------------------------------------------------------------
  bool Remove(T elem) final {
    const size_t h = std::hash<T>{}(elem);
    const size_t lock_index = h % locks_.size();  // lock by hash only
    std::lock_guard<std::mutex> guard(locks_[lock_index]);

    const size_t index = h % table_.size();
    auto& bucket = table_[index];

    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
      if (*it == elem) {
        bucket.erase(it);
        --size_;  // atomic
        return true;
      }
    }
    return false;
  }

  // --------------------------------------------------------------------------
  // Returns true if the element is present in the set.
  // --------------------------------------------------------------------------
  [[nodiscard]] bool Contains(T elem) final {
    const size_t h = std::hash<T>{}(elem);
    const size_t lock_index = h % locks_.size();  // lock by hash only
    std::lock_guard<std::mutex> guard(locks_[lock_index]);

    const size_t index = h % table_.size();
    const auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  // --------------------------------------------------------------------------
  // Returns the total number of stored elements.
  // --------------------------------------------------------------------------
  [[nodiscard]] size_t Size() const final {
    // Return current element count atomically without locking
    return size_.load(std::memory_order_relaxed);
  }

  // --------------------------------------------------------------------------
  // Computes the current load factor.
  // --------------------------------------------------------------------------
  [[nodiscard]] double LoadFactor() const noexcept {
    return static_cast<double>(size_.load()) /
           static_cast<double>(table_.size());
  }

 private:
  // --------------------------------------------------------------------------
  // Resizes the hash table.
  // Acquires all bucket locks to ensure thread safety during rehashing.
  // --------------------------------------------------------------------------
  void Resize() {
    // Serialize resizes to avoid concurrent rehash by multiple threads
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);

    // Acquire all locks in a fixed order to prevent deadlock
    std::vector<std::unique_lock<std::mutex>> all_locks;
    all_locks.reserve(locks_.size());
    for (auto& m : locks_) {
      all_locks.emplace_back(m);
    }

    // Check if another thread has already resized
    if (LoadFactor() <= kLoadFactorThreshold) {
      return;
    }

    const size_t new_capacity = table_.size() * 2;
    std::vector<std::vector<T>> new_table(new_capacity);

    // Rehash all elements into the new table
    for (const auto& bucket : table_) {
      for (const auto& elem : bucket) {
        size_t new_index = std::hash<T>{}(elem) % new_capacity;
        new_table[new_index].push_back(elem);
      }
    }

    table_.swap(new_table);

    // The lock set remains the same — locks_ are not resized.
  }

 private:
  std::vector<std::vector<T>> table_;      // buckets
  mutable std::vector<std::mutex> locks_;  // one lock per bucket
  mutable std::mutex resize_mutex_;        // serialize Resize()
  std::atomic<size_t> size_;               // atomic element count
  static constexpr double kLoadFactorThreshold = 4.0;
};

#endif  // HASH_SET_STRIPED_H
