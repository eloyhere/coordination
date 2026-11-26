#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace coord {

/// A half-open byte range in one source file.
struct Span {
  std::uint32_t file = 0;
  std::uint32_t begin = 0;
  std::uint32_t end = 0;

  [[nodiscard]] bool valid() const { return end > begin; }
};

struct SourceFile {
  std::string path;
  std::string text;       // always UTF-8 internally
  std::string encoding;   // utf-8 / utf-16le / utf-16be / utf-32le / utf-32be
  std::vector<std::uint32_t> lineStarts;
};

/// Decodes a source buffer to UTF-8. Detects and strips a BOM; supports
/// UTF-8, UTF-16LE/BE and UTF-32LE/BE. Invalid code units become U+FFFD.
struct DecodedSource {
  std::string utf8;
  std::string encoding = "utf-8";
  bool hadBom = false;
};

[[nodiscard]] DecodedSource decodeSource(std::string bytes);

struct LineCol {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

/// Owns source buffers. Buffers never move, so string_views stay valid.
class SourceManager {
public:
  std::uint32_t addFile(std::string path, std::string text);

  [[nodiscard]] const SourceFile& file(std::uint32_t id) const;
  [[nodiscard]] std::string_view text(Span span) const;
  [[nodiscard]] LineCol lineCol(Span span) const;
  [[nodiscard]] std::string_view lineText(std::uint32_t fileId,
                                          std::uint32_t line) const;
  [[nodiscard]] std::size_t fileCount() const { return files_.size(); }

private:
  std::vector<std::unique_ptr<SourceFile>> files_;
};

enum class Severity { Note, Warning, Error };

[[nodiscard]] const char* severityName(Severity severity);

struct Diagnostic {
  Severity severity = Severity::Error;
  std::string code;
  std::string message;
  Span span;
};

class DiagnosticEngine {
public:
  void error(Span span, std::string code, std::string message);
  void warning(Span span, std::string code, std::string message);
  void note(Span span, std::string code, std::string message);

  void setWarningsAsErrors(bool enable) { warningsAsErrors_ = enable; }
  [[nodiscard]] bool warningsAsErrors() const { return warningsAsErrors_; }

  [[nodiscard]] bool hasErrors() const { return errorCount_ != 0; }
  [[nodiscard]] std::size_t errorCount() const { return errorCount_; }
  [[nodiscard]] std::size_t warningCount() const { return warningCount_; }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
    return diagnostics_;
  }

  void render(const SourceManager& sources, std::ostream& out) const;
  void clear();

private:
  std::vector<Diagnostic> diagnostics_;
  std::size_t errorCount_ = 0;
  std::size_t warningCount_ = 0;
  bool warningsAsErrors_ = false;
};

}  // namespace coord
