#pragma once

// Package manifests.
//
// Two formats are supported:
//
//   `coord.pkg`          line oriented, M0 compatible:
//                          name app
//                          version 0.1.0
//                          source src
//                          dependency ../lib
//                          dependency math ^1.2
//
//   `coordination.toml`  full format:
//                          [package]
//                          name = "app"
//                          version = "0.1.0"
//                          [dependencies]
//                          math = "^1.2"
//                          local = { path = "../lib" }
//
// Dependency requirements use the semver syntax from `semver.h`.

#include "coord/modules/semver.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::package {

enum class DependencyKind { Normal, Development, Build };

[[nodiscard]] bool platformMatches(std::string_view spec);

[[nodiscard]] std::string_view dependencyKindName(DependencyKind kind);

struct RegistryEntry {
  std::string name;
  std::string url;
};

struct MirrorEntry {
  std::string name;
  std::string registry = "official";
  std::string url;
  int priority = 0;
  bool enabled = true;
  std::string description;
};

struct DependencySpec {
  std::string name;
  std::string rawRequirement = "*";
  semver::Requirement requirement;
  std::optional<std::filesystem::path> path;
  std::string pathSpec;
  std::string registry;
  std::string mirror;
  std::string platform;
  bool optional = false;
  DependencyKind kind = DependencyKind::Normal;

  [[nodiscard]] bool isPath() const { return path.has_value(); }
  [[nodiscard]] bool appliesToHost() const {
    return platform.empty() || platformMatches(platform);
  }
  [[nodiscard]] std::string display() const;
};

struct Manifest {
  std::filesystem::path path;
  std::filesystem::path root;
  std::string name;
  semver::Version version;
  std::string edition = "2026";
  std::string source = "src";
  std::string description;
  std::string license;
  std::string repository;
  std::string homepage;
  std::string registry;
  std::string mirror;
  std::vector<DependencySpec> dependencies;
  std::vector<std::string> members;
  std::vector<RegistryEntry> registries;
  std::vector<MirrorEntry> mirrors;
  bool requireChecksum = false;
  bool allowInsecureHttp = false;
  bool legacy = false;

  [[nodiscard]] const DependencySpec* dependency(std::string_view name) const;
  [[nodiscard]] std::vector<const DependencySpec*> dependenciesOf(
      DependencyKind kind, bool includeOptional = false) const;
  [[nodiscard]] std::string id() const;
  [[nodiscard]] std::filesystem::path sourceDirectory() const {
    return source.empty() ? root : root / std::filesystem::path(source);
  }

  /// `rootOrFile` may be a manifest file or a package directory.
  [[nodiscard]] static std::optional<Manifest> load(
      const std::filesystem::path& rootOrFile, std::string& error);
  [[nodiscard]] static std::optional<Manifest> loadFile(
      const std::filesystem::path& manifestPath, std::string& error);
  [[nodiscard]] static std::optional<Manifest> parse(
      std::string_view text, const std::filesystem::path& manifestPath,
      std::string& error);
};

/// `coordination.toml` is preferred, then `coord.pkg`. Returns nullopt when the
/// directory has no manifest; a file path is returned as-is.
[[nodiscard]] std::optional<std::filesystem::path> findManifest(
    const std::filesystem::path& rootOrFile);

[[nodiscard]] bool isValidPackageName(std::string_view name);

/// Evaluates a dependency platform filter such as "windows", "linux, macos",
/// "unix", "!windows" or "x86_64". Unknown tags never match.
[[nodiscard]] bool platformMatches(std::string_view spec);

/// Host tags used by platform filters: OS, architecture and "unix".
[[nodiscard]] std::vector<std::string> hostPlatformTags();

}  // namespace coord::package
