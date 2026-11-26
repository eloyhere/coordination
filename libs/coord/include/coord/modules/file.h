#pragma once

#include "coord/modules/stream.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace coord {

/// UTF-8 file abstraction. Runtime code and the compiler driver both use it.
/// `openStream` gives line/byte-offset access with cross-platform newline
/// normalization.
class File {
public:
  explicit File(std::filesystem::path path) : path_(std::move(path)) {}

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  [[nodiscard]] bool exists() const {
    std::error_code error;
    return std::filesystem::is_regular_file(path_, error);
  }

  [[nodiscard]] std::optional<std::string> readAll() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
      return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  }

  [[nodiscard]] std::optional<Stream> openStream() const {
    const std::optional<std::string> text = readAll();
    if (!text.has_value()) {
      return std::nullopt;
    }
    return Stream(*text);
  }

  [[nodiscard]] std::optional<std::string> readByLine(
      std::size_t index) const {
    const std::optional<Stream> stream = openStream();
    return stream.has_value() ? stream->readByLine(index) : std::nullopt;
  }

  /// Whole-stream read with LF normalization.
  [[nodiscard]] std::optional<std::string> readByStream() const {
    const std::optional<Stream> stream = openStream();
    return stream.has_value() ? std::optional<std::string>(stream->readByStream())
                              : std::nullopt;
  }

  /// Whole-stream write; creates the file if necessary and writes LF.
  bool writeByStream(std::string_view content) const {
    Stream stream;
    stream.writeByStream(content);
    return stream.save(path_);
  }

  [[nodiscard]] std::optional<std::string> readByByteOffset(
      std::size_t offset) const {
    const std::optional<Stream> stream = openStream();
    return stream.has_value() ? stream->readByByteOffset(offset)
                              : std::nullopt;
  }

  bool writeByLine(std::size_t index, std::string_view content) const {
    const std::optional<Stream> current = openStream();
    if (!current.has_value()) {
      return false;
    }
    Stream stream = *current;
    return stream.writeByLine(index, content) && stream.save(path_);
  }

  bool writeByByteOffset(std::size_t offset, std::string_view content) const {
    const std::optional<Stream> current = openStream();
    if (!current.has_value()) {
      return false;
    }
    Stream stream = *current;
    return stream.writeByByteOffset(offset, content) && stream.save(path_);
  }

  static bool writeAll(const std::filesystem::path& path,
                       std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
      return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
  }

  [[nodiscard]] static bool exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
  }

private:
  std::filesystem::path path_;
};

}  // namespace coord
