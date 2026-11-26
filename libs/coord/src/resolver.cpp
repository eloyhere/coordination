#include "coord/modules/resolver.h"

#include "coord/modules/checksum.h"
#include "coord/modules/platform.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace coord::package {
namespace {

struct Constraint {
  semver::Requirement requirement;
  std::string origin;
  std::string registry;
  std::string mirror;
};

struct PathNode {
  Manifest manifest;
  std::string registry;
  std::string pathSpec;
  std::vector<std::string> dependencies;
  bool direct = false;
  bool dev = false;
};

struct Assignment {
  std::map<std::string, semver::Version> versions;
  std::map<std::string, std::string> registries;
  std::map<std::string, std::string> mirrors;
  std::map<std::string, std::string> checksums;
  std::map<std::string, std::vector<std::string>> edges;
  std::map<std::string, std::vector<Constraint>> constraints;
};

std::string normalizeChecksumText(std::string value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  value.erase(0, begin);
  for (char& character : value) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  if (value.empty()) {
    return std::string();
  }
  if (value.rfind("sha256:", 0) == 0) {
    value.erase(0, 7);
  }
  return "sha256:" + value;
}

bool satisfiesAll(const semver::Version& version,
                  const std::vector<Constraint>& constraints) {
  for (const Constraint& constraint : constraints) {
    if (!constraint.requirement.matches(version)) {
      return false;
    }
  }
  return true;
}

std::string describeConstraints(const std::string& name,
                                const std::vector<Constraint>& constraints) {
  std::ostringstream out;
  out << "cannot resolve dependency '" << name << "' for "
      << platform::operatingSystemName();
  if (!constraints.empty()) {
    out << ":";
    for (const Constraint& constraint : constraints) {
      out << "\n  " << constraint.origin << " requires " << name;
      if (!constraint.requirement.isAny()) {
        out << " " << constraint.requirement.toString();
      }
    }
  }
  return out.str();
}

bool isUpdating(const ResolveOptions& options, const std::string& name) {
  if (options.updateAll) {
    return true;
  }
  return std::find(options.update.begin(), options.update.end(), name) !=
         options.update.end();
}

std::string relativePathSpec(const std::filesystem::path& from,
                             const std::filesystem::path& to) {
  std::error_code error;
  const std::filesystem::path relative = std::filesystem::relative(to, from, error);
  const std::string text =
      platform::normalizeSlashes(platform::pathToUtf8(error ? to : relative));
  return text.empty() ? "." : text;
}

/// Lock entries store dependencies as "name version"; the index syntax wants
/// "name requirement", so pin the exact version when possible.
std::vector<std::string> lockDependenciesToIndex(
    const std::vector<std::string>& dependencies) {
  std::vector<std::string> result;
  result.reserve(dependencies.size());
  for (const std::string& dependency : dependencies) {
    const std::size_t space = dependency.find_last_of(' ');
    if (space == std::string::npos) {
      result.push_back(dependency);
      continue;
    }
    const std::string name = dependency.substr(0, space);
    const std::string version = dependency.substr(space + 1);
    if (semver::Version::parse(version).has_value()) {
      result.push_back(name + " =" + version);
    } else {
      result.push_back(dependency);
    }
  }
  return result;
}

}  // namespace

const ResolvedPackage* Resolution::find(std::string_view packageName) const {
  for (const ResolvedPackage& package : packages) {
    if (package.name == packageName) {
      return &package;
    }
  }
  return nullptr;
}

std::string Resolution::fingerprint() const {
  std::ostringstream canonical;
  for (const ResolvedPackage& package : packages) {
    canonical << package.name << '@' << package.version.toString() << '|'
              << lockSourceName(package.source) << '|' << package.registry
              << '|' << package.url << '|' << package.checksum << '|'
              << package.pathSpec;
    for (const std::string& dependency : package.dependencies) {
      canonical << '|' << dependency;
    }
    canonical << '\n';
  }
  return "sha256:" + checksum::sha256Hex(canonical.str());
}

Resolver::Resolver(IndexProvider indexProvider, RegistryLookup registryLookup)
    : indexProvider_(std::move(indexProvider)),
      registryLookup_(std::move(registryLookup)) {}

Resolution Resolver::resolve(const Manifest& root, const Lockfile* lock,
                             const ResolveOptions& options) {
  Resolution result;
  std::map<std::string, std::vector<Constraint>> constraints;
  std::map<std::string, PathNode> pathNodes;
  std::set<std::string> pathNames;
  std::set<std::string> directNames;
  std::set<std::string> directDevNames;
  std::set<std::string> activePaths;
  std::set<std::string> visitedPaths;
  std::string failure;

  const auto addConstraint = [&](const std::string& name,
                                 const Constraint& constraint) -> bool {
    if (pathNames.count(name) != 0) {
      failure = "package '" + name +
                "' is required both as a path dependency and from registry '" +
                constraint.registry + "'";
      return false;
    }
    std::vector<Constraint>& list = constraints[name];
    for (const Constraint& existing : list) {
      if (!existing.registry.empty() && !constraint.registry.empty() &&
          existing.registry != constraint.registry) {
        failure = "package '" + name + "' is required from two registries ('" +
                  existing.registry + "' and '" + constraint.registry + "')";
        return false;
      }
    }
    list.push_back(constraint);
    return true;
  };

  activePaths.insert(platform::pathKey(root.root));
  std::function<bool(const Manifest&, bool, bool)> visit =
      [&](const Manifest& manifest, bool isRoot, bool devScope) -> bool {
    std::vector<const DependencySpec*> specs;
    for (const DependencySpec& spec : manifest.dependencies) {
      if (!spec.appliesToHost()) {
        continue;
      }
      if (spec.optional && !options.includeOptional) {
        continue;
      }
      if (spec.kind == DependencyKind::Development) {
        if (!isRoot || !options.includeDev) {
          continue;  // dev dependencies of dependencies are not installed
        }
      }
      if (spec.kind == DependencyKind::Build) {
        result.warnings.push_back(
            "ignoring build dependency '" + spec.display() + "' of '" +
            manifest.name + "' (build scripts are not supported yet)");
        continue;
      }
      specs.push_back(&spec);
    }
    for (const DependencySpec* spec : specs) {
      const bool devDependency = spec->kind == DependencyKind::Development;
      const std::string origin =
          manifest.name + "@" + manifest.version.toString();
      if (spec->isPath()) {
        if (!spec->path.has_value()) {
          failure = "path dependency '" + spec->pathSpec + "' has no path";
          return false;
        }
        const std::string key = platform::pathKey(*spec->path);
        if (activePaths.count(key) != 0) {
          failure = "cyclic path dependency at '" +
                    platform::pathToUtf8(*spec->path) + "'";
          return false;
        }
        std::string loadError;
        std::optional<Manifest> target = Manifest::load(*spec->path, loadError);
        if (!target.has_value()) {
          failure = origin + ": cannot load path dependency '" +
                    spec->pathSpec + "': " + loadError;
          return false;
        }
        if (!spec->requirement.isAny() &&
            !spec->requirement.matches(target->version)) {
          failure = origin + " requires '" + target->name + "' " +
                    spec->requirement.toString() + ", but the path package is " +
                    target->version.toString();
          return false;
        }
        const auto existing = pathNodes.find(target->name);
        if (existing != pathNodes.end()) {
          if (platform::pathKey(existing->second.manifest.root) !=
              platform::pathKey(target->root)) {
            failure = "package '" + target->name +
                      "' is provided by two different paths ('" +
                      platform::pathToUtf8(existing->second.manifest.root) +
                      "' and '" + platform::pathToUtf8(target->root) + "')";
            return false;
          }
          continue;
        }
        if (constraints.count(target->name) != 0) {
          failure = "package '" + target->name +
                    "' is required both as a path dependency and from a registry";
          return false;
        }
        PathNode node;
        node.manifest = *target;
        node.registry = spec->registry.empty() ? manifest.registry
                                               : spec->registry;
        node.pathSpec = relativePathSpec(root.root, target->root);
        node.direct = isRoot;
        node.dev = isRoot && devScope && devDependency;
        pathNames.insert(target->name);
        pathNodes.emplace(target->name, std::move(node));
        if (isRoot) {
          directNames.insert(target->name);
          if (devDependency) {
            directDevNames.insert(target->name);
          }
        }
        visitedPaths.insert(key);
        activePaths.insert(key);
        if (!visit(*target, false, false)) {
          return false;
        }
        activePaths.erase(key);
        continue;
      }
      std::string registry = spec->registry;
      if (registry.empty()) {
        registry = manifest.registry;
      }
      Constraint constraint;
      constraint.requirement = spec->requirement;
      constraint.origin = origin;
      constraint.registry = registry;
      constraint.mirror = spec->mirror;
      if (!addConstraint(spec->name, constraint)) {
        return false;
      }
      if (isRoot) {
        directNames.insert(spec->name);
        if (devDependency) {
          directDevNames.insert(spec->name);
        }
      }
    }
    return true;
  };

  if (!visit(root, true, true)) {
    result.error = failure;
    return result;
  }

  // Resolve the registry part with backtracking.
  Assignment state;
  state.constraints = constraints;
  std::map<std::string, IndexEntry> indexCache;
  std::map<std::string, std::string> indexErrors;
  std::string lastFailure;

  const auto lookupIndex = [&](const std::string& registry,
                               const std::string& name,
                               const std::string& mirror) -> const IndexEntry* {
    const std::string key = registry + "|" + name;
    const auto cached = indexCache.find(key);
    if (cached != indexCache.end()) {
      return &cached->second;
    }
    std::string error;
    std::optional<IndexEntry> entry =
        indexProvider_(registry, name, mirror, error);
    if (!entry.has_value()) {
      indexErrors[key] = error.empty() ? "index lookup failed" : error;
      return nullptr;
    }
    const auto inserted =
        indexCache.emplace(key, std::move(*entry));
    return &inserted.first->second;
  };

  const auto loadCandidates =
      [&](const std::string& name, const std::vector<Constraint>& list,
          std::string& error) -> std::vector<IndexVersion> {
    std::vector<IndexVersion> candidates;
    std::string registry;
    std::string mirror;
    for (const Constraint& constraint : list) {
      if (!constraint.registry.empty()) {
        registry = constraint.registry;
      }
      if (!constraint.mirror.empty()) {
        mirror = constraint.mirror;
      }
    }
    const IndexEntry* index = lookupIndex(registry, name, mirror);
    const LockedPackage* locked =
        lock != nullptr ? lock->find(name) : nullptr;
    const bool useLock = locked != nullptr && options.preferLocked &&
                         !isUpdating(options, name) &&
                         locked->source == LockSource::Registry;
    if (index != nullptr) {
      for (const IndexVersion* candidate : index->matching(
               semver::Requirement::any(), false)) {
        if (satisfiesAll(candidate->version, list)) {
          IndexVersion copy = *candidate;
          copy.checksum = normalizeChecksumText(candidate->checksum);
          candidates.push_back(std::move(copy));
        }
      }
    }
    if (useLock && satisfiesAll(locked->version, list)) {
      bool present = false;
      for (const IndexVersion& candidate : candidates) {
        if (candidate.version == locked->version) {
          present = true;
          break;
        }
      }
      if (!present) {
        IndexVersion synthesized;
        synthesized.version = locked->version;
        synthesized.checksum = normalizeChecksumText(locked->checksum);
        synthesized.dependencies = lockDependenciesToIndex(locked->dependencies);
        candidates.insert(candidates.begin(), std::move(synthesized));
      } else {
        // Move the locked version to the front without duplicating it.
        const auto found = std::find_if(
            candidates.begin(), candidates.end(),
            [&](const IndexVersion& candidate) {
              return candidate.version == locked->version;
            });
        if (found != candidates.end()) {
          IndexVersion lockedVersion = *found;
          if (!locked->checksum.empty()) {
            lockedVersion.checksum = normalizeChecksumText(locked->checksum);
          }
          candidates.erase(found);
          candidates.insert(candidates.begin(), std::move(lockedVersion));
        }
      }
      if (index == nullptr) {
        result.warnings.push_back(
            "registry unavailable for '" + name + "'; using the locked version " +
            locked->version.toString());
      }
    }
    if (candidates.empty()) {
      std::string message = describeConstraints(name, list);
      if (index == nullptr) {
        const std::string key = registry + "|" + name;
        const auto indexError = indexErrors.find(key);
        if (indexError != indexErrors.end()) {
          message += "\n  " + indexError->second;
        }
      } else {
        message += "\n  available versions:";
        for (const IndexVersion& candidate : index->versions) {
          message += " " + candidate.version.toString();
          if (candidate.yanked) {
            message += "(yanked)";
          }
        }
        if (index->versions.empty()) {
          message += " (none)";
        }
      }
      error = message;
    }
    return candidates;
  };

  std::function<bool(Assignment&)> solve = [&](Assignment& current) -> bool {
    std::string next;
    for (const auto& [name, list] : current.constraints) {
      if (pathNames.count(name) != 0) {
        continue;
      }
      if (current.versions.count(name) != 0) {
        continue;
      }
      next = name;
      break;
    }
    if (next.empty()) {
      return true;
    }
    std::string candidateError;
    const std::vector<IndexVersion> candidates =
        loadCandidates(next, current.constraints[next], candidateError);
    if (candidates.empty()) {
      lastFailure = candidateError;
      return false;
    }
    for (const IndexVersion& candidate : candidates) {
      Assignment branch = current;
      branch.versions[next] = candidate.version;
      branch.checksums[next] = candidate.checksum;
      std::string registry;
      std::string mirror;
      for (const Constraint& constraint : branch.constraints[next]) {
        if (!constraint.registry.empty()) {
          registry = constraint.registry;
        }
        if (!constraint.mirror.empty()) {
          mirror = constraint.mirror;
        }
      }
      branch.registries[next] = registry;
      branch.mirrors[next] = mirror;
      bool conflict = false;
      for (const std::string& dependencyText : candidate.dependencies) {
        std::string dependencyName;
        semver::Requirement requirement;
        std::string dependencyError;
        if (!parseIndexDependency(dependencyText, dependencyName, requirement,
                                  dependencyError)) {
          lastFailure = "package '" + next + "@" +
                        candidate.version.toString() +
                        "' has an invalid dependency entry: " + dependencyError;
          conflict = true;
          break;
        }
        if (pathNames.count(dependencyName) != 0) {
          lastFailure = "registry package '" + next +
                        "' depends on path package '" + dependencyName + "'";
          conflict = true;
          break;
        }
        Constraint constraint;
        constraint.requirement = requirement;
        constraint.origin =
            next + "@" + candidate.version.toString();
        constraint.registry = registry;
        constraint.mirror = mirror;
        std::vector<Constraint>& list = branch.constraints[dependencyName];
        bool registryConflict = false;
        for (const Constraint& existing : list) {
          if (!existing.registry.empty() && !constraint.registry.empty() &&
              existing.registry != constraint.registry) {
            registryConflict = true;
            break;
          }
        }
        if (registryConflict) {
          lastFailure = "package '" + dependencyName +
                        "' is required from two different registries";
          conflict = true;
          break;
        }
        list.push_back(constraint);
        const auto chosen = branch.versions.find(dependencyName);
        if (chosen != branch.versions.end() &&
            !requirement.matches(chosen->second)) {
          conflict = true;
          break;
        }
        branch.edges[next].push_back(dependencyName);
      }
      if (conflict) {
        continue;
      }
      if (solve(branch)) {
        current = std::move(branch);
        return true;
      }
    }
    return false;
  };

  if (!solve(state)) {
    result.error = lastFailure.empty()
                       ? "dependency resolution failed"
                       : lastFailure + "\nrun `coorc update` to refresh the lockfile";
    return result;
  }

  // Materialize path packages.
  for (const auto& [name, node] : pathNodes) {
    ResolvedPackage package;
    package.name = name;
    package.version = node.manifest.version;
    package.source = LockSource::Path;
    package.path = node.manifest.root;
    package.pathSpec = node.pathSpec;
    package.registry = node.registry;
    package.direct = node.direct;
    package.dev = node.dev;
    for (const DependencySpec& spec : node.manifest.dependencies) {
      if (!spec.appliesToHost() || spec.optional ||
          spec.kind == DependencyKind::Development) {
        continue;
      }
      if (spec.isPath()) {
        std::string loadError;
        const std::optional<Manifest> target =
            Manifest::load(spec.path.value(), loadError);
        if (target.has_value()) {
          package.dependencies.push_back(target->name);
        }
      } else if (spec.kind != DependencyKind::Build) {
        package.dependencies.push_back(spec.name);
      }
    }
    std::sort(package.dependencies.begin(), package.dependencies.end());
    result.packages.push_back(std::move(package));
  }

  for (const auto& [name, version] : state.versions) {
    ResolvedPackage package;
    package.name = name;
    package.version = version;
    package.source = LockSource::Registry;
    package.registry = state.registries[name];
    package.mirror = state.mirrors[name];
    package.checksum = state.checksums[name];
    package.direct = directNames.count(name) != 0;
    package.dev = directDevNames.count(name) != 0;
    const auto edges = state.edges.find(name);
    if (edges != state.edges.end()) {
      package.dependencies = edges->second;
      std::sort(package.dependencies.begin(), package.dependencies.end());
      package.dependencies.erase(
          std::unique(package.dependencies.begin(), package.dependencies.end()),
          package.dependencies.end());
    }
    if (registryLookup_) {
      const std::optional<RegistryEntry> registry =
          registryLookup_(package.registry);
      if (registry.has_value()) {
        package.url = registry->url;
      }
    }
    result.packages.push_back(std::move(package));
  }

  std::sort(result.packages.begin(), result.packages.end(),
            [](const ResolvedPackage& left, const ResolvedPackage& right) {
              return left.name < right.name;
            });
  std::sort(result.warnings.begin(), result.warnings.end());
  result.warnings.erase(std::unique(result.warnings.begin(),
                                    result.warnings.end()),
                        result.warnings.end());
  result.ok = true;
  return result;
}

Lockfile lockfileFromResolution(const Manifest& root,
                                const Resolution& resolution) {
  Lockfile lock;
  lock.manifestHash = manifestFingerprint(root);
  for (const ResolvedPackage& package : resolution.packages) {
    LockedPackage entry;
    entry.name = package.name;
    entry.version = package.version;
    entry.source = package.source;
    entry.registry = package.registry;
    entry.url = package.url;
    entry.checksum = normalizeChecksumText(package.checksum);
    entry.path = package.pathSpec;
    for (const std::string& dependency : package.dependencies) {
      const ResolvedPackage* resolved = resolution.find(dependency);
      if (resolved != nullptr) {
        entry.dependencies.push_back(resolved->name + " " +
                                     resolved->version.toString());
      } else {
        entry.dependencies.push_back(dependency);
      }
    }
    std::sort(entry.dependencies.begin(), entry.dependencies.end());
    lock.packages.push_back(std::move(entry));
  }
  std::sort(lock.packages.begin(), lock.packages.end(),
            [](const LockedPackage& left, const LockedPackage& right) {
              return left.name < right.name;
            });
  return lock;
}

}  // namespace coord::package
