#include "coord/support.h"

#include <algorithm>
#include <initializer_list>
#include <ostream>

namespace coord {
namespace {

constexpr std::uint32_t kInvalidOffset = 0xFFFFFFFFu;

void appendUtf8(std::string& out, char32_t codePoint) {
  if (codePoint > 0x10FFFF ||
      (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
    codePoint = 0xFFFD;
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

bool hasBytes(const std::string& bytes, std::size_t offset,
              std::initializer_list<unsigned char> signature) {
  if (bytes.size() < offset + signature.size()) {
    return false;
  }
  std::size_t index = offset;
  for (unsigned char byte : signature) {
    if (static_cast<unsigned char>(bytes[index]) != byte) {
      return false;
    }
    ++index;
  }
  return true;
}

}  // namespace

DecodedSource decodeSource(std::string bytes) {
  DecodedSource result;
  std::size_t offset = 0;

  if (hasBytes(bytes, 0, {0xFF, 0xFE, 0x00, 0x00})) {
    result.encoding = "utf-32le";
    result.hadBom = true;
    offset = 4;
  } else if (hasBytes(bytes, 0, {0x00, 0x00, 0xFE, 0xFF})) {
    result.encoding = "utf-32be";
    result.hadBom = true;
    offset = 4;
  } else if (hasBytes(bytes, 0, {0xEF, 0xBB, 0xBF})) {
    result.encoding = "utf-8";
    result.hadBom = true;
    offset = 3;
  } else if (hasBytes(bytes, 0, {0xFF, 0xFE})) {
    result.encoding = "utf-16le";
    result.hadBom = true;
    offset = 2;
  } else if (hasBytes(bytes, 0, {0xFE, 0xFF})) {
    result.encoding = "utf-16be";
    result.hadBom = true;
    offset = 2;
  }

  if (result.encoding == "utf-8") {
    result.utf8.assign(bytes, offset, std::string::npos);
    return result;
  }

  const bool littleEndian =
      result.encoding == "utf-16le" || result.encoding == "utf-32le";
  const auto byteAt = [&bytes](std::size_t index) {
    return static_cast<unsigned char>(bytes[index]);
  };

  if (result.encoding.rfind("utf-16", 0) == 0) {
    for (std::size_t i = offset; i + 1 < bytes.size(); i += 2) {
      std::uint32_t unit =
          littleEndian ? static_cast<std::uint32_t>(byteAt(i)) |
                             (static_cast<std::uint32_t>(byteAt(i + 1)) << 8)
                       : (static_cast<std::uint32_t>(byteAt(i)) << 8) |
                             static_cast<std::uint32_t>(byteAt(i + 1));
      char32_t codePoint = unit;
      if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
        const std::uint32_t low =
            littleEndian
                ? static_cast<std::uint32_t>(byteAt(i + 2)) |
                      (static_cast<std::uint32_t>(byteAt(i + 3)) << 8)
                : (static_cast<std::uint32_t>(byteAt(i + 2)) << 8) |
                      static_cast<std::uint32_t>(byteAt(i + 3));
        if (low >= 0xDC00 && low <= 0xDFFF) {
          codePoint = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
          i += 2;
        } else {
          codePoint = 0xFFFD;
        }
      } else if (unit >= 0xD800 && unit <= 0xDFFF) {
        codePoint = 0xFFFD;
      }
      appendUtf8(result.utf8, codePoint);
    }
    if ((bytes.size() - offset) % 2 != 0) {
      appendUtf8(result.utf8, 0xFFFD);
    }
    return result;
  }

  for (std::size_t i = offset; i + 3 < bytes.size(); i += 4) {
    std::uint32_t codePoint = 0;
    if (littleEndian) {
      codePoint = static_cast<std::uint32_t>(byteAt(i)) |
                  (static_cast<std::uint32_t>(byteAt(i + 1)) << 8) |
                  (static_cast<std::uint32_t>(byteAt(i + 2)) << 16) |
                  (static_cast<std::uint32_t>(byteAt(i + 3)) << 24);
    } else {
      codePoint = (static_cast<std::uint32_t>(byteAt(i)) << 24) |
                  (static_cast<std::uint32_t>(byteAt(i + 1)) << 16) |
                  (static_cast<std::uint32_t>(byteAt(i + 2)) << 8) |
                  static_cast<std::uint32_t>(byteAt(i + 3));
    }
    appendUtf8(result.utf8, codePoint);
  }
  if ((bytes.size() - offset) % 4 != 0) {
    appendUtf8(result.utf8, 0xFFFD);
  }
  return result;
}

std::uint32_t SourceManager::addFile(std::string path, std::string text) {
  auto file = std::make_unique<SourceFile>();
  file->path = std::move(path);
  DecodedSource decoded = decodeSource(std::move(text));
  file->text = std::move(decoded.utf8);
  file->encoding = std::move(decoded.encoding);
  file->lineStarts.push_back(0);
  for (std::uint32_t i = 0; i < file->text.size(); ++i) {
    if (file->text[i] == '\n') {
      file->lineStarts.push_back(i + 1);
    }
  }
  files_.push_back(std::move(file));
  return static_cast<std::uint32_t>(files_.size() - 1);
}

const SourceFile& SourceManager::file(std::uint32_t id) const {
  return *files_.at(id);
}

std::string_view SourceManager::text(Span span) const {
  if (span.file >= files_.size()) {
    return {};
  }
  const SourceFile& f = *files_[span.file];
  if (span.begin > f.text.size() || span.end > f.text.size() ||
      span.begin > span.end) {
    return {};
  }
  return std::string_view(f.text).substr(span.begin, span.end - span.begin);
}

LineCol SourceManager::lineCol(Span span) const {
  if (span.file >= files_.size()) {
    return {};
  }
  const SourceFile& f = *files_[span.file];
  const std::uint32_t offset = std::min(span.begin, static_cast<std::uint32_t>(f.text.size()));
  const auto it =
      std::upper_bound(f.lineStarts.begin(), f.lineStarts.end(), offset);
  const std::uint32_t lineIndex =
      static_cast<std::uint32_t>(it - f.lineStarts.begin());
  const std::uint32_t line = lineIndex == 0 ? 1 : lineIndex;
  const std::uint32_t lineStart =
      lineIndex == 0 ? 0 : f.lineStarts[lineIndex - 1];
  return LineCol{line, offset - lineStart + 1};
}

std::string_view SourceManager::lineText(std::uint32_t fileId,
                                         std::uint32_t line) const {
  if (fileId >= files_.size() || line == 0) {
    return {};
  }
  const SourceFile& f = *files_[fileId];
  if (line > f.lineStarts.size()) {
    return {};
  }
  const std::uint32_t begin = f.lineStarts[line - 1];
  std::uint32_t end = static_cast<std::uint32_t>(f.text.size());
  if (line < f.lineStarts.size()) {
    end = f.lineStarts[line] - 1;  // drop '\n'
  }
  if (end < begin) {
    end = begin;
  }
  // Drop a trailing '\r'.
  if (end > begin && f.text[end - 1] == '\r') {
    --end;
  }
  return std::string_view(f.text).substr(begin, end - begin);
}

const char* severityName(Severity severity) {
  switch (severity) {
    case Severity::Note:
      return "note";
    case Severity::Warning:
      return "warning";
    case Severity::Error:
      return "error";
  }
  return "error";
}

void DiagnosticEngine::error(Span span, std::string code, std::string message) {
  static constexpr std::size_t kMaxStoredDiagnostics = 256;
  ++errorCount_;
  if (diagnostics_.size() < kMaxStoredDiagnostics) {
    diagnostics_.push_back(Diagnostic{Severity::Error, std::move(code),
                                      std::move(message), span});
  } else if (!truncated_) {
    truncated_ = true;
    diagnostics_.push_back(
        Diagnostic{Severity::Error, "E0001",
                   "too many diagnostics; further messages suppressed", span});
  }
}

void DiagnosticEngine::warning(Span span, std::string code,
                               std::string message) {
  static constexpr std::size_t kMaxStoredDiagnostics = 256;
  if (warningsAsErrors_) {
    error(span, std::move(code), std::move(message));
    return;
  }
  ++warningCount_;
  if (diagnostics_.size() < kMaxStoredDiagnostics) {
    diagnostics_.push_back(Diagnostic{Severity::Warning, std::move(code),
                                      std::move(message), span});
  }
}

void DiagnosticEngine::note(Span span, std::string code, std::string message) {
  static constexpr std::size_t kMaxStoredDiagnostics = 256;
  if (diagnostics_.size() < kMaxStoredDiagnostics) {
    diagnostics_.push_back(Diagnostic{Severity::Note, std::move(code),
                                      std::move(message), span});
  }
}

void DiagnosticEngine::clear() {
  diagnostics_.clear();
  errorCount_ = 0;
  warningCount_ = 0;
  truncated_ = false;
}

void DiagnosticEngine::render(const SourceManager& sources,
                              std::ostream& out) const {
  for (const Diagnostic& d : diagnostics_) {
    const LineCol lc = sources.lineCol(d.span);
    const SourceFile& f = sources.file(d.span.file);
    out << f.path << ':' << lc.line << ':' << lc.column << ": "
        << severityName(d.severity);
    if (!d.code.empty()) {
      out << '[' << d.code << ']';
    }
    out << ": " << d.message << '\n';

    const std::string_view line = sources.lineText(d.span.file, lc.line);
    if (!line.empty() || d.span.valid()) {
      out << "  " << lc.line << " | " << line << '\n';
      const std::string gutter(std::to_string(lc.line).size(), ' ');
      out << "  " << gutter << " | ";
      const std::uint32_t column =
          lc.column == 0 ? 1 : std::min<std::uint32_t>(lc.column, 512);
      for (std::uint32_t i = 1; i < column; ++i) {
        out << (i - 1 < line.size() && line[i - 1] == '\t' ? '\t' : ' ');
      }
      out << '^';
      const std::uint32_t length =
          d.span.end > d.span.begin
              ? std::min<std::uint32_t>(d.span.end - d.span.begin, 512)
              : 1;
      for (std::uint32_t i = 1; i < length; ++i) {
        out << '~';
      }
      out << '\n';
    }
  }
}

}  // namespace coord
