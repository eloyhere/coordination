#pragma once

// `coord.lock`: a deterministic record of the resolved dependency graph.
//
// The lockfile is intentionally simple TOML so it can be reviewed in a pull
// request. It pins the exact version, source, registry and archive checksum of
// every registry dependency, and records path dependencies relative to the
// project root.

#include "coord/modules/manifest.h"
#include "coord/modules/semver.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace coord::package {

enum class LockSource { Registry, Path };

[[nodiscard]] std::string_view lockSourceName(LockSource source);

struct LockedPackage {
  std::string name;
  semver::Version version;
  LockSource source = LockSource::Registry;
  std::string registry;  // registry name
  std::string url;       // resolved registry base URL
  std::string checksum;  // "sha256:..."
  std::string path;      // path dependency, relative to the project root
  std::vector<std::string> dependencies;  // "name version" entries
};

struct Lockfile {
  int format = 1;
  std::string manifestHash;
  std::vector<LockedPackage> packages;

  [[nodiscard]] const LockedPackage* find(std::string_view name) const;
  [[nodiscard]] std::string fingerprint() const;

  [[nodiscard]] std::string serialize() const;
  [[nodiscard]] static std::optional<Lockfile> parse(std::string_view text,
                                                     std::string& error);
  [[nodiscard]] static std::optional<Lockfile> load(
      const std::filesystem::path& path, std::string& error);
  [[nodiscard]] bool save(const std::filesystem::path& path,
                          std::string& error) const;
};

/// Stable hash of everything in a manifest that can change resolution.
[[nodiscard]] std::string manifestFingerprint(const Manifest& manifest);

}  // namespace coord::package
