#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace coord::pkg {

struct Package {
  std::string name;
  std::string version;
  std::filesystem::path root;
  std::string source = ".";
  std::vector<std::string> dependencies;
};

/// Read `coord.pkg` manifests and return packages in dependency-first order.
/// Cycles and unreadable manifests are reported through `error`.
class Graph {
 public:
  [[nodiscard]] static bool load(const std::filesystem::path& root,
                                 std::vector<Package>& packages,
                                 std::string& error) {
    std::set<std::string> active;
    std::set<std::string> loaded;
    return loadOne(root, packages, active, loaded, error);
  }

 private:
  [[nodiscard]] static bool isPathDependency(std::string_view value) {
    return !value.empty() &&
           (value.front() == '.' || value.front() == '/' ||
            value.front() == '\\' ||
            value.find('/') != std::string_view::npos ||
            value.find('\\') != std::string_view::npos);
  }
  [[nodiscard]] static bool loadOne(
      const std::filesystem::path& root, std::vector<Package>& packages,
      std::set<std::string>& active, std::set<std::string>& loaded,
      std::string& error) {
    std::error_code canonicalError;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(root, canonicalError);
    const std::string key = canonical.string();
    if (active.count(key) != 0) {
      error = "cyclic package dependency at '" + key + "'";
      return false;
    }
    if (loaded.count(key) != 0) {
      return true;
    }
    active.insert(key);
    std::ifstream input(canonical / "coord.pkg");
    if (!input) {
      error = "missing package manifest '" +
              (canonical / "coord.pkg").string() + "'";
      return false;
    }
    Package package;
    package.root = canonical;
    std::string line;
    while (std::getline(input, line)) {
      const std::size_t comment = line.find('#');
      if (comment != std::string::npos) {
        line.erase(comment);
      }
      std::istringstream parser(line);
      std::string field;
      parser >> field;
      if (field.empty()) {
        continue;
      }
      if (field == "name") {
        parser >> package.name;
      } else if (field == "version") {
        parser >> package.version;
      } else if (field == "source") {
        parser >> package.source;
      } else if (field == "dependency" || field == "dev-dependency" ||
                 field == "build-dependency") {
        std::string dependency;
        parser >> dependency;
        if (!dependency.empty()) {
          if (dependency.rfind("path:", 0) == 0) {
            dependency.erase(0, 5);
          }
          if (isPathDependency(dependency)) {
            package.dependencies.push_back(dependency);
          }
          // Registry dependencies need the package resolver, not this local
          // directory graph; `coorc deps` intentionally ignores them.
        }
      } else if (field == "edition" || field == "description" ||
                 field == "license" || field == "repository" ||
                 field == "homepage" || field == "registry" ||
                 field == "mirror" || field == "member") {
        std::string ignored;
        std::getline(parser, ignored);
      } else {
        error = "unknown package field '" + field + "' in '" +
                (canonical / "coord.pkg").string() + "'";
        return false;
      }
    }
    if (package.name.empty()) {
      error = "package manifest '" + (canonical / "coord.pkg").string() +
              "' has no name";
      return false;
    }
    for (const std::string& dependency : package.dependencies) {
      if (!loadOne(canonical / dependency, packages, active, loaded, error)) {
        return false;
      }
    }
    active.erase(key);
    loaded.insert(key);
    packages.push_back(std::move(package));
    return true;
  }
};

}  // namespace coord::pkg

namespace coord {

struct Package {
  std::filesystem::path root;
  std::filesystem::path manifest;
};

/// Recursively discover package manifests under a workspace root.
class PackageScanner {
 public:
  [[nodiscard]] std::vector<Package> scan(
      const std::filesystem::path& root) const {
    std::vector<Package> packages;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         it != end && !error; it.increment(error)) {
      if (it->is_directory(error)) {
        const std::string name = it->path().filename().string();
        if (name == "out" || name == ".git" || name == ".vs" ||
            name == "coord_modules" || name == "build" ||
            name.rfind("cmake-build-", 0) == 0) {
          it.disable_recursion_pending();
        }
        continue;
      }
      const std::string filename = it->path().filename().string();
      if (filename == "coordination.toml" || filename == "coord.pkg") {
        Package package;
        package.root = it->path().parent_path();
        package.manifest = it->path();
        packages.push_back(std::move(package));
      }
    }
    std::sort(packages.begin(), packages.end(),
              [](const Package& left, const Package& right) {
                return left.manifest.string() < right.manifest.string();
              });
    return packages;
  }
};

}  // namespace coord
