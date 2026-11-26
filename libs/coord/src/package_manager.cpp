#include "coord/modules/package_manager.h"

#include "coord/modules/archive.h"
#include "coord/modules/checksum.h"
#include "coord/modules/net.h"
#include "coord/modules/platform.h"
#include "coord/modules/toml.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace coord::package {
namespace {

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string lowerAscii(std::string text) {
  for (char& character : text) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t end = text.find('\n', begin);
    std::string line(text.substr(
        begin, end == std::string_view::npos ? std::string_view::npos
                                             : end - begin));
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return lines;
}

std::string joinLines(const std::vector<std::string>& lines) {
  std::string text;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      text.push_back('\n');
    }
    text += lines[i];
  }
  if (!text.empty()) {
    text.push_back('\n');
  }
  return text;
}

bool isTableHeader(const std::string& line) {
  const std::string text = trim(line);
  return text.size() >= 2 && text.front() == '[' &&
         text.find(']') != std::string::npos;
}

std::string tableHeaderName(const std::string& line) {
  const std::string text = trim(line);
  if (text.size() < 3 || text.front() != '[' || text[1] == '[') {
    return std::string();
  }
  const std::size_t close = text.find(']');
  if (close == std::string::npos) {
    return std::string();
  }
  return trim(text.substr(1, close - 1));
}

struct Section {
  std::size_t header = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

bool findSection(const std::vector<std::string>& lines,
                 const std::string& name, Section& section) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (tableHeaderName(lines[i]) == name) {
      section.header = i;
      section.begin = i + 1;
      section.end = lines.size();
      for (std::size_t j = section.begin; j < lines.size(); ++j) {
        if (isTableHeader(lines[j])) {
          section.end = j;
          break;
        }
      }
      return true;
    }
  }
  return false;
}

bool parseKeyLine(const std::string& line, std::string& key) {
  const std::string text = trim(line);
  if (text.empty() || text.front() == '#') {
    return false;
  }
  const std::size_t equals = text.find('=');
  if (equals == std::string::npos) {
    return false;
  }
  key = trim(text.substr(0, equals));
  if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') ||
                          (key.front() == '\'' && key.back() == '\''))) {
    key = key.substr(1, key.size() - 2);
  }
  return !key.empty();
}

std::string tomlString(std::string_view value) {
  std::string text = "\"";
  for (const char character : value) {
    switch (character) {
      case '"':
        text += "\\\"";
        break;
      case '\\':
        text += "\\\\";
        break;
      case '\n':
        text += "\\n";
        break;
      case '\t':
        text += "\\t";
        break;
      default:
        text.push_back(character);
        break;
    }
  }
  text.push_back('"');
  return text;
}

bool setTomlDependency(std::string& text, const std::string& table,
                       const std::string& name,
                       const std::string& valueLiteral,
                       std::string& error) {
  std::vector<std::string> lines = splitLines(text);
  const bool simpleKey =
      isValidPackageName(name) && name.find('.') == std::string::npos;
  const std::string quotedKey = simpleKey ? name : tomlString(name);
  const std::string assignment = quotedKey + " = " + valueLiteral;
  // A `[dependencies.name]` sub-table takes precedence over an inline key.
  Section subSection;
  if (findSection(lines, table + "." + name, subSection)) {
    if (!valueLiteral.empty() && valueLiteral.front() == '{') {
      error = "dependency '" + name +
              "' already uses a table; edit the manifest manually";
      return false;
    }
    for (std::size_t i = subSection.begin; i < subSection.end; ++i) {
      std::string key;
      if (parseKeyLine(lines[i], key) && key == "version") {
        lines[i] = "version = " + valueLiteral;
        text = joinLines(lines);
        return true;
      }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(subSection.begin),
                 "version = " + valueLiteral);
    text = joinLines(lines);
    return true;
  }
  Section section;
  if (!findSection(lines, table, section)) {
    if (!text.empty() && text.back() != '\n') {
      text.push_back('\n');
    }
    text += "\n[" + table + "]\n" + assignment + "\n";
    return true;
  }
  for (std::size_t i = section.begin; i < section.end; ++i) {
    std::string key;
    if (parseKeyLine(lines[i], key) && key == name) {
      lines[i] = assignment;
      text = joinLines(lines);
      return true;
    }
  }
  std::size_t insertAt = section.begin;
  for (std::size_t i = section.begin; i < section.end; ++i) {
    const std::string stripped = trim(lines[i]);
    if (!stripped.empty() && stripped.front() != '#') {
      insertAt = i + 1;
    }
  }
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
               assignment);
  text = joinLines(lines);
  return true;
}

bool removeTomlDependency(std::string& text, const std::string& table,
                          const std::string& name) {
  std::vector<std::string> lines = splitLines(text);
  bool changed = false;
  Section subSection;
  if (findSection(lines, table + "." + name, subSection)) {
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(subSection.header),
                lines.begin() + static_cast<std::ptrdiff_t>(subSection.end));
    changed = true;
  }
  Section section;
  if (findSection(lines, table, section)) {
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(section.end) - 1;
         i >= static_cast<std::ptrdiff_t>(section.begin); --i) {
      std::string key;
      if (parseKeyLine(lines[static_cast<std::size_t>(i)], key) && key == name) {
        lines.erase(lines.begin() + i);
        changed = true;
      }
    }
  }
  if (changed) {
    text = joinLines(lines);
  }
  return changed;
}

std::string dependencyValue(const std::string& requirement,
                            const std::string& pathSpec, bool isPath) {
  if (isPath) {
    return "{ path = " + tomlString(pathSpec) + " }";
  }
  return tomlString(requirement.empty() ? "*" : requirement);
}

struct LegacyLine {
  std::string field;
  std::string value;
};

bool parseLegacyDependencyLine(const std::string& line, LegacyLine& parsed) {
  const std::string text = trim(line);
  if (text.empty() || text.front() == '#') {
    return false;
  }
  const std::size_t space = text.find_first_of(" \t");
  const std::string field =
      space == std::string::npos ? text : text.substr(0, space);
  if (field != "dependency" && field != "dev-dependency" &&
      field != "build-dependency") {
    return false;
  }
  parsed.field = field;
  parsed.value =
      space == std::string::npos ? std::string() : trim(text.substr(space + 1));
  return true;
}

bool legacyLooksLikePath(std::string_view value) {
  const std::string text = trim(value);
  return !text.empty() &&
         (text.rfind("path:", 0) == 0 || text.front() == '.' ||
          text.front() == '/' || text.front() == '\\' ||
          text.find('/') != std::string::npos ||
          text.find('\\') != std::string::npos);
}

bool legacyDependencyName(const std::string& value,
                          const std::filesystem::path& root,
                          std::string& name, std::string& pathText) {
  std::string text = trim(value);
  if (text.rfind("path:", 0) == 0) {
    text = trim(text.substr(5));
  }
  if (legacyLooksLikePath(text)) {
    pathText = text;
    std::string loadError;
    const std::optional<Manifest> target =
        Manifest::load(root / platform::pathFromUtf8(text), loadError);
    if (target.has_value()) {
      name = target->name;
      return true;
    }
    return false;
  }
  const std::size_t at = text.find('@');
  if (at != std::string::npos) {
    name = trim(text.substr(0, at));
  } else {
    const std::size_t space = text.find_first_of(" \t");
    name = space == std::string::npos ? text : trim(text.substr(0, space));
  }
  return !name.empty();
}

bool setLegacyDependency(std::string& text, const std::filesystem::path& root,
                         const std::string& name, const std::string& requirement,
                         const std::string& pathSpec, bool dev) {
  std::vector<std::string> lines = splitLines(text);
  const std::string field = dev ? "dev-dependency" : "dependency";
  const std::string declaration =
      pathSpec.empty() ? field + " " + name +
                             (requirement.empty() || requirement == "*"
                                  ? ""
                                  : " " + requirement)
                       : field + " " + pathSpec;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    LegacyLine parsed;
    if (!parseLegacyDependencyLine(lines[i], parsed)) {
      continue;
    }
    std::string declaredName;
    std::string declaredPath;
    legacyDependencyName(parsed.value, root, declaredName, declaredPath);
    const bool matches = !pathSpec.empty()
                             ? declaredPath == pathSpec
                             : declaredName == name;
    if (matches) {
      lines[i] = declaration;
      text = joinLines(lines);
      return true;
    }
  }
  if (!text.empty() && text.back() != '\n') {
    text.push_back('\n');
  }
  text += declaration + "\n";
  return true;
}

bool removeLegacyDependency(std::string& text, const std::filesystem::path& root,
                            const std::string& name) {
  std::vector<std::string> lines = splitLines(text);
  bool changed = false;
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(lines.size()) - 1;
       i >= 0; --i) {
    LegacyLine parsed;
    if (!parseLegacyDependencyLine(lines[static_cast<std::size_t>(i)],
                                   parsed)) {
      continue;
    }
    std::string declaredName;
    std::string declaredPath;
    legacyDependencyName(parsed.value, root, declaredName, declaredPath);
    if (declaredName == name) {
      lines.erase(lines.begin() + i);
      changed = true;
    }
  }
  if (changed) {
    text = joinLines(lines);
  }
  return changed;
}

struct AddSpec {
  std::string name;
  std::string requirement = "*";
  std::string pathSpec;
  bool isPath = false;
};

bool parseAddSpec(const std::string& spec, bool pathFlag,
                  const std::filesystem::path& root, AddSpec& parsed,
                  std::string& error) {
  std::string text = trim(spec);
  if (text.empty()) {
    error = "empty package specification";
    return false;
  }
  const bool looksPath = pathFlag || text.front() == '.' ||
                         text.front() == '/' || text.front() == '\\' ||
                         text.find('/') != std::string::npos ||
                         text.find('\\') != std::string::npos;
  if (looksPath) {
    parsed.isPath = true;
    parsed.pathSpec = text;
    std::string loadError;
    const std::optional<Manifest> target =
        Manifest::load(root / platform::pathFromUtf8(text), loadError);
    if (!target.has_value()) {
      error = "cannot add path dependency '" + text + "': " + loadError;
      return false;
    }
    parsed.name = target->name;
    return true;
  }
  const std::size_t at = text.find('@');
  if (at != std::string::npos) {
    parsed.name = trim(text.substr(0, at));
    parsed.requirement = trim(text.substr(at + 1));
  } else {
    const std::size_t space = text.find_first_of(" \t");
    parsed.name = space == std::string::npos
                      ? text
                      : trim(text.substr(0, space));
    if (space != std::string::npos) {
      parsed.requirement = trim(text.substr(space + 1));
    }
  }
  if (!isValidPackageName(parsed.name)) {
    error = "invalid package name '" + parsed.name + "'";
    return false;
  }
  if (parsed.requirement.empty()) {
    parsed.requirement = "*";
  }
  std::string requirementError;
  if (!semver::Requirement::parse(parsed.requirement, &requirementError)
           .has_value()) {
    error = "invalid requirement '" + parsed.requirement + "': " +
            requirementError;
    return false;
  }
  return true;
}

std::optional<std::string> findCurlExecutable() {
  if (const std::optional<std::string> configured =
          platform::environment("COORD_CURL");
      configured.has_value() && !configured->empty()) {
    return configured;
  }
  const std::optional<std::filesystem::path> found =
      platform::findExecutable("curl");
  if (found.has_value()) {
    return platform::pathToUtf8(*found);
  }
  return std::nullopt;
}

bool isSkippedDirectory(const std::string& name) {
  return name == "out" || name == ".git" || name == ".vs" ||
         name == "coord_modules" || name == "build" ||
         name == "cmake-build-debug" || name == "cmake-build-release" ||
         name.rfind("cmake-build-", 0) == 0 || name == ".cache";
}

bool isSkippedFile(const std::string& name) {
  static const char* const kNames[] = {"coord.lock", ".coord-package.toml",
                                       ".DS_Store"};
  for (const char* candidate : kNames) {
    if (name == candidate) {
      return true;
    }
  }
  static const char* const kExtensions[] = {
      ".exe", ".obj", ".o",  ".pdb", ".stamp", ".ilk", ".dll",
      ".lib", ".a",   ".so", ".dylib", ".coord.cpp", ".tar.gz",
      ".tgz", ".zip"};
  for (const char* extension : kExtensions) {
    if (name.size() > std::strlen(extension) &&
        name.compare(name.size() - std::strlen(extension),
                     std::strlen(extension), extension) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

PackageManager::PackageManager(PackageManagerOptions options)
    : options_(std::move(options)) {}

std::ostream& PackageManager::out() {
  return options_.out != nullptr ? *options_.out : std::cout;
}

std::ostream& PackageManager::err() {
  return options_.err != nullptr ? *options_.err : std::cerr;
}

bool PackageManager::loadProject(std::string& error) {
  if (loaded_) {
    return true;
  }
  std::optional<std::filesystem::path> manifestPath =
      options_.manifestPath;
  if (!manifestPath.has_value() || manifestPath->empty()) {
    std::error_code pathError;
    std::filesystem::path current =
        std::filesystem::current_path(pathError);
    if (pathError) {
      error = "cannot determine the current directory";
      return false;
    }
    for (;;) {
      const std::optional<std::filesystem::path> found = findManifest(current);
      if (found.has_value()) {
        manifestPath = found;
        break;
      }
      const std::filesystem::path parent = current.parent_path();
      if (parent == current || parent.empty()) {
        error =
            "no coordination.toml or coord.pkg found (run `coorc init` to "
            "create a package)";
        return false;
      }
      current = parent;
    }
  }
  std::error_code fileError;
  if (std::filesystem::is_directory(*manifestPath, fileError)) {
    const std::optional<std::filesystem::path> found =
        findManifest(*manifestPath);
    if (!found.has_value()) {
      error = "no coordination.toml or coord.pkg in '" +
              platform::pathToUtf8(*manifestPath) + "'";
      return false;
    }
    manifestPath = found;
  }
  std::string loadError;
  const std::optional<Manifest> manifest =
      Manifest::loadFile(*manifestPath, loadError);
  if (!manifest.has_value()) {
    error = loadError;
    return false;
  }
  manifest_ = *manifest;
  manifestPath_ = *manifestPath;
  root_ = manifest_.root;
  vendorPath_ = root_ / "coord_modules";
  lockPath_ = root_ / "coord.lock";
  loaded_ = true;
  return true;
}

bool PackageManager::prepareRegistry(bool refresh, std::string& error) {
  mirrors_ = MirrorManager();
  if (!mirrors_.loadUserConfig(error)) {
    return false;
  }
  if (!mirrors_.loadProjectConfig(manifest_, error)) {
    return false;
  }
  mirrors_.applyEnvironment();
  if (!options_.mirror.empty() &&
      !mirrors_.selectMirror(options_.mirror, error)) {
    return false;
  }
  if (!options_.registry.empty() &&
      !mirrors_.selectRegistry(options_.registry, error)) {
    return false;
  }
  offline_ = options_.offline || mirrors_.config().offline;
  mirrors_.loadHealthCache();
  RegistryOptions registryOptions;
  registryOptions.offline = offline_;
  registryOptions.refreshIndex = refresh || options_.verbose;
  registryOptions.timeout =
      std::chrono::milliseconds(mirrors_.config().timeoutMs);
  registryOptions.cacheRoot = mirrors_.cacheRoot();
  registryOptions.preferredMirror = mirrors_.config().defaultMirror;
  registryOptions.verbose = options_.verbose;
  registryOptions.requireChecksum = manifest_.requireChecksum;
  registryOptions.allowInsecureHttp = manifest_.allowInsecureHttp;
  registryClient_ = std::make_unique<RegistryClient>(mirrors_, registryOptions);
  return true;
}

bool PackageManager::resolveProject(bool updateMode, ResolvedProject& project,
                                    std::string& error) {
  if (!loadProject(error)) {
    return false;
  }
  if (!prepareRegistry(updateMode || options_.updateAll, error)) {
    return false;
  }
  std::error_code lockError;
  if (std::filesystem::is_regular_file(lockPath_, lockError)) {
    std::string loadError;
    const std::optional<Lockfile> lock = Lockfile::load(lockPath_, loadError);
    if (!lock.has_value()) {
      error = loadError + "\nrun `coorc update` to regenerate the lockfile";
      return false;
    }
    project.lock = *lock;
    project.haveLock = true;
  }
  ResolveOptions resolveOptions;
  resolveOptions.includeDev = options_.includeDev;
  resolveOptions.includeOptional = options_.includeOptional;
  resolveOptions.preferLocked = !updateMode;
  resolveOptions.updateAll = updateMode || options_.updateAll;
  resolveOptions.update = options_.update;
  Resolver resolver(
      [this](const std::string& registryName, const std::string& packageName,
             const std::string& mirror, std::string& providerError)
          -> std::optional<IndexEntry> {
        const std::string registry =
            registryName.empty() ? mirrors_.config().defaultRegistry
                                 : registryName;
        return registryClient_->index(registry, packageName, providerError,
                                      mirror);
      },
      [this](const std::string& registryName)
          -> std::optional<RegistryEntry> {
        return mirrors_.registry(
            registryName.empty() ? mirrors_.config().defaultRegistry
                                 : registryName);
      });
  project.resolution =
      resolver.resolve(manifest_, project.haveLock ? &project.lock : nullptr,
                       resolveOptions);
  if (!project.resolution.ok) {
    error = project.resolution.error;
    return false;
  }
  // Record the effective registry name and URL in the resolution so the
  // lockfile is explicit even when the dependency used the default registry.
  for (ResolvedPackage& package : project.resolution.packages) {
    if (package.source != LockSource::Registry) {
      continue;
    }
    if (package.registry.empty()) {
      package.registry = mirrors_.config().defaultRegistry;
    }
    if (package.url.empty()) {
      if (const std::optional<RegistryEntry> entry =
              mirrors_.registry(package.registry);
          entry.has_value()) {
        package.url = entry->url;
      }
    }
  }
  if (options_.locked || options_.frozen) {
    if (!project.haveLock) {
      error = "no coord.lock to verify; run `coorc install` first";
      return false;
    }
    const Lockfile next =
        lockfileFromResolution(manifest_, project.resolution);
    if (next.manifestHash != project.lock.manifestHash) {
      error =
          "coord.lock is out of date with the manifest; run `coorc update`";
      return false;
    }
    if (next.fingerprint() != project.lock.fingerprint()) {
      error = "coord.lock is out of date with the dependency graph; run "
              "`coorc update`";
      return false;
    }
  }
  return true;
}

bool PackageManager::vendorMatches(const ResolvedPackage& package) const {
  const std::filesystem::path marker =
      vendorPath_ / platform::pathFromUtf8(package.name) / ".coord-package.toml";
  std::error_code existsError;
  if (!std::filesystem::is_regular_file(marker, existsError)) {
    return false;
  }
  std::string readError;
  const std::optional<std::string> text = platform::readFile(marker, readError);
  if (!text.has_value()) {
    return false;
  }
  std::string parseError;
  const std::optional<toml::Value> document = toml::parse(*text, parseError);
  if (!document.has_value()) {
    return false;
  }
  if (document->string("name").value_or("") != package.name ||
      document->string("version").value_or("") !=
          package.version.toString()) {
    return false;
  }
  const std::string checksum =
      lowerAscii(document->string("checksum").value_or(""));
  if (!package.checksum.empty() && checksum != lowerAscii(package.checksum)) {
    return false;
  }
  return true;
}

bool PackageManager::syncVendor(Resolution& resolution, int& fetched,
                                int& cached, std::string& error) {
  std::string directoryError;
  if (!platform::ensureDirectory(vendorPath_, directoryError)) {
    error = directoryError;
    return false;
  }
  std::set<std::string> keep;
  for (ResolvedPackage& package : resolution.packages) {
    if (package.source != LockSource::Registry) {
      continue;
    }
    keep.insert(package.name);
    if (vendorMatches(package)) {
      ++cached;
      continue;
    }
    const std::string registry =
        package.registry.empty() ? mirrors_.config().defaultRegistry
                                 : package.registry;
    IndexVersion version;
    version.version = package.version;
    version.checksum = package.checksum;
    const std::optional<PackageSource> source = registryClient_->fetch(
        registry, package.name, version, error);
    if (!source.has_value()) {
      return false;
    }
    package.checksum = source->checksum;
    std::filesystem::path staging;
    if (!platform::createTempDirectory(vendorPath_, "staging-", staging,
                                       error)) {
      return false;
    }
    if (!platform::copyTree(source->directory, staging, error)) {
      std::string removeError;
      (void)platform::removeTree(staging, removeError);
      return false;
    }
    const std::filesystem::path destination =
        vendorPath_ / platform::pathFromUtf8(package.name);
    std::error_code removeError;
    std::filesystem::remove_all(destination, removeError);
    std::error_code renameError;
    std::filesystem::rename(staging, destination, renameError);
    if (renameError) {
      error = "cannot vendor '" + package.name + "': " +
              renameError.message();
      std::string cleanupError;
      (void)platform::removeTree(staging, cleanupError);
      return false;
    }
    if (source->cached) {
      ++cached;
    } else {
      ++fetched;
    }
  }
  std::error_code iterateError;
  for (std::filesystem::directory_iterator iterator(vendorPath_, iterateError),
       end;
       iterator != end && !iterateError; iterator.increment(iterateError)) {
    std::error_code typeError;
    if (!iterator->is_directory(typeError)) {
      continue;
    }
    const std::filesystem::path marker =
        iterator->path() / ".coord-package.toml";
    if (!std::filesystem::is_regular_file(marker, typeError)) {
      continue;  // not created by the package manager
    }
    const std::string name = iterator->path().filename().string();
    if (keep.count(name) == 0) {
      std::string removeError;
      if (!platform::removeTree(iterator->path(), removeError)) {
        error = removeError;
        return false;
      }
    }
  }
  return true;
}

bool PackageManager::writeLock(const Resolution& resolution,
                               std::string& error) const {
  const Lockfile lock = lockfileFromResolution(manifest_, resolution);
  return lock.save(lockPath_, error);
}

void PackageManager::printWarnings() {
  if (registryClient_ != nullptr) {
    for (const std::string& warning : registryClient_->warnings()) {
      err() << "coorc: warning: " << warning << "\n";
    }
    registryClient_->clearWarnings();
  }
}

int PackageManager::install() { return installInternal(false); }

int PackageManager::update() {
  options_.updateAll = true;
  return installInternal(true);
}

int PackageManager::installInternal(bool updateMode) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  if ((options_.locked || options_.frozen) &&
      !std::filesystem::is_regular_file(lockPath_)) {
    err() << "coorc: no coord.lock to verify; run `coorc install` first\n";
    return 1;
  }
  ResolvedProject project;
  if (!resolveProject(updateMode, project, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  printWarnings();
  for (const std::string& warning : project.resolution.warnings) {
    err() << "coorc: warning: " << warning << "\n";
  }
  if (options_.frozen && !project.haveLock) {
    err() << "coorc: --frozen requires an existing coord.lock\n";
    return 1;
  }
  int fetched = 0;
  int cached = 0;
  if (!syncVendor(project.resolution, fetched, cached, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  const Lockfile next = lockfileFromResolution(manifest_, project.resolution);
  if (options_.locked || options_.frozen) {
    if (next.fingerprint() != project.lock.fingerprint()) {
      err() << "coorc: coord.lock is out of date; run `coorc update`\n";
      return 1;
    }
  }
  if (!options_.frozen) {
    std::string writeError;
    if (!next.save(lockPath_, writeError)) {
      err() << "coorc: " << writeError << "\n";
      return 1;
    }
  }
  if (!offline_) {
    std::string healthError;
    (void)mirrors_.saveHealthCache(healthError);
  }
  int registryCount = 0;
  int pathCount = 0;
  int directCount = 0;
  for (const ResolvedPackage& package : project.resolution.packages) {
    if (package.source == LockSource::Path) {
      ++pathCount;
    } else {
      ++registryCount;
    }
    if (package.direct) {
      ++directCount;
    }
  }
  out() << "coorc: installed " << project.resolution.packages.size()
        << " package" << (project.resolution.packages.size() == 1 ? "" : "s")
        << " (" << registryCount << " registry, " << pathCount << " path, "
        << directCount << " direct; " << fetched << " downloaded, " << cached
        << " reused from cache)\n";
  return 0;
}

bool PackageManager::editManifestAdd(const std::string& name,
                                     const std::string& requirement,
                                     const std::string& pathSpec, bool dev,
                                     std::string& error) {
  std::string readError;
  const std::optional<std::string> text =
      platform::readFile(manifestPath_, readError);
  if (!text.has_value()) {
    error = readError;
    return false;
  }
  std::string edited = *text;
  if (manifest_.legacy) {
    if (!setLegacyDependency(edited, root_, name, requirement, pathSpec, dev)) {
      error = "cannot update legacy manifest";
      return false;
    }
  } else {
    const std::string table =
        dev ? "dev-dependencies" : "dependencies";
    if (!setTomlDependency(edited, table, name,
                           dependencyValue(requirement, pathSpec,
                                          !pathSpec.empty()),
                           error)) {
      return false;
    }
  }
  return platform::writeFileAtomic(manifestPath_, edited, error);
}

bool PackageManager::editManifestRemove(const std::string& name,
                                        std::string& error) {
  std::string readError;
  const std::optional<std::string> text =
      platform::readFile(manifestPath_, readError);
  if (!text.has_value()) {
    error = readError;
    return false;
  }
  std::string edited = *text;
  bool changed = false;
  if (manifest_.legacy) {
    changed = removeLegacyDependency(edited, root_, name);
  } else {
    for (const char* table :
         {"dependencies", "dev-dependencies", "build-dependencies"}) {
      if (removeTomlDependency(edited, table, name)) {
        changed = true;
      }
    }
  }
  if (!changed) {
    error = "dependency '" + name + "' is not declared in the manifest";
    return false;
  }
  return platform::writeFileAtomic(manifestPath_, edited, error);
}

int PackageManager::add(const std::string& spec, bool dev, bool path) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  AddSpec parsed;
  if (!parseAddSpec(spec, path, root_, parsed, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  std::string readError;
  const std::optional<std::string> original =
      platform::readFile(manifestPath_, readError);
  if (!editManifestAdd(parsed.name, parsed.requirement, parsed.pathSpec, dev,
                       error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  loaded_ = false;
  const int result = installInternal(false);
  if (result != 0 && original.has_value()) {
    std::string restoreError;
    if (platform::writeFileAtomic(manifestPath_, *original, restoreError)) {
      err() << "coorc: reverted the manifest change\n";
    }
  }
  return result;
}

int PackageManager::remove(const std::string& name) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  std::string readError;
  const std::optional<std::string> original =
      platform::readFile(manifestPath_, readError);
  if (!editManifestRemove(name, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  loaded_ = false;
  const int result = installInternal(false);
  if (result != 0 && original.has_value()) {
    std::string restoreError;
    if (platform::writeFileAtomic(manifestPath_, *original, restoreError)) {
      err() << "coorc: reverted the manifest change\n";
    }
  }
  return result;
}

int PackageManager::list() {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  std::error_code lockError;
  if (std::filesystem::is_regular_file(lockPath_, lockError)) {
    std::string loadError;
    const std::optional<Lockfile> lock = Lockfile::load(lockPath_, loadError);
    if (!lock.has_value()) {
      err() << "coorc: " << loadError << "\n";
      return 1;
    }
    if (!lock->manifestHash.empty() &&
        lock->manifestHash != manifestFingerprint(manifest_)) {
      err() << "coorc: warning: coord.lock is out of date with the manifest "
               "(run `coorc update`)\n";
    }
    out() << "NAME\tVERSION\tSOURCE\n";
    for (const LockedPackage& package : lock->packages) {
      out() << package.name << '\t' << package.version.toString() << '\t';
      if (package.source == LockSource::Path) {
        out() << "path " << package.path;
      } else {
        out() << "registry " << (package.registry.empty() ? "default"
                                                          : package.registry);
      }
      out() << '\n';
    }
    return 0;
  }
  ResolvedProject project;
  if (!resolveProject(false, project, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  printWarnings();
  out() << "NAME\tVERSION\tSOURCE\n";
  for (const ResolvedPackage& package : project.resolution.packages) {
    out() << package.name << '\t' << package.version.toString() << '\t';
    if (package.source == LockSource::Path) {
      out() << "path " << platform::normalizeSlashes(package.pathSpec);
    } else {
      out() << "registry "
            << (package.registry.empty() ? "default" : package.registry);
    }
    out() << '\n';
  }
  return 0;
}

int PackageManager::tree() {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  struct Node {
    std::string version;
    std::string source;
    std::vector<std::string> children;
  };
  std::map<std::string, Node> nodes;
  std::error_code lockError;
  if (std::filesystem::is_regular_file(lockPath_, lockError)) {
    std::string loadError;
    const std::optional<Lockfile> lock = Lockfile::load(lockPath_, loadError);
    if (!lock.has_value()) {
      err() << "coorc: " << loadError << "\n";
      return 1;
    }
    if (!lock->manifestHash.empty() &&
        lock->manifestHash != manifestFingerprint(manifest_)) {
      err() << "coorc: warning: coord.lock is out of date with the manifest "
               "(run `coorc update`)\n";
    }
    for (const LockedPackage& package : lock->packages) {
      Node node;
      node.version = package.version.toString();
      node.source = package.source == LockSource::Path
                        ? "path " + package.path
                        : "registry " +
                              (package.registry.empty() ? "default"
                                                        : package.registry);
      for (const std::string& dependency : package.dependencies) {
        const std::size_t space = dependency.find(' ');
        node.children.push_back(space == std::string::npos
                                    ? dependency
                                    : dependency.substr(0, space));
      }
      std::sort(node.children.begin(), node.children.end());
      nodes[package.name] = std::move(node);
    }
  } else {
    ResolvedProject project;
    if (!resolveProject(false, project, error)) {
      err() << "coorc: " << error << "\n";
      return 1;
    }
    printWarnings();
    for (const ResolvedPackage& package : project.resolution.packages) {
      Node node;
      node.version = package.version.toString();
      node.source = package.source == LockSource::Path
                        ? "path " + platform::normalizeSlashes(package.pathSpec)
                        : "registry " +
                              (package.registry.empty() ? "default"
                                                        : package.registry);
      node.children = package.dependencies;
      std::sort(node.children.begin(), node.children.end());
      nodes[package.name] = std::move(node);
    }
  }
  // Direct dependencies of the root, in manifest order.
  std::vector<std::string> roots;
  for (const DependencySpec& spec : manifest_.dependencies) {
    if (!spec.appliesToHost() || spec.optional) {
      continue;
    }
    if (spec.kind == DependencyKind::Development && !options_.includeDev) {
      continue;
    }
    if (spec.isPath()) {
      std::string loadError;
      const std::optional<Manifest> target =
          Manifest::load(spec.path.value(), loadError);
      if (target.has_value()) {
        roots.push_back(target->name);
      }
    } else if (spec.kind != DependencyKind::Build) {
      roots.push_back(spec.name);
    }
  }
  out() << manifest_.name << " " << manifest_.version.toString() << "\n";
  std::set<std::string> visiting;
  std::function<void(const std::string&, const std::string&)> print =
      [&](const std::string& name, const std::string& prefix) {
        const auto found = nodes.find(name);
        if (found == nodes.end()) {
          out() << prefix << name << " (unresolved)\n";
          return;
        }
        if (visiting.count(name) != 0) {
          out() << prefix << name << " " << found->second.version
                << " (cycle)\n";
          return;
        }
        visiting.insert(name);
        out() << prefix << name << " " << found->second.version << " ("
              << found->second.source << ")\n";
        for (const std::string& child : found->second.children) {
          print(child, prefix + "  ");
        }
        visiting.erase(name);
      };
  for (const std::string& root : roots) {
    print(root, "");
  }
  return 0;
}

int PackageManager::clean(bool cache) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  if (std::filesystem::exists(vendorPath_)) {
    if (!platform::removeTree(vendorPath_, error)) {
      err() << "coorc: " << error << "\n";
      return 1;
    }
    out() << "coorc: removed " << platform::normalizeSlashes(
                 platform::pathToUtf8(vendorPath_))
          << "\n";
  }
  if (cache) {
    const std::filesystem::path cacheRoot = mirrors_.cacheRoot();
    for (const char* name : {"registries", "sources", "downloads", "tmp"}) {
      const std::filesystem::path path = cacheRoot / name;
      if (std::filesystem::exists(path)) {
        std::string removeError;
        if (!platform::removeTree(path, removeError)) {
          err() << "coorc: " << removeError << "\n";
          return 1;
        }
      }
    }
    std::error_code removeError;
    std::filesystem::remove(cacheRoot / "mirror-health.toml", removeError);
    out() << "coorc: cleared " << platform::normalizeSlashes(
                 platform::pathToUtf8(cacheRoot))
          << "\n";
  }
  return 0;
}

namespace {

bool collectPackageFiles(const std::filesystem::path& root,
                         const std::string& prefix,
                         std::vector<archive::ArchiveEntry>& entries,
                         std::string& error) {
  std::error_code iterateError;
  std::size_t count = 0;
  std::uint64_t total = 0;
  for (std::filesystem::recursive_directory_iterator iterator(
           root, std::filesystem::directory_options::skip_permission_denied,
           iterateError),
       end;
       iterator != end && !iterateError; iterator.increment(iterateError)) {
    std::error_code typeError;
    if (iterator->is_directory(typeError)) {
      if (isSkippedDirectory(iterator->path().filename().string())) {
        iterator.disable_recursion_pending();
      }
      continue;
    }
    if (!iterator->is_regular_file(typeError)) {
      continue;
    }
    const std::string filename = iterator->path().filename().string();
    if (isSkippedFile(filename)) {
      continue;
    }
    const std::filesystem::path relative =
        iterator->path().lexically_relative(root);
    if (relative.empty()) {
      continue;
    }
    std::string archivePath =
        platform::normalizeSlashes(platform::pathToUtf8(relative));
    archivePath = prefix + "/" + archivePath;
    std::string readError;
    const std::optional<std::string> data =
        platform::readFile(iterator->path(), readError);
    if (!data.has_value()) {
      error = readError;
      return false;
    }
    total += data->size();
    ++count;
    if (count > 20000) {
      error = "package contains too many files";
      return false;
    }
    if (total > 256ull * 1024 * 1024) {
      error = "package sources exceed 256 MiB";
      return false;
    }
    archive::ArchiveEntry entry;
    entry.path = std::move(archivePath);
    entry.data = *data;
    entry.mode = 0644;
    entries.push_back(std::move(entry));
  }
  if (iterateError) {
    error = "cannot scan the package: " + iterateError.message();
    return false;
  }
  return true;
}

}  // namespace

int PackageManager::pack(const std::filesystem::path& output) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  const std::string prefix =
      manifest_.name + "-" + manifest_.version.toString();
  std::vector<archive::ArchiveEntry> entries;
  if (!collectPackageFiles(root_, prefix, entries, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  if (entries.empty()) {
    err() << "coorc: package has no files to archive\n";
    return 1;
  }
  const std::string archiveBytes = archive::createTarGz(entries, error);
  if (archiveBytes.empty()) {
    err() << "coorc: " << (error.empty() ? "cannot build the archive" : error)
          << "\n";
    return 1;
  }
  std::filesystem::path target = output;
  if (target.empty()) {
    target = root_ / platform::pathFromUtf8(prefix + ".tar.gz");
  }
  std::string writeError;
  if (!platform::writeFile(target, archiveBytes, writeError)) {
    err() << "coorc: " << writeError << "\n";
    return 1;
  }
  out() << "coorc: packed " << platform::normalizeSlashes(
               platform::pathToUtf8(target))
        << " (" << archiveBytes.size() << " bytes, sha256:"
        << checksum::sha256Hex(archiveBytes) << ")\n";
  return 0;
}

int PackageManager::publish(bool dryRun) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  if (!prepareRegistry(true, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  std::string registryName = options_.registry;
  if (registryName.empty()) {
    registryName = manifest_.registry;
  }
  if (registryName.empty()) {
    registryName = mirrors_.config().defaultRegistry;
  }
  const std::optional<RegistryEntry> registry = mirrors_.registry(registryName);
  if (!registry.has_value()) {
    err() << "coorc: unknown registry '" << registryName << "'\n";
    return 1;
  }
  const std::string prefix =
      manifest_.name + "-" + manifest_.version.toString();
  std::vector<archive::ArchiveEntry> entries;
  if (!collectPackageFiles(root_, prefix, entries, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  const std::string archiveBytes = archive::createTarGz(entries, error);
  if (archiveBytes.empty()) {
    err() << "coorc: " << (error.empty() ? "cannot build the archive" : error)
          << "\n";
    return 1;
  }
  const std::string checksumText = checksum::sha256Hex(archiveBytes);
  const std::string targetUrl =
      net::joinUrl(registry->url, "api/v1/packages/" + manifest_.name + "/" +
                                      manifest_.version.toString());
  if (dryRun) {
    out() << "coorc: would publish " << manifest_.id() << " (sha256:"
          << checksumText << ", " << archiveBytes.size() << " bytes)\n";
    out() << "coorc: PUT " << targetUrl << "\n";
    return 0;
  }
  if (net::isLocalUrl(registry->url)) {
    const std::filesystem::path base = net::localPathFromUrl(registry->url);
    const std::filesystem::path packagePath =
        base / "packages" / platform::pathFromUtf8(manifest_.name) /
        platform::pathFromUtf8(manifest_.version.toString() + ".tar.gz");
    if (!platform::ensureDirectory(packagePath.parent_path(), error)) {
      err() << "coorc: " << error << "\n";
      return 1;
    }
    if (!platform::writeFile(packagePath, archiveBytes, error)) {
      err() << "coorc: " << error << "\n";
      return 1;
    }
    // Update index/<name>.toml.
    const std::filesystem::path indexPath =
        base / "index" /
        platform::pathFromUtf8(manifest_.name + ".toml");
    toml::Value index = toml::Value::makeTable();
    std::error_code existsError;
    if (std::filesystem::is_regular_file(indexPath, existsError)) {
      std::string readError;
      const std::optional<std::string> existing =
          platform::readFile(indexPath, readError);
      std::string parseError;
      if (existing.has_value()) {
        const std::optional<toml::Value> parsed =
            toml::parse(*existing, parseError);
        if (parsed.has_value()) {
          index = *parsed;
        } else {
          err() << "coorc: cannot update registry index: " << parseError
                << "\n";
          return 1;
        }
      }
    }
    index.set("name", toml::Value::makeString(manifest_.name));
    if (!manifest_.description.empty()) {
      index.set("description",
                toml::Value::makeString(manifest_.description));
    }
    toml::Value versions = toml::Value::makeArray();
    if (const toml::Value* existing = index.get("version");
        existing != nullptr && existing->isArray()) {
      for (const toml::Value& element : existing->elements()) {
        if (element.string("version").value_or("") !=
            manifest_.version.toString()) {
          versions.push(element);
        }
      }
    }
    toml::Value version = toml::Value::makeTable();
    version.set("version",
                toml::Value::makeString(manifest_.version.toString()));
    version.set("checksum",
                toml::Value::makeString("sha256:" + checksumText));
    toml::Value dependencies = toml::Value::makeArray();
    for (const DependencySpec& spec : manifest_.dependencies) {
      if (spec.kind != DependencyKind::Normal || spec.isPath()) {
        continue;
      }
      std::string text = spec.name;
      if (!spec.rawRequirement.empty() && spec.rawRequirement != "*") {
        text += " " + spec.rawRequirement;
      }
      dependencies.push(toml::Value::makeString(text));
    }
    version.set("dependencies", std::move(dependencies));
    versions.push(std::move(version));
    index.set("version", std::move(versions));
    if (!platform::ensureDirectory(indexPath.parent_path(), error) ||
        !platform::writeFile(indexPath, toml::write(index), error)) {
      err() << "coorc: " << error << "\n";
      return 1;
    }
    // Update the registry catalog when one exists.
    const std::filesystem::path catalogPath = base / "index.toml";
    if (std::filesystem::is_regular_file(catalogPath, existsError)) {
      std::string readError;
      const std::optional<std::string> existing =
          platform::readFile(catalogPath, readError);
      std::string parseError;
      const std::optional<toml::Value> parsed =
          existing.has_value() ? toml::parse(*existing, parseError)
                               : std::nullopt;
      if (!parsed.has_value()) {
        err() << "coorc: cannot update registry catalog: " << parseError
              << "\n";
        return 1;
      }
      toml::Value catalog = *parsed;
      toml::Value packages = toml::Value::makeArray();
      if (const toml::Value* current = catalog.get("package");
          current != nullptr && current->isArray()) {
        for (const toml::Value& element : current->elements()) {
          if (element.string("name").value_or("") != manifest_.name) {
            packages.push(element);
          }
        }
      }
      toml::Value entry = toml::Value::makeTable();
      entry.set("name", toml::Value::makeString(manifest_.name));
      if (!manifest_.description.empty()) {
        entry.set("description",
                  toml::Value::makeString(manifest_.description));
      }
      entry.set("latest",
                toml::Value::makeString(manifest_.version.toString()));
      packages.push(std::move(entry));
      catalog.set("package", std::move(packages));
      if (!platform::writeFile(catalogPath, toml::write(catalog), error)) {
        err() << "coorc: " << error << "\n";
        return 1;
      }
    }
    out() << "coorc: published " << manifest_.id() << " to "
          << platform::normalizeSlashes(platform::pathToUtf8(base)) << "\n";
    out() << "coorc: sha256:" << checksumText << "\n";
    return 0;
  }
  const std::optional<std::string> curl = findCurlExecutable();
  if (!curl.has_value()) {
    err() << "coorc: publishing over HTTP requires curl on PATH\n";
    return 1;
  }
  std::filesystem::path temporary = platform::temporaryDirectory() /
                                    platform::pathFromUtf8(prefix + ".tar.gz");
  if (!platform::writeFile(temporary, archiveBytes, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  std::vector<std::string> arguments = {
      *curl, "--silent", "--show-error", "--fail", "--location",
      "--request", "PUT", "--data-binary",
      "@" + platform::pathToUtf8(temporary),
      "--header", "Content-Type: application/gzip",
      "--header", "User-Agent: coorc/0.2"};
  if (const std::optional<std::string> token =
          platform::environment("COORD_TOKEN");
      token.has_value() && !token->empty()) {
    arguments.push_back("--header");
    arguments.push_back("Authorization: Bearer " + *token);
  }
  arguments.push_back(targetUrl);
  const platform::ProcessResult process = platform::run(arguments);
  std::error_code removeError;
  std::filesystem::remove(temporary, removeError);
  if (!process.ok()) {
    err() << "coorc: publish failed: "
          << (process.started ? process.err : process.failure) << "\n";
    return 1;
  }
  out() << "coorc: published " << manifest_.id() << " to " << targetUrl
        << "\n";
  out() << "coorc: sha256:" << checksumText << "\n";
  return 0;
}

int PackageManager::search(const std::string& term) {
  std::string error;
  if (!loadProject(error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  if (!prepareRegistry(true, error)) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  const std::string registryName =
      options_.registry.empty() ? mirrors_.config().defaultRegistry
                                : options_.registry;
  const std::vector<CatalogEntry> results =
      registryClient_->search(registryName, term, error);
  if (!results.empty()) {
    for (const CatalogEntry& entry : results) {
      out() << entry.name;
      if (!entry.latest.empty()) {
        out() << "\t" << entry.latest;
      }
      if (!entry.description.empty()) {
        out() << "\t" << entry.description;
      }
      out() << '\n';
    }
    return 0;
  }
  if (!error.empty()) {
    err() << "coorc: " << error << "\n";
    return 1;
  }
  out() << "coorc: no packages matched '" << term << "'\n";
  return 0;
}

int PackageManager::init(const std::filesystem::path& directory,
                         const std::string& name, bool force) {
  std::string packageName = name;
  if (packageName.empty()) {
    packageName = directory.filename().string();
    if (packageName.empty()) {
      std::error_code currentError;
      packageName = std::filesystem::current_path(currentError).filename().string();
    }
  }
  if (!isValidPackageName(packageName)) {
    std::cerr << "coorc: invalid package name '" << packageName
              << "' (use letters, digits, '-', '_', '.')\n";
    return 1;
  }
  std::error_code directoryError;
  std::filesystem::create_directories(directory, directoryError);
  if (directoryError) {
    std::cerr << "coorc: cannot create '" << platform::pathToUtf8(directory)
              << "': " << directoryError.message() << "\n";
    return 1;
  }
  const std::filesystem::path manifestPath = directory / "coordination.toml";
  if (std::filesystem::exists(manifestPath) && !force) {
    std::cerr << "coorc: '" << platform::pathToUtf8(manifestPath)
              << "' already exists (use --force to overwrite)\n";
    return 1;
  }
  std::ostringstream manifest;
  manifest << "[package]\n"
           << "name = \"" << packageName << "\"\n"
           << "version = \"0.1.0\"\n"
           << "edition = \"2026\"\n"
           << "source = \"src\"\n"
           << "\n"
           << "[dependencies]\n";
  std::string writeError;
  if (!platform::writeFile(manifestPath, manifest.str(), writeError)) {
    std::cerr << "coorc: " << writeError << "\n";
    return 1;
  }
  const std::filesystem::path sourceDirectory = directory / "src";
  if (!platform::ensureDirectory(sourceDirectory, writeError)) {
    std::cerr << "coorc: " << writeError << "\n";
    return 1;
  }
  const std::filesystem::path mainPath = sourceDirectory / "main.coo";
  if (!std::filesystem::exists(mainPath)) {
    const std::string mainSource =
        "integer main() {\n"
        "    print(\"hello, coordination\");\n"
        "    return 0;\n"
        "}\n";
    if (!platform::writeFile(mainPath, mainSource, writeError)) {
      std::cerr << "coorc: " << writeError << "\n";
      return 1;
    }
  }
  const std::filesystem::path ignorePath = directory / ".gitignore";
  if (!std::filesystem::exists(ignorePath)) {
    (void)platform::writeFile(ignorePath,
                        "coord_modules/\nout/\n*.exe\n*.obj\n*.stamp\n",
                        writeError);
  }
  std::cout << "coorc: created package '" << packageName << "' in "
            << platform::normalizeSlashes(platform::pathToUtf8(directory))
            << "\n";
  std::cout << "  " << platform::pathToUtf8(manifestPath) << "\n";
  std::cout << "  " << platform::pathToUtf8(mainPath) << "\n";
  return 0;
}

}  // namespace coord::package
