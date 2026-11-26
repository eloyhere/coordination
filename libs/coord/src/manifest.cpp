#include "coord/modules/manifest.h"

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

[[nodiscard]] bool readTextFile(const std::filesystem::path& path,
                                std::string& text, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot read manifest '" + platform::pathToUtf8(path) + "'";
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  text = buffer.str();
  if (text.size() >= 3 &&
      static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }
  return true;
}

[[nodiscard]] bool parseRequirement(DependencySpec& spec, std::string& error) {
  if (spec.rawRequirement.empty()) {
    spec.rawRequirement = "*";
  }
  std::string requirementError;
  std::optional<semver::Requirement> parsed =
      semver::Requirement::parse(spec.rawRequirement, &requirementError);
  if (!parsed.has_value()) {
    error = "invalid version requirement '" + spec.rawRequirement +
            "' for dependency '" + spec.name + "': " + requirementError;
    return false;
  }
  spec.requirement = std::move(*parsed);
  return true;
}

[[nodiscard]] bool parseLegacyDependency(std::string_view value,
                                         DependencyKind kind,
                                         const std::filesystem::path& root,
                                         DependencySpec& spec,
                                         std::string& error) {
  std::string text = trim(value);
  if (text.empty()) {
    error = "empty dependency entry";
    return false;
  }
  spec.kind = kind;
  if (text.rfind("path:", 0) == 0) {
    text = trim(text.substr(5));
  }
  const bool looksLikePath =
      text.rfind("path:", 0) == 0 || text.front() == '.' ||
      text.front() == '/' || text.front() == '\\' ||
      text.find('/') != std::string::npos ||
      text.find('\\') != std::string::npos ||
      (text.size() >= 10 &&
       text.rfind("coordination.toml") == text.size() - 10);
  if (looksLikePath) {
    spec.pathSpec = text;
    spec.path = (root / platform::pathFromUtf8(text)).lexically_normal();
    spec.name.clear();
    return true;
  }
  std::string name;
  std::string requirement;
  const std::size_t at = text.find('@');
  if (at != std::string::npos) {
    name = trim(text.substr(0, at));
    requirement = trim(text.substr(at + 1));
  } else {
    const std::size_t space = text.find_first_of(" \t");
    if (space == std::string::npos) {
      name = text;
    } else {
      name = trim(text.substr(0, space));
      requirement = trim(text.substr(space + 1));
    }
  }
  if (!isValidPackageName(name)) {
    error = "invalid dependency name '" + name + "'";
    return false;
  }
  spec.name = name;
  spec.rawRequirement = requirement.empty() ? "*" : requirement;
  return parseRequirement(spec, error);
}

[[nodiscard]] std::optional<Manifest> parseLegacy(
    std::string_view text, const std::filesystem::path& manifestPath,
    std::string& error) {
  Manifest manifest;
  manifest.path = manifestPath;
  manifest.root = manifestPath.parent_path();
  manifest.legacy = true;
  manifest.source = ".";
  std::istringstream lines{std::string(text)};
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(lines, line)) {
    ++lineNumber;
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line.erase(comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    const std::size_t space = line.find_first_of(" \t");
    const std::string field =
        space == std::string::npos ? line : line.substr(0, space);
    const std::string value =
        space == std::string::npos ? std::string() : trim(line.substr(space + 1));
    const auto fail = [&](const std::string& message)
        -> std::optional<Manifest> {
      error = platform::pathToUtf8(manifestPath) + ":" +
              std::to_string(lineNumber) + ": " + message;
      return std::nullopt;
    };
    if (field == "name") {
      manifest.name = value;
    } else if (field == "version") {
      std::string versionError;
      const std::optional<semver::Version> version =
          semver::Version::parse(value, &versionError);
      if (!version.has_value()) {
        return fail("invalid version '" + value + "': " + versionError);
      }
      manifest.version = *version;
    } else if (field == "edition") {
      manifest.edition = value;
    } else if (field == "source") {
      manifest.source = value.empty() ? "." : value;
    } else if (field == "description") {
      manifest.description = value;
    } else if (field == "license") {
      manifest.license = value;
    } else if (field == "repository") {
      manifest.repository = value;
    } else if (field == "homepage") {
      manifest.homepage = value;
    } else if (field == "registry") {
      manifest.registry = value;
    } else if (field == "mirror") {
      manifest.mirror = value;
    } else if (field == "dependency" || field == "dev-dependency" ||
               field == "build-dependency") {
      const DependencyKind kind =
          field == "dev-dependency"
              ? DependencyKind::Development
              : (field == "build-dependency" ? DependencyKind::Build
                                             : DependencyKind::Normal);
      DependencySpec spec;
      if (!parseLegacyDependency(value, kind, manifest.root, spec, error)) {
        return fail(error);
      }
      manifest.dependencies.push_back(std::move(spec));
    } else {
      return fail("unknown package field '" + field + "'");
    }
  }
  if (manifest.name.empty()) {
    error = platform::pathToUtf8(manifestPath) + ": package has no name";
    return std::nullopt;
  }
  if (!isValidPackageName(manifest.name)) {
    error = platform::pathToUtf8(manifestPath) + ": invalid package name '" +
            manifest.name + "'";
    return std::nullopt;
  }
  return manifest;
}

[[nodiscard]] bool readStringField(const toml::Value& table,
                                   std::string_view key, std::string& target,
                                   const std::string& context,
                                   std::string& error) {
  const toml::Value* value = table.get(key);
  if (value == nullptr) {
    return true;
  }
  if (!value->isString()) {
    error = context + ": '" + std::string(key) + "' must be a string";
    return false;
  }
  target = value->asString();
  return true;
}

[[nodiscard]] bool readRequirementValue(const toml::Value& value,
                                        DependencySpec& spec,
                                        const std::string& context,
                                        std::string& error) {
  if (value.isString()) {
    spec.rawRequirement = value.asString();
    return true;
  }
  if (!value.isTable()) {
    error = context + " must be a version string or a table";
    return false;
  }
  for (std::size_t i = 0; i < value.keys().size(); ++i) {
    const std::string& key = value.keys()[i];
    const toml::Value& field = value.values()[i];
    if (key == "version") {
      if (!field.isString()) {
        error = context + ": 'version' must be a string";
        return false;
      }
      spec.rawRequirement = field.asString();
    } else if (key == "path") {
      if (!field.isString()) {
        error = context + ": 'path' must be a string";
        return false;
      }
      // Resolved against the manifest root by the caller.
      spec.pathSpec = field.asString();
    } else if (key == "registry") {
      if (!field.isString()) {
        error = context + ": 'registry' must be a string";
        return false;
      }
      spec.registry = field.asString();
    } else if (key == "mirror") {
      if (!field.isString()) {
        error = context + ": 'mirror' must be a string";
        return false;
      }
      spec.mirror = field.asString();
    } else if (key == "platform") {
      if (!field.isString()) {
        error = context + ": 'platform' must be a string";
        return false;
      }
      spec.platform = field.asString();
    } else if (key == "optional") {
      if (!field.isBoolean()) {
        error = context + ": 'optional' must be a boolean";
        return false;
      }
      spec.optional = field.asBoolean();
    } else if (key == "requires") {
      // Advisory ordering hints used by the M0 dependency scanner; the
      // resolver uses the real package manifests instead.
    } else {
      error = context + ": unknown dependency field '" + key + "'";
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool readDependencyTable(const toml::Value& table,
                                       DependencyKind kind,
                                       const std::filesystem::path& root,
                                       std::vector<DependencySpec>& output,
                                       std::string& error) {
  for (std::size_t i = 0; i < table.keys().size(); ++i) {
    const std::string& name = table.keys()[i];
    const toml::Value& value = table.values()[i];
    if (!isValidPackageName(name)) {
      error = "invalid dependency name '" + name + "'";
      return false;
    }
    DependencySpec spec;
    spec.name = name;
    spec.kind = kind;
    if (!readRequirementValue(value, spec, "dependency '" + name + "'", error)) {
      return false;
    }
    if (!spec.pathSpec.empty()) {
      spec.path =
          (root / platform::pathFromUtf8(spec.pathSpec)).lexically_normal();
    }
    if (!parseRequirement(spec, error)) {
      return false;
    }
    output.push_back(std::move(spec));
  }
  return true;
}

[[nodiscard]] std::optional<Manifest> parseToml(
    std::string_view text, const std::filesystem::path& manifestPath,
    std::string& error) {
  std::string parseError;
  std::optional<toml::Value> document = toml::parse(text, parseError);
  if (!document.has_value()) {
    error = platform::pathToUtf8(manifestPath) + ": " + parseError;
    return std::nullopt;
  }
  Manifest manifest;
  manifest.path = manifestPath;
  manifest.root = manifestPath.parent_path();
  const toml::Value* package = document->get("package");
  const bool hasDependencies = document->get("dependencies") != nullptr ||
                               document->get("dev-dependencies") != nullptr;
  if (package == nullptr) {
    if (!hasDependencies) {
      error = platform::pathToUtf8(manifestPath) +
              ": missing [package] table";
      return std::nullopt;
    }
    // Dependency-only documents (used by `coorc deps`) get a synthetic name.
    manifest.name = manifest.root.filename().string();
    if (manifest.name.empty()) {
      manifest.name = "workspace";
    }
  } else {
    if (!package->isTable()) {
      error = platform::pathToUtf8(manifestPath) +
              ": [package] must be a table";
      return std::nullopt;
    }
    if (const toml::Value* name = package->get("name");
        name != nullptr && name->isString()) {
      manifest.name = name->asString();
    }
    if (const toml::Value* version = package->get("version");
        version != nullptr) {
      if (!version->isString()) {
        error = "package.version must be a string";
        return std::nullopt;
      }
      std::string versionError;
      const std::optional<semver::Version> parsed =
          semver::Version::parse(version->asString(), &versionError);
      if (!parsed.has_value()) {
        error = "invalid package version '" + version->asString() + "': " +
                versionError;
        return std::nullopt;
      }
      manifest.version = *parsed;
    }
    const std::string context = "package";
    struct Field {
      const char* name;
      std::string* target;
    };
    const Field fields[] = {
        {"edition", &manifest.edition},   {"source", &manifest.source},
        {"description", &manifest.description},
        {"license", &manifest.license},   {"repository", &manifest.repository},
        {"homepage", &manifest.homepage}, {"registry", &manifest.registry},
        {"mirror", &manifest.mirror},
    };
    for (const Field& field : fields) {
      if (!readStringField(*package, field.name, *field.target, context,
                           error)) {
        return std::nullopt;
      }
    }
    if (const toml::Value* members = package->get("members");
        members != nullptr) {
      if (!members->isArray()) {
        error = "package.members must be an array";
        return std::nullopt;
      }
      for (const toml::Value& member : members->elements()) {
        if (!member.isString()) {
          error = "package.members must contain strings";
          return std::nullopt;
        }
        manifest.members.push_back(member.asString());
      }
    }
    for (std::size_t i = 0; i < package->keys().size(); ++i) {
      const std::string& key = package->keys()[i];
      static const char* const kKnown[] = {
          "name",        "version",  "edition",    "source",
          "description", "license",  "repository", "homepage",
          "registry",    "mirror",   "members",    "authors",
          "keywords",    "readme",   "categories", "documentation"};
      bool known = false;
      for (const char* candidate : kKnown) {
        if (key == candidate) {
          known = true;
          break;
        }
      }
      if (!known) {
        error = "unknown [package] field '" + key + "'";
        return std::nullopt;
      }
    }
  }
  if (const toml::Value* workspace = document->get("workspace");
      workspace != nullptr) {
    if (!workspace->isTable()) {
      error = "[workspace] must be a table";
      return std::nullopt;
    }
    if (const toml::Value* members = workspace->get("members");
        members != nullptr && members->isArray()) {
      for (const toml::Value& member : members->elements()) {
        if (member.isString()) {
          manifest.members.push_back(member.asString());
        }
      }
    }
  }
  if (const toml::Value* trust = document->get("trust");
      trust != nullptr) {
    if (!trust->isTable()) {
      error = "[trust] must be a table";
      return std::nullopt;
    }
    for (std::size_t i = 0; i < trust->keys().size(); ++i) {
      const std::string& key = trust->keys()[i];
      const toml::Value& value = trust->values()[i];
      if (key == "require_checksum" && value.isBoolean()) {
        manifest.requireChecksum = value.asBoolean();
      } else if (key == "allow_insecure_http" && value.isBoolean()) {
        manifest.allowInsecureHttp = value.asBoolean();
      } else {
        error = "unknown [trust] field '" + key + "'";
        return std::nullopt;
      }
    }
  }
  struct DependencyTable {
    const char* name;
    DependencyKind kind;
  };
  const DependencyTable dependencyTables[] = {
      {"dependencies", DependencyKind::Normal},
      {"dev-dependencies", DependencyKind::Development},
      {"build-dependencies", DependencyKind::Build},
  };
  for (const DependencyTable& entry : dependencyTables) {
    const toml::Value* table = document->get(entry.name);
    if (table == nullptr) {
      continue;
    }
    if (!table->isTable()) {
      error = std::string("[") + entry.name + "] must be a table";
      return std::nullopt;
    }
    if (!readDependencyTable(*table, entry.kind, manifest.root,
                             manifest.dependencies, error)) {
      return std::nullopt;
    }
  }
  if (const toml::Value* registries = document->get("registries");
      registries != nullptr) {
    if (!registries->isTable()) {
      error = "[registries] must be a table";
      return std::nullopt;
    }
    for (std::size_t i = 0; i < registries->keys().size(); ++i) {
      const std::string& name = registries->keys()[i];
      const toml::Value& value = registries->values()[i];
      if (!value.isString()) {
        error = "registry '" + name + "' must be a URL string";
        return std::nullopt;
      }
      manifest.registries.push_back({name, value.asString()});
    }
  }
  if (const toml::Value* mirrors = document->get("mirrors");
      mirrors != nullptr) {
    if (!mirrors->isArray()) {
      error = "[[mirrors]] must be an array of tables";
      return std::nullopt;
    }
    for (const toml::Value& element : mirrors->elements()) {
      if (!element.isTable()) {
        error = "[[mirrors]] entries must be tables";
        return std::nullopt;
      }
      MirrorEntry mirror;
      if (const toml::Value* name = element.get("name");
          name != nullptr && name->isString()) {
        mirror.name = name->asString();
      }
      if (const toml::Value* url = element.get("url");
          url != nullptr && url->isString()) {
        mirror.url = url->asString();
      }
      if (const toml::Value* registry = element.get("registry");
          registry != nullptr && registry->isString()) {
        mirror.registry = registry->asString();
      }
      if (const toml::Value* priority = element.get("priority");
          priority != nullptr && priority->isInteger()) {
        mirror.priority = static_cast<int>(priority->asInteger());
      }
      if (const toml::Value* enabled = element.get("enabled");
          enabled != nullptr && enabled->isBoolean()) {
        mirror.enabled = enabled->asBoolean();
      }
      if (const toml::Value* description = element.get("description");
          description != nullptr && description->isString()) {
        mirror.description = description->asString();
      }
      if (mirror.name.empty() || mirror.url.empty()) {
        error = "[[mirrors]] entry requires 'name' and 'url'";
        return std::nullopt;
      }
      manifest.mirrors.push_back(std::move(mirror));
    }
  }
  if (manifest.name.empty()) {
    error = platform::pathToUtf8(manifestPath) + ": package has no name";
    return std::nullopt;
  }
  if (!isValidPackageName(manifest.name)) {
    error = platform::pathToUtf8(manifestPath) + ": invalid package name '" +
            manifest.name + "'";
    return std::nullopt;
  }
  if (manifest.source.empty()) {
    manifest.source = "src";
  }
  return manifest;
}

}  // namespace

std::string_view dependencyKindName(DependencyKind kind) {
  switch (kind) {
    case DependencyKind::Normal:
      return "normal";
    case DependencyKind::Development:
      return "dev";
    case DependencyKind::Build:
      return "build";
  }
  return "normal";
}

std::string DependencySpec::display() const {
  if (isPath()) {
    return "path " + (pathSpec.empty() ? platform::pathToUtf8(*path)
                                       : pathSpec);
  }
  std::string text = name;
  if (!rawRequirement.empty() && rawRequirement != "*") {
    text += " " + rawRequirement;
  }
  if (!registry.empty()) {
    text += " (registry " + registry + ")";
  }
  return text;
}

const DependencySpec* Manifest::dependency(std::string_view packageName) const {
  for (const DependencySpec& spec : dependencies) {
    if (spec.name == packageName) {
      return &spec;
    }
  }
  return nullptr;
}

std::vector<const DependencySpec*> Manifest::dependenciesOf(
    DependencyKind kind, bool includeOptional) const {
  std::vector<const DependencySpec*> result;
  for (const DependencySpec& spec : dependencies) {
    if (spec.kind != kind || !spec.appliesToHost()) {
      continue;
    }
    if (spec.optional && !includeOptional) {
      continue;
    }
    result.push_back(&spec);
  }
  return result;
}

std::string Manifest::id() const {
  return name + "@" + version.toString();
}

std::optional<Manifest> Manifest::load(
    const std::filesystem::path& rootOrFile, std::string& error) {
  const std::optional<std::filesystem::path> path = findManifest(rootOrFile);
  if (!path.has_value()) {
    error = "no package manifest in '" + platform::pathToUtf8(rootOrFile) + "'";
    return std::nullopt;
  }
  return loadFile(*path, error);
}

std::optional<Manifest> Manifest::loadFile(
    const std::filesystem::path& manifestPath, std::string& error) {
  std::string text;
  if (!readTextFile(manifestPath, text, error)) {
    return std::nullopt;
  }
  return parse(text, manifestPath, error);
}

std::optional<Manifest> Manifest::parse(
    std::string_view text, const std::filesystem::path& manifestPath,
    std::string& error) {
  const std::string filename = manifestPath.filename().string();
  if (filename == "coord.pkg") {
    return parseLegacy(text, manifestPath, error);
  }
  return parseToml(text, manifestPath, error);
}

std::optional<std::filesystem::path> findManifest(
    const std::filesystem::path& rootOrFile) {
  std::error_code error;
  if (std::filesystem::is_regular_file(rootOrFile, error)) {
    return rootOrFile;
  }
  for (const char* name : {"coordination.toml", "coord.pkg"}) {
    const std::filesystem::path candidate = rootOrFile / name;
    if (std::filesystem::is_regular_file(candidate, error)) {
      return candidate;
    }
  }
  return std::nullopt;
}

bool isValidPackageName(std::string_view name) {
  if (name.empty() || name.size() > 128 || name == "." || name == "..") {
    return false;
  }
  const auto isAlphaNumeric = [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
  };
  if (!isAlphaNumeric(name.front())) {
    return false;
  }
  for (const char character : name) {
    if (!isAlphaNumeric(character) && character != '-' && character != '_' &&
        character != '.') {
      return false;
    }
  }
  if (name.back() == '.' || name.back() == '-') {
    return false;
  }
  return true;
}

std::vector<std::string> hostPlatformTags() {
  std::vector<std::string> tags;
  const std::string os = platform::operatingSystemName();
  tags.push_back(os);
  tags.push_back(platform::architectureName());
  if (os == "macos") {
    tags.push_back("darwin");
    tags.push_back("unix");
  } else if (os == "linux" || os == "freebsd") {
    tags.push_back("unix");
  }
  if (platform::architecture() == platform::Architecture::Arm64) {
    tags.push_back("arm64");
  }
  if (platform::architecture() == platform::Architecture::X64) {
    tags.push_back("amd64");
  }
  tags.push_back(os + "-" + platform::architectureName());
  return tags;
}

bool platformMatches(std::string_view spec) {
  const std::string lowered = lowerAscii(std::string(spec));
  if (lowered.empty()) {
    return true;
  }
  std::vector<std::string> tags = hostPlatformTags();
  for (std::string& tag : tags) {
    tag = lowerAscii(tag);
  }
  const auto contains = [&tags](const std::string& tag) {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
  };
  bool sawPositive = false;
  bool matched = false;
  std::size_t position = 0;
  while (position <= lowered.size()) {
    while (position < lowered.size() &&
           (lowered[position] == ' ' || lowered[position] == '\t' ||
            lowered[position] == ',')) {
      ++position;
    }
    const std::size_t begin = position;
    while (position < lowered.size() && lowered[position] != ' ' &&
           lowered[position] != '\t' && lowered[position] != ',') {
      ++position;
    }
    if (begin == position) {
      break;
    }
    std::string token = lowered.substr(begin, position - begin);
    const bool negated = token.front() == '!';
    if (negated) {
      token.erase(0, 1);
    }
    if (token == "all" || token == "*") {
      if (!negated) {
        sawPositive = true;
        matched = true;
      }
      continue;
    }
    const bool isTag = contains(token);
    if (negated) {
      if (isTag) {
        return false;
      }
    } else {
      sawPositive = true;
      if (isTag) {
        matched = true;
      }
    }
  }
  return sawPositive ? matched : true;
}

}  // namespace coord::package
