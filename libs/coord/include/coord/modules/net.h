#pragma once

// Minimal network/file transport used by the registry and mirror layers.
//
// HTTP(S) goes through the system `curl` binary (present on Windows 10+,
// macOS and every mainstream Linux distribution) so the toolchain needs no
// TLS library. `file://` URLs and plain local paths are read directly, which
// keeps local/offline registries and tests dependency-free.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace coord::net {

struct TransportOptions {
  std::chrono::milliseconds timeout{15000};
  std::size_t maxBytes = 64ull * 1024 * 1024;
  bool followRedirects = true;
  std::string userAgent = "coorc/0.2";
};

struct FetchResult {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
  long long latencyMs = 0;
  bool fromCache = false;
};

/// Fetches a URL (`http://`, `https://`, `file://` or a local path).
[[nodiscard]] FetchResult fetch(std::string_view url,
                                const TransportOptions& options = {});

/// Downloads a URL to `destination`; the file is replaced atomically.
[[nodiscard]] FetchResult download(std::string_view url,
                                   const std::filesystem::path& destination,
                                   const TransportOptions& options = {});

[[nodiscard]] bool isRemoteUrl(std::string_view url);
[[nodiscard]] bool isLocalUrl(std::string_view url);
/// Converts a `file://` URL or plain path into a native path.
[[nodiscard]] std::filesystem::path localPathFromUrl(std::string_view url);
/// Joins a base URL/path and a relative reference using '/' separators.
[[nodiscard]] std::string joinUrl(std::string_view base,
                                  std::string_view relative);
/// True when the string contains a URL scheme (`scheme://`).
[[nodiscard]] bool hasScheme(std::string_view url);

}  // namespace coord::net
