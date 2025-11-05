#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <vector>
#include <functional>

#include "src/hash_set_base.h"

// ============================================================================
// Sequential (single-threaded) hash set implementation.
// ----------------------------------------------------------------------------
//  - Not thread-safe.
//  - Uses std::vector<std::vector<T>> as the table.
//  - Simple chaining for collision resolution.
//  - Automatically resizes when load factor exceeds threshold.
// ============================================================================

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity)
    : table_(initial_capacity), size_(0) {
      assert(initial_capacity > 0 && "Initial capacity must be > 0");
  }

  // --------------------------------------------------------------------------
  // Insert an element if not already present.
  // Returns true if insertion occurred, false if element already exists.
  // --------------------------------------------------------------------------
  bool Add(T elem) final {
    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    // check if already exists
    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;
    }

    // insert new element
    bucket.push_back(elem);
    ++size_;

    // resize if load factor exceeded
    if (size_ > kLoadFactorThreshold * table_.size()) {
      Resize();
    }
    return true;
  }

  // --------------------------------------------------------------------------
  // Remove an element if present.
  // Returns true if removal occurred, false otherwise.
  // --------------------------------------------------------------------------
  bool Remove(T elem) final {
    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
      if (*it == elem) {
        bucket.erase(it);
        --size_;
        return true;
      }
    }
    return false;
  }

  // --------------------------------------------------------------------------
  // Check if an element is in the hash set.
  // --------------------------------------------------------------------------
  [[nodiscard]] bool Contains(T elem) final {
    const size_t index = BucketIndex(elem);
    const auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  // --------------------------------------------------------------------------
  // Return the number of stored elements.
  // --------------------------------------------------------------------------
  [[nodiscard]] size_t Size() const final { return size_; }

 private:
  // --------------------------------------------------------------------------
  // Helper: compute the bucket index for an element.
  // --------------------------------------------------------------------------
  [[nodiscard]] size_t BucketIndex(const T& elem) const noexcept {
    return std::hash<T>{}(elem) % table_.size();
  }

  // --------------------------------------------------------------------------
  // Resize the table to double capacity and rehash all elements.
  // --------------------------------------------------------------------------
  void Resize() {
    const size_t new_capacity = table_.size() * 2;
    std::vector<std::vector<T>> new_table(new_capacity);

    for (const auto& bucket : table_) {
      for (const auto& elem : bucket) {
        size_t new_index = std::hash<T>{}(elem) % new_capacity;
        new_table[new_index].push_back(elem);
      }
    }

    table_.swap(new_table);
  }

  std::vector<std::vector<T>> table_;
  size_t size_;

  static constexpr size_t kLoadFactorThreshold = 4;
};

#endif  // HASH_SET_SEQUENTIAL_H
