#pragma once

// Registry and mirror configuration.
//
// A *registry* is a package repository endpoint (`https://...`, `file://...`
// or a local directory). A *mirror* is an alternate endpoint for the same
// registry, usually chosen for region or speed. Mirrors are tried in priority
// order with health-based failover.
//
// Configuration layers (later wins):
//   1. built-in catalog
//   2. user config    (<config>/coord/config.toml)
//   3. project config ([registries] / [[mirrors]] in coordination.toml)
//   4. environment    (COORD_REGISTRY, COORD_MIRROR, COORD_OFFLINE)
//   5. command line   (--registry / --mirror)

#include "coord/modules/manifest.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::package {

struct MirrorConfig {
  std::string defaultRegistry = "official";
  std::string defaultMirror;
  bool offline = false;
  int timeoutMs = 15000;
  std::vector<RegistryEntry> registries;
  std::vector<MirrorEntry> mirrors;
};

struct MirrorProbe {
  bool reachable = false;
  long long latencyMs = 0;
  std::string detail;
};

class MirrorManager {
 public:
  MirrorManager();

  [[nodiscard]] static MirrorManager builtins();

  /// Merges `<config>/coord/config.toml` when it exists.
  [[nodiscard]] bool loadUserConfig(std::string& error);
  /// Merges `[registries]`, `[[mirrors]]` and package defaults.
  [[nodiscard]] bool loadProjectConfig(const Manifest& manifest,
                                       std::string& error);
  /// Applies COORD_REGISTRY, COORD_MIRROR, COORD_OFFLINE and COORD_TIMEOUT_MS.
  void applyEnvironment();

  /// `--mirror <name|url>`; enables a named mirror when it was disabled.
  [[nodiscard]] bool selectMirror(std::string_view nameOrUrl,
                                  std::string& error);
  /// `--registry <name|url>`.
  [[nodiscard]] bool selectRegistry(std::string_view nameOrUrl,
                                    std::string& error);

  [[nodiscard]] const MirrorConfig& config() const { return config_; }
  [[nodiscard]] MirrorConfig& config() { return config_; }

  [[nodiscard]] std::optional<RegistryEntry> registry(
      std::string_view nameOrUrl) const;
  [[nodiscard]] const MirrorEntry* mirror(std::string_view name) const;

  /// Ordered endpoints to try for a registry: explicit preference first, then
  /// enabled mirrors by priority, then the registry URL itself. Mirrors that
  /// failed recently are skipped while their cool-down is active.
  [[nodiscard]] std::vector<MirrorEntry> candidates(
      std::string_view registryNameOrUrl,
      std::string_view preferredMirror = {}) const;

  [[nodiscard]] MirrorProbe probe(
      const MirrorEntry& mirror,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

  void recordFailure(const MirrorEntry& mirror);
  void recordSuccess(const MirrorEntry& mirror, long long latencyMs);

  [[nodiscard]] bool isSkipped(const MirrorEntry& mirror) const;

  [[nodiscard]] std::filesystem::path configPath() const;
  [[nodiscard]] std::filesystem::path cacheRoot() const;
  [[nodiscard]] std::filesystem::path healthCachePath() const;

  void loadHealthCache();
  [[nodiscard]] bool saveHealthCache(std::string& error) const;

  /// Writes the whole configuration (used by `coorc mirror add/remove/use`).
  [[nodiscard]] bool saveUserConfig(std::string& error) const;

  /// Renders the effective configuration as TOML (for `mirror show`).
  [[nodiscard]] std::string describe() const;

 private:
  struct HealthEntry {
    std::string url;
    int failures = 0;
    std::int64_t lastFailure = 0;
    std::int64_t lastSuccess = 0;
    long long latencyMs = 0;
  };

  void mergeMirror(const MirrorEntry& mirror);
  void mergeRegistry(const RegistryEntry& registry);

  MirrorConfig config_;
  std::map<std::string, HealthEntry> health_;
};

[[nodiscard]] std::string normalizeRegistryUrl(std::string_view url);
[[nodiscard]] bool isSupportedRegistryUrl(std::string_view url);

}  // namespace coord::package
