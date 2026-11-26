#pragma once

#include "coord/modules/unicode.h"
#include "coord/support.h"

#include <string>
#include <string_view>

namespace coord {

enum class Charset { Utf8, Utf16LE, Utf16BE, Utf32LE, Utf32BE };

struct DecodedText {
  std::string utf8;
  Charset charset = Charset::Utf8;
  bool hadBom = false;
};

inline Charset charsetFromEncoding(Encoding encoding) {
  switch (encoding) {
    case Encoding::Utf8:
      return Charset::Utf8;
    case Encoding::Utf16LE:
      return Charset::Utf16LE;
    case Encoding::Utf16BE:
      return Charset::Utf16BE;
    case Encoding::Utf32LE:
      return Charset::Utf32LE;
    case Encoding::Utf32BE:
      return Charset::Utf32BE;
  }
  return Charset::Utf8;
}

/// Decodes a source buffer with BOM detection. Internally everything is UTF-8.
inline DecodedText decodeText(std::string bytes) {
  DecodedSource decoded = decodeSource(std::move(bytes));
  Charset charset = Charset::Utf8;
  if (decoded.encoding == "utf-16le") {
    charset = Charset::Utf16LE;
  } else if (decoded.encoding == "utf-16be") {
    charset = Charset::Utf16BE;
  } else if (decoded.encoding == "utf-32le") {
    charset = Charset::Utf32LE;
  } else if (decoded.encoding == "utf-32be") {
    charset = Charset::Utf32BE;
  }
  return DecodedText{std::move(decoded.utf8), charset, decoded.hadBom};
}

}  // namespace coord
