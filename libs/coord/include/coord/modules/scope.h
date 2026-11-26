#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coord {

/// Generic lexical-scope stack. One `Scope` corresponds to one `{ ... }`.
/// Sema owns a type-specific instance; Module/Project tooling can reuse this
/// for symbol lookup without depending on the type checker.
template <class T>
class ScopeStack {
public:
  void push(std::string name = {}) {
    scopes_.push_back(Scope{std::move(name), {}});
  }

  void pop() {
    if (!scopes_.empty()) {
      scopes_.pop_back();
    }
  }

  void declare(const std::string& name, T value) {
    if (scopes_.empty()) {
      push("root");
    }
    scopes_.back().values[name] = std::move(value);
  }

  [[nodiscard]] T* lookup(std::string_view name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      const auto found = it->values.find(std::string(name));
      if (found != it->values.end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  [[nodiscard]] std::size_t depth() const { return scopes_.size(); }
  [[nodiscard]] bool empty() const { return scopes_.empty(); }

private:
  struct Scope {
    std::string name;
    std::unordered_map<std::string, T> values;
  };
  std::vector<Scope> scopes_;
};

}  // namespace coord
