#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace coord {

/// Interned identifier. The numeric value is stable for one compilation and is
/// used by module/package indexes, symbol tables and diagnostics.
using Identifier = std::uint32_t;

class IdentifierTable {
public:
  Identifier intern(std::string_view text) {
    const auto found = index_.find(std::string(text));
    if (found != index_.end()) {
      return found->second;
    }
    const Identifier id = static_cast<Identifier>(texts_.size());
    texts_.emplace_back(text);
    index_.emplace(texts_.back(), id);
    return id;
  }

  [[nodiscard]] std::string_view text(Identifier id) const {
    return id < texts_.size() ? std::string_view(texts_[id]) : std::string_view();
  }

  [[nodiscard]] std::size_t size() const { return texts_.size(); }

private:
  std::deque<std::string> texts_;
  std::unordered_map<std::string, Identifier> index_;
};

}  // namespace coord
