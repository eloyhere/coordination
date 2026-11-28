#include "coord/native.h"

#include "coord/modules/platform.h"
#include "coord/sema.h"

#include <set>
#include <sstream>

namespace coord::native {
namespace {

std::string sanitizeIdentifier(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  for (const char character : name) {
    const bool alpha = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z');
    const bool digit = character >= '0' && character <= '9';
    result.push_back(alpha || digit || character == '_' ? character : '_');
  }
  if (result.empty()) {
    result = "anonymous";
  }
  return result;
}

[[nodiscard]] bool hasPathSeparator(std::string_view text) {
  return text.find('/') != std::string_view::npos ||
         text.find('\\') != std::string_view::npos;
}

[[nodiscard]] bool looksLikeLibraryPath(std::string_view text) {
  if (hasPathSeparator(text)) {
    return true;
  }
  const auto endsWith = [&](std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) ==
               0;
  };
  return endsWith(".a") || endsWith(".lib") || endsWith(".so") ||
         endsWith(".dylib") || endsWith(".dll") || endsWith(".o") ||
         endsWith(".obj");
}

[[nodiscard]] std::filesystem::path resolveAgainst(
    const std::filesystem::path& root, const std::string& value) {
  const std::filesystem::path path = platform::pathFromUtf8(value);
  return path.is_absolute() ? path : (root / path).lexically_normal();
}

[[nodiscard]] bool configuredCompilerExists(const std::string& value) {
  // A bare name (`cl`, `clang++`) is resolved by the operating system; only an
  // explicit path is checked here so a missing toolchain is a hard error.
  if (!hasPathSeparator(value)) {
    return true;
  }
  std::error_code error;
  return std::filesystem::is_regular_file(platform::pathFromUtf8(value),
                                          error);
}

void pushIssue(std::vector<Issue>& issues, std::string code,
               std::string message, const Decl* decl = nullptr) {
  issues.push_back(Issue{std::move(code), std::move(message), decl});
}

/// `void` is only valid as a return type.
[[nodiscard]] bool isVoidType(const Type* type) {
  return type != nullptr && type->name == "void";
}

}  // namespace

std::vector<const Decl*> nativeDeclarations(const ModuleAst& module) {
  std::vector<const Decl*> result;
  for (const auto& decl : module.decls) {
    if (!decl->isNative) {
      continue;
    }
    if (decl->kind == DeclKind::Function || decl->kind == DeclKind::Class) {
      result.push_back(decl.get());
    }
  }
  return result;
}

bool hasNativeDeclarations(const ModuleAst& module) {
  return !nativeDeclarations(module).empty();
}

bool resolveProject(const package::Manifest& manifest, Project& project,
                    std::vector<Issue>& issues) {
  project = Project{};
  project.root = manifest.root;
  project.manifest = manifest.path;
  project.legacyManifest = manifest.legacy;
  project.active = manifest.hasNativeConfiguration();
  if (!project.active) {
    return true;
  }
  bool ok = true;

  if (manifest.buildTarget.empty()) {
    pushIssue(issues, "E2115",
              "native projects require [build] target = \"<triple>\" in " +
                  platform::pathToUtf8(manifest.path));
    return false;
  }
  project.target = manifest.buildTarget;
  if (!platform::isKnownTargetTriple(project.target)) {
    pushIssue(issues, "E2115",
              "unknown [build] target '" + project.target +
                  "'; expected a triple such as x86_64-unknown-linux-gnu or "
                  "x86_64-pc-windows-msvc");
    ok = false;
  }

  const package::ToolchainConfig* toolchain =
      manifest.toolchain(manifest.buildTarget);
  if (toolchain == nullptr) {
    pushIssue(issues, "E2116",
              "no [toolchain." + project.target +
                  "] table in " + platform::pathToUtf8(manifest.path) +
                  "; the compiler and linker are never guessed");
    return false;
  }
  project.cc = toolchain->cc;
  project.cxx = toolchain->cxx;
  project.sysroot = toolchain->sysroot;
  project.cxxflags = toolchain->cxxflags;
  project.ldflags = toolchain->ldflags;
  if (project.cc.empty()) {
    pushIssue(issues, "E2116",
              "[toolchain." + project.target + "] requires cc");
    ok = false;
  } else if (!configuredCompilerExists(project.cc)) {
    pushIssue(issues, "E2116",
              "configured [toolchain." + project.target + "] cc '" +
                  project.cc + "' does not exist");
    ok = false;
  }
  if (project.cxx.empty()) {
    pushIssue(issues, "E2116",
              "[toolchain." + project.target + "] requires cxx");
    ok = false;
  } else if (!configuredCompilerExists(project.cxx)) {
    pushIssue(issues, "E2116",
              "configured [toolchain." + project.target + "] cxx '" +
                  project.cxx + "' does not exist");
    ok = false;
  }
  if (!project.sysroot.empty()) {
    std::error_code error;
    if (!std::filesystem::is_directory(platform::pathFromUtf8(project.sysroot),
                                       error)) {
      pushIssue(issues, "E2118",
                "configured [toolchain." + project.target + "] sysroot '" +
                    project.sysroot + "' does not exist");
      ok = false;
    }
  }

  for (const std::string& entry : manifest.native.include) {
    const std::filesystem::path path = resolveAgainst(manifest.root, entry);
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
      pushIssue(issues, "E2118",
                "[native] include directory '" + entry + "' does not exist ('" +
                    platform::pathToUtf8(path) + "')");
      ok = false;
      continue;
    }
    project.includes.push_back(path);
  }
  for (const std::string& entry : manifest.native.objects) {
    const std::filesystem::path path = resolveAgainst(manifest.root, entry);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
      pushIssue(issues, "E2118",
                "[native] object '" + entry + "' does not exist ('" +
                    platform::pathToUtf8(path) +
                    "'); build the native sources with the project's own "
                    "build system first");
      ok = false;
      continue;
    }
    project.objects.push_back(path);
  }
  for (const std::string& entry : manifest.native.libraries) {
    if (!looksLikeLibraryPath(entry)) {
      project.libraries.push_back(entry);
      continue;
    }
    const std::filesystem::path path = resolveAgainst(manifest.root, entry);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
      pushIssue(issues, "E2118",
                "[native] library '" + entry + "' does not exist ('" +
                    platform::pathToUtf8(path) + "')");
      ok = false;
      continue;
    }
    project.libraries.push_back(platform::pathToUtf8(path));
  }
  return ok;
}

bool validateOptions(const Project& project, const std::string& targetOption,
                     const std::string& cxxOption,
                     const std::string& sysrootOption,
                     std::vector<Issue>& issues) {
  bool ok = true;
  if (!project.active) {
    return true;
  }
  if (!targetOption.empty() && targetOption != project.target) {
    pushIssue(issues, "E2117",
              "--target '" + targetOption +
                  "' contradicts [build] target '" + project.target +
                  "'; the project manifest owns the target triple");
    ok = false;
  }
  if (!cxxOption.empty() && !project.cxx.empty() &&
      cxxOption != project.cxx) {
    pushIssue(issues, "E2117",
              "--cxx '" + cxxOption + "' contradicts [toolchain." +
                  project.target + "] cxx '" + project.cxx +
                  "'; the project manifest owns the toolchain");
    ok = false;
  }
  if (!sysrootOption.empty() && sysrootOption != project.sysroot) {
    pushIssue(issues, "E2117",
              "--sysroot '" + sysrootOption +
                  "' contradicts [toolchain." + project.target + "] sysroot '" +
                  project.sysroot + "'");
    ok = false;
  }
  return ok;
}

bool isAbiTypeSupported(const Type* type, int length) {
  if (type == nullptr) {
    return false;
  }
  if (isVoidType(type)) {
    return length == 0;
  }
  switch (type->kind) {
    case TypeKind::Int:
      return length != 128;
    case TypeKind::Bool:
    case TypeKind::Float:
    case TypeKind::String:
      return length == 0;
    default:
      return false;
  }
}

std::string abiType(const Type* type, int length) {
  if (type == nullptr) {
    return {};
  }
  if (isVoidType(type)) {
    return "void";
  }
  switch (type->kind) {
    case TypeKind::Int: {
      int bits = length;
      if (bits == 0) {
        bits = 64;
      }
      switch (bits) {
        case 8:
          return "std::int8_t";
        case 16:
          return "std::int16_t";
        case 32:
          return "std::int32_t";
        case 64:
          return "std::int64_t";
        default:
          return {};
      }
    }
    case TypeKind::Bool:
      if (length == 0) {
        return "bool";
      }
      return {};
    case TypeKind::Float:
      if (length == 0) {
        return "double";
      }
      return {};
    case TypeKind::String:
      if (length == 0) {
        return "const char*";
      }
      return {};
    default:
      return {};
  }
}

std::string bridgeFunctionSymbol(std::size_t index, const Decl& function) {
  return "coo_native_fn_" + std::to_string(index) + "_" +
         sanitizeIdentifier(function.name);
}

std::string bridgeConstructorSymbol(std::size_t classIndex, const Decl& cls,
                                    std::size_t memberIndex) {
  return "coo_native_c_" + std::to_string(classIndex) + "_" +
         sanitizeIdentifier(cls.name) + "__ctor_" +
         std::to_string(memberIndex);
}

std::string bridgeDestructorSymbol(std::size_t classIndex, const Decl& cls) {
  return "coo_native_c_" + std::to_string(classIndex) + "_" +
         sanitizeIdentifier(cls.name) + "__dtor";
}

std::string bridgeMethodSymbol(std::size_t classIndex, const Decl& cls,
                               std::size_t memberIndex, const Decl& method) {
  return "coo_native_c_" + std::to_string(classIndex) + "_" +
         sanitizeIdentifier(cls.name) + "__m_" +
         std::to_string(memberIndex) + "_" + sanitizeIdentifier(method.name);
}

std::size_t nativeIndexOf(const std::vector<const Decl*>& decls,
                          const Decl* decl) {
  for (std::size_t i = 0; i < decls.size(); ++i) {
    if (decls[i] == decl) {
      return i;
    }
  }
  return decls.size();
}

namespace {

struct BridgeWriter {
  std::ostringstream out;

  void line(const std::string& text) { out << text << '\n'; }
};

[[nodiscard]] std::string stringLiteral(std::string_view text) {
  std::string result = "\"";
  for (const char character : text) {
    if (character == '\\' || character == '"') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] std::string zeroValue(const std::string& abiTypeName) {
  if (abiTypeName == "const char*") {
    return "nullptr";
  }
  return abiTypeName + "{}";
}

/// Renders the ABI-visible parameter list (`void* coo_self` first for methods).
void writeAbiParameters(BridgeWriter& writer, const Decl& callable,
                        bool withSelf) {
  writer.out << '(';
  bool first = true;
  if (withSelf) {
    writer.out << "void* coo_self";
    first = false;
  }
  for (std::size_t i = 0; i < callable.params.size(); ++i) {
    if (!first) {
      writer.out << ", ";
    }
    first = false;
    const Param& param = callable.params[i];
    writer.out << abiType(param.type->resolved, param.type->numericLength)
               << " coo_arg" << i;
  }
  writer.out << ')';
}

/// Renders the argument list of a native call.
[[nodiscard]] std::string cooArgumentExpression(const Param& param,
                                                std::size_t index) {
  const std::string name = "coo_arg" + std::to_string(index);
  if (param.type->resolved->kind == TypeKind::String) {
    return name;
  }
  return "static_cast<" +
         abiType(param.type->resolved, param.type->numericLength) + ">(" +
         name + ")";
}

}  // namespace

bool generateBridge(const ModuleAst& module,
                    const std::vector<const Decl*>& decls,
                    const Project& project, std::string& source,
                    std::string& error) {
  (void)module;
  if (decls.empty()) {
    source.clear();
    return true;
  }
  BridgeWriter writer;
  writer.line("// Generated native bridge. Do not edit.");
  writer.line(
      "// This translation unit is compiled with the project [toolchain." +
      project.target + "] cxx; the user sources are built by the project.");
  writer.line("#include <cstddef>");
  writer.line("#include <cstdint>");
  writer.line("#include <new>");
  writer.line("#include <string>");
  writer.line("");

  std::vector<std::string> headers;
  std::set<std::string> seenHeaders;
  for (const Decl* decl : decls) {
    if (decl->nativeFile.empty()) {
      error = "native declaration '" + decl->name + "' has no header";
      return false;
    }
    if (!seenHeaders.insert(decl->nativeFile).second) {
      continue;
    }
    const std::filesystem::path path =
        platform::pathFromUtf8(decl->nativeFile).is_absolute()
            ? platform::pathFromUtf8(decl->nativeFile)
            : (project.root / platform::pathFromUtf8(decl->nativeFile))
                  .lexically_normal();
    std::error_code existsError;
    if (!std::filesystem::is_regular_file(path, existsError)) {
      error = "native header '" + decl->nativeFile + "' declared by '" +
              decl->name + "' does not exist ('" + platform::pathToUtf8(path) +
              "')";
      return false;
    }
    headers.push_back(decl->nativeFile);
  }
  for (const std::string& header : headers) {
    writer.line("#include " + stringLiteral(header));
  }
  writer.line("");
  writer.line("extern \"C\" {");

  for (std::size_t index = 0; index < decls.size(); ++index) {
    const Decl& decl = *decls[index];
    if (decl.kind == DeclKind::Function) {
      const std::string symbol = bridgeFunctionSymbol(index, decl);
      const std::string returnType = abiType(
          decl.type != nullptr ? decl.type->resolved : nullptr, decl.length);
      if (returnType.empty()) {
        error = "unsupported native return type for '" + decl.name + "'";
        return false;
      }
      const bool returnsString = returnType == "const char*";
      const bool cppAbi = decl.nativeAbi == "c++";
      if (returnsString && cppAbi) {
        // A C++ function may return std::string; copy through a thread-local
        // buffer so the caller sees a stable C string.
        writer.line("thread_local std::string " + symbol + "__result;");
      }
      writer.out << returnType << ' ' << symbol;
      writeAbiParameters(writer, decl, false);
      writer.out << " {\n";
      if (returnType == "void") {
        writer.out << "  ::" << decl.nativeSymbol;
      } else if (returnsString && cppAbi) {
        writer.out << "  " << symbol << "__result = ::" << decl.nativeSymbol;
      } else if (returnsString) {
        writer.out << "  return ::" << decl.nativeSymbol;
      } else {
        writer.out << "  return static_cast<" << returnType << ">(::"
                   << decl.nativeSymbol;
      }
      writer.out << '(';
      for (std::size_t i = 0; i < decl.params.size(); ++i) {
        if (i != 0) {
          writer.out << ", ";
        }
        writer.out << cooArgumentExpression(decl.params[i], i);
      }
      writer.out << ')';
      if (returnsString && cppAbi) {
        writer.out << ";\n";
        writer.out << "  return " << symbol << "__result.c_str();\n";
      } else if (returnType == "void" || returnsString) {
        writer.out << ";\n";
      } else {
        writer.out << ");\n";
      }
      writer.out << "}\n";
      continue;
    }

    // Native class: opaque handle with explicit construction/destruction.
    const std::string className = sanitizeIdentifier(decl.name);
    writer.line("// class " + decl.name + " (opaque handle)");
    for (std::size_t memberIndex = 0; memberIndex < decl.members.size();
         ++memberIndex) {
      const Decl& member = *decl.members[memberIndex];
      if (member.kind == DeclKind::Constructor) {
        const std::string symbol =
            bridgeConstructorSymbol(index, decl, memberIndex);
        writer.out << "void* " << symbol;
        writeAbiParameters(writer, member, false);
        writer.out << " {\n";
        writer.out << "  return static_cast<void*>(new (std::nothrow) "
                   << className << '(';
        for (std::size_t i = 0; i < member.params.size(); ++i) {
          if (i != 0) {
            writer.out << ", ";
          }
          writer.out << cooArgumentExpression(member.params[i], i);
        }
        writer.out << "));\n}\n";
      }
    }
    const std::string destructorSymbol = bridgeDestructorSymbol(index, decl);
    writer.line("void " + destructorSymbol +
                "(void* coo_self) {\n  delete static_cast<" + className +
                "*>(coo_self);\n}");
    for (std::size_t memberIndex = 0; memberIndex < decl.members.size();
         ++memberIndex) {
      const Decl& member = *decl.members[memberIndex];
      if (member.kind != DeclKind::Method) {
        continue;
      }
      const std::string returnType = abiType(
          member.type != nullptr ? member.type->resolved : nullptr,
          member.length);
      if (returnType.empty()) {
        error = "unsupported native return type for '" + decl.name + "::" +
                member.name + "'";
        return false;
      }
      const bool returnsString = returnType == "const char*";
      const std::string symbol =
          bridgeMethodSymbol(index, decl, memberIndex, member);
      if (returnsString) {
        writer.line("thread_local std::string " + symbol + "__result;");
      }
      writer.out << returnType << ' ' << symbol;
      writeAbiParameters(writer, member, true);
      writer.out << " {\n";
      if (returnType != "void") {
        writer.out << "  if (coo_self == nullptr) return "
                   << zeroValue(returnType) << ";\n";
      } else {
        writer.out << "  if (coo_self == nullptr) return;\n";
      }
      writer.out << "  ";
      if (returnType == "void") {
        writer.out << "static_cast<" << className
                   << "*>(coo_self)->" << member.name;
      } else if (returnsString) {
        writer.out << symbol << "__result = static_cast<" << className
                   << "*>(coo_self)->" << member.name;
      } else {
        writer.out << "return static_cast<" << returnType << ">(static_cast<"
                   << className << "*>(coo_self)->" << member.name;
      }
      writer.out << '(';
      for (std::size_t i = 0; i < member.params.size(); ++i) {
        if (i != 0) {
          writer.out << ", ";
        }
        writer.out << cooArgumentExpression(member.params[i], i);
      }
      writer.out << ')';
      if (returnsString) {
        writer.out << ";\n  return " << symbol << "__result.c_str();\n";
      } else if (returnType == "void") {
        writer.out << ";\n";
      } else {
        writer.out << ");\n";
      }
      writer.out << "}\n";
    }
    writer.line("");
  }

  writer.line("}  // extern \"C\"");
  writer.line("");
  source = writer.out.str();
  return true;
}

}  // namespace coord::native
