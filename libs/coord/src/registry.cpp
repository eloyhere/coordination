#include "coord/modules/registry.h"

#include "coord/modules/archive.h"
#include "coord/modules/checksum.h"
#include "coord/modules/net.h"
#include "coord/modules/platform.h"
#include "coord/modules/toml.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

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

std::string normalizeChecksum(std::string value) {
  value = lowerAscii(trim(value));
  if (value.rfind("sha256:", 0) == 0) {
    value.erase(0, 7);
  }
  return value;
}

std::string checksumWithPrefix(std::string_view hex) {
  return "sha256:" + std::string(hex);
}

std::string shortHash(std::string_view text) {
  return checksum::sha256Hex(text).substr(0, 16);
}

bool allowRegistryUrl(std::string_view url, bool allowInsecureHttp,
                      std::string& error) {
  if (url.rfind("http://", 0) != 0 || allowInsecureHttp) {
    return true;
  }
  const std::string_view rest = url.substr(7);
  const std::size_t slash = rest.find('/');
  const std::string_view host = rest.substr(0, slash);
  if (host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
      host.rfind("localhost:", 0) == 0 ||
      host.rfind("127.0.0.1:", 0) == 0 || host.rfind("[::1]:", 0) == 0) {
    return true;
  }
  error = "insecure HTTP registry URL refused: " + std::string(url) +
          "; use HTTPS or set [trust] allow_insecure_http = true";
  return false;
}

std::optional<RegistryEntry> resolveRegistry(const MirrorManager& mirrors,
                                             std::string_view name,
                                             std::string& error) {
  const std::optional<RegistryEntry> entry = mirrors.registry(name);
  if (!entry.has_value()) {
    error = "unknown registry '" + std::string(name) + "'";
    return std::nullopt;
  }
  return entry;
}

}  // namespace

const IndexVersion* IndexEntry::find(const semver::Version& version) const {
  for (const IndexVersion& entry : versions) {
    if (entry.version == version) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<const IndexVersion*> IndexEntry::matching(
    const semver::Requirement& requirement, bool includeYanked) const {
  std::vector<const IndexVersion*> result;
  for (const IndexVersion& entry : versions) {
    if (!includeYanked && entry.yanked) {
      continue;
    }
    if (requirement.matches(entry.version)) {
      result.push_back(&entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const IndexVersion* left, const IndexVersion* right) {
              return right->version < left->version;  // newest first
            });
  return result;
}

bool parseIndexDependency(std::string_view text, std::string& name,
                          semver::Requirement& requirement,
                          std::string& error) {
  const std::string value = trim(text);
  if (value.empty()) {
    error = "empty dependency entry in the registry index";
    return false;
  }
  std::string packageName;
  std::string requirementText;
  const std::size_t at = value.find('@');
  if (at != std::string::npos) {
    packageName = trim(value.substr(0, at));
    requirementText = trim(value.substr(at + 1));
  } else {
    const std::size_t space = value.find_first_of(" \t");
    if (space == std::string::npos) {
      packageName = value;
    } else {
      packageName = trim(value.substr(0, space));
      requirementText = trim(value.substr(space + 1));
    }
  }
  if (!isValidPackageName(packageName)) {
    error = "invalid dependency name '" + packageName + "' in the registry";
    return false;
  }
  name = packageName;
  semver::Requirement parsed = semver::Requirement::any();
  if (!requirementText.empty()) {
    std::string requirementError;
    const std::optional<semver::Requirement> candidate =
        semver::Requirement::parse(requirementText, &requirementError);
    if (!candidate.has_value()) {
      error = "invalid requirement '" + requirementText + "' for '" +
              packageName + "': " + requirementError;
      return false;
    }
    parsed = *candidate;
  }
  requirement = std::move(parsed);
  return true;
}

RegistryClient::RegistryClient(MirrorManager& mirrors, RegistryOptions options)
    : mirrors_(mirrors), options_(std::move(options)) {
  if (options_.cacheRoot.empty()) {
    options_.cacheRoot = platform::cacheDirectory("coord");
  }
}

std::filesystem::path RegistryClient::registryCacheDirectory(
    std::string_view registryName) const {
  std::string error;
  const std::optional<RegistryEntry> entry =
      resolveRegistry(mirrors_, registryName, error);
  const std::string key = entry.has_value() ? entry->url : std::string(registryName);
  return options_.cacheRoot / "registries" / shortHash(key);
}

std::filesystem::path RegistryClient::cacheFile(
    std::string_view registryName, std::string_view relative) const {
  return registryCacheDirectory(registryName) /
         platform::pathFromUtf8(relative);
}

std::optional<std::string> RegistryClient::readCache(
    const std::filesystem::path& path, bool enforceAge) const {
  std::error_code existsError;
  if (!std::filesystem::is_regular_file(path, existsError)) {
    return std::nullopt;
  }
  if (enforceAge) {
    std::error_code timeError;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(path, timeError);
    if (!timeError) {
      const auto age = std::filesystem::file_time_type::clock::now() - modified;
      if (age < std::chrono::seconds(0) || age >= options_.maxIndexAge) {
        return std::nullopt;  // stale: let the caller refresh from a mirror
      }
    }
  }
  std::string readError;
  return platform::readFile(path, readError);
}

void RegistryClient::writeCache(const std::filesystem::path& path,
                                std::string_view body) {
  std::string error;
  if (!platform::ensureDirectory(path.parent_path(), error)) {
    return;
  }
  (void)platform::writeFileAtomic(path, body, error);
}

bool RegistryClient::fetchText(std::string_view registryName,
                               std::string_view relative,
                               std::string_view preferredMirror,
                               std::string& body, std::string& error) {
  const std::filesystem::path cache = cacheFile(registryName, relative);
  if (options_.offline) {
    const std::optional<std::string> cached = readCache(cache, false);
    if (cached.has_value()) {
      body = *cached;
      return true;
    }
    error = "offline: '" + std::string(relative) +
            "' is not present in the local cache";
    return false;
  }
  // Local registries are cheap to read and often change during development, so
  // they bypass the index freshness cache. Remote indexes use the TTL.
  const std::optional<RegistryEntry> registryEntry =
      mirrors_.registry(registryName);
  const bool localRegistry =
      registryEntry.has_value() && net::isLocalUrl(registryEntry->url);
  if (!localRegistry && !options_.refreshIndex) {
    const std::optional<std::string> fresh = readCache(cache, true);
    if (fresh.has_value()) {
      body = *fresh;
      return true;
    }
  }
  net::TransportOptions transport;
  transport.timeout = options_.timeout;
  const std::vector<MirrorEntry> candidates = mirrors_.candidates(
      registryName, preferredMirror.empty() ? options_.preferredMirror
                                            : preferredMirror);
  if (candidates.empty()) {
    error = "no mirror configured for registry '" + std::string(registryName) +
            "'";
    return false;
  }
  std::vector<std::string> attempts;
  for (const MirrorEntry& candidate : candidates) {
    const std::string url = net::joinUrl(candidate.url, relative);
    std::string policyError;
    if (!allowRegistryUrl(url, options_.allowInsecureHttp, policyError)) {
      attempts.push_back(candidate.name + " (" + url + "): " + policyError);
      continue;
    }
    const net::FetchResult result = net::fetch(url, transport);
    if (result.ok) {
      mirrors_.recordSuccess(candidate, result.latencyMs);
      body = result.body;
      writeCache(cache, body);
      return true;
    }
    mirrors_.recordFailure(candidate);
    attempts.push_back(candidate.name + " (" + url + "): " +
                       (result.error.empty() ? "failed" : result.error));
    if (options_.verbose) {
      warnings_.push_back("mirror '" + candidate.name + "' failed: " +
                          result.error);
    }
  }
  const std::optional<std::string> stale = readCache(cache, false);
  if (stale.has_value()) {
    warnings_.push_back("using a stale cached copy of '" +
                        std::string(relative) + "'");
    body = *stale;
    return true;
  }
  error = "cannot fetch '" + std::string(relative) + "' for registry '" +
          std::string(registryName) + "'";
  for (const std::string& attempt : attempts) {
    error += "\n  " + attempt;
  }
  return false;
}

std::optional<IndexEntry> RegistryClient::index(
    std::string_view registryName, std::string_view packageName,
    std::string& error, std::string_view preferredMirror) {
  const std::string relative =
      "index/" + std::string(packageName) + ".toml";
  std::string body;
  if (!fetchText(registryName, relative, preferredMirror, body, error)) {
    return std::nullopt;
  }
  std::string parseError;
  std::optional<toml::Value> document = toml::parse(body, parseError);
  if (!document.has_value()) {
    error = "invalid registry index for '" + std::string(packageName) + "': " +
            parseError;
    return std::nullopt;
  }
  IndexEntry entry;
  entry.name = document->string("name").value_or(std::string(packageName));
  entry.description = document->string("description").value_or("");
  const toml::Value* versions = document->get("version");
  if (versions == nullptr) {
    versions = document->get("versions");
  }
  if (versions == nullptr) {
    error = "registry index for '" + std::string(packageName) +
            "' has no versions";
    return std::nullopt;
  }
  if (!versions->isArray()) {
    error = "registry index for '" + std::string(packageName) +
            "' must use [[version]] tables";
    return std::nullopt;
  }
  for (const toml::Value& element : versions->elements()) {
    if (!element.isTable()) {
      error = "registry index version entries must be tables";
      return std::nullopt;
    }
    const std::string versionText = element.string("version").value_or("");
    std::string versionError;
    const std::optional<semver::Version> version =
        semver::Version::parse(versionText, &versionError);
    if (!version.has_value()) {
      error = "registry index for '" + std::string(packageName) +
              "' has an invalid version '" + versionText + "': " +
              versionError;
      return std::nullopt;
    }
    IndexVersion indexVersion;
    indexVersion.version = *version;
    indexVersion.checksum =
        element.string("checksum").value_or("");
    indexVersion.archive = element.string("archive").value_or("");
    indexVersion.yanked = element.boolean("yanked").value_or(false);
    if (const std::optional<std::vector<std::string>> dependencies =
            element.stringArray("dependencies");
        dependencies.has_value()) {
      indexVersion.dependencies = *dependencies;
    }
    entry.versions.push_back(std::move(indexVersion));
  }
  return entry;
}

std::optional<PackageSource> RegistryClient::fetch(
    std::string_view registryName, std::string_view packageName,
    const IndexVersion& version, std::string& error) {
  const std::string checksum = normalizeChecksum(version.checksum);
  const std::string directoryName =
      std::string(packageName) + "-" + version.version.toString() + "-" +
      (checksum.empty() ? "unverified" : checksum.substr(0, 12));
  const std::filesystem::path sourceDirectory =
      options_.cacheRoot / "sources" / platform::pathFromUtf8(directoryName);
  const std::filesystem::path marker = sourceDirectory / ".coord-package.toml";

  const auto validMarker = [&]() -> bool {
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
    return document->string("name").value_or("") == packageName &&
           document->string("version").value_or("") ==
               version.version.toString() &&
           normalizeChecksum(document->string("checksum").value_or("")) ==
               checksum;
  };

  if (validMarker()) {
    PackageSource source;
    source.directory = sourceDirectory;
    source.checksum = checksum.empty() ? std::string()
                                       : checksumWithPrefix(checksum);
    source.archive = options_.cacheRoot / "downloads" /
                     platform::pathFromUtf8(std::string(packageName) + "-" +
                                            version.version.toString() +
                                            ".archive");
    source.url = "cache";
    source.cached = true;
    return source;
  }
  if (options_.offline) {
    error = "offline: package '" + std::string(packageName) + "@" +
            version.version.toString() + "' is not cached";
    return std::nullopt;
  }

  const std::filesystem::path downloads =
      options_.cacheRoot / "downloads";
  std::string directoryError;
  if (!platform::ensureDirectory(downloads, directoryError)) {
    error = directoryError;
    return std::nullopt;
  }
  const std::filesystem::path archivePath =
      downloads / platform::pathFromUtf8(std::string(packageName) + "-" +
                                         version.version.toString() +
                                         ".archive");

  std::string archiveUrl;
  bool haveArchive = false;
  bool cachedArchive = false;
  if (std::filesystem::is_regular_file(archivePath)) {
    std::string readError;
    const std::optional<std::string> bytes =
        platform::readFile(archivePath, readError);
    if (bytes.has_value()) {
      const std::string actual = checksum::sha256Hex(*bytes);
      if (checksum.empty() || normalizeChecksum(actual) == checksum) {
        haveArchive = true;
        archiveUrl = "cache";
        cachedArchive = true;
      }
    }
  }
  if (!haveArchive) {
    std::vector<std::string> relatives;
    if (!version.archive.empty()) {
      relatives.push_back(version.archive);
    } else {
      const std::string base = "packages/" + std::string(packageName) + "/" +
                               version.version.toString();
      relatives.push_back(base + ".tar.gz");
      relatives.push_back(base + ".tar");
      relatives.push_back(base + ".zip");
    }
    std::vector<std::string> attempts;
    for (const std::string& relative : relatives) {
      if (net::hasScheme(relative)) {
        std::string policyError;
        if (!allowRegistryUrl(relative, options_.allowInsecureHttp,
                              policyError)) {
          attempts.push_back(policyError);
          continue;
        }
        net::TransportOptions transport;
        transport.timeout = options_.timeout;
        const net::FetchResult result =
            net::download(relative, archivePath, transport);
        if (result.ok) {
          archiveUrl = relative;
          haveArchive = true;
          break;
        }
        attempts.push_back(relative + ": " + result.error);
        continue;
      }
      for (const MirrorEntry& candidate :
           mirrors_.candidates(registryName, options_.preferredMirror)) {
        const std::string url = net::joinUrl(candidate.url, relative);
        std::string policyError;
        if (!allowRegistryUrl(url, options_.allowInsecureHttp,
                              policyError)) {
          attempts.push_back(candidate.name + " (" + url + "): " +
                             policyError);
          continue;
        }
        net::TransportOptions transport;
        transport.timeout = options_.timeout;
        const net::FetchResult result =
            net::download(url, archivePath, transport);
        if (result.ok) {
          mirrors_.recordSuccess(candidate, result.latencyMs);
          archiveUrl = url;
          haveArchive = true;
          break;
        }
        if (result.status == 404) {
          attempts.push_back(url + ": not found");
          continue;  // try the next mirror, then the next extension
        }
        mirrors_.recordFailure(candidate);
        attempts.push_back(candidate.name + " (" + url + "): " + result.error);
      }
      if (haveArchive) {
        break;
      }
    }
    if (!haveArchive) {
      error = "cannot download '" + std::string(packageName) + "@" +
              version.version.toString() + "'";
      for (const std::string& attempt : attempts) {
        error += "\n  " + attempt;
      }
      return std::nullopt;
    }
  }

  std::string readError;
  const std::optional<std::string> bytes = platform::readFile(archivePath, readError);
  if (!bytes.has_value()) {
    error = readError;
    return std::nullopt;
  }
  const std::string actualChecksum = checksum::sha256Hex(*bytes);
  if (!checksum.empty() && normalizeChecksum(actualChecksum) != checksum) {
    std::error_code removeError;
    std::filesystem::remove(archivePath, removeError);
    error = "checksum mismatch for '" + std::string(packageName) + "@" +
            version.version.toString() + "': expected sha256:" + checksum +
            ", got sha256:" + actualChecksum;
    return std::nullopt;
  }
  if (checksum.empty()) {
    if (options_.requireChecksum) {
      std::error_code removeError;
      std::filesystem::remove(archivePath, removeError);
      error = "registry index has no checksum for '" +
              std::string(packageName) + "@" +
              version.version.toString() +
              "'; [trust] require_checksum = true refuses unverified packages";
      return std::nullopt;
    }
    warnings_.push_back("registry index has no checksum for '" +
                        std::string(packageName) + "@" +
                        version.version.toString() + "'; pinned sha256:" +
                        actualChecksum);
  }

  std::filesystem::path temporary;
  std::string tempError;
  if (!platform::createTempDirectory(options_.cacheRoot / "tmp", "extract-",
                                     temporary, tempError)) {
    error = tempError;
    return std::nullopt;
  }
  archive::ExtractOptions extractOptions;
  archive::ExtractResult extracted =
      archive::extract(archivePath, temporary, extractOptions);
  if (!extracted.ok) {
    error = "cannot extract '" + std::string(packageName) + "@" +
            version.version.toString() + "': " + extracted.error;
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  std::string manifestError;
  const std::optional<Manifest> manifest =
      Manifest::load(temporary, manifestError);
  if (!manifest.has_value()) {
    error = "package archive for '" + std::string(packageName) + "@" +
            version.version.toString() + "' has no manifest: " + manifestError;
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  if (manifest->name != packageName) {
    error = "package archive is named '" + manifest->name + "', expected '" +
            std::string(packageName) + "'";
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  if (manifest->version != version.version &&
      manifest->version.toString() != "0.0.0") {
    error = "package archive declares version " + manifest->version.toString() +
            ", expected " + version.version.toString();
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  toml::Value markerValue = toml::Value::makeTable();
  markerValue.set("name", toml::Value::makeString(std::string(packageName)));
  markerValue.set("version",
                  toml::Value::makeString(version.version.toString()));
  markerValue.set("checksum",
                  toml::Value::makeString(checksumWithPrefix(actualChecksum)));
  markerValue.set("registry", toml::Value::makeString(std::string(registryName)));
  const std::filesystem::path markerPath = temporary / ".coord-package.toml";
  std::string markerError;
  if (!platform::writeFileAtomic(markerPath, toml::write(markerValue),
                                 markerError)) {
    error = markerError;
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  if (std::filesystem::exists(sourceDirectory)) {
    std::string removeError;
    (void)platform::removeTree(sourceDirectory, removeError);
  }
  std::string directoryMessage;
  if (!platform::ensureDirectory(sourceDirectory.parent_path(),
                                 directoryMessage)) {
    error = directoryMessage;
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  std::error_code renameError;
  std::filesystem::rename(temporary, sourceDirectory, renameError);
  if (renameError) {
    error = "cannot move extracted package into the cache: " +
            renameError.message();
    std::string removeError;
    (void)platform::removeTree(temporary, removeError);
    return std::nullopt;
  }
  PackageSource source;
  source.directory = sourceDirectory;
  source.archive = archivePath;
  source.checksum = checksumWithPrefix(actualChecksum);
  source.url = archiveUrl;
  source.cached = cachedArchive;
  return source;
}

std::optional<CatalogEntry> RegistryClient::packageMetadata(
    std::string_view registryName, std::string_view packageName,
    std::string& error) {
  std::string body;
  std::string catalogError;
  if (fetchText(registryName, "index.toml", {}, body, catalogError)) {
    std::string parseError;
    const std::optional<toml::Value> document = toml::parse(body, parseError);
    if (document.has_value()) {
      for (const char* key : {"package", "packages"}) {
        const toml::Value* packages = document->get(key);
        if (packages == nullptr || !packages->isArray()) {
          continue;
        }
        for (const toml::Value& element : packages->elements()) {
          if (!element.isTable()) {
            continue;
          }
          if (element.string("name").value_or("") != packageName) {
            continue;
          }
          CatalogEntry entry;
          entry.name = std::string(packageName);
          entry.description = element.string("description").value_or("");
          entry.latest = element.string("latest").value_or("");
          return entry;
        }
      }
    }
  }
  const std::optional<IndexEntry> entry = index(registryName, packageName, error);
  if (!entry.has_value()) {
    return std::nullopt;
  }
  CatalogEntry catalog;
  catalog.name = entry->name;
  catalog.description = entry->description;
  for (const IndexVersion& version : entry->versions) {
    if (version.yanked) {
      continue;
    }
    bool newer = catalog.latest.empty();
    if (!newer) {
      const std::optional<semver::Version> current =
          semver::Version::parse(catalog.latest);
      newer = !current.has_value() || version.version > *current;
    }
    if (newer) {
      catalog.latest = version.version.toString();
    }
  }
  return catalog;
}

std::vector<CatalogEntry> RegistryClient::search(
    std::string_view registryName, std::string_view term, std::string& error) {
  std::vector<CatalogEntry> result;
  const std::string needle = lowerAscii(std::string(term));
  std::string body;
  std::string catalogError;
  if (fetchText(registryName, "index.toml", {}, body, catalogError)) {
    std::string parseError;
    const std::optional<toml::Value> document = toml::parse(body, parseError);
    if (document.has_value()) {
      for (const char* key : {"package", "packages"}) {
        const toml::Value* packages = document->get(key);
        if (packages == nullptr || !packages->isArray()) {
          continue;
        }
        for (const toml::Value& element : packages->elements()) {
          if (!element.isTable()) {
            continue;
          }
          CatalogEntry entry;
          entry.name = element.string("name").value_or("");
          if (entry.name.empty()) {
            continue;
          }
          entry.description = element.string("description").value_or("");
          entry.latest = element.string("latest").value_or("");
          const std::string haystack =
              lowerAscii(entry.name + " " + entry.description);
          if (needle.empty() || haystack.find(needle) != std::string::npos) {
            result.push_back(std::move(entry));
          }
        }
      }
    }
  }
  if (result.empty()) {
    const std::optional<RegistryEntry> registry =
        mirrors_.registry(registryName);
    if (registry.has_value() && net::isLocalUrl(registry->url)) {
      const std::filesystem::path indexDirectory =
          net::localPathFromUrl(registry->url) / "index";
      std::error_code iterateError;
      for (std::filesystem::directory_iterator iterator(indexDirectory,
                                                        iterateError),
           end;
           iterator != end && !iterateError; iterator.increment(iterateError)) {
        if (iterator->path().extension() != ".toml") {
          continue;
        }
        std::string readError;
        const std::optional<std::string> text =
            platform::readFile(iterator->path(), readError);
        if (!text.has_value()) {
          continue;
        }
        std::string parseError;
        std::optional<toml::Value> document = toml::parse(*text, parseError);
        if (!document.has_value()) {
          continue;
        }
        CatalogEntry entry;
        entry.name = document->string("name").value_or(
            iterator->path().stem().string());
        entry.description = document->string("description").value_or("");
        const toml::Value* versions = document->get("version");
        if (versions != nullptr && versions->isArray()) {
          for (const toml::Value& version : versions->elements()) {
            const std::string versionText =
                version.string("version").value_or("");
            if (versionText.empty()) {
              continue;
            }
            bool newer = entry.latest.empty();
            if (!newer) {
              const std::optional<semver::Version> current =
                  semver::Version::parse(entry.latest);
              const std::optional<semver::Version> candidate =
                  semver::Version::parse(versionText);
              newer = candidate.has_value() &&
                      (!current.has_value() || *candidate > *current);
            }
            if (newer) {
              entry.latest = versionText;
            }
          }
        }
        const std::string haystack =
            lowerAscii(entry.name + " " + entry.description);
        if (needle.empty() || haystack.find(needle) != std::string::npos) {
          result.push_back(std::move(entry));
        }
      }
    }
  }
  if (result.empty() && !catalogError.empty()) {
    error = catalogError;
  }
  std::sort(result.begin(), result.end(),
            [](const CatalogEntry& left, const CatalogEntry& right) {
              return left.name < right.name;
            });
  return result;
}

}  // namespace coord::package
