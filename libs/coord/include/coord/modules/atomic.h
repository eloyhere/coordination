#pragma once

#include <atomic>

namespace coord {

/// Thin, language-facing atomic wrapper. `Atomic<T>` is the only type allowed
/// to cross thread boundaries without a lock.
template <class T>
class Atomic {
public:
  Atomic() = default;
  explicit Atomic(T value) : value_(value) {}

  Atomic(const Atomic&) = delete;
  Atomic& operator=(const Atomic&) = delete;

  [[nodiscard]] T load(std::memory_order order = std::memory_order_seq_cst) const {
    return value_.load(order);
  }

  void store(T value,
             std::memory_order order = std::memory_order_seq_cst) {
    value_.store(value, order);
  }

  [[nodiscard]] T exchange(
      T value, std::memory_order order = std::memory_order_seq_cst) {
    return value_.exchange(value, order);
  }

  [[nodiscard]] T fetchAdd(
      T delta, std::memory_order order = std::memory_order_seq_cst) {
    return value_.fetch_add(delta, order);
  }

  [[nodiscard]] T fetchSub(
      T delta, std::memory_order order = std::memory_order_seq_cst) {
    return value_.fetch_sub(delta, order);
  }

private:
  std::atomic<T> value_{};
};

using AtomicBool = Atomic<bool>;
using AtomicInt = Atomic<int>;
using AtomicInt64 = Atomic<long long>;
using AtomicSize = Atomic<std::size_t>;

}  // namespace coord
