#include "coord/modules/mirror.h"

#include "coord/modules/net.h"
#include "coord/modules/platform.h"
#include "coord/modules/toml.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
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

bool looksLikeLocation(std::string_view text) {
  return net::hasScheme(text) || text.find('/') != std::string_view::npos ||
         text.find('\\') != std::string_view::npos ||
         (!text.empty() && text.front() == '.');
}

std::int64_t nowSeconds() {
  return static_cast<std::int64_t>(std::time(nullptr));
}

const MirrorEntry* findMirror(const std::vector<MirrorEntry>& mirrors,
                              std::string_view name) {
  for (const MirrorEntry& mirror : mirrors) {
    if (mirror.name == name) {
      return &mirror;
    }
  }
  return nullptr;
}

MirrorEntry* findMirror(std::vector<MirrorEntry>& mirrors,
                        std::string_view name) {
  for (MirrorEntry& mirror : mirrors) {
    if (mirror.name == name) {
      return &mirror;
    }
  }
  return nullptr;
}

}  // namespace

std::string normalizeRegistryUrl(std::string_view url) {
  std::string text = trim(url);
  while (text.size() > 1 &&
         (text.back() == '/' || text.back() == '\\')) {
    text.pop_back();
  }
  return text;
}

bool isSupportedRegistryUrl(std::string_view url) {
  const std::string text = normalizeRegistryUrl(url);
  if (text.empty()) {
    return false;
  }
  return net::isRemoteUrl(text) || net::isLocalUrl(text);
}

MirrorManager::MirrorManager() {
  config_.registries.push_back(
      {"official", "https://registry.coordination-lang.org"});
  config_.mirrors.push_back({"official", "official",
                             "https://registry.coordination-lang.org", 0, true,
                             "default registry endpoint"});
  config_.mirrors.push_back(
      {"cn", "official", "https://cn.registry.coordination-lang.org", 10, false,
       "Asia-Pacific mirror (enable with `coorc mirror use cn`)"});
  config_.mirrors.push_back(
      {"eu", "official", "https://eu.registry.coordination-lang.org", 5, false,
       "European mirror (enable with `coorc mirror use eu`)"});
}

MirrorManager MirrorManager::builtins() { return MirrorManager(); }

void MirrorManager::mergeMirror(const MirrorEntry& mirror) {
  if (MirrorEntry* existing = findMirror(config_.mirrors, mirror.name);
      existing != nullptr) {
    *existing = mirror;
  } else {
    config_.mirrors.push_back(mirror);
  }
}

void MirrorManager::mergeRegistry(const RegistryEntry& registry) {
  for (RegistryEntry& existing : config_.registries) {
    if (existing.name == registry.name) {
      const std::string previousUrl = normalizeRegistryUrl(existing.url);
      const std::string nextUrl = normalizeRegistryUrl(registry.url);
      existing.url = registry.url;
      if (!previousUrl.empty() && previousUrl != nextUrl) {
        // Mirrors that pointed at the previous endpoint are replicas of a
        // repository that is no longer in use; drop them so an overridden
        // local registry does not fall back to a remote default.
        config_.mirrors.erase(
            std::remove_if(config_.mirrors.begin(), config_.mirrors.end(),
                           [&](const MirrorEntry& mirror) {
                             return normalizeRegistryUrl(mirror.url) ==
                                    previousUrl;
                           }),
            config_.mirrors.end());
      }
      return;
    }
  }
  config_.registries.push_back(registry);
}

bool MirrorManager::loadUserConfig(std::string& error) {
  const std::filesystem::path path = configPath();
  std::error_code existsError;
  if (!std::filesystem::exists(path, existsError)) {
    return true;
  }
  std::string readError;
  const std::optional<std::string> text = platform::readFile(path, readError);
  if (!text.has_value()) {
    error = readError;
    return false;
  }
  std::string parseError;
  std::optional<toml::Value> document = toml::parse(*text, parseError);
  if (!document.has_value()) {
    error = platform::pathToUtf8(path) + ": " + parseError;
    return false;
  }
  if (const toml::Value* defaults = document->get("mirror");
      defaults != nullptr) {
    if (!defaults->isTable()) {
      error = "[mirror] must be a table";
      return false;
    }
    for (std::size_t i = 0; i < defaults->keys().size(); ++i) {
      const std::string& key = defaults->keys()[i];
      const toml::Value& value = defaults->values()[i];
      if (key == "default") {
        if (!value.isString()) {
          error = "mirror.default must be a string";
          return false;
        }
        config_.defaultMirror = value.asString();
      } else if (key == "registry") {
        if (!value.isString()) {
          error = "mirror.registry must be a string";
          return false;
        }
        config_.defaultRegistry = value.asString();
      } else if (key == "offline") {
        if (!value.isBoolean()) {
          error = "mirror.offline must be a boolean";
          return false;
        }
        config_.offline = value.asBoolean();
      } else if (key == "timeout-ms") {
        if (!value.isInteger() || value.asInteger() <= 0) {
          error = "mirror.timeout-ms must be a positive integer";
          return false;
        }
        config_.timeoutMs = static_cast<int>(value.asInteger());
      } else {
        error = "unknown [mirror] field '" + key + "'";
        return false;
      }
    }
  }
  if (const toml::Value* registries = document->get("registries");
      registries != nullptr) {
    if (!registries->isTable()) {
      error = "[registries] must be a table";
      return false;
    }
    for (std::size_t i = 0; i < registries->keys().size(); ++i) {
      const std::string& name = registries->keys()[i];
      const toml::Value& value = registries->values()[i];
      if (!value.isString() || value.asString().empty()) {
        error = "registry '" + name + "' must be a non-empty URL string";
        return false;
      }
      if (!isSupportedRegistryUrl(value.asString())) {
        error = "registry '" + name + "' has an unsupported URL '" +
                value.asString() + "'";
        return false;
      }
      mergeRegistry({name, normalizeRegistryUrl(value.asString())});
    }
  }
  if (const toml::Value* mirrors = document->get("mirrors");
      mirrors != nullptr) {
    if (!mirrors->isArray()) {
      error = "[[mirrors]] must be an array of tables";
      return false;
    }
    for (const toml::Value& element : mirrors->elements()) {
      if (!element.isTable()) {
        error = "[[mirrors]] entries must be tables";
        return false;
      }
      MirrorEntry mirror;
      for (std::size_t i = 0; i < element.keys().size(); ++i) {
        const std::string& key = element.keys()[i];
        const toml::Value& value = element.values()[i];
        if (key == "name") {
          if (!value.isString()) {
            error = "mirror.name must be a string";
            return false;
          }
          mirror.name = value.asString();
        } else if (key == "url") {
          if (!value.isString()) {
            error = "mirror.url must be a string";
            return false;
          }
          mirror.url = value.asString();
        } else if (key == "registry") {
          if (!value.isString()) {
            error = "mirror.registry must be a string";
            return false;
          }
          mirror.registry = value.asString();
        } else if (key == "priority") {
          if (!value.isInteger()) {
            error = "mirror.priority must be an integer";
            return false;
          }
          mirror.priority = static_cast<int>(value.asInteger());
        } else if (key == "enabled") {
          if (!value.isBoolean()) {
            error = "mirror.enabled must be a boolean";
            return false;
          }
          mirror.enabled = value.asBoolean();
        } else if (key == "description") {
          if (!value.isString()) {
            error = "mirror.description must be a string";
            return false;
          }
          mirror.description = value.asString();
        } else {
          error = "unknown [[mirrors]] field '" + key + "'";
          return false;
        }
      }
      if (mirror.name.empty() || mirror.url.empty()) {
        error = "[[mirrors]] entry requires 'name' and 'url'";
        return false;
      }
      mirror.url = normalizeRegistryUrl(mirror.url);
      if (!isSupportedRegistryUrl(mirror.url)) {
        error = "mirror '" + mirror.name + "' has an unsupported URL '" +
                mirror.url + "'";
        return false;
      }
      mergeMirror(mirror);
    }
  }
  return true;
}

bool MirrorManager::loadProjectConfig(const Manifest& manifest,
                                      std::string& error) {
  for (const RegistryEntry& registry : manifest.registries) {
    if (!isSupportedRegistryUrl(registry.url)) {
      error = "registry '" + registry.name + "' has an unsupported URL '" +
              registry.url + "'";
      return false;
    }
    mergeRegistry({registry.name, normalizeRegistryUrl(registry.url)});
  }
  for (const MirrorEntry& mirror : manifest.mirrors) {
    if (!isSupportedRegistryUrl(mirror.url)) {
      error = "mirror '" + mirror.name + "' has an unsupported URL '" +
              mirror.url + "'";
      return false;
    }
    MirrorEntry normalized = mirror;
    normalized.url = normalizeRegistryUrl(mirror.url);
    mergeMirror(normalized);
  }
  if (!manifest.registry.empty()) {
    config_.defaultRegistry = manifest.registry;
  }
  if (!manifest.mirror.empty()) {
    config_.defaultMirror = manifest.mirror;
    if (MirrorEntry* entry = findMirror(config_.mirrors, manifest.mirror);
        entry != nullptr) {
      entry->enabled = true;
    }
  }
  return true;
}

void MirrorManager::applyEnvironment() {
  if (platform::environmentFlag("COORD_OFFLINE")) {
    config_.offline = true;
  }
  if (const std::optional<std::string> timeout =
          platform::environment("COORD_TIMEOUT_MS");
      timeout.has_value() && !timeout->empty()) {
    const long value = std::strtol(timeout->c_str(), nullptr, 10);
    if (value > 0) {
      config_.timeoutMs = static_cast<int>(value);
    }
  }
  if (const std::optional<std::string> registry =
          platform::environment("COORD_REGISTRY");
      registry.has_value() && !registry->empty()) {
    const std::string url = normalizeRegistryUrl(*registry);
    if (isSupportedRegistryUrl(url)) {
      mergeRegistry({"official", url});
      MirrorEntry entry{"official", "official", url, 100, true,
                        "COORD_REGISTRY"};
      mergeMirror(entry);
    }
  }
  if (const std::optional<std::string> mirror =
          platform::environment("COORD_MIRROR");
      mirror.has_value() && !mirror->empty()) {
    if (looksLikeLocation(*mirror)) {
      const std::string url = normalizeRegistryUrl(*mirror);
      if (isSupportedRegistryUrl(url)) {
        mergeMirror({"env", config_.defaultRegistry, url, 1000, true,
                     "COORD_MIRROR"});
        config_.defaultMirror = "env";
      }
    } else {
      config_.defaultMirror = *mirror;
      if (MirrorEntry* entry = findMirror(config_.mirrors, *mirror);
          entry != nullptr) {
        entry->enabled = true;
      }
    }
  }
}

bool MirrorManager::selectMirror(std::string_view nameOrUrl,
                                 std::string& error) {
  const std::string value = trim(nameOrUrl);
  if (value.empty()) {
    error = "empty mirror name";
    return false;
  }
  if (looksLikeLocation(value)) {
    const std::string url = normalizeRegistryUrl(value);
    if (!isSupportedRegistryUrl(url)) {
      error = "unsupported mirror URL '" + value + "'";
      return false;
    }
    mergeMirror({"cli", config_.defaultRegistry, url, 1000, true,
                 "command line"});
    config_.defaultMirror = "cli";
    return true;
  }
  if (MirrorEntry* entry = findMirror(config_.mirrors, value);
      entry != nullptr) {
    entry->enabled = true;
    config_.defaultMirror = value;
    return true;
  }
  for (const RegistryEntry& registry : config_.registries) {
    if (registry.name == value) {
      config_.defaultRegistry = value;
      config_.defaultMirror.clear();
      return true;
    }
  }
  error = "unknown mirror or registry '" + value + "'";
  return false;
}

bool MirrorManager::selectRegistry(std::string_view nameOrUrl,
                                   std::string& error) {
  const std::string value = trim(nameOrUrl);
  if (value.empty()) {
    error = "empty registry name";
    return false;
  }
  if (looksLikeLocation(value)) {
    const std::string url = normalizeRegistryUrl(value);
    if (!isSupportedRegistryUrl(url)) {
      error = "unsupported registry URL '" + value + "'";
      return false;
    }
    mergeRegistry({"cli", url});
    mergeMirror({"cli", "cli", url, 1000, true, "command line"});
    config_.defaultRegistry = "cli";
    config_.defaultMirror = "cli";
    return true;
  }
  for (const RegistryEntry& registry : config_.registries) {
    if (registry.name == value) {
      config_.defaultRegistry = value;
      return true;
    }
  }
  error = "unknown registry '" + value + "'";
  return false;
}

std::optional<RegistryEntry> MirrorManager::registry(
    std::string_view nameOrUrl) const {
  std::string name = trim(nameOrUrl);
  if (name.empty()) {
    name = config_.defaultRegistry;
  }
  for (const RegistryEntry& entry : config_.registries) {
    if (entry.name == name) {
      return entry;
    }
  }
  if (looksLikeLocation(name) && isSupportedRegistryUrl(name)) {
    return RegistryEntry{name, normalizeRegistryUrl(name)};
  }
  return std::nullopt;
}

const MirrorEntry* MirrorManager::mirror(std::string_view name) const {
  return findMirror(config_.mirrors, name);
}

std::vector<MirrorEntry> MirrorManager::candidates(
    std::string_view registryNameOrUrl,
    std::string_view preferredMirror) const {
  std::vector<MirrorEntry> result;
  const std::optional<RegistryEntry> entry = registry(registryNameOrUrl);
  if (!entry.has_value()) {
    return result;
  }
  std::vector<std::string> seen;
  const auto add = [&](const MirrorEntry& mirror, bool explicitChoice) {
    const std::string url = normalizeRegistryUrl(mirror.url);
    if (url.empty()) {
      return;
    }
    if (std::find(seen.begin(), seen.end(), url) != seen.end()) {
      return;
    }
    if (!explicitChoice && isSkipped(mirror)) {
      return;
    }
    seen.push_back(url);
    result.push_back(mirror);
  };
  if (!preferredMirror.empty()) {
    if (const MirrorEntry* preferred = mirror(preferredMirror);
        preferred != nullptr) {
      add(*preferred, true);
    } else if (looksLikeLocation(preferredMirror) &&
               isSupportedRegistryUrl(preferredMirror)) {
      add(MirrorEntry{"cli", entry->name,
                      normalizeRegistryUrl(preferredMirror), 1000, true,
                      "preferred"},
          true);
    }
  }
  std::vector<MirrorEntry> enabled;
  for (const MirrorEntry& mirror : config_.mirrors) {
    if (!mirror.enabled || mirror.registry != entry->name) {
      continue;
    }
    enabled.push_back(mirror);
  }
  std::sort(enabled.begin(), enabled.end(),
            [](const MirrorEntry& left, const MirrorEntry& right) {
              if (left.priority != right.priority) {
                return left.priority > right.priority;
              }
              return left.name < right.name;
            });
  for (const MirrorEntry& mirror : enabled) {
    add(mirror, false);
  }
  add(MirrorEntry{entry->name, entry->name, entry->url, 0, true, "registry"},
      false);
  return result;
}

MirrorProbe MirrorManager::probe(const MirrorEntry& mirror,
                                 std::chrono::milliseconds timeout) {
  MirrorProbe result;
  net::TransportOptions options;
  options.timeout = timeout.count() > 0
                        ? timeout
                        : std::chrono::milliseconds(config_.timeoutMs);
  const net::FetchResult health =
      net::fetch(net::joinUrl(mirror.url, "health"), options);
  if (health.ok) {
    result.reachable = true;
    result.latencyMs = health.latencyMs;
    result.detail = "health endpoint";
    return result;
  }
  const net::FetchResult index =
      net::fetch(net::joinUrl(mirror.url, "index.toml"), options);
  if (index.ok) {
    result.reachable = true;
    result.latencyMs = index.latencyMs;
    result.detail = "index";
    return result;
  }
  result.reachable = false;
  result.latencyMs = index.latencyMs;
  result.detail = index.error.empty() ? health.error : index.error;
  return result;
}

void MirrorManager::recordFailure(const MirrorEntry& mirror) {
  HealthEntry& entry = health_[normalizeRegistryUrl(mirror.url)];
  entry.url = normalizeRegistryUrl(mirror.url);
  ++entry.failures;
  entry.lastFailure = nowSeconds();
}

void MirrorManager::recordSuccess(const MirrorEntry& mirror,
                                  long long latencyMs) {
  HealthEntry& entry = health_[normalizeRegistryUrl(mirror.url)];
  entry.url = normalizeRegistryUrl(mirror.url);
  entry.failures = 0;
  entry.lastSuccess = nowSeconds();
  entry.latencyMs = latencyMs;
}

bool MirrorManager::isSkipped(const MirrorEntry& mirror) const {
  const auto found = health_.find(normalizeRegistryUrl(mirror.url));
  if (found == health_.end()) {
    return false;
  }
  const HealthEntry& entry = found->second;
  if (entry.failures < 2) {
    return false;
  }
  return nowSeconds() - entry.lastFailure < 300;
}

std::filesystem::path MirrorManager::configPath() const {
  if (const std::optional<std::string> configured =
          platform::environment("COORD_CONFIG");
      configured.has_value() && !configured->empty()) {
    return platform::pathFromUtf8(*configured);
  }
  return platform::configDirectory("coord") / "config.toml";
}

std::filesystem::path MirrorManager::cacheRoot() const {
  if (const std::optional<std::string> configured =
          platform::environment("COORD_CACHE_DIR");
      configured.has_value() && !configured->empty()) {
    return platform::pathFromUtf8(*configured);
  }
  return platform::cacheDirectory("coord");
}

std::filesystem::path MirrorManager::healthCachePath() const {
  return cacheRoot() / "mirror-health.toml";
}

void MirrorManager::loadHealthCache() {
  const std::filesystem::path path = healthCachePath();
  std::string error;
  const std::optional<std::string> text = platform::readFile(path, error);
  if (!text.has_value()) {
    return;
  }
  std::string parseError;
  std::optional<toml::Value> document = toml::parse(*text, parseError);
  if (!document.has_value()) {
    return;
  }
  const toml::Value* mirrors = document->get("mirror");
  if (mirrors == nullptr || !mirrors->isArray()) {
    return;
  }
  for (const toml::Value& element : mirrors->elements()) {
    if (!element.isTable()) {
      continue;
    }
    HealthEntry entry;
    entry.url = element.string("url").value_or("");
    if (entry.url.empty()) {
      continue;
    }
    entry.failures = static_cast<int>(element.integer("failures").value_or(0));
    entry.lastFailure = element.integer("last-failure").value_or(0);
    entry.lastSuccess = element.integer("last-success").value_or(0);
    entry.latencyMs = element.integer("latency-ms").value_or(0);
    health_[entry.url] = std::move(entry);
  }
}

bool MirrorManager::saveHealthCache(std::string& error) const {
  toml::Value root = toml::Value::makeTable();
  toml::Value mirrors = toml::Value::makeArray();
  for (const auto& [url, entry] : health_) {
    toml::Value value = toml::Value::makeTable();
    value.set("url", toml::Value::makeString(url));
    value.set("failures", toml::Value::makeInteger(entry.failures));
    if (entry.lastFailure != 0) {
      value.set("last-failure", toml::Value::makeInteger(entry.lastFailure));
    }
    if (entry.lastSuccess != 0) {
      value.set("last-success", toml::Value::makeInteger(entry.lastSuccess));
    }
    if (entry.latencyMs != 0) {
      value.set("latency-ms", toml::Value::makeInteger(entry.latencyMs));
    }
    mirrors.push(std::move(value));
  }
  root.set("mirror", std::move(mirrors));
  const std::string text = toml::write(root);
  const std::filesystem::path path = healthCachePath();
  std::string directoryError;
  if (!platform::ensureDirectory(path.parent_path(), directoryError)) {
    error = directoryError;
    return false;
  }
  return platform::writeFileAtomic(path, text, error);
}

std::string MirrorManager::describe() const {
  toml::Value root = toml::Value::makeTable();
  toml::Value defaults = toml::Value::makeTable();
  defaults.set("registry", toml::Value::makeString(config_.defaultRegistry));
  if (!config_.defaultMirror.empty()) {
    defaults.set("default", toml::Value::makeString(config_.defaultMirror));
  }
  defaults.set("offline", toml::Value::makeBoolean(config_.offline));
  defaults.set("timeout-ms", toml::Value::makeInteger(config_.timeoutMs));
  root.set("mirror", std::move(defaults));
  toml::Value registries = toml::Value::makeTable();
  for (const RegistryEntry& registry : config_.registries) {
    registries.set(registry.name, toml::Value::makeString(registry.url));
  }
  root.set("registries", std::move(registries));
  toml::Value mirrors = toml::Value::makeArray();
  for (const MirrorEntry& mirror : config_.mirrors) {
    toml::Value entry = toml::Value::makeTable();
    entry.set("name", toml::Value::makeString(mirror.name));
    entry.set("registry", toml::Value::makeString(mirror.registry));
    entry.set("url", toml::Value::makeString(mirror.url));
    if (mirror.priority != 0) {
      entry.set("priority", toml::Value::makeInteger(mirror.priority));
    }
    entry.set("enabled", toml::Value::makeBoolean(mirror.enabled));
    if (!mirror.description.empty()) {
      entry.set("description", toml::Value::makeString(mirror.description));
    }
    mirrors.push(std::move(entry));
  }
  root.set("mirrors", std::move(mirrors));
  return toml::write(root);
}

bool MirrorManager::saveUserConfig(std::string& error) const {
  const std::filesystem::path path = configPath();
  std::string directoryError;
  if (!platform::ensureDirectory(path.parent_path(), directoryError)) {
    error = directoryError;
    return false;
  }
  return platform::writeFileAtomic(path, describe(), error);
}

}  // namespace coord::package
