#pragma once

// Dependency resolution.
//
// Path dependencies are read from disk; registry dependencies are resolved
// against registry indexes with full backtracking. Locked versions are tried
// first unless the package is being updated. The result is deterministic
// (packages sorted by name, versions chosen newest-first).

#include "coord/modules/lockfile.h"
#include "coord/modules/manifest.h"
#include "coord/modules/registry.h"
#include "coord/modules/semver.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace coord::package {

struct ResolvedPackage {
  std::string name;
  semver::Version version;
  LockSource source = LockSource::Registry;
  std::filesystem::path path;  // absolute, path dependencies only
  std::string pathSpec;        // relative to the project root
  std::string registry;
  std::string mirror;
  std::string url;
  std::string checksum;
  std::vector<std::string> dependencies;  // resolved package names
  bool direct = false;
  bool dev = false;
};

struct Resolution {
  bool ok = false;
  std::string error;
  std::vector<ResolvedPackage> packages;
  std::vector<std::string> warnings;

  [[nodiscard]] const ResolvedPackage* find(std::string_view name) const;
  [[nodiscard]] std::string fingerprint() const;
};

struct ResolveOptions {
  bool includeDev = true;
  bool includeOptional = false;
  bool preferLocked = true;
  bool updateAll = false;
  std::vector<std::string> update;
};

class Resolver {
 public:
  using IndexProvider = std::function<std::optional<IndexEntry>(
      const std::string& registry, const std::string& package,
      const std::string& mirror, std::string& error)>;
  using RegistryLookup =
      std::function<std::optional<RegistryEntry>(const std::string& name)>;

  explicit Resolver(IndexProvider indexProvider,
                    RegistryLookup registryLookup = {});

  [[nodiscard]] Resolution resolve(const Manifest& root,
                                   const Lockfile* lock,
                                   const ResolveOptions& options);

 private:
  IndexProvider indexProvider_;
  RegistryLookup registryLookup_;
};

/// Converts a resolution into a lockfile (registry URLs are already resolved).
[[nodiscard]] Lockfile lockfileFromResolution(const Manifest& root,
                                              const Resolution& resolution);

}  // namespace coord::package
