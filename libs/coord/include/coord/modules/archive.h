#pragma once

// Archive support for the package manager.
//
// Read side:
//   * raw DEFLATE (RFC 1951) and gzip (RFC 1952) decompression;
//   * ustar/GNU/PAX tar extraction;
//   * ZIP extraction (stored and deflated entries).
//
// Write side:
//   * deterministic `.tar` generation;
//   * gzip framing with stored (uncompressed) DEFLATE blocks.
//
// All extraction validates entry names, rejects absolute paths and `..`
// traversal, ignores or rejects links, and enforces byte/entry limits.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace coord::archive {

struct ExtractOptions {
  /// Strips a single common top-level directory (`pkg-1.2.3/...`).
  bool stripSingleRoot = true;
  /// Symlinks are rejected unless this is enabled; targets must stay inside.
  bool allowSymlinks = false;
  std::uint64_t maxTotalBytes = 512ull * 1024 * 1024;
  std::size_t maxEntries = 100000;
};

struct ExtractResult {
  bool ok = false;
  std::string error;
  std::vector<std::string> warnings;
  std::uint64_t files = 0;
  std::uint64_t directories = 0;
  std::uint64_t bytes = 0;
};

/// Detects `.tar`, `.tar.gz`/`.tgz` and `.zip` content (by magic, not suffix)
/// and extracts it below `destination`.
[[nodiscard]] ExtractResult extract(const std::filesystem::path& archive,
                                    const std::filesystem::path& destination,
                                    const ExtractOptions& options = {});

[[nodiscard]] ExtractResult extractTar(std::string_view tar,
                                       const std::filesystem::path& destination,
                                       const ExtractOptions& options = {});

[[nodiscard]] ExtractResult extractZip(std::string_view zip,
                                       const std::filesystem::path& destination,
                                       const ExtractOptions& options = {});

[[nodiscard]] bool inflate(std::string_view deflate, std::string& output,
                           std::string& error,
                           std::size_t maxOutput = 512ull * 1024 * 1024);

[[nodiscard]] bool gunzip(std::string_view gzip, std::string& output,
                          std::string& error,
                          std::size_t maxOutput = 512ull * 1024 * 1024);

struct ArchiveEntry {
  std::string path;  // archive-relative, '/' separated
  std::string data;
  std::uint32_t mode = 0644;
  bool directory = false;
};

/// Builds a deterministic `.tar.gz` (mtime 0, sorted entries, stored DEFLATE).
[[nodiscard]] std::string createTarGz(const std::vector<ArchiveEntry>& entries,
                                      std::string& error);

/// Wraps bytes in a valid gzip container using stored DEFLATE blocks.
[[nodiscard]] std::string gzipStore(std::string_view data);

}  // namespace coord::archive
