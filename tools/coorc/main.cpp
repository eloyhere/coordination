#include "coord/modules/compiler.h"
#include "coord/modules/dependency.h"
#include "coord/modules/manifest.h"
#include "coord/modules/mirror.h"
#include "coord/modules/package.h"
#include "coord/modules/package_manager.h"
#include "coord/modules/platform.h"
#include "coord/modules/toml.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace coord;

constexpr const char* kVersion = "0.2.0";

void printUsage(std::ostream& out) {
  out << "coorc " << kVersion << " - the Coordination compiler\n\n";
  out << "Usage:\n";
  out << "  coorc check   <file.coo | package-dir> [options]\n";
  out << "  coorc build   <file.coo | package-dir> [-o <app.exe>] [options]\n";
  out << "  coorc run     <file.coo | package-dir> [options]\n";
  out << "  coorc test    <file.coo | package-dir> [--filter <substring>]\n";
  out << "  coorc outline <file.coo> [--docs]\n";
  out << "  coorc deps    <manifest | package-dir>\n";
  out << "  coorc --emit-cpp <file.cpp | -> <file.coo>\n\n";
  out << "Packages:\n";
  out << "  coorc install [--locked | --frozen] [--offline] [--no-dev]\n";
  out << "  coorc update  [<package>...]\n";
  out << "  coorc add     <name[@requirement] | path> [--dev] [--path]\n";
  out << "  coorc remove  <name>\n";
  out << "  coorc list\n";
  out << "  coorc tree\n";
  out << "  coorc clean   [--cache]\n";
  out << "  coorc init    [dir] [--name <name>] [--force]\n";
  out << "  coorc pack    [-o <archive.tar.gz>]\n";
  out << "  coorc publish [--dry-run]\n";
  out << "  coorc search  <term>\n\n";
  out << "Registries and mirrors:\n";
  out << "  coorc mirror list | show | add <name> <url> | remove <name>\n";
  out << "  coorc mirror use <name> | test [name] | auto\n";
  out << "  coorc config list | get <key> | set <key> <value>\n\n";
  out << "Diagnostics:\n";
  out << "  coorc doctor [--network]\n";
  out << "  coorc platform\n\n";
  out << "Options:\n";
  out << "  -o <path>               output executable/archive path\n";
  out << "  --emit-cpp <path>       stop after C++ lowering; '-' means stdout\n";
  out << "  --emit-coox <path>      write a .coox interface summary\n";
  out << "  --cxx <path>            host C++ compiler (default: $COORD_CXX, $CXX, then cl/c++)\n";
  out << "  --cxx-flavor <msvc|gnu> force the host compiler argument style\n";
  out << "  --target <triple>       cross-compile for a target triple\n";
  out << "  --sysroot <path>        sysroot passed to clang/g++\n";
  out << "  --keep-cpp              keep the generated .cpp next to the executable\n";
  out << "  --incremental           skip project rebuild when sources are unchanged\n";
  out << "  --dump-tokens           print the token stream\n";
  out << "  --dump-ast              print the abstract syntax tree\n";
  out << "  --print-consts          print compile-time const values\n";
  out << "  --verbose               print commands and compiler diagnostics\n";
  out << "  -Werror                 treat warnings as errors\n";
  out << "  --manifest <path>       use a specific coordination.toml or coord.pkg\n";
  out << "  --mirror <name|url>     prefer a registry mirror\n";
  out << "  --registry <name|url>   select a registry\n";
  out << "  --offline               never touch the network\n";
  out << "  --locked                fail if coord.lock would change\n";
  out << "  --frozen                like --locked, and never write coord.lock\n";
  out << "  --no-dev                skip dev-dependencies\n";
  out << "  --all-optional          include optional dependencies\n";
  out << "  -h, --help              show this help\n";
  out << "  --version               show the compiler version\n";
}

bool isCompileCommand(const std::string& value) {
  return value == "check" || value == "build" || value == "run" ||
         value == "test" || value == "outline" || value == "deps";
}

bool isPackageCommand(const std::string& value) {
  return value == "install" || value == "update" || value == "add" ||
         value == "remove" || value == "list" || value == "tree" ||
         value == "clean" || value == "init" || value == "pack" ||
         value == "publish" || value == "search";
}

std::string lowerAscii(std::string text) {
  for (char& character : text) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

int printVersion() {
  std::cout << "coorc " << kVersion << " (" << platform::describeHost()
            << ")\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Compile commands (check/build/run/test/outline/deps)

int runDeps(const std::string& inputPath) {
  const std::filesystem::path path = platform::pathFromUtf8(inputPath);
  std::error_code pathError;
  if (std::filesystem::is_directory(path, pathError)) {
    if (std::filesystem::exists(path / "coord.pkg", pathError)) {
      std::vector<pkg::Package> packages;
      std::string error;
      if (!pkg::Graph::load(path, packages, error)) {
        std::cerr << "coorc: package error: " << error << "\n";
        return 1;
      }
      for (const pkg::Package& package : packages) {
        std::cout << package.name << '\n';
      }
      return 0;
    }
    if (package::findManifest(path).has_value()) {
      std::vector<std::string> names;
      std::set<std::string> active;
      std::set<std::string> loaded;
      std::string error;
      std::function<bool(const std::filesystem::path&)> visit =
          [&](const std::filesystem::path& root) -> bool {
        const std::string key = platform::pathKey(root);
        if (loaded.count(key) != 0) {
          return true;
        }
        if (!active.insert(key).second) {
          error = "cyclic package dependency at '" +
                  platform::pathToUtf8(root) + "'";
          return false;
        }
        const std::optional<package::Manifest> manifest =
            package::Manifest::load(root, error);
        if (!manifest.has_value()) {
          return false;
        }
        for (const package::DependencySpec& spec : manifest->dependencies) {
          if (spec.isPath() && spec.path.has_value() &&
              !visit(*spec.path)) {
            return false;
          }
        }
        names.push_back(manifest->name);
        active.erase(key);
        loaded.insert(key);
        return true;
      };
      if (!visit(path)) {
        std::cerr << "coorc: " << error << "\n";
        return 1;
      }
      for (const std::string& name : names) {
        std::cout << name << '\n';
      }
      return 0;
    }
    std::cerr << "coorc: no coordination.toml or coord.pkg in '" << inputPath
              << "'\n";
    return 1;
  }
  DependencyManager manager;
  manager.scan(path);
  bool ok = true;
  const std::vector<std::string> order = manager.topologicalOrder(ok);
  if (!ok) {
    std::cerr << "coorc: dependency cycle or unknown requirement\n";
    return 1;
  }
  for (const std::string& name : order) {
    std::cout << name << '\n';
  }
  return 0;
}

int runCompile(const std::vector<std::string>& arguments) {
  CompileOptions options;
  std::string command = "check";
  std::string manifestOption;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];
    if (argument == "-h" || argument == "--help") {
      printUsage(std::cout);
      return 0;
    }
    if (argument == "--version") {
      return printVersion();
    }
    if (isCompileCommand(argument)) {
      command = argument;
      if (argument == "outline") {
        options.dumpOutline = true;
      }
      continue;
    }
    if (argument == "--docs") {
      options.dumpOutlineDocs = true;
      continue;
    }
    if (argument == "--dump-tokens") {
      options.dumpTokens = true;
      continue;
    }
    if (argument == "--dump-ast") {
      options.dumpAst = true;
      continue;
    }
    if (argument == "--incremental") {
      options.incremental = true;
      continue;
    }
    if (argument == "--print-consts" || argument == "--run") {
      options.printConstEval = true;
      continue;
    }
    if (argument == "-Werror") {
      options.warningsAsErrors = true;
      continue;
    }
    if (argument == "--verbose") {
      options.verbose = true;
      continue;
    }
    if (argument == "--emit-coox") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --emit-coox requires a path\n";
        return 2;
      }
      options.interfacePath = arguments[++i];
      continue;
    }
    if (argument == "--keep-cpp") {
      options.keepCpp = true;
      continue;
    }
    if (argument == "--emit-cpp") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --emit-cpp requires a path\n";
        return 2;
      }
      options.output = OutputKind::Cpp;
      options.outputPath = arguments[++i];
      continue;
    }
    if (argument == "-o" || argument == "--output") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: -o requires a path\n";
        return 2;
      }
      options.outputPath = arguments[++i];
      continue;
    }
    if (argument == "--filter") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --filter requires a substring\n";
        return 2;
      }
      options.testFilter = arguments[++i];
      continue;
    }
    if (argument == "--cxx") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --cxx requires a path\n";
        return 2;
      }
      options.cxxCompiler = arguments[++i];
      continue;
    }
    if (argument == "--cxx-flavor") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --cxx-flavor requires 'msvc' or 'gnu'\n";
        return 2;
      }
      const std::string flavor = lowerAscii(arguments[++i]);
      if (flavor != "msvc" && flavor != "gnu") {
        std::cerr << "coorc: --cxx-flavor must be 'msvc' or 'gnu'\n";
        return 2;
      }
      options.cxxFlavor = flavor;
      continue;
    }
    if (argument == "--target") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --target requires a triple\n";
        return 2;
      }
      options.targetTriple = arguments[++i];
      continue;
    }
    if (argument == "--sysroot") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --sysroot requires a path\n";
        return 2;
      }
      options.sysroot = arguments[++i];
      continue;
    }
    if (argument == "--manifest") {
      if (i + 1 >= arguments.size()) {
        std::cerr << "coorc: --manifest requires a path\n";
        return 2;
      }
      manifestOption = arguments[++i];
      continue;
    }
    if (!argument.empty() && argument.front() == '-') {
      std::cerr << "coorc: unknown option '" << argument << "'\n";
      return 2;
    }
    if (options.inputPath.empty()) {
      options.inputPath = argument;
    } else {
      std::cerr << "coorc: multiple input files are not supported yet\n";
      return 2;
    }
  }
  if (options.inputPath.empty() && !manifestOption.empty()) {
    std::filesystem::path manifestPath = platform::pathFromUtf8(manifestOption);
    if (is_directory(manifestPath)) {
      options.inputPath = platform::pathToUtf8(manifestPath);
    } else {
      options.inputPath =
          platform::pathToUtf8(manifestPath.parent_path());
    }
  }
  if (options.inputPath.empty()) {
    std::cerr << "coorc: no input file\n";
    return 2;
  }
  if (command == "deps") {
    return runDeps(options.inputPath);
  }
  if (options.output != OutputKind::Cpp) {
    if (command == "build" || command == "test") {
      options.output = OutputKind::Executable;
    } else if (command == "run") {
      options.output = OutputKind::Executable;
      options.runAfterBuild = true;
    } else {
      options.output = OutputKind::Check;
    }
  }
  if (command == "test") {
    options.testMode = true;
    options.output = OutputKind::Executable;
    options.runAfterBuild = true;
  }
  try {
    return Compiler{}.compile(options, std::cout, std::cerr);
  } catch (const std::exception& error) {
    std::cerr << "coorc: internal error: " << error.what() << "\n";
    return 70;
  } catch (...) {
    std::cerr << "coorc: internal error: unknown exception\n";
    return 70;
  }
}

// ---------------------------------------------------------------------------
// Package commands

struct PackageCliOptions {
  package::PackageManagerOptions manager;
  std::string output;
  std::string name;
  std::string term;
  bool dev = false;
  bool path = false;
  bool force = false;
  bool cache = false;
  bool dryRun = false;
  bool network = false;
  std::vector<std::string> positionals;
};

bool parsePackageOptions(bool isAdd, PackageCliOptions& options,
                         const std::vector<std::string>& arguments,
                         std::string& error) {
  options.manager.out = &std::cout;
  options.manager.err = &std::cerr;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];
    const auto next = [&](const char* name) -> std::optional<std::string> {
      if (i + 1 >= arguments.size()) {
        error = std::string(name) + " requires a value";
        return std::nullopt;
      }
      return arguments[++i];
    };
    if (argument == "-h" || argument == "--help") {
      printUsage(std::cout);
      std::exit(0);
    }
    if (argument == "--offline") {
      options.manager.offline = true;
    } else if (argument == "--locked") {
      options.manager.locked = true;
    } else if (argument == "--frozen") {
      options.manager.frozen = true;
      options.manager.locked = true;
    } else if (argument == "--no-dev") {
      options.manager.includeDev = false;
    } else if (argument == "--all-optional") {
      options.manager.includeOptional = true;
    } else if (argument == "--verbose") {
      options.manager.verbose = true;
    } else if (argument == "--manifest") {
      const std::optional<std::string> value = next("--manifest");
      if (!value.has_value()) {
        return false;
      }
      options.manager.manifestPath = platform::pathFromUtf8(*value);
    } else if (argument == "--mirror") {
      const std::optional<std::string> value = next("--mirror");
      if (!value.has_value()) {
        return false;
      }
      options.manager.mirror = *value;
    } else if (argument == "--registry") {
      const std::optional<std::string> value = next("--registry");
      if (!value.has_value()) {
        return false;
      }
      options.manager.registry = *value;
    } else if (argument == "--dev" && isAdd) {
      options.dev = true;
    } else if (argument == "--path" && isAdd) {
      options.path = true;
    } else if ((argument == "-o" || argument == "--output")) {
      const std::optional<std::string> value = next("-o");
      if (!value.has_value()) {
        return false;
      }
      options.output = *value;
    } else if (argument == "--dry-run") {
      options.dryRun = true;
    } else if (argument == "--cache") {
      options.cache = true;
    } else if (argument == "--force") {
      options.force = true;
    } else if (argument == "--name") {
      const std::optional<std::string> value = next("--name");
      if (!value.has_value()) {
        return false;
      }
      options.name = *value;
    } else if (argument == "--network") {
      options.network = true;
    } else if (!argument.empty() && argument.front() == '-') {
      error = "unknown option '" + argument + "'";
      return false;
    } else {
      options.positionals.push_back(argument);
    }
  }
  return true;
}

int runPackage(const std::string& command,
               const std::vector<std::string>& arguments) {
  PackageCliOptions options;
  std::string error;
  if (!parsePackageOptions(command == "add", options, arguments, error)) {
    std::cerr << "coorc: " << error << "\n";
    return 2;
  }
  if (command == "init") {
    if (options.positionals.size() > 1) {
      std::cerr << "coorc: init takes at most one directory\n";
      return 2;
    }
    const std::filesystem::path directory =
        options.positionals.empty()
            ? std::filesystem::current_path()
            : platform::pathFromUtf8(options.positionals.front());
    return package::PackageManager::init(directory, options.name, options.force);
  }
  if (command == "update") {
    if (options.positionals.empty()) {
      options.manager.updateAll = true;
    } else {
      options.manager.update = options.positionals;
    }
  }
  package::PackageManager manager(options.manager);
  if (command == "install") {
    if (!options.positionals.empty()) {
      std::cerr << "coorc: install takes no arguments\n";
      return 2;
    }
    return manager.install();
  }
  if (command == "update") {
    if (options.manager.updateAll) {
      return manager.update();
    }
    return manager.install();
  }
  if (command == "add") {
    if (options.positionals.size() != 1) {
      std::cerr << "coorc: add requires one package specification\n";
      return 2;
    }
    return manager.add(options.positionals.front(), options.dev, options.path);
  }
  if (command == "remove") {
    if (options.positionals.size() != 1) {
      std::cerr << "coorc: remove requires one package name\n";
      return 2;
    }
    return manager.remove(options.positionals.front());
  }
  if (command == "list") {
    return manager.list();
  }
  if (command == "tree") {
    return manager.tree();
  }
  if (command == "clean") {
    return manager.clean(options.cache);
  }
  if (command == "pack") {
    return manager.pack(options.output.empty()
                            ? std::filesystem::path()
                            : platform::pathFromUtf8(options.output));
  }
  if (command == "publish") {
    return manager.publish(options.dryRun);
  }
  if (command == "search") {
    if (options.positionals.size() != 1) {
      std::cerr << "coorc: search requires one term\n";
      return 2;
    }
    return manager.search(options.positionals.front());
  }
  std::cerr << "coorc: unknown package command '" << command << "'\n";
  return 2;
}

// ---------------------------------------------------------------------------
// Mirror and configuration helpers

toml::Value loadUserConfigDocument(const package::MirrorManager& manager,
                                   std::string& error) {
  const std::filesystem::path path = manager.configPath();
  std::error_code existsError;
  if (!std::filesystem::is_regular_file(path, existsError)) {
    return toml::Value::makeTable();
  }
  std::string readError;
  const std::optional<std::string> text = platform::readFile(path, readError);
  if (!text.has_value()) {
    error = readError;
    return toml::Value::makeTable();
  }
  std::string parseError;
  const std::optional<toml::Value> document = toml::parse(*text, parseError);
  if (!document.has_value()) {
    error = platform::pathToUtf8(path) + ": " + parseError;
    return toml::Value::makeTable();
  }
  return *document;
}

bool saveUserConfigDocument(const package::MirrorManager& manager,
                            const toml::Value& root, std::string& error) {
  const std::filesystem::path path = manager.configPath();
  std::string directoryError;
  if (!platform::ensureDirectory(path.parent_path(), directoryError)) {
    error = directoryError;
    return false;
  }
  return platform::writeFileAtomic(path, toml::write(root), error);
}

toml::Value mirrorEntryValue(const package::MirrorEntry& mirror) {
  toml::Value entry = toml::Value::makeTable();
  entry.set("name", toml::Value::makeString(mirror.name));
  entry.set("registry", toml::Value::makeString(mirror.registry));
  entry.set("url", toml::Value::makeString(mirror.url));
  if (mirror.priority != 0) {
    entry.set("priority", toml::Value::makeInteger(mirror.priority));
  }
  if (!mirror.enabled) {
    entry.set("enabled", toml::Value::makeBoolean(false));
  }
  if (!mirror.description.empty()) {
    entry.set("description", toml::Value::makeString(mirror.description));
  }
  return entry;
}

bool setUserConfigValue(toml::Value& root, const std::string& key,
                        const std::string& value, std::string& error) {
  if (key == "mirror.default" || key == "mirror.registry") {
    toml::Value* defaults = root.get("mirror");
    if (defaults == nullptr || !defaults->isTable()) {
      root.set("mirror", toml::Value::makeTable());
      defaults = root.get("mirror");
    }
    defaults->set(key == "mirror.default" ? "default" : "registry",
                  toml::Value::makeString(value));
    return true;
  }
  if (key == "mirror.offline") {
    toml::Value* defaults = root.get("mirror");
    if (defaults == nullptr || !defaults->isTable()) {
      root.set("mirror", toml::Value::makeTable());
      defaults = root.get("mirror");
    }
    const std::string lowered = lowerAscii(value);
    if (lowered != "true" && lowered != "false" && lowered != "1" &&
        lowered != "0" && lowered != "on" && lowered != "off") {
      error = "expected a boolean value";
      return false;
    }
    const bool enabled = lowered == "true" || lowered == "1" ||
                         lowered == "on";
    defaults->set("offline", toml::Value::makeBoolean(enabled));
    return true;
  }
  if (key == "mirror.timeout-ms") {
    char* end = nullptr;
    const long timeout = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || timeout <= 0) {
      error = "expected a positive integer";
      return false;
    }
    toml::Value* defaults = root.get("mirror");
    if (defaults == nullptr || !defaults->isTable()) {
      root.set("mirror", toml::Value::makeTable());
      defaults = root.get("mirror");
    }
    defaults->set("timeout-ms", toml::Value::makeInteger(timeout));
    return true;
  }
  if (key.rfind("registries.", 0) == 0) {
    const std::string name = key.substr(11);
    if (name.empty()) {
      error = "missing registry name";
      return false;
    }
    if (!package::isSupportedRegistryUrl(value)) {
      error = "unsupported registry URL '" + value + "'";
      return false;
    }
    toml::Value* registries = root.get("registries");
    if (registries == nullptr || !registries->isTable()) {
      root.set("registries", toml::Value::makeTable());
      registries = root.get("registries");
    }
    registries->set(name, toml::Value::makeString(value));
    return true;
  }
  error = "unknown configuration key '" + key + "'";
  return false;
}

bool selectUserMirror(toml::Value& root, const std::string& name,
                      std::string& error) {
  toml::Value* mirrors = root.get("mirrors");
  if (mirrors == nullptr || !mirrors->isArray()) {
    error = "mirror '" + name + "' is not configured";
    return false;
  }
  bool found = false;
  for (toml::Value& entry : mirrors->elements()) {
    if (!entry.isTable()) {
      continue;
    }
    if (entry.string("name").value_or("") == name) {
      found = true;
      entry.set("enabled", toml::Value::makeBoolean(true));
    }
  }
  if (!found) {
    error = "mirror '" + name + "' is not configured";
    return false;
  }
  toml::Value* defaults = root.get("mirror");
  if (defaults == nullptr || !defaults->isTable()) {
    root.set("mirror", toml::Value::makeTable());
    defaults = root.get("mirror");
  }
  defaults->set("default", toml::Value::makeString(name));
  return true;
}

int runMirror(const std::vector<std::string>& arguments) {
  std::string subcommand = "list";
  std::vector<std::string> rest;
  if (!arguments.empty() && !arguments.front().empty() &&
      arguments.front().front() != '-') {
    subcommand = arguments.front();
    rest.assign(arguments.begin() + 1, arguments.end());
  } else {
    rest = arguments;
  }
  if (subcommand == "-h" || subcommand == "--help" ||
      (!rest.empty() && (rest.front() == "-h" || rest.front() == "--help"))) {
    std::cout << "Usage: coorc mirror <list|show|add|remove|use|test|auto>\n"
              << "  list                     show configured mirrors\n"
              << "  show                     print the effective configuration\n"
              << "  add <name> <url>         add or replace a mirror\n"
              << "      [--registry <name>] [--priority <n>] [--description <text>]\n"
              << "  remove <name>            remove a user mirror\n"
              << "  use <name>               make a mirror the default\n"
              << "  test [name]              probe mirrors\n"
              << "  auto                     select the fastest reachable mirror\n";
    return 0;
  }
  package::MirrorManager manager;
  std::string error;
  if (!manager.loadUserConfig(error)) {
    std::cerr << "coorc: " << error << "\n";
    return 1;
  }
  if (const std::optional<std::filesystem::path> manifestPath =
          [&]() -> std::optional<std::filesystem::path> {
            std::error_code pathError;
            std::filesystem::path current =
                std::filesystem::current_path(pathError);
            if (pathError) {
              return std::nullopt;
            }
            for (;;) {
              const std::optional<std::filesystem::path> found =
                  package::findManifest(current);
              if (found.has_value()) {
                return found;
              }
              const std::filesystem::path parent = current.parent_path();
              if (parent == current || parent.empty()) {
                return std::nullopt;
              }
              current = parent;
            }
          }();
      manifestPath.has_value()) {
    std::string loadError;
    const std::optional<package::Manifest> manifest =
        package::Manifest::loadFile(*manifestPath, loadError);
    if (manifest.has_value()) {
      if (!manager.loadProjectConfig(*manifest, error)) {
        std::cerr << "coorc: " << error << "\n";
        return 1;
      }
    }
  }
  manager.applyEnvironment();
  if (subcommand == "list" || subcommand == "ls") {
    const package::MirrorConfig& config = manager.config();
    std::vector<package::MirrorEntry> mirrors = config.mirrors;
    std::sort(mirrors.begin(), mirrors.end(),
              [](const package::MirrorEntry& left,
                 const package::MirrorEntry& right) {
                return left.name < right.name;
              });
    std::cout << "  NAME\tREGISTRY\tPRIORITY\tENABLED\tURL\n";
    for (const package::MirrorEntry& mirror : mirrors) {
      const bool isDefault = mirror.name == config.defaultMirror;
      std::cout << (isDefault ? "* " : "  ") << mirror.name << '\t'
                << mirror.registry << '\t' << mirror.priority << '\t'
                << (mirror.enabled ? "yes" : "no") << '\t' << mirror.url
                << '\n';
    }
    std::cout << "default mirror: "
              << (config.defaultMirror.empty() ? "(by priority)"
                                               : config.defaultMirror)
              << "\n";
    std::cout << "default registry: " << config.defaultRegistry << "\n";
    return 0;
  }
  if (subcommand == "show") {
    std::cout << manager.describe();
    return 0;
  }
  if (subcommand == "add") {
    if (rest.size() < 2) {
      std::cerr << "coorc: mirror add requires <name> <url>\n";
      return 2;
    }
    package::MirrorEntry mirror;
    mirror.name = rest[0];
    mirror.url = package::normalizeRegistryUrl(rest[1]);
    if (!package::isSupportedRegistryUrl(mirror.url)) {
      std::cerr << "coorc: unsupported mirror URL '" << rest[1] << "'\n";
      return 2;
    }
    mirror.registry = manager.config().defaultRegistry;
    for (std::size_t i = 2; i < rest.size(); ++i) {
      if (rest[i] == "--registry" && i + 1 < rest.size()) {
        mirror.registry = rest[++i];
      } else if (rest[i] == "--priority" && i + 1 < rest.size()) {
        mirror.priority = std::atoi(rest[++i].c_str());
      } else if (rest[i] == "--description" && i + 1 < rest.size()) {
        mirror.description = rest[++i];
      } else if (rest[i] == "--disabled") {
        mirror.enabled = false;
      } else {
        std::cerr << "coorc: unknown mirror add option '" << rest[i] << "'\n";
        return 2;
      }
    }
    toml::Value root = loadUserConfigDocument(manager, error);
    if (!error.empty()) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    toml::Value mirrors = toml::Value::makeArray();
    if (const toml::Value* existing = root.get("mirrors");
        existing != nullptr && existing->isArray()) {
      for (const toml::Value& entry : existing->elements()) {
        if (entry.isTable() &&
            entry.string("name").value_or("") == mirror.name) {
          continue;
        }
        mirrors.push(entry);
      }
    }
    mirrors.push(mirrorEntryValue(mirror));
    root.set("mirrors", std::move(mirrors));
    if (!saveUserConfigDocument(manager, root, error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    std::cout << "coorc: mirror '" << mirror.name << "' -> " << mirror.url
              << " (" << mirror.registry << ")\n";
    return 0;
  }
  if (subcommand == "remove" || subcommand == "rm") {
    if (rest.size() != 1) {
      std::cerr << "coorc: mirror remove requires <name>\n";
      return 2;
    }
    toml::Value root = loadUserConfigDocument(manager, error);
    if (!error.empty()) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    const toml::Value* existing = root.get("mirrors");
    if (existing == nullptr || !existing->isArray()) {
      std::cerr << "coorc: mirror '" << rest[0] << "' is not configured\n";
      return 1;
    }
    toml::Value mirrors = toml::Value::makeArray();
    bool found = false;
    for (const toml::Value& entry : existing->elements()) {
      if (entry.isTable() &&
          entry.string("name").value_or("") == rest[0]) {
        found = true;
        continue;
      }
      mirrors.push(entry);
    }
    if (!found) {
      std::cerr << "coorc: mirror '" << rest[0] << "' is not configured\n";
      return 1;
    }
    root.set("mirrors", std::move(mirrors));
    if (!saveUserConfigDocument(manager, root, error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    std::cout << "coorc: removed mirror '" << rest[0] << "'\n";
    return 0;
  }
  if (subcommand == "use") {
    if (rest.size() != 1) {
      std::cerr << "coorc: mirror use requires <name>\n";
      return 2;
    }
    toml::Value root = loadUserConfigDocument(manager, error);
    if (!error.empty()) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    if (!selectUserMirror(root, rest[0], error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    if (!saveUserConfigDocument(manager, root, error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    std::cout << "coorc: default mirror is now '" << rest[0] << "'\n";
    return 0;
  }
  if (subcommand == "test") {
    std::vector<package::MirrorEntry> candidates;
    if (!rest.empty()) {
      if (const package::MirrorEntry* mirror = manager.mirror(rest[0]);
          mirror != nullptr) {
        candidates.push_back(*mirror);
      } else if (package::isSupportedRegistryUrl(rest[0])) {
        package::MirrorEntry mirror;
        mirror.name = "argument";
        mirror.url = package::normalizeRegistryUrl(rest[0]);
        candidates.push_back(std::move(mirror));
      } else {
        std::cerr << "coorc: unknown mirror '" << rest[0] << "'\n";
        return 1;
      }
    } else {
      candidates = manager.config().mirrors;
    }
    bool anyReachable = false;
    for (const package::MirrorEntry& mirror : candidates) {
      if (!mirror.enabled && rest.empty()) {
        continue;
      }
      const package::MirrorProbe probe = manager.probe(mirror);
      std::cout << (probe.reachable ? "ok  " : "fail") << '\t' << mirror.name
                << '\t' << probe.latencyMs << " ms\t" << probe.detail
                << "\n";
      if (probe.reachable) {
        anyReachable = true;
        manager.recordSuccess(mirror, probe.latencyMs);
      } else {
        manager.recordFailure(mirror);
      }
    }
    std::string healthError;
    (void)manager.saveHealthCache(healthError);
    return anyReachable ? 0 : 1;
  }
  if (subcommand == "auto") {
    const package::MirrorProbe* best = nullptr;
    package::MirrorEntry bestMirror;
    package::MirrorProbe bestProbe;
    for (const package::MirrorEntry& mirror : manager.config().mirrors) {
      if (!mirror.enabled) {
        continue;
      }
      const package::MirrorProbe probe = manager.probe(mirror);
      std::cout << (probe.reachable ? "ok  " : "fail") << '\t' << mirror.name
                << '\t' << probe.latencyMs << " ms\n";
      if (probe.reachable &&
          (best == nullptr || probe.latencyMs < bestProbe.latencyMs)) {
        bestProbe = probe;
        bestMirror = mirror;
        best = &bestProbe;
      }
    }
    if (best == nullptr) {
      std::cerr << "coorc: no reachable mirror found\n";
      return 1;
    }
    toml::Value root = loadUserConfigDocument(manager, error);
    if (!error.empty()) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    if (!selectUserMirror(root, bestMirror.name, error) ||
        !saveUserConfigDocument(manager, root, error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    std::cout << "coorc: default mirror is now '" << bestMirror.name << "' ("
              << bestProbe.latencyMs << " ms)\n";
    return 0;
  }
  std::cerr << "coorc: unknown mirror command '" << subcommand << "'\n";
  return 2;
}

int runConfig(const std::vector<std::string>& arguments) {
  package::MirrorManager manager;
  std::string error;
  if (!manager.loadUserConfig(error)) {
    std::cerr << "coorc: " << error << "\n";
    return 1;
  }
  manager.applyEnvironment();
  const std::string subcommand =
      arguments.empty() ? "list" : arguments.front();
  if (subcommand == "-h" || subcommand == "--help") {
    std::cout << "Usage: coorc config <list|get|set|path>\n"
              << "  list                     print the effective configuration\n"
              << "  get <key>                read a configuration value\n"
              << "  set <key> <value>        write a user configuration value\n"
              << "  path                     print config and cache paths\n"
              << "Keys: mirror.default, mirror.registry, mirror.offline,\n"
              << "      mirror.timeout-ms, registries.<name>, cache.dir,\n"
              << "      config.path\n";
    return 0;
  }
  if (subcommand == "list" || subcommand == "show") {
    std::cout << manager.describe();
    return 0;
  }
  if (subcommand == "path") {
    std::cout << platform::pathToUtf8(manager.configPath()) << '\n';
    std::cout << platform::pathToUtf8(manager.cacheRoot()) << '\n';
    return 0;
  }
  if (subcommand == "get") {
    if (arguments.size() < 2) {
      std::cerr << "coorc: config get requires a key\n";
      return 2;
    }
    const std::string& key = arguments[1];
    const package::MirrorConfig& config = manager.config();
    if (key == "mirror.default") {
      std::cout << config.defaultMirror << '\n';
    } else if (key == "mirror.registry") {
      std::cout << config.defaultRegistry << '\n';
    } else if (key == "mirror.offline") {
      std::cout << (config.offline ? "true" : "false") << '\n';
    } else if (key == "mirror.timeout-ms") {
      std::cout << config.timeoutMs << '\n';
    } else if (key == "cache.dir") {
      std::cout << platform::pathToUtf8(manager.cacheRoot()) << '\n';
    } else if (key == "config.path") {
      std::cout << platform::pathToUtf8(manager.configPath()) << '\n';
    } else if (key.rfind("registries.", 0) == 0) {
      const std::string name = key.substr(11);
      const std::optional<package::RegistryEntry> registry =
          manager.registry(name);
      if (!registry.has_value()) {
        std::cerr << "coorc: registry '" << name << "' is not configured\n";
        return 1;
      }
      std::cout << registry->url << '\n';
    } else {
      std::cerr << "coorc: unknown configuration key '" << key << "'\n";
      return 1;
    }
    return 0;
  }
  if (subcommand == "set") {
    if (arguments.size() < 3) {
      std::cerr << "coorc: config set requires a key and a value\n";
      return 2;
    }
    toml::Value root = loadUserConfigDocument(manager, error);
    if (!error.empty()) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    if (!setUserConfigValue(root, arguments[1], arguments[2], error) ||
        !saveUserConfigDocument(manager, root, error)) {
      std::cerr << "coorc: " << error << "\n";
      return 1;
    }
    std::cout << "coorc: " << arguments[1] << " = " << arguments[2] << '\n';
    return 0;
  }
  std::cerr << "coorc: unknown config command '" << subcommand << "'\n";
  return 2;
}

int runDoctor(const std::vector<std::string>& arguments) {
  bool network = false;
  for (const std::string& argument : arguments) {
    if (argument == "--network") {
      network = true;
    } else {
      std::cerr << "coorc: unknown doctor option '" << argument << "'\n";
      return 2;
    }
  }
  std::cout << "coorc " << kVersion << " (" << platform::describeHost()
            << ")\n";
  std::cout << "  executable suffix: " << platform::executableSuffix()
            << "  object suffix: " << platform::objectFileSuffix() << "\n";
  for (const char* variable : {"COORD_CXX", "CXX"}) {
    const std::optional<std::string> value = platform::environment(variable);
    std::cout << "  " << variable << ": "
              << (value.has_value() && !value->empty() ? *value : "(unset)")
              << "\n";
  }
  std::cout << "  host compilers:";
  for (const char* candidate : {"cl", "clang++", "g++", "c++", "clang-cl"}) {
    const std::optional<std::filesystem::path> found =
        platform::findExecutable(candidate);
    std::cout << " " << candidate << "="
              << (found.has_value() ? platform::pathToUtf8(*found)
                                    : "missing");
  }
  std::cout << "\n";
  const std::optional<std::filesystem::path> curl =
      platform::findExecutable("curl");
  std::cout << "  curl: "
            << (curl.has_value() ? platform::pathToUtf8(*curl) : "missing")
            << "\n";
  package::MirrorManager manager;
  std::string error;
  const bool configOk = manager.loadUserConfig(error);
  if (!configOk) {
    std::cout << "  config error: " << error << "\n";
  }
  manager.applyEnvironment();
  std::cout << "  config: " << platform::pathToUtf8(manager.configPath())
            << "\n";
  std::cout << "  cache: " << platform::pathToUtf8(manager.cacheRoot())
            << "\n";
  std::cout << "  default registry: " << manager.config().defaultRegistry
            << "  default mirror: "
            << (manager.config().defaultMirror.empty()
                    ? "(by priority)"
                    : manager.config().defaultMirror)
            << "\n";
  std::optional<std::filesystem::path> manifestPath;
  std::error_code pathError;
  std::filesystem::path current = std::filesystem::current_path(pathError);
  while (!pathError) {
    const std::optional<std::filesystem::path> found =
        package::findManifest(current);
    if (found.has_value()) {
      manifestPath = found;
      break;
    }
    const std::filesystem::path parent = current.parent_path();
    if (parent == current || parent.empty()) {
      break;
    }
    current = parent;
  }
  std::cout << "  manifest: "
            << (manifestPath.has_value()
                    ? platform::pathToUtf8(*manifestPath)
                    : "(none found)")
            << "\n";
  if (network) {
    for (const package::MirrorEntry& mirror : manager.config().mirrors) {
      if (!mirror.enabled) {
        continue;
      }
      const package::MirrorProbe probe = manager.probe(mirror);
      std::cout << "  mirror " << mirror.name << ": "
                << (probe.reachable ? "ok" : "unreachable") << " ("
                << probe.latencyMs << " ms) " << probe.detail << "\n";
    }
  }
  if (!configOk) {
    return 1;
  }
  return 0;
}

int runPlatformCommand() {
  std::cout << "operating system: " << platform::operatingSystemName() << "\n";
  std::cout << "architecture: " << platform::architectureName() << "\n";
  std::cout << "target triple: " << platform::targetTriple() << "\n";
  std::cout << "little endian: "
            << (platform::isLittleEndian() ? "yes" : "no") << "\n";
  std::cout << "executable: *" << platform::executableSuffix() << "\n";
  std::cout << "object: *" << platform::objectFileSuffix() << "\n";
  std::cout << "static library: "
            << platform::staticLibraryFileName("coord") << "\n";
  std::cout << "shared library: "
            << platform::sharedLibraryFileName("coord") << "\n";
  std::cout << "config: "
            << platform::pathToUtf8(platform::configDirectory("coord"))
            << "\n";
  std::cout << "cache: "
            << platform::pathToUtf8(platform::cacheDirectory("coord"))
            << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  platform::enableUtf8Console();
  std::vector<std::string> arguments;
  for (int i = 1; i < argc; ++i) {
    arguments.emplace_back(argv[i]);
  }
  if (arguments.empty()) {
    printUsage(std::cerr);
    return 2;
  }
  if (arguments.front() == "-h" || arguments.front() == "--help") {
    printUsage(std::cout);
    return 0;
  }
  if (arguments.front() == "--version") {
    return printVersion();
  }
  const std::string command = arguments.front();
  if (isPackageCommand(command)) {
    return runPackage(command,
                      std::vector<std::string>(arguments.begin() + 1,
                                               arguments.end()));
  }
  if (command == "mirror") {
    return runMirror(std::vector<std::string>(arguments.begin() + 1,
                                              arguments.end()));
  }
  if (command == "config") {
    return runConfig(std::vector<std::string>(arguments.begin() + 1,
                                              arguments.end()));
  }
  if (command == "doctor") {
    return runDoctor(std::vector<std::string>(arguments.begin() + 1,
                                              arguments.end()));
  }
  if (command == "platform") {
    return runPlatformCommand();
  }
  return runCompile(arguments);
}
