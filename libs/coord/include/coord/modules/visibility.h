#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace coord {

/// Visibility set of one scope.
///
/// Each visible name maps to a declaration identity (name by default).
/// Set operations are used to model scope intersection/union/complement
/// without changing lexical lookup order.
template <class T>
class VisibilitySet {
public:
  struct Entry {
    std::string declaration;
    std::string ownerScope;
    T value;
  };

  void insert(std::string name, T value, std::string declaration = {},
              std::string ownerScope = {}) {
    if (declaration.empty()) {
      declaration = name;
    }
    entries_[std::move(name)] = Entry{std::move(declaration),
                                      std::move(ownerScope), std::move(value)};
  }

  [[nodiscard]] std::string ownerOf(std::string_view name) const {
    const auto found = entries_.find(std::string(name));
    return found == entries_.end() ? std::string() : found->second.ownerScope;
  }

  [[nodiscard]] bool contains(std::string_view name) const {
    return entries_.find(std::string(name)) != entries_.end();
  }

  [[nodiscard]] const T* find(std::string_view name) const {
    const auto found = entries_.find(std::string(name));
    return found == entries_.end() ? nullptr : &found->second.value;
  }

  [[nodiscard]] const std::map<std::string, Entry>& entries() const {
    return entries_;
  }

  [[nodiscard]] bool empty() const { return entries_.empty(); }
  [[nodiscard]] std::size_t size() const { return entries_.size(); }

  /// Names visible in both sets with the same declaration.
  /// Same name but different declarations is an ambiguity conflict.
  [[nodiscard]] VisibilitySet intersect(const VisibilitySet& other) const {
    VisibilitySet result;
    for (const auto& [name, entry] : entries_) {
      const auto found = other.entries_.find(name);
      if (found == other.entries_.end()) {
        continue;
      }
      if (found->second.declaration != entry.declaration ||
          found->second.ownerScope != entry.ownerScope) {
        result.conflicts_.push_back(name);
        continue;
      }
      result.entries_[name] = entry;
    }
    return result;
  }

  /// Names visible in either set. Same name and same declaration is shared;
  /// same name with different declarations is recorded as a conflict and the
  /// left declaration wins.
  [[nodiscard]] VisibilitySet unite(const VisibilitySet& other) const {
    VisibilitySet result = *this;
    for (const auto& [name, entry] : other.entries_) {
      const auto found = result.entries_.find(name);
      if (found == result.entries_.end()) {
        result.entries_[name] = entry;
      } else if (found->second.declaration != entry.declaration ||
                 found->second.ownerScope != entry.ownerScope) {
        result.conflicts_.push_back(name);
      }
    }
    return result;
  }

  /// Remove every name visible in `other`; declarations are not restored from
  /// any outer scope.
  [[nodiscard]] VisibilitySet complement(const VisibilitySet& other) const {
    VisibilitySet result;
    for (const auto& [name, entry] : entries_) {
      if (!other.contains(name)) {
        result.entries_[name] = entry;
      }
    }
    return result;
  }

  [[nodiscard]] const std::vector<std::string>& conflicts() const {
    return conflicts_;
  }

  [[nodiscard]] bool hasConflicts() const { return !conflicts_.empty(); }

private:
  std::map<std::string, Entry> entries_;
  std::vector<std::string> conflicts_;
};

}  // namespace coord
