#pragma once

// High level package manager operations behind `coorc install/add/remove/...`.
//
// Layout of a project:
//
//   coordination.toml | coord.pkg     manifest
//   coord.lock                        resolved graph (generated)
//   coord_modules/<package>/          vendored registry dependencies
//   src/                              package sources

#include "coord/modules/lockfile.h"
#include "coord/modules/manifest.h"
#include "coord/modules/mirror.h"
#include "coord/modules/registry.h"
#include "coord/modules/resolver.h"

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace coord::package {

struct PackageManagerOptions {
  std::filesystem::path manifestPath;  // empty: search upwards from cwd
  bool offline = false;
  bool locked = false;   // fail when coord.lock would change
  bool frozen = false;   // like locked, and never writes the lockfile
  bool includeDev = true;
  bool includeOptional = false;
  bool updateAll = false;
  std::vector<std::string> update;
  std::string mirror;
  std::string registry;
  bool verbose = false;
  std::ostream* out = nullptr;
  std::ostream* err = nullptr;
};

class PackageManager {
 public:
  explicit PackageManager(PackageManagerOptions options);

  [[nodiscard]] int install();
  [[nodiscard]] int update();
  [[nodiscard]] int add(const std::string& spec, bool dev, bool path);
  [[nodiscard]] int remove(const std::string& name);
  [[nodiscard]] int list();
  [[nodiscard]] int tree();
  [[nodiscard]] int clean(bool cache);
  [[nodiscard]] int pack(const std::filesystem::path& output);
  [[nodiscard]] int publish(bool dryRun);
  [[nodiscard]] int search(const std::string& term);

  [[nodiscard]] static int init(const std::filesystem::path& directory,
                                const std::string& name, bool force);

 private:
  struct ResolvedProject {
    Resolution resolution;
    Lockfile lock;
    bool haveLock = false;
  };

  [[nodiscard]] bool loadProject(std::string& error);
  [[nodiscard]] bool prepareRegistry(bool refresh, std::string& error);
  [[nodiscard]] bool resolveProject(bool updateMode, ResolvedProject& project,
                                    std::string& error);
  [[nodiscard]] bool syncVendor(Resolution& resolution, int& fetched,
                                int& cached, std::string& error);
  [[nodiscard]] bool vendorMatches(const ResolvedPackage& package) const;
  [[nodiscard]] bool writeLock(const Resolution& resolution,
                               std::string& error) const;

  [[nodiscard]] int installInternal(bool updateMode);
  [[nodiscard]] bool editManifestAdd(const std::string& name,
                                     const std::string& requirement,
                                     const std::string& pathSpec, bool dev,
                                     std::string& error);
  [[nodiscard]] bool editManifestRemove(const std::string& name,
                                        std::string& error);

  [[nodiscard]] std::ostream& out();
  [[nodiscard]] std::ostream& err();
  void printWarnings();

  PackageManagerOptions options_;
  std::filesystem::path manifestPath_;
  std::filesystem::path root_;
  std::filesystem::path vendorPath_;
  std::filesystem::path lockPath_;
  Manifest manifest_;
  bool loaded_ = false;
  MirrorManager mirrors_;
  std::unique_ptr<RegistryClient> registryClient_;
  bool offline_ = false;
};

}  // namespace coord::package
