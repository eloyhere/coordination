#pragma once

#include "coord/ast.h"
#include "coord/modules/access.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace coord {

/// One-way `.coox` interface writer. It is intentionally a readable,
/// declaration-level format (not a compiled artifact yet); P5 will add the
/// reader and incremental metadata. Only declaration signatures are emitted —
/// no function bodies, no runtime metadata.
class InterfaceWriter {
 public:
  [[nodiscard]] static std::string write(const ModuleAst& module) {
    std::ostringstream out;
    out << "// Coordination interface (generated)\n";
    out << "module ";
    if (module.modulePath.empty()) {
      out << "<module>";
    } else {
      for (std::size_t i = 0; i < module.modulePath.size(); ++i) {
        if (i != 0) {
          out << '.';
        }
        out << module.modulePath[i];
      }
    }
    out << "\n\n";

    for (const auto& decl : module.decls) {
      if (decl->isCore || decl->isStandard) {
        continue;
      }
      if (decl->kind == DeclKind::Annotation && decl->isCore) {
        continue;
      }
      writeDecl(out, *decl, 0);
    }
    return out.str();
  }

 private:
  static std::string annotations(const Decl& decl) {
    std::string result;
    for (const AnnotationUse& use : decl.annotations) {
      result += "@" + use.name + " ";
    }
    return result;
  }

  static std::string visibility(Access access) {
    if (access == Access::Public) {
      return "public: ";
    }
    if (access == Access::Protected) {
      return "protected: ";
    }
    if (access == Access::Private) {
      return "private: ";
    }
    return "";
  }

  static std::string signature(const Decl& decl) {
    std::string result;
    if (decl.kind == DeclKind::Constructor) {
      result = decl.name;
    } else if (decl.kind == DeclKind::Destructor) {
      result = "~" + decl.name;
    } else {
      result = decl.type != nullptr ? describeType(decl.type.get()) : "void";
      result += " " + decl.name;
    }
    result += "(";
    for (std::size_t i = 0; i < decl.params.size(); ++i) {
      if (i != 0) {
        result += ", ";
      }
      if (decl.params[i].type != nullptr) {
        result += describeType(decl.params[i].type.get());
      }
      if (!decl.params[i].name.empty()) {
        result += " " + decl.params[i].name;
      }
    }
    result += ")";
    if (decl.isConstMethod) {
      result += " const";
    }
    return result;
  }

  static void writeMembers(std::ostringstream& out, const Decl& decl,
                           int depth) {
    const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
    for (const auto& member : decl.members) {
      if (member->synthesizedDestructor) {
        continue;
      }
      if (member->access != Access::Public) {
        continue;
      }
      out << pad;
      switch (member->kind) {
        case DeclKind::Field:
          out << (member->type != nullptr
                      ? describeType(member->type.get())
                      : "void")
              << ' ' << member->name << ";\n";
          break;
        case DeclKind::Constructor:
        case DeclKind::Destructor:
        case DeclKind::Method:
          out << annotations(*member) << signature(*member) << ";\n";
          break;
        default:
          break;
      }
    }
  }

  static void writeDecl(std::ostringstream& out, const Decl& decl, int depth) {
    const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
    const std::string prefix = decl.exported ? "export " : "";
    switch (decl.kind) {
      case DeclKind::Class:
      case DeclKind::Interface:
        out << pad << annotations(decl) << prefix
            << (decl.kind == DeclKind::Class ? "class " : "interface ")
            << decl.name;
        if (decl.base != nullptr) {
          out << " extends " << describeType(decl.base.get());
        }
        if (!decl.interfaces.empty()) {
          out << " implements ";
          for (std::size_t i = 0; i < decl.interfaces.size(); ++i) {
            if (i != 0) {
              out << ", ";
            }
            out << describeType(decl.interfaces[i].get());
          }
        }
        out << " {\n";
        writeMembers(out, decl, depth + 1);
        out << pad << "}\n\n";
        break;
      case DeclKind::Enum:
        out << pad << annotations(decl) << prefix << "enumerate " << decl.name
            << " {\n";
        for (const EnumVariant& variant : decl.variants) {
          out << pad << "  " << variant.name << " = "
              << variant.resolvedValue << ";\n";
        }
        writeMembers(out, decl, depth + 1);
        out << pad << "}\n\n";
        break;
      case DeclKind::Function:
        out << pad << annotations(decl) << prefix << signature(decl) << ";\n\n";
        break;
      case DeclKind::Const:
        out << pad << annotations(decl) << prefix << "const "
            << (decl.type != nullptr ? describeType(decl.type.get()) : "void")
            << ' ' << decl.name << ";\n";
        break;
      case DeclKind::Annotation:
        out << pad << annotations(decl) << prefix << "annotation "
            << decl.name << " {\n";
        writeMembers(out, decl, depth + 1);
        out << pad << "}\n\n";
        break;
      default:
        break;
    }
  }
};

struct InterfaceMember {
  enum class Kind { Field, Method, Constructor, Destructor };
  Kind kind = Kind::Field;
  std::string type;
  std::string name;
  std::vector<std::pair<std::string, std::string>> params;
  bool isConst = false;
};

struct InterfaceDecl {
  enum class Kind { Class, Interface, Enum, Annotation, Function, Const };
  Kind kind = Kind::Class;
  std::string name;
  std::string type;
  std::string base;
  std::vector<std::string> interfaces;
  std::vector<InterfaceMember> members;
  std::vector<std::pair<std::string, std::int64_t>> variants;
};

struct InterfaceModule {
  std::vector<std::string> modulePath;
  std::vector<InterfaceDecl> declarations;
};

/// Read the readable `.coox` format produced by InterfaceWriter. The reader is
/// intentionally tolerant of whitespace but rejects malformed headers or
/// unbalanced braces. It does not execute or type-check the interface.
class InterfaceReader {
 public:
  [[nodiscard]] static bool read(std::string_view text,
                                 InterfaceModule& module,
                                 std::string& error) {
    std::istringstream input{std::string(text)};
    std::string line;
    InterfaceDecl* current = nullptr;
    int depth = 0;
    while (std::getline(input, line)) {
      const std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed.rfind("//", 0) == 0) {
        continue;
      }
      if (current == nullptr) {
        if (startsWith(trimmed, "module ")) {
          module.modulePath = splitPath(trimmed.substr(7));
          continue;
        }
        std::string header = stripDecorations(trimmed);
        if (startsWith(header, "class ") ||
            startsWith(header, "interface ") ||
            startsWith(header, "enumerate ") ||
            startsWith(header, "annotation ")) {
          InterfaceDecl decl;
          decl.kind = header.rfind("class ", 0) == 0
                          ? InterfaceDecl::Kind::Class
                      : header.rfind("interface ", 0) == 0
                          ? InterfaceDecl::Kind::Interface
                      : header.rfind("enumerate ", 0) == 0
                          ? InterfaceDecl::Kind::Enum
                          : InterfaceDecl::Kind::Annotation;
          std::size_t offset = header.find(' ') + 1;
          std::size_t nameEnd = header.find_first_of(" {", offset);
          decl.name = header.substr(offset, nameEnd - offset);
          if (decl.name.empty()) {
            error = "missing declaration name";
            return false;
          }
          const std::string rest =
              nameEnd == std::string::npos ? "" : header.substr(nameEnd);
          const std::size_t extends = rest.find("extends ");
          if (extends != std::string::npos) {
            const std::size_t begin = extends + 8;
            const std::size_t end = rest.find(" implements ", begin);
            decl.base = trim(rest.substr(
                begin, end == std::string::npos ? std::string::npos
                                                : end - begin));
            decl.base = trim(decl.base.substr(0, decl.base.find('{')));
          }
          const std::size_t implementsAt = rest.find("implements ");
          if (implementsAt != std::string::npos) {
            std::string list = rest.substr(implementsAt + 11);
            list = trim(list.substr(0, list.find('{')));
            for (const std::string& part : split(list, ',')) {
              const std::string item = trim(part);
              if (!item.empty()) {
                decl.interfaces.push_back(item);
              }
            }
          }
          if (rest.find('{') == std::string::npos) {
            error = "expected '{' in declaration '" + decl.name + "'";
            return false;
          }
          module.declarations.push_back(std::move(decl));
          current = &module.declarations.back();
          ++depth;
          continue;
        }
        if (startsWith(header, "const ")) {
          InterfaceDecl decl;
          decl.kind = InterfaceDecl::Kind::Const;
          const std::string rest = header.substr(6);
          const std::size_t splitAt = rest.find_last_of(' ');
          if (splitAt == std::string::npos) {
            error = "malformed const declaration";
            return false;
          }
          decl.type = trim(rest.substr(0, splitAt));
          decl.name = trim(rest.substr(splitAt + 1));
          module.declarations.push_back(std::move(decl));
          continue;
        }
        if (header.find('(') != std::string::npos) {
          InterfaceDecl decl;
          decl.kind = InterfaceDecl::Kind::Function;
          InterfaceMember member;
          if (!parseCallable(header, member, error)) {
            return false;
          }
          decl.name = member.name;
          decl.members.push_back(std::move(member));
          module.declarations.push_back(std::move(decl));
          continue;
        }
        error = "unrecognized interface line: " + trimmed;
        return false;
      }

      if (trimmed == "}") {
        current = nullptr;
        --depth;
        if (depth < 0) {
          error = "unbalanced '}'";
          return false;
        }
        continue;
      }
      if (current->kind == InterfaceDecl::Kind::Enum &&
          trimmed.find('=') != std::string::npos &&
          trimmed.back() == ';') {
        const std::size_t equal = trimmed.find('=');
        const std::string name = trim(trimmed.substr(0, equal));
        std::string value = trim(trimmed.substr(equal + 1));
        value.pop_back();
        try {
          current->variants.emplace_back(name, std::stoll(trim(value)));
        } catch (const std::exception&) {
          error = "invalid enum value for '" + name + "'";
          return false;
        }
        continue;
      }
      InterfaceMember member;
      if (trimmed.find('(') != std::string::npos) {
        if (!parseCallable(trimmed, member, error)) {
          return false;
        }
      } else {
        std::string body = trimmed;
        if (!body.empty() && body.back() == ';') {
          body.pop_back();
        }
        const std::size_t splitAt = body.find_last_of(' ');
        if (splitAt == std::string::npos) {
          error = "malformed member: " + trimmed;
          return false;
        }
        member.kind = InterfaceMember::Kind::Field;
        member.type = trim(body.substr(0, splitAt));
        member.name = trim(body.substr(splitAt + 1));
      }
      current->members.push_back(std::move(member));
    }
    if (depth != 0 || current != nullptr) {
      error = "unterminated declaration";
      return false;
    }
    return true;
  }

 private:
  static std::string trim(std::string_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
      ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
      --end;
    }
    return std::string(value.substr(begin, end - begin));
  }

  static bool startsWith(const std::string& value, std::string_view prefix) {
    return value.rfind(prefix, 0) == 0;
  }

  static std::string stripDecorations(const std::string& line) {
    std::string result = line;
    while (!result.empty() && result.front() == '@') {
      const std::size_t space = result.find(' ');
      if (space == std::string::npos) {
        return result;
      }
      result = trim(result.substr(space + 1));
    }
    if (startsWith(result, "export ")) {
      result = result.substr(7);
    }
    return result;
  }

  static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> result;
    for (const std::string& part : split(path, '.')) {
      const std::string item = trim(part);
      if (!item.empty()) {
        result.push_back(item);
      }
    }
    return result;
  }

  static std::vector<std::string> split(const std::string& value, char sep) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
      const std::size_t end = value.find(sep, begin);
      result.push_back(value.substr(begin, end == std::string::npos
                                                 ? std::string::npos
                                                 : end - begin));
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1;
    }
    return result;
  }

  static bool parseCallable(const std::string& line, InterfaceMember& member,
                            std::string& error) {
    const std::size_t open = line.find('(');
    const std::size_t close = line.find(')', open);
    if (open == std::string::npos || close == std::string::npos) {
      error = "malformed callable: " + line;
      return false;
    }
    const std::string before = trim(line.substr(0, open));
    const std::size_t space = before.find_last_of(' ');
    member.name = space == std::string::npos ? before : before.substr(space + 1);
    member.type = space == std::string::npos ? "" : trim(before.substr(0, space));
    if (!member.name.empty() && member.name.front() == '~') {
      member.kind = InterfaceMember::Kind::Destructor;
    } else if (member.type.empty()) {
      member.kind = InterfaceMember::Kind::Constructor;
    } else {
      member.kind = InterfaceMember::Kind::Method;
    }
    const std::string params = line.substr(open + 1, close - open - 1);
    for (const std::string& part : split(params, ',')) {
      const std::string item = trim(part);
      if (item.empty()) {
        continue;
      }
      const std::size_t splitAt = item.find_last_of(' ');
      if (splitAt == std::string::npos) {
        member.params.emplace_back(item, "");
      } else {
        member.params.emplace_back(trim(item.substr(0, splitAt)),
                                   trim(item.substr(splitAt + 1)));
      }
    }
    member.isConst = line.find(" const", close) != std::string::npos;
    return true;
  }
};

}  // namespace coord
