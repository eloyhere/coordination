#include "coord/modules/net.h"

#include "coord/modules/platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace coord::net {
namespace {

std::string toLower(std::string text) {
  for (char& character : text) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

int hexDigit(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

std::string percentDecode(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int high = hexDigit(text[i + 1]);
      const int low = hexDigit(text[i + 2]);
      if (high >= 0 && low >= 0) {
        result.push_back(static_cast<char>(high * 16 + low));
        i += 2;
        continue;
      }
    }
    result.push_back(text[i]);
  }
  return result;
}

std::string percentEncodePath(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (std::isalnum(value) != 0 || character == '-' || character == '_' ||
        character == '.' || character == '~' || character == '/' ||
        character == ':') {
      result.push_back(character);
    } else {
      result.push_back('%');
      result.push_back(kHex[(value >> 4) & 0x0F]);
      result.push_back(kHex[value & 0x0F]);
    }
  }
  return result;
}

std::string trimSlashes(std::string text) {
  while (!text.empty() && (text.back() == '/' || text.back() == '\\')) {
    text.pop_back();
  }
  return text;
}

std::optional<std::string> findCurl() {
  if (const std::optional<std::string> configured = platform::environment("COORD_CURL");
      configured.has_value() && !configured->empty()) {
    return configured;
  }
  if (const std::optional<std::filesystem::path> path =
          platform::findExecutable("curl");
      path.has_value()) {
    return platform::pathToUtf8(*path);
  }
  return std::nullopt;
}

long long elapsedMs(std::chrono::steady_clock::time_point start) {
  return static_cast<long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

FetchResult readLocal(std::string_view url, const TransportOptions& options) {
  FetchResult result;
  const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  const std::filesystem::path path = localPathFromUrl(url);
  std::error_code sizeError;
  const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
  if (sizeError) {
    result.status = 404;
    result.error = "cannot read '" + platform::pathToUtf8(path) + "'";
    result.latencyMs = elapsedMs(start);
    return result;
  }
  if (size > options.maxBytes) {
    result.status = 413;
    result.error = "file is larger than the transport limit";
    result.latencyMs = elapsedMs(start);
    return result;
  }
  std::string readError;
  std::optional<std::string> body = platform::readFile(path, readError);
  if (!body.has_value()) {
    result.status = 404;
    result.error = readError;
    result.latencyMs = elapsedMs(start);
    return result;
  }
  result.ok = true;
  result.status = 200;
  result.body = std::move(*body);
  result.latencyMs = elapsedMs(start);
  return result;
}

int parseStatusFromTrailer(const std::string& text, std::string& body) {
  const std::size_t newline = text.find_last_of('\n');
  if (newline == std::string::npos) {
    body = text;
    return 0;
  }
  const std::string trailer = text.substr(newline + 1);
  if (trailer.empty()) {
    body = text.substr(0, newline);
    return 0;
  }
  int status = 0;
  for (const char character : trailer) {
    if (character < '0' || character > '9') {
      body = text;
      return 0;
    }
    status = status * 10 + (character - '0');
  }
  body = text.substr(0, newline);
  return status;
}

}  // namespace

bool hasScheme(std::string_view url) {
  const std::size_t position = url.find("://");
  return position != std::string_view::npos && position > 0;
}

bool isRemoteUrl(std::string_view url) {
  const std::string lowered = toLower(std::string(url));
  return lowered.rfind("http://", 0) == 0 ||
         lowered.rfind("https://", 0) == 0;
}

bool isLocalUrl(std::string_view url) {
  if (isRemoteUrl(url)) {
    return false;
  }
  const std::string lowered = toLower(std::string(url));
  if (lowered.rfind("file://", 0) == 0) {
    return true;
  }
  if (hasScheme(url)) {
    return false;  // ftp://, git://, ... are not supported
  }
  return true;
}

std::filesystem::path localPathFromUrl(std::string_view url) {
  std::string text(url);
  const std::string lowered = toLower(text);
  if (lowered.rfind("file://", 0) == 0) {
    text.erase(0, 7);
    // file:///C:/path -> /C:/path (Windows drive letter)
    if (text.size() >= 3 && text[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(text[1])) != 0 &&
        text[2] == ':') {
      text.erase(0, 1);
    }
    // file://server/share -> //server/share
    if (text.rfind("localhost/", 0) == 0) {
      text.erase(0, 9);
    }
    text = percentDecode(text);
  }
  return platform::pathFromUtf8(text);
}

std::string joinUrl(std::string_view base, std::string_view relative) {
  if (relative.empty()) {
    return std::string(base);
  }
  if (hasScheme(relative) || relative.front() == '/') {
    return std::string(relative);
  }
  std::string result = trimSlashes(std::string(base));
  result.push_back('/');
  result += percentEncodePath(relative);
  return result;
}

FetchResult fetch(std::string_view url, const TransportOptions& options) {
  FetchResult result;
  const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  if (isLocalUrl(url)) {
    return readLocal(url, options);
  }
  if (!isRemoteUrl(url)) {
    result.error = "unsupported URL scheme in '" + std::string(url) + "'";
    result.latencyMs = elapsedMs(start);
    return result;
  }
  const std::optional<std::string> curl = findCurl();
  if (!curl.has_value()) {
    result.error =
        "HTTP registries require curl on PATH (or set COORD_CURL); "
        "use a file:// registry for offline builds";
    result.latencyMs = elapsedMs(start);
    return result;
  }
  const long long timeoutMs = std::max<long long>(1, options.timeout.count());
  const std::string connectSeconds =
      std::to_string(std::max<long long>(1, (timeoutMs + 999) / 1000));
  const std::string maxSeconds =
      std::to_string(std::max<long long>(1, (timeoutMs + 999) / 1000 + 5));
  std::vector<std::string> arguments = {
      *curl,
      "--silent",
      "--show-error",
      "--location",
      "--connect-timeout",
      connectSeconds,
      "--max-time",
      maxSeconds,
      "--max-filesize",
      std::to_string(options.maxBytes),
      "--write-out",
      "\n%{http_code}",
      "--user-agent",
      options.userAgent,
  };
  if (!options.followRedirects) {
    arguments.push_back("--max-redirs");
    arguments.push_back("0");
  }
  arguments.emplace_back(url);
  platform::ProcessOptions processOptions;
  processOptions.captureOutput = true;
  const platform::ProcessResult process = platform::run(arguments, processOptions);
  result.latencyMs = elapsedMs(start);
  if (!process.started) {
    result.error = process.failure;
    return result;
  }
  std::string body;
  const int status = parseStatusFromTrailer(process.out, body);
  result.status = status;
  if (process.exitCode != 0) {
    result.error = process.err.empty() ? "curl failed with exit code " +
                                            std::to_string(process.exitCode)
                                      : process.err;
    return result;
  }
  if (status >= 400) {
    result.error = "HTTP " + std::to_string(status);
    return result;
  }
  result.ok = true;
  result.body = std::move(body);
  return result;
}

FetchResult download(std::string_view url,
                     const std::filesystem::path& destination,
                     const TransportOptions& options) {
  FetchResult result;
  const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  if (isLocalUrl(url)) {
    const std::filesystem::path source = localPathFromUrl(url);
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(source, sizeError);
    if (!sizeError && size > options.maxBytes) {
      result.status = 413;
      result.error = "file is larger than the transport limit";
      result.latencyMs = elapsedMs(start);
      return result;
    }
    std::error_code copyError;
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               copyError);
    if (copyError) {
      result.status = 404;
      result.error = "cannot copy '" + platform::pathToUtf8(source) + "': " +
                     copyError.message();
      result.latencyMs = elapsedMs(start);
      return result;
    }
    result.ok = true;
    result.status = 200;
    result.latencyMs = elapsedMs(start);
    return result;
  }
  const std::optional<std::string> curl = findCurl();
  if (!curl.has_value()) {
    result.error =
        "HTTP registries require curl on PATH (or set COORD_CURL); "
        "use a file:// registry for offline builds";
    return result;
  }
  const long long timeoutMs = std::max<long long>(1, options.timeout.count());
  const std::string connectSeconds =
      std::to_string(std::max<long long>(1, (timeoutMs + 999) / 1000));
  const std::string maxSeconds = std::to_string(
      std::max<long long>(1, (timeoutMs + 999) / 1000 + 30));
  const std::string destinationText = platform::pathToUtf8(destination);
  std::vector<std::string> arguments = {
      *curl,
      "--silent",
      "--show-error",
      "--location",
      "--connect-timeout",
      connectSeconds,
      "--max-time",
      maxSeconds,
      "--max-filesize",
      std::to_string(options.maxBytes),
      "--output",
      destinationText,
      "--write-out",
      "%{http_code}",
      "--user-agent",
      options.userAgent,
      std::string(url),
  };
  platform::ProcessOptions processOptions;
  processOptions.captureOutput = true;
  const platform::ProcessResult process = platform::run(arguments, processOptions);
  result.latencyMs = elapsedMs(start);
  if (!process.started) {
    result.error = process.failure;
    return result;
  }
  result.status = process.out.empty() ? 0 : std::atoi(process.out.c_str());
  if (process.exitCode != 0 || result.status >= 400) {
    std::error_code removeError;
    std::filesystem::remove(destination, removeError);
    result.error = process.err.empty()
                       ? "download failed with HTTP " +
                             std::to_string(result.status)
                       : process.err;
    return result;
  }
  std::error_code existsError;
  if (!std::filesystem::exists(destination, existsError)) {
    result.error = "curl did not create the download file";
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace coord::net
