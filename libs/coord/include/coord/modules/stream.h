#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace coord {

/// Platform-independent text stream.
///
/// Normalization contract:
/// - line endings are normalized to LF (`\n`) on construction/read;
/// - `save` always writes LF, so byte offsets are identical on every OS;
/// - line indexes are 0-based, byte offsets are UTF-8 byte offsets;
/// - `readByLine` returns the line without the trailing newline.
class Stream {
public:
  Stream() = default;
  explicit Stream(std::string content)
      : content_(normalizeNewlines(std::move(content))) {}

  [[nodiscard]] static std::optional<Stream> open(
      const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return Stream(buffer.str());
  }

  [[nodiscard]] bool save(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
      return false;
    }
    output.write(content_.data(),
                 static_cast<std::streamsize>(content_.size()));
    return static_cast<bool>(output);
  }

  [[nodiscard]] const std::string& text() const { return content_; }

  /// Whole-stream read: returns the normalized text (LF line endings).
  [[nodiscard]] std::string readByStream() const { return content_; }

  /// Whole-stream write: replaces the content and normalizes line endings.
  bool writeByStream(std::string_view content) {
    content_ = normalizeNewlines(std::string(content));
    return true;
  }

  [[nodiscard]] std::size_t lineCount() const {
    if (content_.empty()) {
      return 0;
    }
    std::size_t count = 0;
    for (const char c : content_) {
      if (c == '\n') {
        ++count;
      }
    }
    if (content_.back() != '\n') {
      ++count;
    }
    return count;
  }

  [[nodiscard]] std::optional<std::string> readByLine(
      std::size_t index) const {
    const auto range = lineRange(index);
    if (!range.has_value()) {
      return std::nullopt;
    }
    return content_.substr(range->first, range->second - range->first);
  }

  /// Returns the normalized UTF-8 text starting at `offset` to end of file.
  [[nodiscard]] std::optional<std::string> readByByteOffset(
      std::size_t offset) const {
    if (offset > content_.size()) {
      return std::nullopt;
    }
    return content_.substr(offset);
  }

  /// Replaces the line at `index`; the new content must not contain a newline.
  bool writeByLine(std::size_t index, std::string_view content) {
    if (content.find('\n') != std::string_view::npos) {
      return false;
    }
    const auto range = lineRange(index);
    if (!range.has_value()) {
      return false;
    }
    content_.replace(range->first, range->second - range->first, content);
    return true;
  }

  /// Overwrites bytes starting at `offset`; if content runs past the end it is
  /// appended. Offsets are in the normalized UTF-8 text.
  bool writeByByteOffset(std::size_t offset, std::string_view content) {
    if (offset > content_.size()) {
      return false;
    }
    content_.replace(offset, content.size(), content);
    return true;
  }

  [[nodiscard]] static std::string normalizeNewlines(std::string text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r') {
        if (i + 1 < text.size() && text[i + 1] == '\n') {
          ++i;
        }
        result.push_back('\n');
      } else {
        result.push_back(text[i]);
      }
    }
    return result;
  }

private:
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> lineRange(
      std::size_t index) const {
    if (index >= lineCount()) {
      return std::nullopt;
    }
    std::size_t start = 0;
    for (std::size_t line = 0; line < index; ++line) {
      const std::size_t newline = content_.find('\n', start);
      if (newline == std::string::npos) {
        return std::nullopt;
      }
      start = newline + 1;
    }
    std::size_t end = content_.find('\n', start);
    if (end == std::string::npos) {
      end = content_.size();
    }
    return std::make_pair(start, end);
  }

  std::string content_;
};

}  // namespace coord
