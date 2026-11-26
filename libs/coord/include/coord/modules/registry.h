#pragma once

// Registry client.
//
// A registry is a directory tree (local or served over HTTP):
//
//   index.toml                       optional catalog used by `coorc search`
//   index/<package>.toml             versions of one package
//   packages/<package>/<version>.tar.gz
//   health                           optional mirror health endpoint
//
// `index/<package>.toml`:
//
//   name = "math"
//   description = "small math helpers"
//   [[version]]
//   version = "1.2.0"
//   checksum = "sha256:..."
//   yanked = false
//   dependencies = ["core ^0.3"]
//   archive = "packages/math/1.2.0.tar.gz"   # optional override
//
// The client tries the configured mirrors in order, verifies SHA-256
// checksums, caches indexes and extracted sources, and falls back to stale
// caches when the network is unavailable (unless running strictly offline).

#include "coord/modules/manifest.h"
#include "coord/modules/mirror.h"
#include "coord/modules/semver.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::package {

struct IndexVersion {
  semver::Version version;
  std::string checksum;
  std::string archive;
  bool yanked = false;
  std::vector<std::string> dependencies;  // "name requirement" entries
};

struct IndexEntry {
  std::string name;
  std::string description;
  std::vector<IndexVersion> versions;

  [[nodiscard]] const IndexVersion* find(const semver::Version& version) const;
  [[nodiscard]] std::vector<const IndexVersion*> matching(
      const semver::Requirement& requirement, bool includeYanked = false) const;
};

struct CatalogEntry {
  std::string name;
  std::string description;
  std::string latest;
};

struct PackageSource {
  std::filesystem::path directory;  // extracted package root
  std::filesystem::path archive;    // cached archive
  std::string checksum;
  std::string url;
  bool cached = false;  // served from the local cache, not the network
};

struct RegistryOptions {
  bool offline = false;
  bool refreshIndex = false;
  std::chrono::milliseconds timeout{15000};
  std::chrono::seconds maxIndexAge{3600};
  std::filesystem::path cacheRoot;
  std::string preferredMirror;
  bool verbose = false;
  bool requireChecksum = false;
  bool allowInsecureHttp = false;
};

class RegistryClient {
 public:
  RegistryClient(MirrorManager& mirrors, RegistryOptions options);

  [[nodiscard]] std::optional<IndexEntry> index(
      std::string_view registryName, std::string_view packageName,
      std::string& error, std::string_view preferredMirror = {});

  [[nodiscard]] std::optional<PackageSource> fetch(
      std::string_view registryName, std::string_view packageName,
      const IndexVersion& version, std::string& error);

  [[nodiscard]] std::optional<CatalogEntry> packageMetadata(
      std::string_view registryName, std::string_view packageName,
      std::string& error);

  [[nodiscard]] std::vector<CatalogEntry> search(
      std::string_view registryName, std::string_view term,
      std::string& error);

  [[nodiscard]] const std::vector<std::string>& warnings() const {
    return warnings_;
  }
  void clearWarnings() { warnings_.clear(); }

  [[nodiscard]] const RegistryOptions& options() const { return options_; }
  [[nodiscard]] MirrorManager& mirrors() { return mirrors_; }

 private:
  [[nodiscard]] std::filesystem::path registryCacheDirectory(
      std::string_view registryName) const;
  [[nodiscard]] std::filesystem::path cacheFile(std::string_view registryName,
                                                std::string_view relative) const;

  [[nodiscard]] bool fetchText(std::string_view registryName,
                               std::string_view relative,
                               std::string_view preferredMirror,
                               std::string& body, std::string& error);

  [[nodiscard]] std::optional<std::string> readCache(
      const std::filesystem::path& path, bool enforceAge) const;
  void writeCache(const std::filesystem::path& path, std::string_view body);

  [[nodiscard]] std::optional<std::string> downloadArchive(
      std::string_view registryName, std::string_view packageName,
      const IndexVersion& version, const std::filesystem::path& destination,
      std::string& error);

  MirrorManager& mirrors_;
  RegistryOptions options_;
  std::vector<std::string> warnings_;
};

/// Parses a dependency entry from an index file (`name`, `name@^1.2`,
/// `name ^1.2`).
[[nodiscard]] bool parseIndexDependency(std::string_view text,
                                        std::string& name,
                                        semver::Requirement& requirement,
                                        std::string& error);

}  // namespace coord::package
