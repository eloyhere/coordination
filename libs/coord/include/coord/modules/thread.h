#pragma once

#include <cstddef>
#include <thread>
#include <utility>

namespace coord {

/// Minimal RAII thread. The standard library provides the full surface; this
/// module exists so the language runtime has a stable `Thread` boundary.
class Thread {
public:
  Thread() = default;

  template <class Function>
  explicit Thread(Function&& function)
      : thread_(std::forward<Function>(function)) {}

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  Thread(Thread&&) noexcept = default;
  Thread& operator=(Thread&&) noexcept = default;

  ~Thread() {
    if (thread_.joinable()) {
      thread_.detach();
    }
  }

  [[nodiscard]] bool joinable() const { return thread_.joinable(); }

  void join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void detach() {
    if (thread_.joinable()) {
      thread_.detach();
    }
  }

  [[nodiscard]] static unsigned hardwareConcurrency() {
    return std::thread::hardware_concurrency();
  }

private:
  std::thread thread_;
};

}  // namespace coord
