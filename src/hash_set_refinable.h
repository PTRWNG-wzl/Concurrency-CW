#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity) : size_(0) {
    assert(initial_capacity > 0 && "Initial capacity must be > 0");
    auto initial_state = std::make_shared<TableState>(initial_capacity);
    std::atomic_store(&state_, initial_state);
  }

  bool Add(T elem) final {
    while (true) {
      auto state = std::atomic_load(&state_);
      const size_t index = BucketIndex(elem, *state);
      std::unique_lock<std::mutex> bucket_lock(state->locks[index]);
      if (state != std::atomic_load(&state_)) {
        continue;
      }

      auto& bucket = state->buckets[index];
      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;
      }

      bucket.push_back(elem);
      const size_t new_size = size_.fetch_add(1, std::memory_order_relaxed) + 1;

      const bool should_resize =
          new_size > kLoadFactorThreshold * state->buckets.size();

      bucket_lock.unlock();
      if (should_resize) {
        MaybeResize(state);
      }

      return true;
    }
  }

  bool Remove(T elem) final {
    while (true) {
      auto state = std::atomic_load(&state_);
      const size_t index = BucketIndex(elem, *state);
      std::unique_lock<std::mutex> bucket_lock(state->locks[index]);
      if (state != std::atomic_load(&state_)) {
        continue;
      }

      auto& bucket = state->buckets[index];
      for (auto it = bucket.begin(); it != bucket.end(); ++it) {
        if (*it == elem) {
          bucket.erase(it);
          size_.fetch_sub(1, std::memory_order_relaxed);
          return true;
        }
      }

      return false;
    }
  }

  [[nodiscard]] bool Contains(T elem) final {
    while (true) {
      auto state = std::atomic_load(&state_);
      const size_t index = BucketIndex(elem, *state);
      std::unique_lock<std::mutex> bucket_lock(state->locks[index]);
      if (state != std::atomic_load(&state_)) {
        continue;
      }

      const auto& bucket = state->buckets[index];
      return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
    }
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load(std::memory_order_acquire);
  }

 private:
  struct TableState {
    explicit TableState(size_t capacity) : buckets(capacity), locks(capacity) {}

    std::vector<std::vector<T>> buckets;
    std::vector<std::mutex> locks;
  };

  static size_t BucketIndex(const T& elem, const TableState& state) {
    return std::hash<T>{}(elem) % state.buckets.size();
  }

  void MaybeResize(const std::shared_ptr<TableState>& expected_state) {
    if (size_.load(std::memory_order_relaxed) <=
        kLoadFactorThreshold * expected_state->buckets.size()) {
      return;
    }

    std::unique_lock<std::mutex> resize_guard(resize_mutex_, std::try_to_lock);
    if (!resize_guard.owns_lock()) {
      return;
    }

    auto current_state = std::atomic_load(&state_);
    if (current_state != expected_state) {
      return;
    }

    if (size_.load(std::memory_order_relaxed) <=
        kLoadFactorThreshold * current_state->buckets.size()) {
      return;
    }

    std::vector<std::unique_lock<std::mutex>> locked_buckets;
    locked_buckets.reserve(current_state->locks.size());
    for (auto& lock : current_state->locks) {
      locked_buckets.emplace_back(lock);
    }

    const size_t new_capacity = current_state->buckets.size() * 4;
    auto new_state = std::make_shared<TableState>(new_capacity);
    for (const auto& bucket : current_state->buckets) {
      for (const auto& elem : bucket) {
        const size_t new_index =
            std::hash<T>{}(elem) % new_state->buckets.size();
        new_state->buckets[new_index].push_back(elem);
      }
    }

    std::atomic_store(&state_, new_state);
  }

  static constexpr size_t kLoadFactorThreshold = 4;

  mutable std::mutex resize_mutex_;
  std::shared_ptr<TableState> state_;
  std::atomic<size_t> size_;
};

#endif  // HASH_SET_REFINABLE_H
