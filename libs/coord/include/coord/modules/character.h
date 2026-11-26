#pragma once

#include "coord/modules/unicode.h"

#include <string>
#include <string_view>

namespace coord {

/// A single Unicode scalar value. `char` in Coordination is exactly this type.
class Character {
public:
  constexpr Character() = default;
  constexpr explicit Character(CodePoint value) : value_(value) {}

  [[nodiscard]] constexpr CodePoint value() const { return value_; }
  [[nodiscard]] bool isAscii() const { return coord::isAscii(value_); }
  [[nodiscard]] bool isDigit() const { return isDecimalDigit(value_); }
  [[nodiscard]] bool isLetter() const { return coord::isLetter(value_); }
  [[nodiscard]] bool isWhitespace() const {
    return coord::isWhitespace(value_);
  }
  [[nodiscard]] bool isControl() const { return coord::isControl(value_); }

  [[nodiscard]] std::string toUtf8() const { return encodeUtf8(value_); }

  [[nodiscard]] static bool decode(std::string_view text, std::size_t& offset,
                                   Character& out) {
    CodePoint codePoint = 0;
    if (!decodeUtf8(text, offset, codePoint)) {
      return false;
    }
    out = Character(codePoint);
    return true;
  }

private:
  CodePoint value_ = 0;
};

}  // namespace coord
