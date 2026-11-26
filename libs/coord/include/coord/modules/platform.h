#pragma once

// Cross-platform host abstraction used by the driver, the package manager and
// the mirror manager.
//
// Design rules:
//   * every function is available on Windows, macOS, Linux and the BSDs;
//   * paths crossing a module boundary are UTF-8 (`std::filesystem::path` is
//     used only where the operating system needs a native representation);
//   * child processes are spawned with an argument vector, never through a
//     shell, so quoting cannot corrupt compiler invocations;
//   * no function throws: failures are reported as values.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::platform {

enum class OperatingSystem { Windows, MacOs, Linux, FreeBsd, Other };
enum class Architecture { X86, X64, Arm, Arm64, RiscV64, Unknown };

[[nodiscard]] OperatingSystem operatingSystem() noexcept;
[[nodiscard]] Architecture architecture() noexcept;

/// Stable lower-case names: "windows", "macos", "linux", "freebsd", "other"
/// and "x86", "x86_64", "arm", "aarch64", "riscv64", "unknown".
[[nodiscard]] std::string_view operatingSystemName(OperatingSystem value) noexcept;
[[nodiscard]] std::string_view architectureName(Architecture value) noexcept;
[[nodiscard]] std::string operatingSystemName();
[[nodiscard]] std::string architectureName();

/// Rust-style target triple, e.g. `x86_64-pc-windows-msvc`,
/// `aarch64-apple-darwin`, `x86_64-unknown-linux-gnu`.
[[nodiscard]] std::string targetTriple();
/// Accepts triples such as `x86_64-pc-windows-msvc`,
/// `aarch64-apple-darwin`, `armv7-unknown-linux-gnueabihf`.
[[nodiscard]] bool isKnownTargetTriple(std::string_view triple);
/// "windows x86_64 (x86_64-pc-windows-msvc)"
[[nodiscard]] std::string describeHost();
[[nodiscard]] bool isLittleEndian() noexcept;

// ---------------------------------------------------------------- file names

[[nodiscard]] std::string_view executableSuffix() noexcept;   // ".exe" or ""
[[nodiscard]] std::string_view objectFileSuffix() noexcept;   // ".obj" or ".o"
[[nodiscard]] std::string_view staticLibraryPrefix() noexcept;  // "" or "lib"
[[nodiscard]] std::string_view staticLibrarySuffix() noexcept;  // ".lib" or ".a"
[[nodiscard]] std::string_view sharedLibrarySuffix() noexcept;  // ".dll", ".so", ".dylib"
[[nodiscard]] std::string_view archiveSuffix() noexcept;        // ".zip" or ".tar.gz"

[[nodiscard]] std::string executableFileName(std::string_view stem);
[[nodiscard]] std::string objectFileName(std::string_view stem);
[[nodiscard]] std::string staticLibraryFileName(std::string_view stem);
[[nodiscard]] std::string sharedLibraryFileName(std::string_view stem);

// ------------------------------------------------------------- paths, listing

/// UTF-8 representation that is safe to print and to send over the wire.
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);
/// Replace '\\' with '/' (for stable diagnostics and lockfile entries).
[[nodiscard]] std::string normalizeSlashes(std::string text);
/// Comparison key for paths: absolute, normalized, forward slashes.
[[nodiscard]] std::string pathKey(const std::filesystem::path& path);
/// `base / relative` when relative is relative, otherwise `relative`.
[[nodiscard]] std::filesystem::path resolvePath(const std::filesystem::path& base,
                                                std::string_view relative);
/// True when `child` is `parent` or lives below it (both are resolved first).
[[nodiscard]] bool isWithin(const std::filesystem::path& child,
                            const std::filesystem::path& parent);

// ---------------------------------------------------------------- environment

[[nodiscard]] std::optional<std::string> environment(std::string_view name);
/// True for "1", "true", "yes", "on"; false for unset, "", "0", "false",
/// "no", "off".
[[nodiscard]] bool environmentFlag(std::string_view name);
[[nodiscard]] std::vector<std::filesystem::path> searchPath();
[[nodiscard]] std::optional<std::filesystem::path> findExecutable(
    std::string_view name);
[[nodiscard]] std::filesystem::path homeDirectory();
/// `%APPDATA%\app`, `~/Library/Application Support/app`, `$XDG_CONFIG_HOME/app`.
[[nodiscard]] std::filesystem::path configDirectory(std::string_view app);
/// `%LOCALAPPDATA%\app\cache`, `~/Library/Caches/app`, `$XDG_CACHE_HOME/app`.
[[nodiscard]] std::filesystem::path cacheDirectory(std::string_view app);
/// `%APPDATA%\app`, `~/Library/Application Support/app`, `$XDG_DATA_HOME/app`.
[[nodiscard]] std::filesystem::path dataDirectory(std::string_view app);
[[nodiscard]] std::filesystem::path temporaryDirectory();

// ------------------------------------------------------------------- file I/O

[[nodiscard]] std::optional<std::string> readFile(
    const std::filesystem::path& path, std::string& error);
/// Write through a temporary file in the same directory, then rename.
[[nodiscard]] bool writeFileAtomic(const std::filesystem::path& path,
                                   std::string_view content, std::string& error);
[[nodiscard]] bool writeFile(const std::filesystem::path& path,
                             std::string_view content, std::string& error);
[[nodiscard]] bool ensureDirectory(const std::filesystem::path& path,
                                   std::string& error);
[[nodiscard]] bool removeTree(const std::filesystem::path& path,
                              std::string& error);
[[nodiscard]] bool copyTree(const std::filesystem::path& from,
                            const std::filesystem::path& to, std::string& error);
[[nodiscard]] bool createTempDirectory(const std::filesystem::path& parent,
                                       std::string_view prefix,
                                       std::filesystem::path& created,
                                       std::string& error);

// --------------------------------------------------------------------- console

/// Switches the Windows console to UTF-8; harmless elsewhere.
void enableUtf8Console() noexcept;
[[nodiscard]] bool stdoutIsTerminal() noexcept;
[[nodiscard]] bool supportsColor() noexcept;

// ------------------------------------------------------------------- processes

struct ProcessOptions {
  std::filesystem::path workingDirectory;
  /// 0 keeps waiting forever; otherwise the child is terminated on expiry.
  std::chrono::milliseconds timeout{0};
  /// Extra variables merged over the inherited environment.
  std::map<std::string, std::string> environment;
  /// Bytes piped to the child's standard input (capture mode only).
  std::string input;
  /// Capture stdout/stderr into the result instead of inheriting the console.
  bool captureOutput = true;
};

struct ProcessResult {
  bool started = false;
  bool timedOut = false;
  int exitCode = -1;
  std::string out;
  std::string err;
  /// Raw bytes written to the child's stdin after stripping the BOM.
  std::size_t inputSize = 0;
  std::string failure;   // spawn failure / timeout / signal description
  std::string command;   // printable command line for diagnostics

  [[nodiscard]] bool ok() const {
    return started && !timedOut && exitCode == 0;
  }
};

/// Runs `arguments[0]` (looked up on PATH) with the remaining arguments.
[[nodiscard]] ProcessResult run(const std::vector<std::string>& arguments,
                                const ProcessOptions& options = {});
/// Runs a command line through the host shell (`cmd.exe /c` on Windows).
[[nodiscard]] ProcessResult runShell(std::string_view command,
                                     const ProcessOptions& options = {});

/// Quote one argument for the host command line (`run`).
[[nodiscard]] std::string quoteArgument(std::string_view argument);
[[nodiscard]] std::string commandLine(const std::vector<std::string>& arguments);
/// Quote one argument for the host shell (`runShell`).
[[nodiscard]] std::string quoteShellArgument(std::string_view argument);
[[nodiscard]] std::string shellCommandLine(
    const std::vector<std::string>& arguments);

}  // namespace coord::platform
