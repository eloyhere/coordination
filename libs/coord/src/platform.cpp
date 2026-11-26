#include "coord/modules/platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <thread>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace coord::platform {
namespace {

#ifdef _WIN32

std::wstring widen(std::string_view text) {
  if (text.empty()) {
    return std::wstring();
  }
  UINT codePage = CP_UTF8;
  int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) {
    codePage = CP_ACP;
    size = ::MultiByteToWideChar(CP_ACP, 0, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0);
  }
  if (size <= 0) {
    return std::wstring();
  }
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  ::MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
  return result;
}

std::string narrow(std::wstring_view text) {
  if (text.empty()) {
    return std::string();
  }
  const int size = ::WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    return std::string();
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
  return result;
}

/// Text read from a compiler pipe is usually UTF-8 under /utf-8 but may be in
/// the active ANSI code page. Decode heuristically so diagnostics stay readable.
std::string decodeProcessOutput(const std::string& bytes) {
  if (bytes.empty()) {
    return bytes;
  }
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                            static_cast<int>(bytes.size()), nullptr, 0) > 0) {
    return bytes;
  }
  const int size = ::MultiByteToWideChar(
      CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
  if (size <= 0) {
    return bytes;
  }
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  ::MultiByteToWideChar(CP_ACP, 0, bytes.data(),
                        static_cast<int>(bytes.size()), wide.data(), size);
  return narrow(wide);
}

std::string windowsErrorMessage(DWORD code) {
  LPWSTR buffer = nullptr;
  const DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::string message;
  if (length != 0 && buffer != nullptr) {
    message = narrow(std::wstring_view(buffer, length));
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r' ||
            message.back() == ' ')) {
      message.pop_back();
    }
  }
  ::LocalFree(buffer);
  if (message.empty()) {
    message = "error " + std::to_string(code);
  }
  return message;
}

std::string readFromHandle(HANDLE handle) {
  std::string bytes;
  std::array<char, 8192> buffer{};
  DWORD read = 0;
  for (;;) {
    if (::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                   &read, nullptr) == 0 ||
        read == 0) {
      break;
    }
    bytes.append(buffer.data(), read);
  }
  return bytes;
}

bool buildEnvironmentBlock(
    const std::map<std::string, std::string>& overrides,
    std::vector<wchar_t>& block, std::string& error) {
  if (overrides.empty()) {
    return true;  // inherit
  }
  std::map<std::wstring, std::wstring> entries;
  LPWCH current = ::GetEnvironmentStringsW();
  if (current == nullptr) {
    error = "cannot read the process environment";
    return false;
  }
  for (const wchar_t* cursor = current; *cursor != L'\0';
       cursor += std::wcslen(cursor) + 1) {
    if (*cursor == L'=') {
      continue;  // hidden drive variables such as "=C:=C:\..."
    }
    const wchar_t* equals = std::wcschr(cursor, L'=');
    if (equals == nullptr) {
      continue;
    }
    entries[std::wstring(cursor, equals)] =
        std::wstring(equals + 1);
  }
  ::FreeEnvironmentStringsW(current);
  for (const auto& [name, value] : overrides) {
    entries[widen(name)] = widen(value);
  }
  for (const auto& entry : entries) {
    block.insert(block.end(), entry.first.begin(), entry.first.end());
    block.push_back(L'=');
    block.insert(block.end(), entry.second.begin(), entry.second.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return true;
}

struct HandleCloser {
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~HandleCloser() { reset(); }
  void reset() {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
      ::CloseHandle(handle);
    }
    handle = INVALID_HANDLE_VALUE;
  }
};

bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }
  const std::size_t offset = text.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const unsigned char left =
        static_cast<unsigned char>(text[offset + i]);
    const unsigned char right = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

#endif  // _WIN32

std::string lowerAscii(std::string text) {
  for (char& character : text) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

bool splitPathList(std::string_view value, char separator,
                   std::vector<std::string>& entries) {
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t end = value.find(separator, begin);
    std::string entry(value.substr(
        begin, end == std::string_view::npos ? std::string_view::npos
                                             : end - begin));
    if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"') {
      entry = entry.substr(1, entry.size() - 2);
    }
    if (!entry.empty()) {
      entries.push_back(std::move(entry));
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return !entries.empty();
}

std::string randomSuffix() {
#ifdef _WIN32
  const auto pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
  const auto pid = static_cast<unsigned long>(::getpid());
#endif
  const auto ticks = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::to_string(pid) + "-" + std::to_string(ticks);
}

}  // namespace

OperatingSystem operatingSystem() noexcept {
#if defined(_WIN32)
  return OperatingSystem::Windows;
#elif defined(__APPLE__)
  return OperatingSystem::MacOs;
#elif defined(__linux__)
  return OperatingSystem::Linux;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
  return OperatingSystem::FreeBsd;
#else
  return OperatingSystem::Other;
#endif
}

Architecture architecture() noexcept {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
  return Architecture::X64;
#elif defined(_M_IX86) || defined(__i386__)
  return Architecture::X86;
#elif defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
  return Architecture::Arm64;
#elif defined(_M_ARM) || defined(__arm__)
  return Architecture::Arm;
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
  return Architecture::RiscV64;
#else
  return Architecture::Unknown;
#endif
}

std::string_view operatingSystemName(OperatingSystem value) noexcept {
  switch (value) {
    case OperatingSystem::Windows:
      return "windows";
    case OperatingSystem::MacOs:
      return "macos";
    case OperatingSystem::Linux:
      return "linux";
    case OperatingSystem::FreeBsd:
      return "freebsd";
    case OperatingSystem::Other:
      return "other";
  }
  return "other";
}

std::string_view architectureName(Architecture value) noexcept {
  switch (value) {
    case Architecture::X86:
      return "x86";
    case Architecture::X64:
      return "x86_64";
    case Architecture::Arm:
      return "arm";
    case Architecture::Arm64:
      return "aarch64";
    case Architecture::RiscV64:
      return "riscv64";
    case Architecture::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string operatingSystemName() { return std::string(operatingSystemName(operatingSystem())); }

std::string architectureName() { return std::string(architectureName(architecture())); }

std::string targetTriple() {
  const std::string arch = architectureName();
  switch (operatingSystem()) {
    case OperatingSystem::Windows:
#if defined(_MSC_VER)
      return arch + "-pc-windows-msvc";
#else
      return arch + "-pc-windows-gnu";
#endif
    case OperatingSystem::MacOs:
      return arch + "-apple-darwin";
    case OperatingSystem::Linux:
      return arch + "-unknown-linux-gnu";
    case OperatingSystem::FreeBsd:
      return arch + "-unknown-freebsd";
    case OperatingSystem::Other:
      break;
  }
  return arch + "-unknown-unknown";
}

std::string describeHost() {
  return operatingSystemName() + " " + architectureName() + " (" +
         targetTriple() + ")";
}

bool isKnownTargetTriple(std::string_view triple) {
  if (triple.empty() || triple.find(' ') != std::string_view::npos ||
      triple.find('\t') != std::string_view::npos) {
    return false;
  }
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= triple.size()) {
    const std::size_t end = triple.find('-', begin);
    const std::string_view part =
        triple.substr(begin, end == std::string_view::npos
                                 ? std::string_view::npos
                                 : end - begin);
    if (part.empty()) {
      return false;
    }
    parts.emplace_back(part);
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  if (parts.size() < 3) {
    return false;
  }
  // <arch>-<vendor>-<os>[-<abi>]: the OS is always the third component.
  const std::string os = lowerAscii(parts[2]);
  static const char* const kKnownOs[] = {
      "windows", "linux", "darwin", "macos", "freebsd",
      "openbsd", "netbsd", "android", "ios", "wasi"};
  bool known = false;
  for (const char* candidate : kKnownOs) {
    if (os == candidate) {
      known = true;
      break;
    }
  }
  if (!known) {
    return false;
  }
  for (const char& character : parts[0]) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (std::isalnum(value) == 0 && character != '_') {
      return false;
    }
  }
  return true;
}

bool isLittleEndian() noexcept {
#if defined(_WIN32)
  return true;
#else
  const std::uint16_t value = 1;
  return *reinterpret_cast<const std::uint8_t*>(&value) == 1;
#endif
}

std::string_view executableSuffix() noexcept {
#if defined(_WIN32)
  return ".exe";
#else
  return "";
#endif
}

std::string_view objectFileSuffix() noexcept {
#if defined(_WIN32)
  return ".obj";
#else
  return ".o";
#endif
}

std::string_view staticLibraryPrefix() noexcept {
#if defined(_WIN32)
  return "";
#else
  return "lib";
#endif
}

std::string_view staticLibrarySuffix() noexcept {
#if defined(_WIN32)
  return ".lib";
#else
  return ".a";
#endif
}

std::string_view sharedLibrarySuffix() noexcept {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::string_view archiveSuffix() noexcept {
#if defined(_WIN32)
  return ".zip";
#else
  return ".tar.gz";
#endif
}

std::string executableFileName(std::string_view stem) {
  return std::string(stem) + std::string(executableSuffix());
}

std::string objectFileName(std::string_view stem) {
  return std::string(stem) + std::string(objectFileSuffix());
}

std::string staticLibraryFileName(std::string_view stem) {
  return std::string(staticLibraryPrefix()) + std::string(stem) +
         std::string(staticLibrarySuffix());
}

std::string sharedLibraryFileName(std::string_view stem) {
  return std::string(staticLibraryPrefix()) + std::string(stem) +
         std::string(sharedLibrarySuffix());
}

std::string pathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::u8string utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
  return path.string();
#endif
}

std::filesystem::path pathFromUtf8(std::string_view text) {
#if defined(_WIN32)
  const std::u8string utf8(reinterpret_cast<const char8_t*>(text.data()),
                           text.size());
  return std::filesystem::path(utf8);
#else
  return std::filesystem::path(std::string(text));
#endif
}

std::string normalizeSlashes(std::string text) {
  for (char& character : text) {
    if (character == '\\') {
      character = '/';
    }
  }
  return text;
}

std::string pathKey(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
  if (error) {
    error.clear();
    result = std::filesystem::absolute(path, error);
    if (error) {
      result = path;
    }
  }
  std::string key = normalizeSlashes(pathToUtf8(result.lexically_normal()));
  while (key.size() > 1 && key.back() == '/') {
    key.pop_back();
  }
#if defined(_WIN32)
  key = lowerAscii(std::move(key));
#endif
  return key;
}

std::filesystem::path resolvePath(const std::filesystem::path& base,
                                  std::string_view relative) {
  const std::filesystem::path candidate = pathFromUtf8(relative);
  if (candidate.is_absolute()) {
    return candidate;
  }
  return base / candidate;
}

bool isWithin(const std::filesystem::path& child,
              const std::filesystem::path& parent) {
  const std::filesystem::path childPath = pathFromUtf8(pathKey(child));
  const std::filesystem::path parentPath = pathFromUtf8(pathKey(parent));
  auto childIt = childPath.begin();
  for (auto parentIt = parentPath.begin(); parentIt != parentPath.end();
       ++parentIt, ++childIt) {
    if (childIt == childPath.end()) {
      return false;
    }
    const std::string left = pathToUtf8(*childIt);
    const std::string right = pathToUtf8(*parentIt);
#if defined(_WIN32)
    if (lowerAscii(left) != lowerAscii(right)) {
      return false;
    }
#else
    if (left != right) {
      return false;
    }
#endif
  }
  return true;
}

std::optional<std::string> environment(std::string_view name) {
  const std::string key(name);
#if defined(_WIN32)
  char* buffer = nullptr;
  std::size_t size = 0;
  if (::_dupenv_s(&buffer, &size, key.c_str()) == 0 && buffer != nullptr) {
    std::string value(buffer);
    std::free(buffer);
    return value;
  }
  return std::nullopt;
#else
  if (const char* value = std::getenv(key.c_str()); value != nullptr) {
    return std::string(value);
  }
  return std::nullopt;
#endif
}

bool environmentFlag(std::string_view name) {
  const std::optional<std::string> value = environment(name);
  if (!value.has_value()) {
    return false;
  }
  const std::string lowered = lowerAscii(*value);
  return !(lowered.empty() || lowered == "0" || lowered == "false" ||
           lowered == "no" || lowered == "off");
}

std::vector<std::filesystem::path> searchPath() {
  std::vector<std::filesystem::path> result;
#if defined(_WIN32)
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::vector<std::string> entries;
  const std::optional<std::string> value = environment("PATH");
  if (!value.has_value() || !splitPathList(*value, separator, entries)) {
    return result;
  }
  result.reserve(entries.size());
  for (const std::string& entry : entries) {
    result.push_back(pathFromUtf8(entry));
  }
  return result;
}

std::optional<std::filesystem::path> findExecutable(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  const std::filesystem::path candidate = pathFromUtf8(name);
  const bool hasSeparator =
      name.find('/') != std::string_view::npos ||
      name.find('\\') != std::string_view::npos;
  std::vector<std::string> extensions;
  if (!candidate.has_extension()) {
#if defined(_WIN32)
    const std::optional<std::string> pathext = environment("PATHEXT");
    if (pathext.has_value()) {
      std::vector<std::string> parts;
      splitPathList(*pathext, ';', parts);
      extensions = std::move(parts);
    }
    if (extensions.empty()) {
      extensions = {".COM", ".EXE", ".BAT", ".CMD"};
    }
#else
    extensions.emplace_back("");
#endif
  } else {
    extensions.emplace_back("");
  }

  const auto usable = [](const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      return false;
    }
#if defined(_WIN32)
    return true;
#else
    return ::access(pathToUtf8(path).c_str(), X_OK) == 0;
#endif
  };

  if (hasSeparator) {
    for (const std::string& extension : extensions) {
      std::filesystem::path full = candidate;
      if (!extension.empty()) {
        full += extension;
      }
      if (usable(full)) {
        return full;
      }
    }
    return std::nullopt;
  }

  for (const std::filesystem::path& directory : searchPath()) {
    for (const std::string& extension : extensions) {
      std::filesystem::path full = directory / candidate;
      if (!extension.empty()) {
        full += extension;
      }
      if (usable(full)) {
        return full;
      }
    }
  }
  return std::nullopt;
}

std::filesystem::path homeDirectory() {
#if defined(_WIN32)
  if (const std::optional<std::string> profile = environment("USERPROFILE");
      profile.has_value() && !profile->empty()) {
    return pathFromUtf8(*profile);
  }
  const std::optional<std::string> drive = environment("HOMEDRIVE");
  const std::optional<std::string> path = environment("HOMEPATH");
  if (drive.has_value() && path.has_value()) {
    return pathFromUtf8(*drive + *path);
  }
#else
  if (const std::optional<std::string> home = environment("HOME");
      home.has_value() && !home->empty()) {
    return pathFromUtf8(*home);
  }
#endif
  std::error_code error;
  const std::filesystem::path current = std::filesystem::current_path(error);
  return error ? std::filesystem::path(".") : current;
}

std::filesystem::path configDirectory(std::string_view app) {
  const std::filesystem::path name = pathFromUtf8(app);
#if defined(_WIN32)
  if (const std::optional<std::string> appdata = environment("APPDATA");
      appdata.has_value() && !appdata->empty()) {
    return pathFromUtf8(*appdata) / name;
  }
  return homeDirectory() / "AppData" / "Roaming" / name;
#elif defined(__APPLE__)
  return homeDirectory() / "Library" / "Application Support" / name;
#else
  if (const std::optional<std::string> xdg = environment("XDG_CONFIG_HOME");
      xdg.has_value() && !xdg->empty()) {
    return pathFromUtf8(*xdg) / name;
  }
  return homeDirectory() / ".config" / name;
#endif
}

std::filesystem::path cacheDirectory(std::string_view app) {
  const std::filesystem::path name = pathFromUtf8(app);
#if defined(_WIN32)
  if (const std::optional<std::string> local = environment("LOCALAPPDATA");
      local.has_value() && !local->empty()) {
    return pathFromUtf8(*local) / name / "cache";
  }
  return homeDirectory() / "AppData" / "Local" / name / "cache";
#elif defined(__APPLE__)
  return homeDirectory() / "Library" / "Caches" / name;
#else
  if (const std::optional<std::string> xdg = environment("XDG_CACHE_HOME");
      xdg.has_value() && !xdg->empty()) {
    return pathFromUtf8(*xdg) / name;
  }
  return homeDirectory() / ".cache" / name;
#endif
}

std::filesystem::path dataDirectory(std::string_view app) {
  const std::filesystem::path name = pathFromUtf8(app);
#if defined(_WIN32)
  if (const std::optional<std::string> appdata = environment("APPDATA");
      appdata.has_value() && !appdata->empty()) {
    return pathFromUtf8(*appdata) / name;
  }
  return homeDirectory() / "AppData" / "Roaming" / name;
#elif defined(__APPLE__)
  return homeDirectory() / "Library" / "Application Support" / name;
#else
  if (const std::optional<std::string> xdg = environment("XDG_DATA_HOME");
      xdg.has_value() && !xdg->empty()) {
    return pathFromUtf8(*xdg) / name;
  }
  return homeDirectory() / ".local" / "share" / name;
#endif
}

std::filesystem::path temporaryDirectory() {
  for (const char* variable : {"TMPDIR", "TEMP", "TMP"}) {
    if (const std::optional<std::string> value = environment(variable);
        value.has_value() && !value->empty()) {
      return pathFromUtf8(*value);
    }
  }
  std::error_code error;
  const std::filesystem::path result =
      std::filesystem::temp_directory_path(error);
  if (!error) {
    return result;
  }
#if defined(_WIN32)
  return homeDirectory();
#else
  return std::filesystem::path("/tmp");
#endif
}

std::optional<std::string> readFile(const std::filesystem::path& path,
                                    std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot read '" + pathToUtf8(path) + "'";
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    error = "failed while reading '" + pathToUtf8(path) + "'";
    return std::nullopt;
  }
  return buffer.str();
}

bool writeFile(const std::filesystem::path& path, std::string_view content,
               std::string& error) {
  std::error_code directoryError;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
      error = "cannot create directory '" + pathToUtf8(path.parent_path()) +
              "': " + directoryError.message();
      return false;
    }
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "cannot write '" + pathToUtf8(path) + "'";
    return false;
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.flush();
  if (!output) {
    error = "failed while writing '" + pathToUtf8(path) + "'";
    return false;
  }
  return true;
}

bool writeFileAtomic(const std::filesystem::path& path,
                     std::string_view content, std::string& error) {
  std::filesystem::path temporary = path;
  temporary += ".tmp-" + randomSuffix();
  if (!writeFile(temporary, content, error)) {
    return false;
  }
  std::error_code renameError;
  std::filesystem::rename(temporary, path, renameError);
  if (renameError) {
    // Windows refuses to rename over an existing file on some file systems.
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    renameError.clear();
    std::filesystem::rename(temporary, path, renameError);
  }
  if (renameError) {
    error = "cannot replace '" + pathToUtf8(path) + "': " +
            renameError.message();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

bool ensureDirectory(const std::filesystem::path& path, std::string& error) {
  std::error_code createError;
  std::filesystem::create_directories(path, createError);
  if (createError) {
    error = "cannot create directory '" + pathToUtf8(path) + "': " +
            createError.message();
    return false;
  }
  return true;
}

bool removeTree(const std::filesystem::path& path, std::string& error) {
  std::error_code removeError;
  std::filesystem::remove_all(path, removeError);
  if (removeError) {
    error = "cannot remove '" + pathToUtf8(path) + "': " +
            removeError.message();
    return false;
  }
  return true;
}

bool copyTree(const std::filesystem::path& from,
              const std::filesystem::path& to, std::string& error) {
  std::error_code inspectError;
  if (!std::filesystem::exists(from, inspectError)) {
    error = "cannot copy '" + pathToUtf8(from) + "': source does not exist";
    return false;
  }
  if (!ensureDirectory(to, error)) {
    return false;
  }
  std::error_code iterateError;
  for (std::filesystem::recursive_directory_iterator iterator(
           from, std::filesystem::directory_options::skip_permission_denied,
           iterateError),
       end;
       iterator != end && !iterateError; iterator.increment(iterateError)) {
    const std::filesystem::path relative =
        iterator->path().lexically_relative(from);
    const std::filesystem::path destination = to / relative;
    std::error_code entryError;
    if (iterator->is_symlink(entryError)) {
      iterator.disable_recursion_pending();
      continue;  // vendored packages are plain trees; links would escape
    }
    if (iterator->is_directory(entryError)) {
      std::filesystem::create_directories(destination, entryError);
      if (entryError) {
        error = "cannot create '" + pathToUtf8(destination) + "': " +
                entryError.message();
        return false;
      }
      continue;
    }
    if (!iterator->is_regular_file(entryError)) {
      continue;
    }
    std::filesystem::copy_file(iterator->path(), destination,
                               std::filesystem::copy_options::overwrite_existing,
                               entryError);
    if (entryError) {
      error = "cannot copy '" + pathToUtf8(iterator->path()) + "': " +
              entryError.message();
      return false;
    }
  }
  if (iterateError) {
    error = "cannot copy '" + pathToUtf8(from) + "': " +
            iterateError.message();
    return false;
  }
  return true;
}

bool createTempDirectory(const std::filesystem::path& parent,
                         std::string_view prefix,
                         std::filesystem::path& created, std::string& error) {
  std::error_code directoryError;
  std::filesystem::create_directories(parent, directoryError);
  if (directoryError) {
    error = "cannot create directory '" + pathToUtf8(parent) + "': " +
            directoryError.message();
    return false;
  }
  for (int attempt = 0; attempt < 128; ++attempt) {
    const std::filesystem::path candidate =
        parent / (std::string(prefix) + randomSuffix());
    std::error_code createError;
    if (std::filesystem::create_directory(candidate, createError)) {
      created = candidate;
      return true;
    }
    if (createError && createError != std::errc::file_exists) {
      error = "cannot create temporary directory under '" +
              pathToUtf8(parent) + "': " + createError.message();
      return false;
    }
  }
  error = "cannot create a unique temporary directory under '" +
          pathToUtf8(parent) + "'";
  return false;
}

void enableUtf8Console() noexcept {
#if defined(_WIN32)
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
}

bool stdoutIsTerminal() noexcept {
#if defined(_WIN32)
  return ::_isatty(::_fileno(stdout)) != 0;
#else
  return ::isatty(::fileno(stdout)) != 0;
#endif
}

bool supportsColor() noexcept {
  if (environment("NO_COLOR").has_value() ||
      environment("COORD_NO_COLOR").has_value()) {
    return false;
  }
  if (!stdoutIsTerminal()) {
    return false;
  }
#if defined(_WIN32)
  HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD mode = 0;
  if (::GetConsoleMode(handle, &mode) == 0) {
    return false;
  }
  if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
    return true;
  }
  return ::SetConsoleMode(handle,
                          mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
  const char* term = std::getenv("TERM");
  return term != nullptr && std::strcmp(term, "dumb") != 0;
#endif
}

std::string quoteArgument(std::string_view argument) {
#if defined(_WIN32)
  if (argument.empty()) {
    return "\"\"";
  }
  if (argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
    return std::string(argument);
  }
  std::string quoted = "\"";
  std::size_t backslashes = 0;
  for (const char character : argument) {
    if (character == '\\') {
      ++backslashes;
      continue;
    }
    if (character == '"') {
      quoted.append(backslashes * 2 + 1, '\\');
      quoted.push_back('"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, '\\');
    backslashes = 0;
    quoted.push_back(character);
  }
  quoted.append(backslashes * 2, '\\');
  quoted.push_back('"');
  return quoted;
#else
  if (argument.empty()) {
    return "''";
  }
  const bool needsQuoting =
      argument.find_first_of(" \t\n\v'\"\\$`!&|;<>()*?[]{}#~=") !=
      std::string_view::npos;
  if (!needsQuoting) {
    return std::string(argument);
  }
  std::string quoted = "'";
  for (const char character : argument) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
#endif
}

std::string commandLine(const std::vector<std::string>& arguments) {
  std::string result;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i != 0) {
      result.push_back(' ');
    }
    result += quoteArgument(arguments[i]);
  }
  return result;
}

std::string quoteShellArgument(std::string_view argument) {
#if defined(_WIN32)
  if (argument.empty()) {
    return "\"\"";
  }
  if (argument.find_first_of(" \t\n\v&|<>^()%\"") == std::string_view::npos) {
    return std::string(argument);
  }
  std::string quoted = "\"";
  for (const char character : argument) {
    if (character == '"') {
      quoted += "\"\"";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('"');
  return quoted;
#else
  return quoteArgument(argument);
#endif
}

std::string shellCommandLine(const std::vector<std::string>& arguments) {
  std::string result;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i != 0) {
      result.push_back(' ');
    }
    result += quoteShellArgument(arguments[i]);
  }
  return result;
}

#ifdef _WIN32

ProcessResult run(const std::vector<std::string>& arguments,
                  const ProcessOptions& options) {
  ProcessResult result;
  result.command = commandLine(arguments);
  if (arguments.empty()) {
    result.failure = "no executable given";
    return result;
  }

  std::vector<std::string> effective = arguments;
  std::string application = arguments.front();
  std::string command = result.command;
  if (endsWithIgnoreCase(application, ".bat") ||
      endsWithIgnoreCase(application, ".cmd")) {
    const std::optional<std::string> shell = environment("COMSPEC");
    application = shell.has_value() && !shell->empty() ? *shell : "cmd.exe";
    command = "/d /s /c \"" + result.command + "\"";
    result.command = application + " " + command;
  }

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;

  HandleCloser outRead;
  HandleCloser outWrite;
  HandleCloser errRead;
  HandleCloser errWrite;
  HandleCloser inRead;
  HandleCloser inWrite;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (options.captureOutput || !options.input.empty()) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    if (options.captureOutput) {
      if (::CreatePipe(&outRead.handle, &outWrite.handle, &attributes, 0) == 0 ||
          ::CreatePipe(&errRead.handle, &errWrite.handle, &attributes, 0) == 0) {
        result.failure =
            "cannot create pipes: " + windowsErrorMessage(::GetLastError());
        return result;
      }
      ::SetHandleInformation(outRead.handle, HANDLE_FLAG_INHERIT, 0);
      ::SetHandleInformation(errRead.handle, HANDLE_FLAG_INHERIT, 0);
      startup.hStdOutput = outWrite.handle;
      startup.hStdError = errWrite.handle;
    } else {
      startup.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
      startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    }
    if (!options.input.empty()) {
      if (::CreatePipe(&inRead.handle, &inWrite.handle, &attributes, 0) == 0) {
        result.failure =
            "cannot create stdin pipe: " + windowsErrorMessage(::GetLastError());
        return result;
      }
      ::SetHandleInformation(inWrite.handle, HANDLE_FLAG_INHERIT, 0);
      startup.hStdInput = inRead.handle;
    } else {
      startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    }
  }

  std::vector<wchar_t> environmentBlock;
  std::string environmentError;
  if (!buildEnvironmentBlock(options.environment, environmentBlock,
                             environmentError)) {
    result.failure = environmentError;
    return result;
  }

  // Resolve the program ourselves so `cl`, `g++` or `curl` are found on PATH;
  // CreateProcessW does not search PATH when lpApplicationName is non-null.
  std::wstring applicationWide;
  if (const std::optional<std::filesystem::path> located =
          findExecutable(application);
      located.has_value()) {
    applicationWide = widen(pathToUtf8(*located));
  }
  std::wstring commandWide = widen(command);
  if (commandWide.empty()) {
    result.failure = "cannot encode the command line as UTF-16";
    return result;
  }
  std::wstring workingDirectory;
  if (!options.workingDirectory.empty()) {
    workingDirectory = widen(pathToUtf8(options.workingDirectory));
  }

  PROCESS_INFORMATION process{};
  const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
  if (::CreateProcessW(
          applicationWide.empty() ? nullptr : applicationWide.c_str(),
          commandWide.data(), nullptr, nullptr, TRUE, creationFlags,
          environmentBlock.empty() ? nullptr : environmentBlock.data(),
          workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
          &startup, &process) == 0) {
    result.failure = "cannot start '" + application + "': " +
                     windowsErrorMessage(::GetLastError());
    return result;
  }
  result.started = true;
  HandleCloser processHandle{process.hProcess};
  HandleCloser threadHandle{process.hThread};

  // Parent copies of the child ends must be closed before joining readers.
  outWrite.reset();
  errWrite.reset();
  inRead.reset();

  std::thread outReader;
  std::thread errReader;
  std::string rawOut;
  std::string rawErr;
  if (options.captureOutput) {
    outReader = std::thread([&] { rawOut = readFromHandle(outRead.handle); });
    errReader = std::thread([&] { rawErr = readFromHandle(errRead.handle); });
  }

  if (!options.input.empty() && inWrite.handle != INVALID_HANDLE_VALUE) {
    std::size_t written = 0;
    while (written < options.input.size()) {
      DWORD chunk = 0;
      if (::WriteFile(inWrite.handle, options.input.data() + written,
                      static_cast<DWORD>(options.input.size() - written),
                      &chunk, nullptr) == 0) {
        break;
      }
      written += chunk;
    }
    result.inputSize = written;
    inWrite.reset();  // the child must see EOF
  }

  DWORD waitResult = WAIT_OBJECT_0;
  if (options.timeout.count() > 0) {
    waitResult = ::WaitForSingleObject(
        process.hProcess,
        static_cast<DWORD>(std::min<long long>(options.timeout.count(),
                                               0xFFFFFFFFLL)));
  } else {
    ::WaitForSingleObject(process.hProcess, INFINITE);
  }
  if (waitResult == WAIT_TIMEOUT) {
    ::TerminateProcess(process.hProcess, 1);
    ::WaitForSingleObject(process.hProcess, 5000);
    result.timedOut = true;
    result.failure = "process timed out after " +
                     std::to_string(options.timeout.count()) + " ms";
  }

  if (outReader.joinable()) {
    outReader.join();
  }
  if (errReader.joinable()) {
    errReader.join();
  }

  DWORD exitCode = 1;
  ::GetExitCodeProcess(process.hProcess, &exitCode);
  result.exitCode = static_cast<int>(exitCode);
  if (options.captureOutput) {
    result.out = decodeProcessOutput(rawOut);
    result.err = decodeProcessOutput(rawErr);
  }
  if (result.timedOut) {
    result.exitCode = -1;
  }
  return result;
}

ProcessResult runShell(std::string_view command,
                       const ProcessOptions& options) {
  const std::optional<std::string> shell = environment("COMSPEC");
  const std::string executable =
      shell.has_value() && !shell->empty() ? *shell : "cmd.exe";
  ProcessOptions adjusted = options;
  return run({executable, "/d", "/s", "/c", std::string(command)}, adjusted);
}

#else  // POSIX

ProcessResult run(const std::vector<std::string>& arguments,
                  const ProcessOptions& options) {
  ProcessResult result;
  result.command = commandLine(arguments);
  if (arguments.empty()) {
    result.failure = "no executable given";
    return result;
  }

  const bool capture = options.captureOutput;
  int stdinPipe[2] = {-1, -1};
  int stdoutPipe[2] = {-1, -1};
  int stderrPipe[2] = {-1, -1};
  const auto closePipe = [](int pipe[2]) {
    if (pipe[0] >= 0) {
      ::close(pipe[0]);
      pipe[0] = -1;
    }
    if (pipe[1] >= 0) {
      ::close(pipe[1]);
      pipe[1] = -1;
    }
  };
  if (capture) {
    if (::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0 ||
        ::pipe(stdinPipe) != 0) {
      result.failure = std::string("cannot create pipes: ") +
                       std::strerror(errno);
      closePipe(stdoutPipe);
      closePipe(stderrPipe);
      closePipe(stdinPipe);
      return result;
    }
  }
  // The child reports exec failures through this close-on-exec pipe, so a
  // missing program is reported as a spawn failure (like CreateProcessW)
  // instead of a mysterious exit code 127.
  int execPipe[2] = {-1, -1};
  if (::pipe(execPipe) != 0) {
    result.failure = std::string("cannot create exec pipe: ") +
                     std::strerror(errno);
    closePipe(stdoutPipe);
    closePipe(stderrPipe);
    closePipe(stdinPipe);
    return result;
  }
  ::fcntl(execPipe[1], F_SETFD, FD_CLOEXEC);

  const pid_t pid = ::fork();
  if (pid < 0) {
    result.failure = std::string("cannot fork: ") + std::strerror(errno);
    closePipe(stdoutPipe);
    closePipe(stderrPipe);
    closePipe(stdinPipe);
    ::close(execPipe[0]);
    ::close(execPipe[1]);
    return result;
  }
  if (pid == 0) {
    ::close(execPipe[0]);
    ::setpgid(0, 0);
    if (capture) {
      ::dup2(stdinPipe[0], STDIN_FILENO);
      ::dup2(stdoutPipe[1], STDOUT_FILENO);
      ::dup2(stderrPipe[1], STDERR_FILENO);
      ::close(stdinPipe[0]);
      ::close(stdinPipe[1]);
      ::close(stdoutPipe[0]);
      ::close(stdoutPipe[1]);
      ::close(stderrPipe[0]);
      ::close(stderrPipe[1]);
    }
    if (!options.workingDirectory.empty()) {
      if (::chdir(pathToUtf8(options.workingDirectory).c_str()) != 0) {
        const int chdirError = errno;
        const ssize_t ignored =
            ::write(execPipe[1], &chdirError, sizeof(chdirError));
        (void)ignored;
        ::_exit(126);
      }
    }
    for (const auto& [name, value] : options.environment) {
      ::setenv(name.c_str(), value.c_str(), 1);
    }
    std::vector<char*> raw;
    raw.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) {
      raw.push_back(const_cast<char*>(argument.c_str()));
    }
    raw.push_back(nullptr);
    ::execvp(raw[0], raw.data());
    const int execError = errno;
    const ssize_t ignored = ::write(execPipe[1], &execError, sizeof(execError));
    (void)ignored;
    ::_exit(127);
  }

  ::close(execPipe[1]);
  execPipe[1] = -1;
  int execError = 0;
  ssize_t execRead = 0;
  do {
    execRead = ::read(execPipe[0], &execError, sizeof(execError));
  } while (execRead < 0 && errno == EINTR);
  ::close(execPipe[0]);
  execPipe[0] = -1;
  if (execRead == static_cast<ssize_t>(sizeof(execError))) {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    closePipe(stdinPipe);
    closePipe(stdoutPipe);
    closePipe(stderrPipe);
    result.exitCode = 127;
    result.failure = "cannot start '" + arguments.front() + "': " +
                     std::strerror(execError);
    return result;
  }

  ::setpgid(pid, pid);
  // Parent keeps only the ends it uses: stdout/stderr read ends and the stdin
  // write end. Closing the read ends here would make poll() return POLLNVAL.
  const int stdoutFd = stdoutPipe[0];
  const int stderrFd = stderrPipe[0];
  const int stdinFd = stdinPipe[1];
  if (stdoutPipe[1] >= 0) {
    ::close(stdoutPipe[1]);
    stdoutPipe[1] = -1;
  }
  if (stderrPipe[1] >= 0) {
    ::close(stderrPipe[1]);
    stderrPipe[1] = -1;
  }
  if (stdinPipe[0] >= 0) {
    ::close(stdinPipe[0]);
    stdinPipe[0] = -1;
  }
  stdoutPipe[0] = -1;
  stderrPipe[0] = -1;
  stdinPipe[1] = -1;
  if (!capture) {
    int status = 0;
    if (options.timeout.count() > 0) {
      const auto deadline = std::chrono::steady_clock::now() + options.timeout;
      for (;;) {
        const pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
          break;
        }
        if (waited < 0 && errno != EINTR) {
          result.failure = std::string("cannot wait for child: ") +
                           std::strerror(errno);
          return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          ::kill(-pid, SIGKILL);
          ::waitpid(pid, &status, 0);
          result.started = true;
          result.timedOut = true;
          result.exitCode = -1;
          result.failure = "process timed out after " +
                           std::to_string(options.timeout.count()) + " ms";
          return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    } else {
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
    result.started = true;
    if (WIFEXITED(status)) {
      result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      result.exitCode = 128 + WTERMSIG(status);
      result.failure = "terminated by signal " +
                       std::to_string(WTERMSIG(status));
    }
    return result;
  }

  if (options.input.empty()) {
    if (stdinFd >= 0) {
      ::close(stdinFd);
    }
  }

  const auto makeNonBlocking = [](int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  };
  makeNonBlocking(stdoutFd);
  makeNonBlocking(stderrFd);

  bool stdoutOpen = true;
  bool stderrOpen = true;
  bool stdinOpen = options.input.empty() ? false : true;
  std::size_t inputWritten = 0;
  const auto deadline = options.timeout.count() > 0
                            ? std::optional(
                                  std::chrono::steady_clock::now() +
                                  options.timeout)
                            : std::nullopt;
  bool timedOut = false;
  std::array<char, 8192> buffer{};

  while (stdoutOpen || stderrOpen || stdinOpen) {
    struct pollfd descriptors[3] = {};
    int count = 0;
    int stdoutIndex = -1;
    int stderrIndex = -1;
    int stdinIndex = -1;
    if (stdoutOpen) {
      stdoutIndex = count;
      descriptors[count].fd = stdoutFd;
      descriptors[count].events = POLLIN;
      ++count;
    }
    if (stderrOpen) {
      stderrIndex = count;
      descriptors[count].fd = stderrFd;
      descriptors[count].events = POLLIN;
      ++count;
    }
    if (stdinOpen) {
      stdinIndex = count;
      descriptors[count].fd = stdinFd;
      descriptors[count].events = POLLOUT;
      ++count;
    }
    int timeoutMs = 1000;
    if (deadline.has_value()) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          *deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0) {
        timedOut = true;
        break;
      }
      timeoutMs = static_cast<int>(
          std::min<long long>(remaining.count(), 1000));
    }
    const int ready = ::poll(descriptors, static_cast<nfds_t>(count), timeoutMs);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (ready == 0) {
      continue;
    }
    if (stdoutIndex >= 0 &&
        (descriptors[stdoutIndex].revents &
         (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
      const ssize_t read = ::read(stdoutFd, buffer.data(), buffer.size());
      if (read > 0) {
        result.out.append(buffer.data(), static_cast<std::size_t>(read));
      } else if (read == 0 ||
                 (read < 0 && errno != EAGAIN && errno != EINTR)) {
        stdoutOpen = false;
        ::close(stdoutFd);
      }
    }
    if (stderrIndex >= 0 &&
        (descriptors[stderrIndex].revents &
         (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
      const ssize_t read = ::read(stderrFd, buffer.data(), buffer.size());
      if (read > 0) {
        result.err.append(buffer.data(), static_cast<std::size_t>(read));
      } else if (read == 0 ||
                 (read < 0 && errno != EAGAIN && errno != EINTR)) {
        stderrOpen = false;
        ::close(stderrFd);
      }
    }
    if (stdinIndex >= 0 &&
        (descriptors[stdinIndex].revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
      const std::size_t remaining = options.input.size() - inputWritten;
      if (remaining == 0 ||
          (descriptors[stdinIndex].revents & (POLLERR | POLLHUP)) != 0) {
        stdinOpen = false;
        ::close(stdinFd);
      } else {
        const ssize_t written =
            ::write(stdinFd, options.input.data() + inputWritten, remaining);
        if (written > 0) {
          inputWritten += static_cast<std::size_t>(written);
        } else if (written < 0 && errno != EAGAIN && errno != EINTR) {
          stdinOpen = false;
          ::close(stdinFd);
        }
      }
    }
    if (timedOut) {
      break;
    }
  }
  if (stdinOpen) {
    ::close(stdinFd);
  }
  if (stdoutOpen) {
    ::close(stdoutFd);
  }
  if (stderrOpen) {
    ::close(stderrFd);
  }
  if (timedOut) {
    ::kill(-pid, SIGKILL);
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  result.started = true;
  result.inputSize = inputWritten;
  if (timedOut) {
    result.timedOut = true;
    result.exitCode = -1;
    result.failure = "process timed out after " +
                     std::to_string(options.timeout.count()) + " ms";
    return result;
  }
  if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exitCode = 128 + WTERMSIG(status);
    result.failure =
        "terminated by signal " + std::to_string(WTERMSIG(status));
  }
  return result;
}

ProcessResult runShell(std::string_view command,
                       const ProcessOptions& options) {
  ProcessOptions adjusted = options;
  return run({"/bin/sh", "-c", std::string(command)}, adjusted);
}

#endif

}  // namespace coord::platform
