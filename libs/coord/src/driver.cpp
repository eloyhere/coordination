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

int invokeHostCompiler(const std::string& compiler, const std::string& flavor,
                       const std::string& target, const std::string& sysroot,
                       bool crossTarget,
                       const std::filesystem::path& cppPath,
                       const std::filesystem::path& executablePath,
                       const std::filesystem::path& objectPath, bool verbose,
                       std::ostream& out, std::ostream& err) {
  const bool msvc = isMsvcCompiler(compiler, flavor);
  std::vector<std::string> arguments;
  if (msvc) {
    arguments = {compiler,
                 "/nologo",
                 "/std:c++20",
                 "/EHsc",
                 "/utf-8",
                 platform::pathToUtf8(cppPath),
                 "/Fe" + platform::pathToUtf8(executablePath),
                 "/Fo" + platform::pathToUtf8(objectPath)};
  } else {
    arguments = {compiler, "-std=c++20", "-O2"};
#if !defined(_WIN32)
    arguments.push_back("-pthread");
#endif
    if (crossTarget && looksLikeClang(compiler)) {
      arguments.push_back("--target=" + target);
    }
    if (!sysroot.empty()) {
      arguments.push_back("--sysroot=" + sysroot);
    }
    arguments.push_back(platform::pathToUtf8(cppPath));
    arguments.push_back("-o");
    arguments.push_back(platform::pathToUtf8(executablePath));
  }
  platform::ProcessOptions processOptions;
  processOptions.captureOutput = true;
  const platform::ProcessResult result =
      platform::run(arguments, processOptions);
  const std::string command = platform::commandLine(arguments);
  if (!result.started) {
    err << "coorc: cannot start the host C++ compiler: " << result.failure
        << "\n";
    err << "coorc: command: " << command << "\n";
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
    err << "coorc: host C++ compilation failed (exit " << result.exitCode
        << ")\n";
    err << "coorc: command: " << command << "\n";
    if (msvc) {
      err << "coorc: on Windows run from a Visual Studio developer prompt, or "
             "pass --cxx <path-to-cl.exe>\n";
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

  // The host compiler is resolved before the incremental check so that a
  // compiler or target change invalidates the stamp.
  const std::string compiler = pickCompiler(options.cxxCompiler);
  const std::string compilerFlavor = options.cxxFlavor;
  const bool msvcCompiler = isMsvcCompiler(compiler, compilerFlavor);
  std::string compilerIdentity = compiler;
  if (const std::optional<std::filesystem::path> resolved =
          platform::findExecutable(compiler);
      resolved.has_value()) {
    compilerIdentity = platform::pathToUtf8(*resolved);
  }
  const std::string target =
      options.targetTriple.empty() ? platform::targetTriple()
                                   : options.targetTriple;
  if (!platform::isKnownTargetTriple(target)) {
    err << "coorc: invalid target triple '" << target << "'\n";
    err << "coorc: expected something like x86_64-pc-windows-msvc or "
           "aarch64-unknown-linux-gnu\n";
    return 2;
  }
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
    if (!options.sysroot.empty() && msvcCompiler) {
      err << "coorc: --sysroot is not supported by MSVC\n";
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
    const std::string toolchain =
        target + "|" + options.sysroot + "|" + compilerIdentity + "|" +
        compilerFlavor + "|" + (msvcCompiler ? "msvc" : "gnu") + "|" +
        std::to_string(options.warningsAsErrors ? 1 : 0);
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

  // Standard-library annotations: ordinary Coo declarations loaded from
  // lib/standard/annotations.coo; only the meta-annotations are C++.
  std::string standardError;
  const std::string standardSource =
      standardAnnotationsSource(&standardError);
  if (standardSource.empty()) {
    diagnostics.error({}, "E2071", standardError);
    diagnostics.render(sources, err);
    return 1;
  }
  const std::uint32_t standardFileId =
      sources.addFile("<standard-annotations>", standardSource);
  Lexer standardLexer(sources, standardFileId, diagnostics);
  Parser standardParser(standardLexer.lexAll(), diagnostics);
  std::unique_ptr<ModuleAst> standardModule =
      standardParser.parseModule(standardFileId);
  if (diagnostics.hasErrors()) {
    diagnostics.render(sources, err);
    return 1;
  }
  for (auto& decl : standardModule->decls) {
    decl->isStandard = true;
  }
  module->decls.insert(module->decls.begin(),
                       std::make_move_iterator(standardModule->decls.begin()),
                       std::make_move_iterator(standardModule->decls.end()));

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

  if (options.output == OutputKind::Cpp) {
    if (diagnostics.warningCount() != 0) {
      diagnostics.render(sources, err);
    }
    return 0;
  }

  // Stage 6: host C++ compiler and linker. The compiler and its argument
  // style were chosen before the incremental check.
  const int compileResult =
      invokeHostCompiler(compiler, compilerFlavor, target, options.sysroot,
                         crossTarget, cppPath, executablePath, objectPath,
                         options.verbose, out, err);
  if (compileResult != 0) {
    if (temporaryCpp) {
      std::error_code ignored;
      std::filesystem::remove(cppPath, ignored);
    }
    return compileResult;
  }

  if (temporaryCpp) {
    std::error_code ignored;
    std::filesystem::remove(cppPath, ignored);
    std::filesystem::remove(objectPath, ignored);
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
