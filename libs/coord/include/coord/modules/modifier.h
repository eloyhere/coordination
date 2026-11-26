#pragma once

#include <initializer_list>
#include <vector>

namespace coord {

/// Declaration modifiers shared by the parser, Sema and backends.
enum class Modifier { Static, Virtual, Override, Final, Const, Abstract };

class ModifierSet {
public:
  ModifierSet() = default;
  ModifierSet(std::initializer_list<Modifier> modifiers) {
    for (Modifier modifier : modifiers) {
      add(modifier);
    }
  }

  void add(Modifier modifier) {
    for (Modifier existing : modifiers_) {
      if (existing == modifier) {
        return;
      }
    }
    modifiers_.push_back(modifier);
  }

  [[nodiscard]] bool has(Modifier modifier) const {
    for (Modifier existing : modifiers_) {
      if (existing == modifier) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool empty() const { return modifiers_.empty(); }

private:
  std::vector<Modifier> modifiers_;
};

}  // namespace coord
