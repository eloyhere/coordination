#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace coord {

using CodePoint = char32_t;
inline constexpr CodePoint kReplacementCharacter = 0xFFFD;
inline constexpr CodePoint kMaxCodePoint = 0x10FFFF;

enum class Encoding { Utf8, Utf16LE, Utf16BE, Utf32LE, Utf32BE };

struct BomInfo {
  Encoding encoding = Encoding::Utf8;
  std::size_t length = 0;
};

[[nodiscard]] inline BomInfo detectBom(std::string_view bytes) {
  const auto has = [&bytes](std::initializer_list<unsigned char> signature) {
    if (bytes.size() < signature.size()) {
      return false;
    }
    std::size_t index = 0;
    for (unsigned char byte : signature) {
      if (static_cast<unsigned char>(bytes[index]) != byte) {
        return false;
      }
      ++index;
    }
    return true;
  };
  if (has({0xFF, 0xFE, 0x00, 0x00})) {
    return {Encoding::Utf32LE, 4};
  }
  if (has({0x00, 0x00, 0xFE, 0xFF})) {
    return {Encoding::Utf32BE, 4};
  }
  if (has({0xEF, 0xBB, 0xBF})) {
    return {Encoding::Utf8, 3};
  }
  if (has({0xFF, 0xFE})) {
    return {Encoding::Utf16LE, 2};
  }
  if (has({0xFE, 0xFF})) {
    return {Encoding::Utf16BE, 2};
  }
  return {Encoding::Utf8, 0};
}

[[nodiscard]] inline bool isValidCodePoint(CodePoint codePoint) {
  return codePoint <= kMaxCodePoint &&
         !(codePoint >= 0xD800 && codePoint <= 0xDFFF);
}

inline void appendUtf8(std::string& out, CodePoint codePoint) {
  if (!isValidCodePoint(codePoint)) {
    codePoint = kReplacementCharacter;
  }
  if (codePoint < 0x80) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

[[nodiscard]] inline std::string encodeUtf8(CodePoint codePoint) {
  std::string result;
  appendUtf8(result, codePoint);
  return result;
}

/// Decodes one UTF-8 scalar at `offset` and advances `offset` on success.
[[nodiscard]] inline bool decodeUtf8(std::string_view text,
                                     std::size_t& offset,
                                     CodePoint& codePoint) {
  if (offset >= text.size()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(text[offset]);
  std::size_t length = 0;
  if (first < 0x80) {
    length = 1;
    codePoint = first;
  } else if ((first & 0xE0) == 0xC0) {
    length = 2;
    codePoint = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    length = 3;
    codePoint = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    length = 4;
    codePoint = first & 0x07;
  } else {
    return false;
  }
  if (offset + length > text.size()) {
    return false;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[offset + i]);
    if ((next & 0xC0) != 0x80) {
      return false;
    }
    codePoint = (codePoint << 6) | (next & 0x3F);
  }
  if (!isValidCodePoint(codePoint)) {
    codePoint = kReplacementCharacter;
  }
  offset += length;
  return true;
}

[[nodiscard]] inline bool isAscii(CodePoint codePoint) {
  return codePoint < 0x80;
}

[[nodiscard]] inline bool isControl(CodePoint codePoint) {
  return codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F);
}

[[nodiscard]] inline bool isWhitespace(CodePoint codePoint) {
  return codePoint == 0x20 || codePoint == 0x09 || codePoint == 0x0A ||
         codePoint == 0x0D || codePoint == 0x0B || codePoint == 0x0C ||
         codePoint == 0x00A0 || codePoint == 0x1680 ||
         (codePoint >= 0x2000 && codePoint <= 0x200A) ||
         codePoint == 0x2028 || codePoint == 0x2029 || codePoint == 0x202F ||
         codePoint == 0x205F || codePoint == 0x3000;
}

[[nodiscard]] inline bool isDecimalDigit(CodePoint codePoint) {
  return codePoint >= '0' && codePoint <= '9';
}

[[nodiscard]] inline bool isLetter(CodePoint codePoint) {
  return (codePoint >= 'A' && codePoint <= 'Z') ||
         (codePoint >= 'a' && codePoint <= 'z') ||
         (codePoint >= 0x00C0 && codePoint <= 0x024F) ||
         (codePoint >= 0x0370 && codePoint <= 0x03FF) ||
         (codePoint >= 0x0400 && codePoint <= 0x04FF) ||
         (codePoint >= 0x3040 && codePoint <= 0x30FF) ||
         (codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||
         (codePoint >= 0xAC00 && codePoint <= 0xD7AF);
}

[[nodiscard]] inline bool isIdentifierStart(CodePoint codePoint) {
  return codePoint == '_' || isLetter(codePoint);
}

[[nodiscard]] inline bool isIdentifierContinue(CodePoint codePoint) {
  return isIdentifierStart(codePoint) || isDecimalDigit(codePoint) ||
         (codePoint >= 0x80 && !isWhitespace(codePoint) && !isControl(codePoint));
}

/// Validates and normalizes a UTF-8 view; invalid sequences become U+FFFD.
[[nodiscard]] inline std::string normalizeUtf8(std::string_view text) {
  std::string result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t before = offset;
    CodePoint codePoint = 0;
    if (!decodeUtf8(text, offset, codePoint)) {
      offset = before + 1;
      appendUtf8(result, kReplacementCharacter);
      continue;
    }
    appendUtf8(result, codePoint);
  }
  return result;
}

}  // namespace coord
