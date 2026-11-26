#pragma once

namespace coord {

/// Visibility of a declaration. `Default` is resolved by the enclosing
/// declaration: class members default to `Private`, interface/enum members to
/// `Public`, module declarations to `Public` when exported.
enum class Access { Default, Public, Private, Protected };

[[nodiscard]] constexpr const char* accessName(Access access) {
  switch (access) {
    case Access::Default:
      return "default";
    case Access::Public:
      return "public";
    case Access::Private:
      return "private";
    case Access::Protected:
      return "protected";
  }
  return "default";
}

}  // namespace coord
