#include "coord/driver.h"

#include "coord/standard_library.h"

#include "coord/ast.h"
#include "coord/codegen.h"
#include "coord/interp.h"
#include "coord/modules/interface.h"
#include "coord/modules/manifest.h"
#include "coord/modules/outline.h"
#include "coord/modules/platform.h"
#include "coord/modules/processor.h"
#include "coord/native.h"
#include "coord/parser.h"
#include "coord/sema.h"
#include "coord/support.h"
#include "coord/syntax.h"

#include <sstream>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coord {
namespace {

struct LoadedFile {
  bool ok = false;
  std::string text;
};

LoadedFile readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return LoadedFile{true, buffer.str()};
}

bool collectPackageSources(const std::filesystem::path& root,
                           std::vector<std::filesystem::path>& files,
                           std::set<std::string>& active,
                           std::set<std::string>& loaded, std::string& error,
                           bool includeVendored) {
  std::error_code canonicalError;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(root, canonicalError);
  const std::filesystem::path packageRoot =
      canonicalError ? root : canonical;
  const std::string key = platform::pathKey(packageRoot);
  if (loaded.count(key) != 0) {
    return true;
  }
  if (!active.insert(key).second) {
    error = "cyclic package dependency at '" +
            platform::pathToUtf8(packageRoot) + "'";
    return false;
  }
  std::string manifestError;
  const std::optional<package::Manifest> manifest =
      package::Manifest::load(packageRoot, manifestError);
  if (!manifest.has_value()) {
    error = manifestError;
    return false;
  }
  for (const package::DependencySpec& dependency : manifest->dependencies) {
    if (!dependency.isPath() || !dependency.path.has_value()) {
      continue;
    }
    if (!collectPackageSources(*dependency.path, files, active, loaded, error,
                               false)) {
      return false;
    }
  }
  if (includeVendored) {
    std::error_code iterateError;
    const std::filesystem::path vendorRoot = packageRoot / "coord_modules";
    std::vector<std::filesystem::path> vendors;
    for (std::filesystem::directory_iterator iterator(vendorRoot,
                                                      iterateError),
         end;
         iterator != end && !iterateError; iterator.increment(iterateError)) {
      std::error_code typeError;
      if (!iterator->is_directory(typeError)) {
        continue;
      }
      if (package::findManifest(iterator->path()).has_value()) {
        vendors.push_back(iterator->path());
      }
    }
    std::sort(vendors.begin(), vendors.end());
    for (const std::filesystem::path& vendor : vendors) {
      if (!collectPackageSources(vendor, files, active, loaded, error,
                                 false)) {
        return false;
      }
    }
  }
  const std::filesystem::path sourceDirectory = manifest->sourceDirectory();
  std::error_code sourceError;
  if (!std::filesystem::is_directory(sourceDirectory, sourceError)) {
    error = "source directory '" + platform::pathToUtf8(sourceDirectory) +
            "' of package '" + manifest->name + "' does not exist";
    return false;
  }
  std::vector<std::filesystem::path> local;
  std::error_code iterateError;
  for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory,
                                                              iterateError),
       end;
       iterator != end && !iterateError; iterator.increment(iterateError)) {
    std::error_code typeError;
    if (iterator->is_directory(typeError)) {
      const std::string name = iterator->path().filename().string();
      if (name == "out" || name == ".git" || name == ".vs" ||
          name == "coord_modules" || name == "build" ||
          name.rfind("cmake-build-", 0) == 0) {
        iterator.disable_recursion_pending();
      }
      continue;
    }
    if (iterator->path().extension() == ".coo") {
      local.push_back(iterator->path());
    }
  }
  if (iterateError) {
    error = "cannot scan package '" + platform::pathToUtf8(packageRoot) + "'";
    return false;
  }
  std::sort(local.begin(), local.end());
  for (const std::filesystem::path& file : local) {
    const std::string fileKey = platform::pathKey(file);
    if (loaded.insert(fileKey).second) {
      files.push_back(file);
    }
  }
  active.erase(key);
  loaded.insert(key);
  return true;
}


bool writeFile(const std::filesystem::path& path, const std::string& text) {
  std::error_code directoryError;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), directoryError);
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << text;
  return static_cast<bool>(output);
}

void dumpTokens(const std::vector<Token>& tokens, const SourceManager& sources,
                std::ostream& out) {
  for (const Token& token : tokens) {
    const LineCol lc = sources.lineCol(token.span);
    out << lc.line << ':' << lc.column << ' ' << tokenKindName(token.kind);
    if (!token.text.empty()) {
      out << " \"" << token.text << '"';
    }
    out << '\n';
  }
}

std::string replaceExtension(const std::string& path,
                             const std::string& extension) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return path + extension;
  }
  return path.substr(0, dot) + extension;
}

std::string defaultExecutablePath(const std::string& inputPath) {
  return replaceExtension(inputPath, std::string(platform::executableSuffix()));
}

std::string defaultCppPath(const std::string& inputPath) {
  return replaceExtension(inputPath, ".cpp");
}

bool looksLikeMsvc(std::string compiler) {
  const std::size_t slash = compiler.find_last_of("/\\");
  if (slash != std::string::npos) {
    compiler = compiler.substr(slash + 1);
  }
  for (char& character : compiler) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  if (compiler.size() > 4 && compiler.compare(compiler.size() - 4, 4, ".exe") == 0) {
    compiler.erase(compiler.size() - 4);
  }
  return compiler == "cl" || compiler == "clang-cl";
}

std::string pickCompiler(const std::string& requested) {
  if (!requested.empty()) {
    return requested;
  }
  for (const char* variable : {"COORD_CXX", "CXX"}) {
    if (const std::optional<std::string> value = platform::environment(variable);
        value.has_value() && !value->empty()) {
      return *value;
    }
  }
#if defined(_WIN32)
  const std::vector<std::string> candidates = {"cl", "clang++", "g++"};
#else
  const std::vector<std::string> candidates = {"c++", "clang++", "g++"};
#endif
  for (const std::string& candidate : candidates) {
    if (platform::findExecutable(candidate).has_value()) {
      return candidate;
    }
  }
  return candidates.front();
}

bool isMsvcCompiler(const std::string& compiler, const std::string& flavor) {
  if (flavor == "msvc") {
    return true;
  }
  if (flavor == "gnu") {
    return false;
  }
  return looksLikeMsvc(compiler);
}

bool looksLikeClang(std::string compiler) {
  const std::size_t slash = compiler.find_last_of("/\\");
  if (slash != std::string::npos) {
    compiler = compiler.substr(slash + 1);
  }
  for (char& character : compiler) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return compiler.find("clang") != std::string::npos;
}

/// Everything the C++ compiler/linker invocation needs. For a plain
/// Coordination build only `compiler`/`target`/`sysroot` are set; a native
/// project adds the configured flags, include directories, objects and
/// libraries. `bridgeCppPath`/`bridgeObjectPath` are empty when the module has
/// no native declarations.
struct ToolchainInvocation {
  std::string compiler;
  std::string flavor;
  std::string target;
  std::string sysroot;
  bool crossTarget = false;
  std::vector<std::string> cxxflags;
  std::vector<std::string> ldflags;
  std::vector<std::filesystem::path> includeDirs;
  std::vector<std::filesystem::path> objectFiles;
  std::vector<std::string> libraries;
  std::filesystem::path cppPath;
  std::filesystem::path bridgeCppPath;
  std::filesystem::path objectPath;
  std::filesystem::path bridgeObjectPath;
  std::filesystem::path executablePath;
};

/// One compiler process. Prints the command line on failure so a broken
/// project configuration never fails silently.
int runCompileStep(const std::vector<std::string>& arguments, bool verbose,
                   bool msvc, std::ostream& out, std::ostream& err,
                   const char* stage) {
  platform::ProcessOptions processOptions;
  processOptions.captureOutput = true;
  const platform::ProcessResult result =
      platform::run(arguments, processOptions);
  const std::string command = platform::commandLine(arguments);
  if (verbose) {
    out << "coorc: " << stage << ": " << command << "\n";
  }
  if (!result.started) {
    err << "coorc: cannot start the configured C++ compiler: "
        << result.failure << "\n";
    err << "coorc: command: " << command << "\n";
    err << "coorc: check [toolchain.<target>] cxx in coordination.toml\n";
    return 3;
  }
  const bool hasWarning =
      result.out.find("warning") != std::string::npos ||
      result.out.find("Warning") != std::string::npos ||
      result.err.find("warning") != std::string::npos ||
      result.err.find("Warning") != std::string::npos;
  if (result.exitCode != 0) {
    if (!result.out.empty()) {
      out << result.out;
    }
    if (!result.err.empty()) {
      err << result.err;
    }
    err << "coorc: " << stage << " failed (exit " << result.exitCode << ")\n";
    err << "coorc: command: " << command << "\n";
    if (msvc) {
      err << "coorc: run from a Visual Studio developer prompt so cl.exe can "
             "find the Windows SDK, or point [toolchain.<target>] at a "
             "complete compiler\n";
    }
    return 3;
  }
  if (verbose || hasWarning) {
    if (!result.out.empty()) {
      out << result.out;
    }
    if (!result.err.empty()) {
      err << result.err;
    }
  }
  return 0;
}

bool looksLikeLibraryPath(const std::string& value) {
  if (value.find('/') != std::string::npos ||
      value.find('\\') != std::string::npos) {
    return true;
  }
  for (const char* suffix :
       {".a", ".lib", ".so", ".dylib", ".dll", ".o", ".obj"}) {
    const std::string ending(suffix);
    if (value.size() >= ending.size() &&
        value.compare(value.size() - ending.size(), ending.size(), ending) ==
            0) {
      return true;
    }
  }
  return false;
}

int invokeToolchain(const ToolchainInvocation& invocation, bool verbose,
                    std::ostream& out, std::ostream& err) {
  const bool msvc = isMsvcCompiler(invocation.compiler, invocation.flavor);
  const auto appendCommonCompileFlags = [&](std::vector<std::string>& arguments) {
    if (msvc) {
      arguments.push_back("/nologo");
      arguments.push_back("/std:c++20");
      arguments.push_back("/EHsc");
      arguments.push_back("/utf-8");
    } else {
      arguments.push_back("-std=c++20");
      arguments.push_back("-O2");
#if !defined(_WIN32)
      arguments.push_back("-pthread");
#endif
      if (invocation.crossTarget && looksLikeClang(invocation.compiler)) {
        arguments.push_back("--target=" + invocation.target);
      }
      if (!invocation.sysroot.empty()) {
        arguments.push_back("--sysroot=" + invocation.sysroot);
      }
    }
    for (const std::filesystem::path& include : invocation.includeDirs) {
      arguments.push_back((msvc ? "/I" : "-I") + platform::pathToUtf8(include));
    }
    for (const std::string& flag : invocation.cxxflags) {
      arguments.push_back(flag);
    }
  };
  const auto compileOne =
      [&](const std::filesystem::path& cpp,
          const std::filesystem::path& object) -> int {
    std::vector<std::string> arguments = {invocation.compiler};
    appendCommonCompileFlags(arguments);
    if (msvc) {
      arguments.push_back("/c");
      arguments.push_back(platform::pathToUtf8(cpp));
      arguments.push_back("/Fo" + platform::pathToUtf8(object));
    } else {
      arguments.push_back("-c");
      arguments.push_back(platform::pathToUtf8(cpp));
      arguments.push_back("-o");
      arguments.push_back(platform::pathToUtf8(object));
    }
    return runCompileStep(arguments, verbose, msvc, out, err,
                          "C++ compilation");
  };

  if (const int result = compileOne(invocation.cppPath, invocation.objectPath);
      result != 0) {
    return result;
  }
  const bool hasBridge =
      !invocation.bridgeCppPath.empty() &&
      !invocation.bridgeObjectPath.empty();
  if (hasBridge) {
    if (const int result = compileOne(invocation.bridgeCppPath,
                                      invocation.bridgeObjectPath);
        result != 0) {
      return result;
    }
  }

  // Link step. Objects come from the generated translation units; the
  // configured objects/libraries come from the project's own native build.
  std::vector<std::string> arguments = {invocation.compiler};
  if (msvc) {
    arguments.push_back("/nologo");
    arguments.push_back(platform::pathToUtf8(invocation.objectPath));
  } else {
    arguments.push_back(platform::pathToUtf8(invocation.objectPath));
  }
  if (hasBridge) {
    arguments.push_back(platform::pathToUtf8(invocation.bridgeObjectPath));
  }
  for (const std::filesystem::path& object : invocation.objectFiles) {
    arguments.push_back(platform::pathToUtf8(object));
  }
  for (const std::string& library : invocation.libraries) {
    if (looksLikeLibraryPath(library)) {
      arguments.push_back(library);
    } else if (msvc) {
      arguments.push_back(library + ".lib");
    } else {
      arguments.push_back("-l" + library);
    }
  }
  if (msvc) {
    arguments.push_back("/Fe" + platform::pathToUtf8(invocation.executablePath));
  } else {
    arguments.push_back("-o");
    arguments.push_back(platform::pathToUtf8(invocation.executablePath));
    if (invocation.crossTarget && looksLikeClang(invocation.compiler)) {
      arguments.push_back("--target=" + invocation.target);
    }
    if (!invocation.sysroot.empty()) {
      arguments.push_back("--sysroot=" + invocation.sysroot);
    }
#if !defined(_WIN32)
    arguments.push_back("-pthread");
#endif
  }
  for (const std::string& flag : invocation.ldflags) {
    arguments.push_back(flag);
  }
  return runCompileStep(arguments, verbose, msvc, out, err, "C++ link");
}

}  // namespace

int runCompiler(const CompileOptions& options, std::ostream& out,
                std::ostream& err) {
  // Stage 1: load and lex. A directory means project mode: the package
  // manifest drives dependency ordering; otherwise every `.coo` below the
  // directory is parsed and merged (sorted for deterministic output).
  const std::filesystem::path inputRoot =
      platform::pathFromUtf8(options.inputPath);
  const bool projectMode = std::filesystem::is_directory(inputRoot);
  std::optional<std::filesystem::path> projectManifest;
  std::vector<std::filesystem::path> inputFiles;
  if (projectMode) {
    projectManifest = package::findManifest(inputRoot);
    if (projectManifest.has_value()) {
      std::set<std::string> active;
      std::set<std::string> loaded;
      std::string error;
      if (!collectPackageSources(inputRoot, inputFiles, active, loaded, error,
                                 true)) {
        err << "coorc: package error: " << error << "\n";
        return 2;
      }
    } else {
      std::error_code iterateError;
      for (std::filesystem::recursive_directory_iterator iterator(
               inputRoot, iterateError),
           end;
           iterator != end && !iterateError; iterator.increment(iterateError)) {
        std::error_code typeError;
        if (iterator->is_directory(typeError)) {
          const std::string name = iterator->path().filename().string();
          if (name == "out" || name == ".git" || name == ".vs" ||
              name == "coord_modules" || name == "build" ||
              name.rfind("cmake-build-", 0) == 0) {
            iterator.disable_recursion_pending();
          }
          continue;
        }
        if (iterator->path().extension() == ".coo") {
          inputFiles.push_back(iterator->path());
        }
      }
      if (iterateError) {
        err << "coorc: cannot scan project '" << platform::pathToUtf8(inputRoot)
            << "'\n";
        return 2;
      }
      std::sort(inputFiles.begin(), inputFiles.end());
    }
  } else {
    inputFiles.emplace_back(inputRoot);
  }

  SourceManager sources;
  DiagnosticEngine diagnostics;
  diagnostics.setWarningsAsErrors(options.warningsAsErrors);
  auto module = std::make_unique<ModuleAst>();

  // Native build configuration is read exclusively from the project manifest.
  // A project without [build]/[toolchain]/[native] keeps the host compiler
  // path; a native project must be complete or the build fails closed.
  native::Project nativeProject;
  if (projectMode && projectManifest.has_value()) {
    std::string manifestError;
    const std::optional<package::Manifest> loaded =
        package::Manifest::loadFile(*projectManifest, manifestError);
    if (!loaded.has_value()) {
      err << "coorc: " << manifestError << "\n";
      return 2;
    }
    std::vector<native::Issue> issues;
    if (!native::resolveProject(*loaded, nativeProject, issues)) {
      for (const native::Issue& issue : issues) {
        err << "coorc: error[" << issue.code << "]: " << issue.message << "\n";
      }
      return 1;
    }
    if (nativeProject.active &&
        !native::validateOptions(nativeProject, options.targetTriple,
                                 options.cxxCompiler, options.sysroot,
                                 issues)) {
      for (const native::Issue& issue : issues) {
        err << "coorc: error[" << issue.code << "]: " << issue.message << "\n";
      }
      return 1;
    }
  }

  // The compiler is resolved before the incremental check so that a compiler
  // or target change invalidates the stamp.
  const std::string compiler = nativeProject.active
                                   ? nativeProject.cxx
                                   : pickCompiler(options.cxxCompiler);
  const std::string compilerFlavor = options.cxxFlavor;
  const bool msvcCompiler = isMsvcCompiler(compiler, compilerFlavor);
  std::string compilerIdentity = compiler;
  if (const std::optional<std::filesystem::path> resolved =
          platform::findExecutable(compiler);
      resolved.has_value()) {
    compilerIdentity = platform::pathToUtf8(*resolved);
  }
  const std::string target =
      nativeProject.active
          ? nativeProject.target
          : (options.targetTriple.empty() ? platform::targetTriple()
                                          : options.targetTriple);
  if (!platform::isKnownTargetTriple(target)) {
    err << "coorc: invalid target triple '" << target << "'\n";
    err << "coorc: expected something like x86_64-pc-windows-msvc or "
           "aarch64-unknown-linux-gnu\n";
    return 2;
  }
  const std::string sysroot =
      nativeProject.active ? nativeProject.sysroot : options.sysroot;
  const bool crossTarget =
      target != platform::targetTriple();
  if (options.output == OutputKind::Executable) {
    if (crossTarget && msvcCompiler) {
      err << "coorc: MSVC cannot cross-compile to '" << target
          << "'; use clang with --target or a cross toolchain via --cxx\n";
      return 2;
    }
    if (crossTarget && options.runAfterBuild) {
      err << "coorc: cannot run a cross-compiled target '" << target
          << "' on '" << platform::targetTriple() << "'\n";
      return 2;
    }
    if (!sysroot.empty() && msvcCompiler) {
      err << "coorc: a sysroot is not supported by MSVC\n";
      return 2;
    }
  }

  std::string incrementalStampPath;
  std::string incrementalStampValue;
  if (options.incremental && options.output == OutputKind::Executable) {
    const std::string executable =
        options.outputPath.empty()
            ? defaultExecutablePath(options.inputPath)
            : options.outputPath;
    incrementalStampPath = executable + ".stamp";
    std::uintmax_t latest = 0;
    const auto touch = [&latest](const std::filesystem::path& file) {
      std::error_code timeError;
      const auto time = std::filesystem::last_write_time(file, timeError);
      if (!timeError) {
        latest = std::max(
            latest, static_cast<std::uintmax_t>(time.time_since_epoch().count()));
      }
    };
    for (const auto& file : inputFiles) {
      touch(file);
    }
    std::uintmax_t cooxCount = 0;
    const std::filesystem::path scanRoot =
        projectMode ? inputRoot : inputRoot.parent_path();
    std::error_code scanError;
    for (std::filesystem::recursive_directory_iterator iterator(scanRoot,
                                                                scanError),
         end;
         iterator != end && !scanError; iterator.increment(scanError)) {
      std::error_code typeError;
      if (iterator->is_directory(typeError)) {
        const std::string name = iterator->path().filename().string();
        if (name == "out" || name == ".git" || name == ".vs" ||
            name == "coord_modules") {
          iterator.disable_recursion_pending();
        }
        continue;
      }
      if (iterator->path().extension() == ".coox") {
        ++cooxCount;
        touch(iterator->path());
      }
    }
    if (projectManifest.has_value()) {
      touch(*projectManifest);
      touch(inputRoot / "coord.lock");
      std::error_code vendorError;
      for (std::filesystem::directory_iterator iterator(
               inputRoot / "coord_modules", vendorError),
           end;
           iterator != end && !vendorError; iterator.increment(vendorError)) {
        if (!iterator->is_directory(vendorError)) {
          continue;
        }
        if (const std::optional<std::filesystem::path> manifest =
                package::findManifest(iterator->path());
            manifest.has_value()) {
          touch(*manifest);
        }
      }
    }
    for (const std::filesystem::path& object : nativeProject.objects) {
      touch(object);
    }
    std::string nativeIdentity;
    if (nativeProject.active) {
      nativeIdentity = "|native:" + platform::pathToUtf8(nativeProject.manifest);
      for (const std::string& flag : nativeProject.cxxflags) {
        nativeIdentity += "|cxx:" + flag;
      }
      for (const std::string& flag : nativeProject.ldflags) {
        nativeIdentity += "|ld:" + flag;
      }
      for (const std::filesystem::path& include : nativeProject.includes) {
        nativeIdentity += "|I:" + platform::pathToUtf8(include);
      }
      for (const std::string& library : nativeProject.libraries) {
        nativeIdentity += "|l:" + library;
      }
      for (const std::filesystem::path& object : nativeProject.objects) {
        nativeIdentity += "|o:" + platform::pathToUtf8(object);
      }
    }
    const std::string toolchain =
        target + "|" + sysroot + "|" + compilerIdentity + "|" +
        compilerFlavor + "|" + (msvcCompiler ? "msvc" : "gnu") + "|" +
        std::to_string(options.warningsAsErrors ? 1 : 0) + nativeIdentity;
    incrementalStampValue =
        std::to_string(inputFiles.size()) + ":" + std::to_string(latest) + ":" +
        std::to_string(cooxCount) + ":" + toolchain;
    std::ifstream stampInput(incrementalStampPath);
    std::string existing;
    std::getline(stampInput, existing);
    std::error_code executableError;
    if (existing == incrementalStampValue &&
        std::filesystem::exists(executable, executableError)) {
      out << "coorc: up to date\n";
      return 0;
    }
  }

  for (const std::filesystem::path& inputPath : inputFiles) {
    LoadedFile loaded = readFile(inputPath);
    if (!loaded.ok) {
      err << "coorc: cannot read '" << platform::pathToUtf8(inputPath)
          << "'\n";
      return 2;
    }
    const std::uint32_t fileId = sources.addFile(
        platform::pathToUtf8(inputPath), std::move(loaded.text));
    Lexer lexer(sources, fileId, diagnostics);
    std::vector<Token> tokens = lexer.lexAll();
    if (options.dumpTokens) {
      dumpTokens(tokens, sources, out);
    }
    if (diagnostics.hasErrors()) {
      diagnostics.render(sources, err);
      return 1;
    }

    Parser parser(std::move(tokens), diagnostics);
    std::unique_ptr<ModuleAst> parsed = parser.parseModule(fileId);
    if (diagnostics.hasErrors()) {
      diagnostics.render(sources, err);
      return 1;
    }
    if (module->fileId == 0) {
      module->fileId = fileId;
    }
    module->decls.insert(module->decls.end(),
                         std::make_move_iterator(parsed->decls.begin()),
                         std::make_move_iterator(parsed->decls.end()));
    module->imports.insert(module->imports.end(),
                           std::make_move_iterator(parsed->imports.begin()),
                           std::make_move_iterator(parsed->imports.end()));
  }

  if (module->decls.empty() && !projectMode) {
    diagnostics.render(sources, err);
    return 1;
  }

  // Module imports: resolve relative to the importing file, parse each once,
  // and merge declarations before the user module. Cyclic/repeated imports are
  // visited once; a missing module is an error.
  std::set<std::string> loadedModules;
  loadedModules.insert(platform::pathKey(inputRoot));

  std::function<bool(ModuleAst&, const std::filesystem::path&)> loadImports;
  loadImports = [&](ModuleAst& owner,
                    const std::filesystem::path& baseDir) -> bool {
    for (const ImportDecl& import : owner.imports) {
      if (import.path.empty()) {
        continue;
      }
      std::filesystem::path relative;
      for (std::size_t i = 0; i < import.path.size(); ++i) {
        relative /= import.path[i];
      }
      if (relative.extension().empty()) {
        relative += ".coo";
      }
      const std::filesystem::path candidate = baseDir / relative;
      std::error_code existsError;
      if (!std::filesystem::exists(candidate, existsError)) {
        diagnostics.error(
            import.span, "E2070",
            "cannot resolve import '" + platform::pathToUtf8(relative) + "'");
        return false;
      }

      const std::string key = platform::pathKey(candidate);
      if (!loadedModules.insert(key).second) {
        continue;
      }

      std::ifstream input(candidate, std::ios::binary);
      if (!input) {
        diagnostics.error(import.span, "E2070",
                          "cannot read import '" +
                              platform::pathToUtf8(candidate) + "'");
        return false;
      }
      std::ostringstream buffer;
      buffer << input.rdbuf();
      const std::uint32_t importedFileId =
          sources.addFile(platform::pathToUtf8(candidate), buffer.str());
      Lexer importedLexer(sources, importedFileId, diagnostics);
      Parser importedParser(importedLexer.lexAll(), diagnostics);
      std::unique_ptr<ModuleAst> imported =
          importedParser.parseModule(importedFileId);
      if (diagnostics.hasErrors()) {
        return false;
      }
      if (!loadImports(*imported, candidate.parent_path())) {
        return false;
      }
      if (!import.items.empty()) {
        const std::set<std::string> selected(import.items.begin(),
                                             import.items.end());
        imported->decls.erase(
            std::remove_if(
                imported->decls.begin(), imported->decls.end(),
                [&](const DeclPtr& declaration) {
                  return !declaration->isCore &&
                         !declaration->isStandard &&
                         selected.count(declaration->name) == 0;
                }),
            imported->decls.end());
      }
      if (import.hasAlias) {
        std::vector<std::string>& names =
            owner.importAliases[import.alias];
        for (const auto& declaration : imported->decls) {
          if (!declaration->isCore && !declaration->isStandard) {
            names.push_back(declaration->name);
          }
        }
      }
      owner.decls.insert(owner.decls.begin(),
                         std::make_move_iterator(imported->decls.begin()),
                         std::make_move_iterator(imported->decls.end()));
    }
    return true;
  };

  if (!projectMode &&
      !loadImports(*module, inputRoot.parent_path())) {
    diagnostics.render(sources, err);
    return 1;
  }

  // Standard-library declarations are ordinary Coo sources loaded from
  // lib/standard: annotations.coo (meta-annotations and helpers), lambda.coo
  // (callable interfaces with abstract operator()) and utility.coo (the stable
  // utility entry point). Only the meta-annotations are special in C++.
  const auto loadStandardModule = [&](const std::string& displayName,
                                      const std::string& source) -> bool {
    if (source.empty()) {
      return false;
    }
    const std::uint32_t fileId = sources.addFile(displayName, source);
    Lexer lexer(sources, fileId, diagnostics);
    Parser parser(lexer.lexAll(), diagnostics);
    std::unique_ptr<ModuleAst> imported = parser.parseModule(fileId);
    if (diagnostics.hasErrors()) {
      return false;
    }
    for (auto& decl : imported->decls) {
      decl->isStandard = true;
    }
    module->decls.insert(module->decls.begin(),
                         std::make_move_iterator(imported->decls.begin()),
                         std::make_move_iterator(imported->decls.end()));
    return true;
  };

  std::string standardError;
  const std::string annotationSource =
      standardAnnotationsSource(&standardError);
  const std::string lambdaSource = standardLambdaSource(&standardError);
  const std::string utilitySource = standardUtilitySource(&standardError);
  if (!loadStandardModule("<standard-annotations>", annotationSource) ||
      !loadStandardModule("<standard-lambda>", lambdaSource) ||
      !loadStandardModule("<standard-utility>", utilitySource)) {
    if (standardError.empty()) {
      standardError = "cannot read the standard library";
    }
    diagnostics.error({}, "E2071", standardError);
    diagnostics.render(sources, err);
    return 1;
  }

  if (options.dumpAst) {
    dumpAst(*module, out);
  }

  if (options.dumpOutline) {
    const outline::Outline tree = outline::Outline::build(*module);
    const auto summaryOf = [](const std::string& document) {
      std::istringstream lines(document);
      std::string line;
      while (std::getline(lines, line)) {
        const std::size_t begin = line.find_first_not_of(" \t\r*");
        if (begin == std::string::npos) {
          continue;
        }
        line = line.substr(begin);
        if (line.empty() || line[0] == '@') {
          continue;
        }
        return line;
      }
      return std::string();
    };
    const auto printNode = [&](const auto& self, const outline::OutlineNode& node,
                               int depth) -> void {
      out << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "- "
          << node.name << " [" << node.kindName() << "]";
      if (node.isProxy) {
        out << " (proxy)";
      }
      if (node.leaf) {
        const std::string* signature = node.property("signature");
        if (signature != nullptr && !signature->empty()) {
          out << " " << *signature;
        }
      }
      out << '\n';
      const std::string summary = summaryOf(node.document);
      if (options.dumpOutlineDocs && !node.document.empty()) {
        std::istringstream docLines(node.document);
        std::string docLine;
        while (std::getline(docLines, docLine)) {
          const std::size_t begin = docLine.find_first_not_of(" \t\r*");
          if (begin == std::string::npos) continue;
          docLine = docLine.substr(begin);
          if (docLine.empty()) continue;
          out << std::string(static_cast<std::size_t>(depth + 1) * 2, ' ')
              << "/// " << docLine << '\n';
        }
      } else if (!summary.empty()) {
        out << std::string(static_cast<std::size_t>(depth + 1) * 2, ' ')
            << "/// " << summary << '\n';
      }
      for (const outline::OutlineNode& child : node.children) {
        self(self, child, depth + 1);
      }
    };
    printNode(printNode, tree.root, 0);
    return 0;
  }

  // Stage 3: execute restricted annotations processors before Sema so their
  // generated declarations participate in type checking.
  processor::Executor processorExecutor(*module, diagnostics);
  processorExecutor.run();
  if (diagnostics.hasErrors()) {
    diagnostics.render(sources, err);
    return 1;
  }
  for (auto& generated : processorExecutor.takeGenerated()) {
    module->decls.push_back(std::move(generated));
  }

  // Stage 3.5: semantic analysis.
  Checker checker(sources, diagnostics);
  checker.check(*module);
  if (diagnostics.hasErrors()) {
    diagnostics.render(sources, err);
    return 1;
  }

  // Native declarations need a complete project configuration: header/ABI come
  // from @Native, everything else from coordination.toml (or coord.pkg).
  const std::vector<const Decl*> nativeDecls =
      native::nativeDeclarations(*module);
  if (!nativeDecls.empty()) {
    if (!nativeProject.active) {
      diagnostics.error(
          nativeDecls.front()->span, "E2112",
          projectMode && projectManifest.has_value()
              ? "@Native requires a complete native configuration in "
                "coordination.toml: [build] target, [toolchain.<target>] cc "
                "and cxx, and the [native] objects/libraries produced by the "
                "project's own build system"
              : "@Native requires a project configuration (coordination.toml "
                "or coord.pkg); build the package directory (for example "
                "`coorc build .`) so the manifest is read - single-file "
                "compilation never guesses a compiler, target or header "
                "prelude");
    } else {
      for (const Decl* decl : nativeDecls) {
        if (decl->nativeFile.empty()) {
          continue;
        }
        const std::filesystem::path declared =
            platform::pathFromUtf8(decl->nativeFile);
        const std::filesystem::path header =
            declared.is_absolute()
                ? declared
                : (nativeProject.root / declared).lexically_normal();
        std::error_code headerError;
        if (!std::filesystem::is_regular_file(header, headerError)) {
          diagnostics.error(
              decl->span, "E2118",
              "native header '" + decl->nativeFile +
                  "' declared by @" + decl->name + " does not exist ('" +
                  platform::pathToUtf8(header) + "')");
        }
      }
    }
    if (diagnostics.hasErrors()) {
      diagnostics.render(sources, err);
      return 1;
    }
  }

  if (!options.interfacePath.empty()) {
    std::ofstream interfaceFile(options.interfacePath, std::ios::binary);
    if (!interfaceFile) {
      err << "coorc: cannot write interface '" << options.interfacePath
          << "'\n";
      return 2;
    }
    interfaceFile << InterfaceWriter::write(*module);
    if (!interfaceFile) {
      err << "coorc: failed writing interface '" << options.interfacePath
          << "'\n";
      return 2;
    }
  }

  // Stage 4: compile-time evaluation.
  Interpreter interpreter(*module, diagnostics);  interpreter.evaluate();
  interpreter.executeRetainedBlocks();
  if (diagnostics.hasErrors()) {
    diagnostics.render(sources, err);
    return 1;
  }
  if (options.printConstEval) {
    if (!interpreter.compileTimeOutput().empty()) {
      out << interpreter.compileTimeOutput();
    }
    for (const auto& entry : interpreter.constValues()) {
      out << "const " << entry.first << " = " << entry.second.toString()
          << '\n';
    }
  }

  if (options.output == OutputKind::Check) {
    if (diagnostics.warningCount() != 0) {
      diagnostics.render(sources, err);
    }
    return 0;
  }

  // Stage 5: lower to a C++20 translation unit.
  std::filesystem::path executablePath;
  std::filesystem::path cppPath;
  std::filesystem::path objectPath;
  std::filesystem::path bridgeCppPath;
  std::filesystem::path bridgeObjectPath;
  bool temporaryCpp = false;

  if (options.output == OutputKind::Cpp) {
    cppPath = options.outputPath.empty()
                  ? platform::pathFromUtf8(defaultCppPath(options.inputPath))
                  : platform::pathFromUtf8(options.outputPath);
  } else {
    executablePath =
        options.outputPath.empty()
            ? platform::pathFromUtf8(defaultExecutablePath(options.inputPath))
            : platform::pathFromUtf8(options.outputPath);
    cppPath = executablePath;
    cppPath += options.keepCpp ? ".cpp" : ".coord.cpp";
    objectPath = executablePath;
    objectPath += platform::objectFileSuffix();
    temporaryCpp = !options.keepCpp;
  }

  if (options.output == OutputKind::Cpp && options.outputPath == "-") {
    CppEmitter emitter(*module, interpreter.constValues(), out);
    emitter.setTestMode(options.testMode);
    emitter.setTestFilter(options.testFilter);
    emitter.emit();
  } else {
    std::ostringstream generated;
    CppEmitter emitter(*module, interpreter.constValues(), generated);
    emitter.setTestMode(options.testMode);
    emitter.setTestFilter(options.testFilter);
    emitter.emit();
    if (!writeFile(cppPath, generated.str())) {
      err << "coorc: cannot write '" << platform::pathToUtf8(cppPath) << "'\n";
      return 2;
    }
  }

  // Generate the ABI-compatible bridge alongside the program. Bridge and main
  // translation unit are compiled with the same configured [toolchain] cxx.
  if (options.output != OutputKind::Cpp || options.outputPath != "-") {
    if (!nativeDecls.empty()) {
      if (!nativeProject.active) {
        err << "coorc: internal error: native declarations without a project "
               "configuration\n";
        return 2;
      }
      std::string bridgeSource;
      std::string bridgeError;
      if (!native::generateBridge(*module, nativeDecls, nativeProject,
                                  bridgeSource, bridgeError)) {
        err << "coorc: native bridge: " << bridgeError << "\n";
        return 2;
      }
      if (options.output == OutputKind::Cpp) {
        bridgeCppPath = platform::pathFromUtf8(
            replaceExtension(platform::pathToUtf8(cppPath), ".native.cpp"));
      } else {
        bridgeCppPath = cppPath;
        bridgeCppPath += ".native.cpp";
      }
      if (!writeFile(bridgeCppPath, bridgeSource)) {
        err << "coorc: cannot write '"
            << platform::pathToUtf8(bridgeCppPath) << "'\n";
        return 2;
      }
      if (options.output == OutputKind::Executable) {
        bridgeObjectPath = executablePath;
        bridgeObjectPath += ".native";
        bridgeObjectPath += platform::objectFileSuffix();
      }
    }
  }

  if (options.output == OutputKind::Cpp) {
    if (diagnostics.warningCount() != 0) {
      diagnostics.render(sources, err);
    }
    return 0;
  }

  // Stage 6: the configured C++ compiler and linker. Never falls back to the
  // host default: a native project without a complete [toolchain] already
  // failed closed above.
  ToolchainInvocation invocation;
  invocation.compiler = compiler;
  invocation.flavor = compilerFlavor;
  invocation.target = target;
  invocation.sysroot = sysroot;
  invocation.crossTarget = crossTarget;
  invocation.cppPath = cppPath;
  invocation.bridgeCppPath = bridgeCppPath;
  invocation.objectPath = objectPath;
  invocation.bridgeObjectPath = bridgeObjectPath;
  invocation.executablePath = executablePath;
  if (nativeProject.active) {
    invocation.cxxflags = nativeProject.cxxflags;
    invocation.ldflags = nativeProject.ldflags;
    invocation.includeDirs = nativeProject.includes;
    bool hasRoot = false;
    for (const std::filesystem::path& include : invocation.includeDirs) {
      if (platform::pathKey(include) == platform::pathKey(nativeProject.root)) {
        hasRoot = true;
      }
    }
    if (!hasRoot) {
      invocation.includeDirs.push_back(nativeProject.root);
    }
    invocation.objectFiles = nativeProject.objects;
    invocation.libraries = nativeProject.libraries;
  }
  const int compileResult = invokeToolchain(invocation, options.verbose, out,
                                            err);
  if (compileResult != 0) {
    if (temporaryCpp) {
      std::error_code ignored;
      std::filesystem::remove(cppPath, ignored);
      std::filesystem::remove(objectPath, ignored);
      if (!bridgeCppPath.empty()) {
        std::filesystem::remove(bridgeCppPath, ignored);
      }
      if (!bridgeObjectPath.empty()) {
        std::filesystem::remove(bridgeObjectPath, ignored);
      }
    }
    return compileResult;
  }

  if (temporaryCpp) {
    std::error_code ignored;
    std::filesystem::remove(cppPath, ignored);
    std::filesystem::remove(objectPath, ignored);
    if (!bridgeCppPath.empty()) {
      std::filesystem::remove(bridgeCppPath, ignored);
    }
    if (!bridgeObjectPath.empty()) {
      std::filesystem::remove(bridgeObjectPath, ignored);
    }
  }

  if (diagnostics.warningCount() != 0) {
    diagnostics.render(sources, err);
  }

  // Stage 7: execute the produced binary directly (never through a shell).
  if (options.runAfterBuild) {
    std::filesystem::path runnable = executablePath;
    if (runnable.is_relative()) {
      std::error_code absoluteError;
      const std::filesystem::path absolute =
          std::filesystem::absolute(runnable, absoluteError);
      if (!absoluteError) {
        runnable = absolute;
      }
    }
    platform::ProcessOptions processOptions;
    processOptions.captureOutput = false;
    const platform::ProcessResult runResult =
        platform::run({platform::pathToUtf8(runnable)}, processOptions);
    if (!runResult.started) {
      err << "coorc: cannot run '" << platform::pathToUtf8(runnable)
          << "': " << runResult.failure << "\n";
      return 1;
    }
    if (runResult.exitCode != 0) {
      err << "coorc: program exited with code " << runResult.exitCode << "\n";
      return runResult.exitCode;
    }
  }

  if (!incrementalStampPath.empty()) {
    std::ofstream stampOutput(incrementalStampPath);
    stampOutput << incrementalStampValue << '\n';
  }

  return 0;
}

}  // namespace coord
