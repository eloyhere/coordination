#include "coord/codegen.h"

#include "coord/sema.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace coord {
namespace {

std::string pascalCase(std::string name) {
  if (!name.empty()) {
    name[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(name[0])));
  }
  return name;
}

const std::set<std::string>& cppKeywords() {
  static const std::set<std::string> keywords = {
      "alignas",    "alignof",   "and",       "and_eq",   "asm",
      "auto",       "bitand",    "bitor",     "bool",     "break",
      "case",       "catch",     "char",      "char16_t", "char32_t",
      "class",      "compl",     "concept",   "const",    "consteval",
      "constexpr",  "constinit", "const_cast","continue", "co_await",
      "co_return",  "co_yield",  "decltype",  "default",  "delete",
      "do",         "double",    "dynamic_cast", "else",  "enum",
      "explicit",   "export",    "extern",    "false",    "float",
      "for",        "friend",    "goto",      "if",       "inline",
      "int",        "long",      "mutable",   "namespace","new",
      "noexcept",   "not",       "not_eq",    "nullptr",  "operator",
      "or",         "or_eq",     "override",  "private",  "protected",
      "public",     "register",  "reinterpret_cast", "requires", "return",
      "short",      "signed",    "sizeof",    "static",   "static_assert",
      "static_cast", "struct",   "switch",    "template", "this",
      "throw",      "true",      "try",       "typedef",  "typeid",
      "typename",   "union",     "unsigned",  "using",    "virtual",
      "void",       "volatile",  "wchar_t",   "while",    "xor",
      "xor_eq"};
  return keywords;
}

std::string smartPointerOpen(bool unique, bool share, bool weak) {
  if (unique) {
    return "std::unique_ptr<";
  }
  if (share) {
    return "std::shared_ptr<";
  }
  if (weak) {
    return "std::weak_ptr<";
  }
  return {};
}

std::string formatCppInteger(std::int64_t value, int radix) {
  if (radix != 2 && radix != 8 && radix != 16) {
    return std::to_string(value);
  }
  const bool negative = value < 0;
  std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(value + 1)) + 1
               : static_cast<std::uint64_t>(value);
  const char* digits = "0123456789ABCDEF";
  std::string body;
  do {
    body.push_back(digits[magnitude % static_cast<std::uint64_t>(radix)]);
    magnitude /= static_cast<std::uint64_t>(radix);
  } while (magnitude != 0);
  std::reverse(body.begin(), body.end());
  std::string result;
  if (negative) {
    result.push_back('-');
  }
  if (radix == 16) {
    result += "0X";
  } else if (radix == 2) {
    result += "0B";
  } else {
    result.push_back('0');
  }
  result += body;
  return result;
}

std::string escapeString(const std::string& text) {
  std::string result;
  for (const char c : text) {
    switch (c) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\r':
        result += "\\r";
        break;
      default:
        result += c;
        break;
    }
  }
  return result;
}

std::string charLiteral(char32_t value) {
  std::ostringstream stream;
  stream << "U'\\x" << std::hex << std::uppercase
         << static_cast<std::uint32_t>(value) << "'";
  return stream.str();
}

}  // namespace

CppEmitter::CppEmitter(const ModuleAst& module,
                       const std::map<std::string, Value>& constantValues,
                       std::ostream& out)
    : module_(module), constantValues_(constantValues), out_(out) {}

void CppEmitter::indent() {
  for (int i = 0; i < depth_; ++i) {
    out_ << "  ";
  }
}

void CppEmitter::line(const std::string& text) {
  indent();
  out_ << text << '\n';
}

std::string CppEmitter::sanitize(const std::string& name) const {
  if (cppKeywords().count(name) != 0) {
    return name + "_";
  }
  return name;
}

const Value* CppEmitter::constant(const std::string& name) const {
  const auto it = constantValues_.find(name);
  return it != constantValues_.end() ? &it->second : nullptr;
}

std::string CppEmitter::literal(const Value& value) const {
  switch (value.kind) {
    case Value::Kind::Void:
      return "void";
    case Value::Kind::Bool:
      return value.boolValue ? "true" : "false";
    case Value::Kind::Int:
      return "static_cast<std::int64_t>(" + std::to_string(value.intValue) + ")";
    case Value::Kind::Decimal: {
      std::ostringstream stream;
      stream << std::setprecision(17) << value.decimalValue;
      return stream.str();
    }
    case Value::Kind::Char:
      return charLiteral(value.charValue);
    case Value::Kind::String:
      return "std::string(\"" + escapeString(value.stringValue) + "\")";
  }
  return "0";
}

std::string CppEmitter::cppTypeName(const Type* type) const {
  if (type == nullptr) {
    return "void";
  }
  if (type->kind == TypeKind::Function) {
    std::string result =
        "std::function<" + cppTypeName(type->returnType) + "(";
    for (std::size_t i = 0; i < type->alternatives.size(); ++i) {
      if (i != 0) {
        result += ", ";
      }
      result += cppTypeName(type->alternatives[i]);
    }
    result += ")>";
    return result;
  }
  if (type->kind == TypeKind::Class || type->kind == TypeKind::Interface ||
      type->kind == TypeKind::Enum) {
    return sanitize(type->name);
  }
  if (type->kind == TypeKind::Queue) {
    return "std::deque<" + cppTypeName(type->pointee) + ">";
  }
  if (type->kind == TypeKind::Array) {
    return "std::array<" + cppTypeName(type->pointee) + ", " +
           std::to_string(type->arrayLength) + ">";
  }
  if (type->kind == TypeKind::ArrayView) {
    return "coord_rt::ArrayView<" + cppTypeName(type->pointee) + ">";
  }
  if (type->kind == TypeKind::Map) {
    return std::string(type->unordered ? "std::unordered_map<"
                                      : "std::map<") +
           cppTypeName(type->alternatives[0]) + ", " +
           cppTypeName(type->alternatives[1]) + ">";
  }
  if (type->kind == TypeKind::Set) {
    return std::string(type->unordered ? "std::unordered_set<"
                                      : "std::set<") +
           cppTypeName(type->pointee) + ">";
  }
  const std::string& name = type->name;
  if (name == "integer") {
    return "std::int64_t";
  }
  if (name == "boolean" || name == "bool") {
    return "bool";
  }
  if (name == "character" || name == "char") {
    return "char32_t";
  }
  if (name == "string") {
    return "std::string";
  }
  if (name == "decimal") {
    return "coord_rt::Decimal";
  }
  if (name == "float") {
    return "double";
  }
  if (name == "void") {
    return "void";
  }
  return sanitize(name);
}

std::string CppEmitter::typeToString(const TypeExpr* type) const {
  if (type == nullptr) {
    return "void";
  }
  switch (type->kind) {
    case TypeExprKind::Named: {
      if (type->resolved != nullptr &&
          type->resolved->kind == TypeKind::Function) {
        return cppTypeName(type->resolved);
      }
      if (type->resolved != nullptr &&
          type->name.find('.') != std::string::npos) {
        return sanitize(type->resolved->name);
      }
      if (typeParams_.count(type->name) != 0) {
        return sanitize(type->name);
      }
      if (type->name == "Array" && type->args.size() == 1) {
        return "std::deque<" + typeToString(type->args[0].get()) + ">";
      }
      if (type->name == "ArrayView" && type->args.size() == 1) {
        return "coord_rt::ArrayView<" + typeToString(type->args[0].get()) +
               ">";
      }
      if (type->name == "Reflector" && type->args.size() == 1) {
        return "coord_rt::Reflector<" +
               typeToString(type->args[0].get()) + ">";
      }
      if ((type->name == "OrderedMap" || type->name == "UnorderedMap") &&
          type->args.size() == 2) {
        return std::string(type->name == "UnorderedMap" ? "std::unordered_map<"
                                                        : "std::map<") +
               typeToString(type->args[0].get()) + ", " +
               typeToString(type->args[1].get()) + ">";
      }
      if ((type->name == "OrderedSet" || type->name == "UnorderedSet") &&
          type->args.size() == 1) {
        return std::string(type->name == "UnorderedSet" ? "std::unordered_set<"
                                                        : "std::set<") +
               typeToString(type->args[0].get()) + ">";
      }
      static const std::map<std::string, std::string> builtins = {
          {"void", "void"},
          {"bool", "bool"},
          {"integer", "std::int64_t"},
          {"i8", "std::int8_t"},
          {"i16", "std::int16_t"},
          {"i32", "std::int32_t"},
          {"i64", "std::int64_t"},
          {"u8", "std::uint8_t"},
          {"u16", "std::uint16_t"},
          {"u32", "std::uint32_t"},
          {"u64", "std::uint64_t"},
          {"isize", "std::ptrdiff_t"},
          {"usize", "std::size_t"},
          {"f32", "float"},
          {"f64", "double"},
          {"decimal", "coord_rt::Decimal"},
          {"float", "double"},
          {"record", "coord_rt::Record"},
          {"Record", "coord_rt::Record"},
          {"Integer", "std::int64_t"},
          {"String", "std::string"},
          {"Character", "char32_t"},
          {"Boolean", "bool"},
          {"Float", "double"},
          {"Decimal", "coord_rt::Decimal"},
          {"integer", "std::int64_t"},
          {"character", "char32_t"},
          {"boolean", "bool"},
          {"Enumeration", "std::int64_t"},
          {"char", "char32_t"},
          {"string", "std::string"},
          {"Description", "std::string"},
      };
      const auto it = builtins.find(type->name);
      if (it != builtins.end()) {
        return it->second;
      }
      return sanitize(type->name);
    }
    case TypeExprKind::Pointer:
      return typeToString(type->inner.get()) + "*";
    case TypeExprKind::Ref:
      return (type->constRef ? "const " : "") +
             typeToString(type->inner.get()) + "&";
    case TypeExprKind::Const:
      return "const " + typeToString(type->inner.get());
    case TypeExprKind::Array: {
      std::string length = "0";
      if (type->arraySize != nullptr &&
          type->arraySize->kind == ExprKind::IntLit) {
        length = std::to_string(type->arraySize->intValue);
      }
      return "std::array<" + typeToString(type->inner.get()) + ", " + length +
             ">";
    }
    case TypeExprKind::Function: {
      std::string parameters;
      for (std::size_t i = 0; i < type->args.size(); ++i) {
        if (i != 0) {
          parameters += ", ";
        }
        parameters += typeToString(type->args[i].get());
      }
      return "std::function<" + typeToString(type->returnType.get()) + "(" +
             parameters + ")>";
    }
    case TypeExprKind::Union:
    case TypeExprKind::Intersect:
      return "void*";
  }
  return "void";
}

void CppEmitter::emitAnnotations(
    const std::vector<AnnotationUse>& annotations) {
  if (annotations.empty()) {
    return;
  }
  out_ << "// annotations:";
  for (const AnnotationUse& use : annotations) {
    out_ << " @" << use.name;
    if (!use.args.empty()) {
      out_ << '(';
      for (std::size_t i = 0; i < use.args.size(); ++i) {
        if (i != 0) {
          out_ << ", ";
        }
        const Expr* arg = use.args[i].get();
        if (arg == nullptr) {
          out_ << "?";
        } else if (arg->kind == ExprKind::Name) {
          out_ << arg->name;
        } else if (arg->kind == ExprKind::StringLit) {
          out_ << '"' << arg->stringValue << '"';
        } else if (arg->kind == ExprKind::IntLit) {
          out_ << arg->intValue;
        } else if (arg->kind == ExprKind::BoolLit) {
          out_ << (arg->boolValue ? "true" : "false");
        } else {
          out_ << "...";
        }
      }
      out_ << ')';
    }
  }
  out_ << '\n';
}

void CppEmitter::emitInheritedAnnotations(const Decl& decl) {
  if (decl.inheritedAnnotationNames.empty()) {
    return;
  }
  out_ << "// inherited annotations:";
  for (const std::string& name : decl.inheritedAnnotationNames) {
    out_ << " @" << name;
  }
  out_ << '\n';
}

void CppEmitter::emitReturnType(const Decl& decl) {
  if (decl.isResult || decl.isExpect) {
    out_ << (decl.isExpect ? "coord_rt::Expect<" : "coord_rt::Result<");
  }
  const bool smart = decl.isUnique || decl.isShare || decl.isWeak;
  if (smart) {
    out_ << smartPointerOpen(decl.isUnique, decl.isShare, decl.isWeak);
  }
  emitStorageType(decl.type.get(), decl.isUnsigned, decl.length);
  if (smart) {
    out_ << '>';
  }
  if (decl.isResult || decl.isExpect) {
    out_ << '>';
  }
}

void CppEmitter::emitStorageType(const TypeExpr* type, bool isUnsigned,
                                 int length) {
  if (type == nullptr || type->resolved == nullptr) {
    emitType(type);
    return;
  }
  const std::string& name = type->resolved->name;
  if (name == "integer") {
    const int bits = length != 0 ? length : 64;
    if (bits == 128) {
      out_ << (isUnsigned ? "coord_rt::UInt128" : "coord_rt::Int128");
      return;
    }
    out_ << (isUnsigned ? "std::uint" : "std::int") << bits << "_t";
    return;
  }
  if (name == "character") {
    const int bits = length != 0 ? length : 32;
    if (bits == 8) {
      out_ << "char8_t";
    } else if (bits == 16) {
      out_ << "char16_t";
    } else {
      out_ << "char32_t";
    }
    return;
  }
  if (name == "decimal") {
    if (length == 8 || length == 16 || length == 32 || length == 64) {
      out_ << "coord_rt::DecimalT<std::int" << length << "_t>";
    } else {
      out_ << "coord_rt::Decimal";
    }
    return;
  }
  if (name == "float") {
    out_ << "double";
    return;
  }
  if (name == "boolean") {
    out_ << "bool";
    return;
  }
  emitType(type);
}

void CppEmitter::emitType(const TypeExpr* type) { out_ << typeToString(type); }

void CppEmitter::emitPrelude() {
  out_ << "// Generated by coorc from a .coo module. Do not edit.\n";
  out_ << "// RAII: members are destroyed by C++ in reverse declaration order;\n";
  out_ << "//       ~this.member; declarations are erased to avoid a double\n";
  out_ << "//       destruction.\n\n";
  out_ << "#include <algorithm>\n";
  out_ << "#include <array>\n";
  out_ << "#include <cstddef>\n";
  out_ << "#include <cstdlib>\n";
  out_ << "#include <deque>\n";
  out_ << "#include <filesystem>\n";
  out_ << "#include <map>\n";
  out_ << "#include <memory>\n";
  out_ << "#include <set>\n";
  out_ << "#include <unordered_map>\n";
  out_ << "#include <unordered_set>\n";
  out_ << "#include <utility>\n";
  out_ << "#include <cstdint>\n";
  out_ << "#include <iostream>\n";
  out_ << "#include <string>\n";
  out_ << "#include <cstring>\n";
  out_ << "#include <type_traits>\n";
  out_ << "#include <limits>\n";
  out_ << "#include <vector>\n";
  out_ << "#include <functional>\n";
  out_ << "#include <fstream>\n";
  out_ << "#include <chrono>\n";
  out_ << "#include <thread>\n";
  out_ << "#include <cstdio>\n";
  out_ << "#include <utility>\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "#ifndef WIN32_LEAN_AND_MEAN\n";
  out_ << "#define WIN32_LEAN_AND_MEAN\n";
  out_ << "#endif\n";
  out_ << "#ifndef NOMINMAX\n";
  out_ << "#define NOMINMAX\n";
  out_ << "#endif\n";
  out_ << "#include <windows.h>\n";
  out_ << "#else\n";
  out_ << "#include <unistd.h>\n";
  out_ << "#endif\n\n";
  out_ << "namespace coord_rt {\n";
  out_ << "template <class T>\n";
  out_ << "T& proxyHandler() {\n";
  out_ << "  static T instance;\n";
  out_ << "  return instance;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "std::map<const void*, T>& proxyHandlers() {\n";
  out_ << "  static std::map<const void*, T> instances;\n";
  out_ << "  return instances;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T& proxyHandler(const void* object) {\n";
  out_ << "  return proxyHandlers<T>()[object];\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "void proxyRelease(const void* object) {\n";
  out_ << "  proxyHandlers<T>().erase(object);\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "struct Reflector {};\n";
  out_ << "}  // namespace coord_rt\n";
  out_ << "namespace coord_meta {\n";
  out_ << "bool hasAnnotation(const std::string& declaration,\n";
  out_ << "                   const std::string& annotation);\n";
  out_ << "}  // namespace coord_meta\n";
  out_ << "namespace coord_rt {\n";
  out_ << "struct UInt128 {\n";
  out_ << "  std::uint64_t lo = 0;\n";
  out_ << "  std::uint64_t hi = 0;\n";
  out_ << "  UInt128() = default;\n";
  out_ << "  template <class T,\n";
  out_ << "            class = std::enable_if_t<std::is_integral_v<T>>>\n";
  out_ << "  UInt128(T value) : lo(static_cast<std::uint64_t>(value)) {}\n";
  out_ << "};\n";
  out_ << "using Int128 = UInt128;\n";
  out_ << "inline UInt128 operator+(const UInt128& a, const UInt128& b) {\n";
  out_ << "  UInt128 r;\n";
  out_ << "  r.lo = a.lo + b.lo;\n";
  out_ << "  r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);\n";
  out_ << "  return r;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator-(const UInt128& a, const UInt128& b) {\n";
  out_ << "  UInt128 r;\n";
  out_ << "  r.lo = a.lo - b.lo;\n";
  out_ << "  r.hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0);\n";
  out_ << "  return r;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator*(const UInt128& a, const UInt128& b) {\n";
  out_ << "  const std::uint64_t a0 = a.lo & 0xffffffffULL;\n";
  out_ << "  const std::uint64_t a1 = a.lo >> 32;\n";
  out_ << "  const std::uint64_t b0 = b.lo & 0xffffffffULL;\n";
  out_ << "  const std::uint64_t b1 = b.lo >> 32;\n";
  out_ << "  const std::uint64_t p00 = a0 * b0;\n";
  out_ << "  const std::uint64_t p01 = a0 * b1;\n";
  out_ << "  const std::uint64_t p10 = a1 * b0;\n";
  out_ << "  const std::uint64_t p11 = a1 * b1;\n";
  out_ << "  const std::uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffULL) +\n";
  out_ << "                            (p10 & 0xffffffffULL);\n";
  out_ << "  UInt128 r;\n";
  out_ << "  r.lo = (p00 & 0xffffffffULL) | (mid << 32);\n";
  out_ << "  r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32) +\n";
  out_ << "         a.hi * b.lo + a.lo * b.hi;\n";
  out_ << "  return r;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator/(const UInt128& a, const UInt128& b);\n";
  out_ << "inline UInt128 operator%(const UInt128& a, const UInt128& b);\n";
  out_ << "inline bool operator<(const UInt128& a, const UInt128& b);\n";
  out_ << "inline UInt128 operator/(const UInt128& a, const UInt128& b) {\n";
  out_ << "  UInt128 quotient;\n";
  out_ << "  UInt128 remainder;\n";
  out_ << "  for (int bit = 127; bit >= 0; --bit) {\n";
  out_ << "    remainder = remainder + remainder;\n";
  out_ << "    const std::uint64_t next =\n";
  out_ << "        bit < 64 ? ((a.lo >> bit) & 1ULL)\n";
  out_ << "                 : ((a.hi >> (bit - 64)) & 1ULL);\n";
  out_ << "    if (next != 0) remainder = remainder + UInt128(1);\n";
  out_ << "    if (!(remainder < b)) {\n";
  out_ << "      remainder = remainder - b;\n";
  out_ << "      if (bit < 64) quotient.lo |= (1ULL << bit);\n";
  out_ << "      else quotient.hi |= (1ULL << (bit - 64));\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return quotient;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator%(const UInt128& a, const UInt128& b) {\n";
  out_ << "  UInt128 remainder;\n";
  out_ << "  for (int bit = 127; bit >= 0; --bit) {\n";
  out_ << "    remainder = remainder + remainder;\n";
  out_ << "    const std::uint64_t next =\n";
  out_ << "        bit < 64 ? ((a.lo >> bit) & 1ULL)\n";
  out_ << "                 : ((a.hi >> (bit - 64)) & 1ULL);\n";
  out_ << "    if (next != 0) remainder = remainder + UInt128(1);\n";
  out_ << "    if (!(remainder < b)) remainder = remainder - b;\n";
  out_ << "  }\n";
  out_ << "  return remainder;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator<<(const UInt128& value, int amount) {\n";
  out_ << "  if (amount <= 0) return value;\n";
  out_ << "  if (amount >= 128) return UInt128{};\n";
  out_ << "  UInt128 result;\n";
  out_ << "  if (amount < 64) {\n";
  out_ << "    result.lo = value.lo << amount;\n";
  out_ << "    result.hi = (value.hi << amount) |\n";
  out_ << "                (value.lo >> (64 - amount));\n";
  out_ << "  } else {\n";
  out_ << "    result.lo = 0;\n";
  out_ << "    result.hi = value.lo << (amount - 64);\n";
  out_ << "  }\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "inline UInt128 operator>>(const UInt128& value, int amount) {\n";
  out_ << "  if (amount <= 0) return value;\n";
  out_ << "  if (amount >= 128) return UInt128{};\n";
  out_ << "  UInt128 result;\n";
  out_ << "  if (amount < 64) {\n";
  out_ << "    result.lo = (value.lo >> amount) |\n";
  out_ << "                (value.hi << (64 - amount));\n";
  out_ << "    result.hi = value.hi >> amount;\n";
  out_ << "  } else {\n";
  out_ << "    result.lo = value.hi >> (amount - 64);\n";
  out_ << "    result.hi = 0;\n";
  out_ << "  }\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "inline bool operator==(const UInt128& a, const UInt128& b) {\n";
  out_ << "  return a.lo == b.lo && a.hi == b.hi;\n";
  out_ << "}\n";
  out_ << "inline bool operator!=(const UInt128& a, const UInt128& b) {\n";
  out_ << "  return !(a == b);\n";
  out_ << "}\n";
  out_ << "inline bool operator<(const UInt128& a, const UInt128& b) {\n";
  out_ << "  return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);\n";
  out_ << "}\n";
  out_ << "inline std::uint32_t divMod10(UInt128& value) {\n";
  out_ << "  std::uint32_t words[4];\n";
  out_ << "  std::uint64_t remainder = 0;\n";
  out_ << "  for (int i = 3; i >= 0; --i) {\n";
  out_ << "    const std::uint64_t word =\n";
  out_ << "        i >= 2 ? ((value.hi >> ((i - 2) * 32)) & 0xffffffffULL)\n";
  out_ << "               : ((value.lo >> (i * 32)) & 0xffffffffULL);\n";
  out_ << "    const std::uint64_t current = (remainder << 32) | word;\n";
  out_ << "    words[i] = static_cast<std::uint32_t>(current / 10);\n";
  out_ << "    remainder = current % 10;\n";
  out_ << "  }\n";
  out_ << "  value.lo = (static_cast<std::uint64_t>(words[1]) << 32) |\n";
  out_ << "             words[0];\n";
  out_ << "  value.hi = (static_cast<std::uint64_t>(words[3]) << 32) |\n";
  out_ << "             words[2];\n";
  out_ << "  return static_cast<std::uint32_t>(remainder);\n";
  out_ << "}\n";
  out_ << "inline std::string toString(const UInt128& value) {\n";
  out_ << "  if (value.lo == 0 && value.hi == 0) return \"0\";\n";
  out_ << "  UInt128 copy = value;\n";
  out_ << "  std::string digits;\n";
  out_ << "  while (copy.lo != 0 || copy.hi != 0) {\n";
  out_ << "    digits.push_back(static_cast<char>('0' + divMod10(copy)));\n";
  out_ << "  }\n";
  out_ << "  return std::string(digits.rbegin(), digits.rend());\n";
  out_ << "}\n";
  out_ << "inline std::ostream& operator<<(std::ostream& os,\n";
  out_ << "                                 const UInt128& value) {\n";
  out_ << "  return os << toString(value);\n";
  out_ << "}\n";
  out_ << "inline void print(const std::string& value) {\n";
  out_ << "  std::cout << value << '\\n';\n";
  out_ << "}\n";
  out_ << "inline void print(bool value) {\n";
  out_ << "  std::cout << (value ? \"true\" : \"false\") << '\\n';\n";
  out_ << "}\n";
  out_ << "inline void print(char32_t value) {\n";
  out_ << "  std::cout << static_cast<std::uint32_t>(value) << '\\n';\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "void print(const T& value) {\n";
  out_ << "  if constexpr (std::is_enum_v<T>) {\n";
  out_ << "    std::cout << static_cast<std::int64_t>(value) << '\\n';\n";
  out_ << "  } else if constexpr (std::is_integral_v<T> &&\n";
  out_ << "                     sizeof(T) < sizeof(int)) {\n";
  out_ << "    std::cout << static_cast<long long>(value) << '\\n';\n";
  out_ << "  } else {\n";
  out_ << "    std::cout << value << '\\n';\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T& optionalGet(T* pointer) {\n";
  out_ << "  if (pointer == nullptr) std::abort();\n";
  out_ << "  return *pointer;\n";
  out_ << "}\n";
  out_ << "template <class P>\n";
  out_ << "auto optionalAbort(P&& pointer, const char* message)\n";
  out_ << "    -> decltype(*pointer) {\n";
  out_ << "  if (!pointer) {\n";
  out_ << "    std::cerr << message << '\\n';\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "  return *pointer;\n";
  out_ << "}\n";
  out_ << "template <class P>\n";
  out_ << "void optionalCheck(P&& pointer, const char* message) {\n";
  out_ << "  if (!pointer) {\n";
  out_ << "    std::cerr << message << '\\n';\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T checkedNeg(T value, const char* message) {\n";
  out_ << "  if constexpr (std::is_signed_v<T>) {\n";
  out_ << "    if (value == std::numeric_limits<T>::min()) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return static_cast<T>(-value);\n";
  out_ << "}\n";
  out_ << "template <class A, class B,\n";
  out_ << "          class = std::enable_if_t<\n";
  out_ << "              !std::is_same_v<std::decay_t<A>, UInt128> &&\n";
  out_ << "              !std::is_same_v<std::decay_t<B>, UInt128>>>\n";
  out_ << "auto checkedAdd(A a, B b, const char* message)\n";
  out_ << "    -> std::common_type_t<A, B> {\n";
  out_ << "  using R = std::common_type_t<A, B>;\n";
  out_ << "  const R x = static_cast<R>(a);\n";
  out_ << "  const R y = static_cast<R>(b);\n";
  out_ << "  if constexpr (std::is_unsigned_v<R>) {\n";
  out_ << "    if (y > std::numeric_limits<R>::max() - x) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  } else {\n";
  out_ << "    if (y > 0 && x > std::numeric_limits<R>::max() - y) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "    if (y < 0 && x < std::numeric_limits<R>::min() - y) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return static_cast<R>(x + y);\n";
  out_ << "}\n";
  out_ << "template <class A, class B,\n";
  out_ << "          class = std::enable_if_t<\n";
  out_ << "              !std::is_same_v<std::decay_t<A>, UInt128> &&\n";
  out_ << "              !std::is_same_v<std::decay_t<B>, UInt128>>>\n";
  out_ << "auto checkedSub(A a, B b, const char* message)\n";
  out_ << "    -> std::common_type_t<A, B> {\n";
  out_ << "  using R = std::common_type_t<A, B>;\n";
  out_ << "  const R x = static_cast<R>(a);\n";
  out_ << "  const R y = static_cast<R>(b);\n";
  out_ << "  if constexpr (std::is_unsigned_v<R>) {\n";
  out_ << "    if (y > x) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  } else {\n";
  out_ << "    if (y > 0 && x < std::numeric_limits<R>::min() + y) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "    if (y < 0 && x > std::numeric_limits<R>::max() + y) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return static_cast<R>(x - y);\n";
  out_ << "}\n";
  out_ << "template <class A, class B,\n";
  out_ << "          class = std::enable_if_t<\n";
  out_ << "              !std::is_same_v<std::decay_t<A>, UInt128> &&\n";
  out_ << "              !std::is_same_v<std::decay_t<B>, UInt128>>>\n";
  out_ << "auto checkedMul(A a, B b, const char* message)\n";
  out_ << "    -> std::common_type_t<A, B> {\n";
  out_ << "  using R = std::common_type_t<A, B>;\n";
  out_ << "  const R x = static_cast<R>(a);\n";
  out_ << "  const R y = static_cast<R>(b);\n";
  out_ << "  if constexpr (std::is_unsigned_v<R>) {\n";
  out_ << "    if (x != 0 && y > std::numeric_limits<R>::max() / x) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  } else {\n";
  out_ << "    if (x != 0 && y != 0) {\n";
  out_ << "      if ((x == -1 && y == std::numeric_limits<R>::min()) ||\n";
  out_ << "          (y == -1 && x == std::numeric_limits<R>::min())) {\n";
  out_ << "        std::cerr << message << '\\n'; std::abort();\n";
  out_ << "      }\n";
  out_ << "      if (x > 0) {\n";
  out_ << "        if (y > 0 ? x > std::numeric_limits<R>::max() / y\n";
  out_ << "                  : y < std::numeric_limits<R>::min() / x) {\n";
  out_ << "          std::cerr << message << '\\n'; std::abort();\n";
  out_ << "        }\n";
  out_ << "      } else {\n";
  out_ << "        if (y > 0 ? x < std::numeric_limits<R>::min() / y\n";
  out_ << "                  : y < std::numeric_limits<R>::max() / x) {\n";
  out_ << "          std::cerr << message << '\\n'; std::abort();\n";
  out_ << "        }\n";
  out_ << "      }\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return static_cast<R>(x * y);\n";
  out_ << "}\n";
  out_ << "inline UInt128 checkedAdd(UInt128 a, UInt128 b, const char* message) {\n";
  out_ << "  const UInt128 result = a + b;\n";
  out_ << "  if (result < a) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "inline UInt128 checkedSub(UInt128 a, UInt128 b, const char* message) {\n";
  out_ << "  if (a < b) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return a - b;\n";
  out_ << "}\n";
  out_ << "inline UInt128 checkedMul(UInt128 a, UInt128 b, const char* message) {\n";
  out_ << "  const UInt128 result = a * b;\n";
  out_ << "  if (!(a == UInt128(0)) && !((result / a) == b)) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "template <class A, class B>\n";
  out_ << "auto checkedDiv(A a, B b, const char* message) -> decltype(a / b) {\n";
  out_ << "  if (b == 0) { std::cerr << message << '\\n'; std::abort(); }\n";
  out_ << "  if constexpr (std::is_signed_v<A> && std::is_signed_v<B>) {\n";
  out_ << "    if (a == std::numeric_limits<A>::min() && b == -1) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return a / b;\n";
  out_ << "}\n";
  out_ << "template <class A, class B>\n";
  out_ << "auto checkedMod(A a, B b, const char* message) -> decltype(a % b) {\n";
  out_ << "  if (b == 0) { std::cerr << message << '\\n'; std::abort(); }\n";
  out_ << "  if constexpr (std::is_signed_v<A> && std::is_signed_v<B>) {\n";
  out_ << "    if (a == std::numeric_limits<A>::min() && b == -1) return 0;\n";
  out_ << "  }\n";
  out_ << "  return a % b;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T& checkedInc(T& value, int delta, const char* message) {\n";
  out_ << "  value = checkedAdd(value, delta, message);\n";
  out_ << "  return value;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T checkedPostInc(T& value, int delta, const char* message) {\n";
  out_ << "  T previous = value;\n";
  out_ << "  value = checkedAdd(value, delta, message);\n";
  out_ << "  return previous;\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T& checkedAddAssign(T& target, U value, const char* message) {\n";
  out_ << "  target = checkedAdd(target, value, message);\n";
  out_ << "  return target;\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T& checkedSubAssign(T& target, U value, const char* message) {\n";
  out_ << "  target = checkedSub(target, value, message);\n";
  out_ << "  return target;\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T& checkedMulAssign(T& target, U value, const char* message) {\n";
  out_ << "  target = checkedMul(target, value, message);\n";
  out_ << "  return target;\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T& checkedDivAssign(T& target, U value, const char* message) {\n";
  out_ << "  target = checkedDiv(target, value, message);\n";
  out_ << "  return target;\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T& checkedModAssign(T& target, U value, const char* message) {\n";
  out_ << "  target = checkedMod(target, value, message);\n";
  out_ << "  return target;\n";
  out_ << "}\n";
  out_ << "inline UInt128 checkedShl(UInt128 value, std::int64_t amount,\n";
  out_ << "                           const char* message) {\n";
  out_ << "  if (amount < 0 || amount >= 128) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  const UInt128 shifted = value << static_cast<int>(amount);\n";
  out_ << "  if (!((shifted >> static_cast<int>(amount)) == value)) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return shifted;\n";
  out_ << "}\n";
  out_ << "inline UInt128 checkedShr(UInt128 value, std::int64_t amount,\n";
  out_ << "                           const char* message) {\n";
  out_ << "  if (amount < 0 || amount >= 128) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return value >> static_cast<int>(amount);\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T checkedShl(T value, std::int64_t amount, const char* message) {\n";
  out_ << "  if (amount < 0 || amount >= std::numeric_limits<T>::digits) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  if constexpr (std::is_signed_v<T>) {\n";
  out_ << "    if (value < 0) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  using U = std::make_unsigned_t<T>;\n";
  out_ << "  const U original = static_cast<U>(value);\n";
  out_ << "  const U shifted = static_cast<U>(original << amount);\n";
  out_ << "  if (static_cast<U>(shifted >> amount) != original) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  if constexpr (std::is_signed_v<T>) {\n";
  out_ << "    if (shifted > static_cast<U>(std::numeric_limits<T>::max())) {\n";
  out_ << "      std::cerr << message << '\\n'; std::abort();\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  return static_cast<T>(shifted);\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T checkedShr(T value, std::int64_t amount, const char* message) {\n";
  out_ << "  if (amount < 0 || amount >= std::numeric_limits<T>::digits) {\n";
  out_ << "    std::cerr << message << '\\n'; std::abort();\n";
  out_ << "  }\n";
  out_ << "  return static_cast<T>(value >> amount);\n";
  out_ << "}\n";
  out_ << "struct ShareRegistryEntry {\n";
  out_ << "  std::weak_ptr<void> weak;\n";
  out_ << "};\n";
  out_ << "inline std::vector<ShareRegistryEntry>& shareRegistry() {\n";
  out_ << "  static std::vector<ShareRegistryEntry> registry;\n";
  out_ << "  return registry;\n";
  out_ << "}\n";
  out_ << "inline std::unordered_map<void*, std::vector<void*>>& shareEdges() {\n";
  out_ << "  static std::unordered_map<void*, std::vector<void*>> edges;\n";
  out_ << "  return edges;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "void* shareObjectAddress(T& value) { return &value; }\n";
  out_ << "template <class T>\n";
  out_ << "void* shareObjectAddress(std::shared_ptr<T>& value) {\n";
  out_ << "  return value.get();\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "void* shareObjectAddress(const std::shared_ptr<T>& value) {\n";
  out_ << "  return value.get();\n";
  out_ << "}\n";
  out_ << "inline void recordShareEdge(void* from, void* to) {\n";
  out_ << "  if (from == nullptr) return;\n";
  out_ << "  if (to == nullptr) {\n";
  out_ << "    shareEdges().erase(from);\n";
  out_ << "    return;\n";
  out_ << "  }\n";
  out_ << "  auto& edges = shareEdges()[from];\n";
  out_ << "  if (std::find(edges.begin(), edges.end(), to) == edges.end()) {\n";
  out_ << "    edges.push_back(to);\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "template <class T, class... Args>\n";
  out_ << "std::shared_ptr<T> makeShared(Args&&... args) {\n";
  out_ << "  auto value = std::make_shared<T>(std::forward<Args>(args)...);\n";
  out_ << "  shareRegistry().push_back(ShareRegistryEntry{value});\n";
  out_ << "  return value;\n";
  out_ << "}\n";
  out_ << "inline void reportShareLeaks() {\n";
  out_ << "  std::unordered_map<void*, std::weak_ptr<void>> live;\n";
  out_ << "  for (const auto& entry : shareRegistry()) {\n";
  out_ << "    if (auto locked = entry.weak.lock()) {\n";
  out_ << "      live[locked.get()] = entry.weak;\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  std::unordered_map<void*, int> color;\n";
  out_ << "  bool cycle = false;\n";
  out_ << "  std::function<void(void*)> visit = [&](void* node) {\n";
  out_ << "    color[node] = 1;\n";
  out_ << "    const auto found = shareEdges().find(node);\n";
  out_ << "    if (found != shareEdges().end()) {\n";
  out_ << "      for (void* next : found->second) {\n";
  out_ << "        if (live.find(next) == live.end()) continue;\n";
  out_ << "        const int state = color[next];\n";
  out_ << "        if (state == 1) cycle = true;\n";
  out_ << "        else if (state == 0) visit(next);\n";
  out_ << "      }\n";
  out_ << "    }\n";
  out_ << "    color[node] = 2;\n";
  out_ << "  };\n";
  out_ << "  for (const auto& entry : live) {\n";
  out_ << "    if (color[entry.first] == 0) visit(entry.first);\n";
  out_ << "  }\n";
  out_ << "  if (cycle) {\n";
  out_ << "    std::cerr << \"W0005: reference cycle detected among @Share \"\n";
  out_ << "                 \"objects; use @Weak to break it\\n\";\n";
  out_ << "  } else if (!live.empty()) {\n";
  out_ << "    std::cerr << \"W0005: \" << live.size()\n";
  out_ << "              << \" @Share object(s) still alive at shutdown (possible \"\n";
  out_ << "                 \"leak; consider @Weak)\\n\";\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "inline void check(bool condition) {\n";
  out_ << "  if (!condition) {\n";
  out_ << "    std::cerr << \"assertion failed\\n\";\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "template <class A, class B>\n";
  out_ << "void checkEq(const A& left, const B& right) {\n";
  out_ << "  if (!(left == right)) {\n";
  out_ << "    std::cerr << \"assertion failed: values differ\\n\";\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "}\n";
  out_ << "template <class T, class U>\n";
  out_ << "T optionalOrElse(T* pointer, U fallback) {\n";
  out_ << "  return pointer != nullptr ? *pointer : static_cast<T>(fallback);\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T queuePopFront(std::deque<T>& queue) {\n";
  out_ << "  T value = std::move(queue.front());\n";
  out_ << "  queue.pop_front();\n";
  out_ << "  return value;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "T queuePopBack(std::deque<T>& queue) {\n";
  out_ << "  T value = std::move(queue.back());\n";
  out_ << "  queue.pop_back();\n";
  out_ << "  return value;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "struct ArrayView {\n";
  out_ << "  const std::deque<T>* queue = nullptr;\n";
  out_ << "  std::size_t size = 0;\n";
  out_ << "  const T& operator[](std::size_t index) const {\n";
  out_ << "    return (*queue)[index];\n";
  out_ << "  }\n";
  out_ << "};\n";
  out_ << "template <class T>\n";
  out_ << "ArrayView<T> makeArrayView(const std::deque<T>& queue) {\n";
  out_ << "  return ArrayView<T>{&queue, queue.size()};\n";
  out_ << "}\n";
  out_ << "template <class C>\n";
  out_ << "auto checkedIndex(C& container, std::int64_t index, const char* message)\n";
  out_ << "    -> decltype(container[static_cast<std::size_t>(0)]) {\n";
  out_ << "  if (index < 0 || static_cast<std::size_t>(index) >= "
           "container.size()) {\n";
  out_ << "    std::cerr << message << '\\n';\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "  return container[static_cast<std::size_t>(index)];\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "const T& checkedIndexView(ArrayView<T> view, std::int64_t index,\n";
  out_ << "                           const char* message) {\n";
  out_ << "  if (index < 0 || static_cast<std::size_t>(index) >= view.size) {\n";
  out_ << "    std::cerr << message << '\\n';\n";
  out_ << "    std::abort();\n";
  out_ << "  }\n";
  out_ << "  return view[static_cast<std::size_t>(index)];\n";
  out_ << "}\n";
  out_ << "template <class C>\n";
  out_ << "auto arrayGet(C& container, std::int64_t index)\n";
  out_ << "    -> decltype(&container[0]) {\n";
  out_ << "  if (index < 0 ||\n";
  out_ << "      static_cast<std::size_t>(index) >= container.size()) {\n";
  out_ << "    return nullptr;\n";
  out_ << "  }\n";
  out_ << "  return &container[static_cast<std::size_t>(index)];\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "const T* arrayViewGet(const ArrayView<T>& view, std::int64_t index) {\n";
  out_ << "  if (index < 0 ||\n";
  out_ << "      static_cast<std::size_t>(index) >= view.size) {\n";
  out_ << "    return nullptr;\n";
  out_ << "  }\n";
  out_ << "  return &view[static_cast<std::size_t>(index)];\n";
  out_ << "}\n";
  out_ << "template <class Map, class Key>\n";
  out_ << "auto mapFind(Map& map, const Key& key)\n";
  out_ << "    -> decltype(&map.begin()->second) {\n";
  out_ << "  auto it = map.find(key);\n";
  out_ << "  return it == map.end() ? nullptr : &it->second;\n";
  out_ << "}\n";
  out_ << "template <class T>\n";
  out_ << "struct DecimalT {\n";
  out_ << "  T value{};\n";
  out_ << "  T scale{};\n";
  out_ << "  DecimalT() = default;\n";
  out_ << "  DecimalT(std::int64_t v) : value(static_cast<T>(v)), scale(0) {}\n";
  out_ << "  DecimalT(T v, T s) : value(v), scale(s) {}\n";
  out_ << "  template <class U>\n";
  out_ << "  DecimalT(const DecimalT<U>& other)\n";
  out_ << "      : value(static_cast<T>(other.value)),\n";
  out_ << "        scale(static_cast<T>(other.scale)) {}\n";
  out_ << "  DecimalT(double v) {\n";
  out_ << "    scale = 6;\n";
  out_ << "    value = static_cast<T>(v * 1000000.0);\n";
  out_ << "  }\n";
  out_ << "  operator double() const {\n";
  out_ << "    double divisor = 1.0;\n";
  out_ << "    for (std::int64_t i = 0; i < scale; ++i) divisor *= 10.0;\n";
  out_ << "    return static_cast<double>(value) / divisor;\n";
  out_ << "  }\n";
  out_ << "};\n";
  out_ << "using Decimal = DecimalT<std::int64_t>;\n";
  out_ << "struct Record {};\n";
  out_ << "inline Decimal decimalFromDouble(double v) {\n";
  out_ << "  return Decimal(static_cast<std::int64_t>(v * 1000000.0), 6);\n";
  out_ << "}\n";
  out_ << "inline void print(const Decimal& value) {\n";
  out_ << "  std::int64_t scale = value.scale;\n";
  out_ << "  std::int64_t absolute = value.value < 0 ? -value.value : value.value;\n";
  out_ << "  std::string digits = std::to_string(absolute);\n";
  out_ << "  if (scale <= 0) { std::cout << value.value << '\\n'; return; }\n";
  out_ << "  while (static_cast<std::int64_t>(digits.size()) <= scale) {\n";
  out_ << "    digits.insert(digits.begin(), '0');\n";
  out_ << "  }\n";
  out_ << "  std::string whole = digits.substr(0, digits.size() - scale);\n";
  out_ << "  std::string fraction = digits.substr(digits.size() - scale);\n";
  out_ << "  while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();\n";
  out_ << "  if (value.value < 0) std::cout << '-';\n";
  out_ << "  std::cout << whole;\n";
  out_ << "  if (!fraction.empty()) std::cout << '.' << fraction;\n";
  out_ << "  std::cout << '\\n';\n";
  out_ << "}\n";
  out_ << "inline std::int64_t decimalPow10(std::int64_t n) {\n";
  out_ << "  std::int64_t result = 1;\n";
  out_ << "  for (std::int64_t i = 0; i < n; ++i) result *= 10;\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "inline void decimalAlign(const Decimal& a, const Decimal& b,\n";
  out_ << "                         std::int64_t& av, std::int64_t& bv,\n";
  out_ << "                         std::int64_t& scale) {\n";
  out_ << "  scale = a.scale > b.scale ? a.scale : b.scale;\n";
  out_ << "  av = a.value * decimalPow10(scale - a.scale);\n";
  out_ << "  bv = b.value * decimalPow10(scale - b.scale);\n";
  out_ << "}\n";
  out_ << "inline Decimal operator+(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{av + bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator-(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{av - bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator*(const Decimal& a, const Decimal& b) {\n";
  out_ << "  return Decimal{a.value * b.value, a.scale + b.scale};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator/(const Decimal& a, const Decimal& b) {\n";
  out_ << "  if (b.value == 0) return Decimal{0, a.scale};\n";
  out_ << "  return Decimal{(a.value * decimalPow10(b.scale)) / b.value,\n";
  out_ << "                 a.scale};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator%(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{bv == 0 ? 0 : av % bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator&(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{av & bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator|(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{av | bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator^(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return Decimal{av ^ bv, s};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator~(const Decimal& a) {\n";
  out_ << "  return Decimal{~a.value, a.scale};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator<<(const Decimal& a, std::int64_t n) {\n";
  out_ << "  return Decimal{a.value << n, a.scale};\n";
  out_ << "}\n";
  out_ << "inline Decimal operator>>(const Decimal& a, std::int64_t n) {\n";
  out_ << "  return Decimal{a.value >> n, a.scale};\n";
  out_ << "}\n";
  out_ << "inline bool operator==(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return av == bv;\n";
  out_ << "}\n";
  out_ << "inline bool operator!=(const Decimal& a, const Decimal& b) { return !(a == b); }\n";
  out_ << "inline bool operator<(const Decimal& a, const Decimal& b) {\n";
  out_ << "  std::int64_t av, bv, s; decimalAlign(a, b, av, bv, s);\n";
  out_ << "  return av < bv;\n";
  out_ << "}\n";
  out_ << "inline bool operator<=(const Decimal& a, const Decimal& b) { return a < b || a == b; }\n";
  out_ << "inline bool operator>(const Decimal& a, const Decimal& b) { return b < a; }\n";
  out_ << "inline bool operator>=(const Decimal& a, const Decimal& b) { return !(a < b); }\n";
  out_ << "inline double operator+(double a, const Decimal& b) { return a + static_cast<double>(b); }\n";
  out_ << "inline double operator+(const Decimal& a, double b) { return static_cast<double>(a) + b; }\n";
  out_ << "inline double operator-(double a, const Decimal& b) { return a - static_cast<double>(b); }\n";
  out_ << "inline double operator-(const Decimal& a, double b) { return static_cast<double>(a) - b; }\n";
  out_ << "inline double operator*(double a, const Decimal& b) { return a * static_cast<double>(b); }\n";
  out_ << "inline double operator*(const Decimal& a, double b) { return static_cast<double>(a) * b; }\n";
  out_ << "inline double operator/(double a, const Decimal& b) { return a / static_cast<double>(b); }\n";
  out_ << "inline double operator/(const Decimal& a, double b) { return static_cast<double>(a) / b; }\n";
  out_ << "inline bool operator==(double a, const Decimal& b) { return a == static_cast<double>(b); }\n";
  out_ << "inline bool operator<(double a, const Decimal& b) { return a < static_cast<double>(b); }\n";
  out_ << "inline bool operator<(const Decimal& a, double b) { return static_cast<double>(a) < b; }\n";
  out_ << "template <class T>\n";
  out_ << "struct Result {\n";
  out_ << "  bool ok;\n";
  out_ << "  T value;\n";
  out_ << "  std::string error;\n";
  out_ << "  static Result success(const T& v) { return Result{true, v, {}}; }\n";
  out_ << "  static Result failure(std::string message) {\n";
  out_ << "    return Result{false, T{}, std::move(message)};\n";
  out_ << "  }\n";
  out_ << "  bool isOk() const { return ok; }\n";
  out_ << "  bool isErr() const { return !ok; }\n";
  out_ << "  T& unwrap() {\n";
  out_ << "    if (!ok) std::abort();\n";
  out_ << "    return value;\n";
  out_ << "  }\n";
  out_ << "  const T& unwrap() const {\n";
  out_ << "    if (!ok) std::abort();\n";
  out_ << "    return value;\n";
  out_ << "  }\n";
  out_ << "  T& expect(const std::string& message) {\n";
  out_ << "    if (!ok) { std::cerr << message << '\\n'; std::abort(); }\n";
  out_ << "    return value;\n";
  out_ << "  }\n";
  out_ << "};\n";
  out_ << "template <class T>\n";
  out_ << "using Expect = Result<T>;\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "inline std::wstring utf8ToWide(const std::string& text) {\n";
  out_ << "  if (text.empty()) return std::wstring();\n";
  out_ << "  const int size = ::MultiByteToWideChar(\n";
  out_ << "      CP_UTF8, 0, text.data(), static_cast<int>(text.size()),\n";
  out_ << "      nullptr, 0);\n";
  out_ << "  if (size <= 0) return std::wstring();\n";
  out_ << "  std::wstring result(static_cast<std::size_t>(size), L'\\0');\n";
  out_ << "  ::MultiByteToWideChar(CP_UTF8, 0, text.data(),\n";
  out_ << "                        static_cast<int>(text.size()), result.data(),\n";
  out_ << "                        size);\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "inline std::string wideToUtf8(const wchar_t* text) {\n";
  out_ << "  if (text == nullptr) return std::string();\n";
  out_ << "  const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr,\n";
  out_ << "                                         0, nullptr, nullptr);\n";
  out_ << "  if (size <= 0) return std::string();\n";
  out_ << "  std::string result(static_cast<std::size_t>(size), '\\0');\n";
  out_ << "  const int written = ::WideCharToMultiByte(\n";
  out_ << "      CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);\n";
  out_ << "  if (written > 0) result.resize(static_cast<std::size_t>(written));\n";
  out_ << "  if (!result.empty() && result.back() == '\\0') result.pop_back();\n";
  out_ << "  return result;\n";
  out_ << "}\n";
  out_ << "#endif\n";
  out_ << "inline void enableUtf8Console() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  ::SetConsoleOutputCP(CP_UTF8);\n";
  out_ << "  ::SetConsoleCP(CP_UTF8);\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::filesystem::path toPath(const std::string& text) {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return std::filesystem::path(\n";
  out_ << "      std::u8string(reinterpret_cast<const char8_t*>(text.data()),\n";
  out_ << "                    text.size()));\n";
  out_ << "#else\n";
  out_ << "  return std::filesystem::path(text);\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string fromPath(const std::filesystem::path& path) {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  const std::u8string text = path.u8string();\n";
  out_ << "  return std::string(reinterpret_cast<const char*>(text.data()),\n";
  out_ << "                     text.size());\n";
  out_ << "#else\n";
  out_ << "  return path.string();\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string operatingSystem() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return \"windows\";\n";
  out_ << "#elif defined(__APPLE__)\n";
  out_ << "  return \"macos\";\n";
  out_ << "#elif defined(__linux__)\n";
  out_ << "  return \"linux\";\n";
  out_ << "#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)\n";
  out_ << "  return \"freebsd\";\n";
  out_ << "#else\n";
  out_ << "  return \"other\";\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string architecture() {\n";
  out_ << "#if defined(_M_X64) || defined(__x86_64__)\n";
  out_ << "  return \"x86_64\";\n";
  out_ << "#elif defined(_M_IX86) || defined(__i386__)\n";
  out_ << "  return \"x86\";\n";
  out_ << "#elif defined(_M_ARM64) || defined(__aarch64__)\n";
  out_ << "  return \"aarch64\";\n";
  out_ << "#elif defined(_M_ARM) || defined(__arm__)\n";
  out_ << "  return \"arm\";\n";
  out_ << "#elif defined(__riscv) && (__riscv_xlen == 64)\n";
  out_ << "  return \"riscv64\";\n";
  out_ << "#else\n";
  out_ << "  return \"unknown\";\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string targetTriple() {\n";
  out_ << "  const std::string arch = architecture();\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "#if defined(_MSC_VER)\n";
  out_ << "  return arch + \"-pc-windows-msvc\";\n";
  out_ << "#else\n";
  out_ << "  return arch + \"-pc-windows-gnu\";\n";
  out_ << "#endif\n";
  out_ << "#elif defined(__APPLE__)\n";
  out_ << "  return arch + \"-apple-darwin\";\n";
  out_ << "#elif defined(__linux__)\n";
  out_ << "  return arch + \"-unknown-linux-gnu\";\n";
  out_ << "#elif defined(__FreeBSD__)\n";
  out_ << "  return arch + \"-unknown-freebsd\";\n";
  out_ << "#else\n";
  out_ << "  return arch + \"-unknown-unknown\";\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline char32_t pathSeparator() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return U'\\\\';\n";
  out_ << "#else\n";
  out_ << "  return U'/';\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string lineSeparator() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return \"\\r\\n\";\n";
  out_ << "#else\n";
  out_ << "  return \"\\n\";\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string executableSuffix() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return \".exe\";\n";
  out_ << "#else\n";
  out_ << "  return \"\";\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string currentDirectory() {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  const std::filesystem::path path = std::filesystem::current_path(error);\n";
  out_ << "  return error ? std::string(\".\") : fromPath(path);\n";
  out_ << "}\n";
  out_ << "inline std::string temporaryDirectory() {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  const std::filesystem::path path =\n";
  out_ << "      std::filesystem::temp_directory_path(error);\n";
  out_ << "  return error ? currentDirectory() : fromPath(path);\n";
  out_ << "}\n";
  out_ << "inline Result<std::string> environment(const std::string& name) {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  const std::wstring wideName = utf8ToWide(name);\n";
  out_ << "  const DWORD size =\n";
  out_ << "      ::GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);\n";
  out_ << "  if (size == 0) {\n";
  out_ << "    return Result<std::string>::failure(\n";
  out_ << "        \"environment variable is not set: \" + name);\n";
  out_ << "  }\n";
  out_ << "  std::wstring value(static_cast<std::size_t>(size), L'\\0');\n";
  out_ << "  ::GetEnvironmentVariableW(wideName.c_str(), value.data(), size);\n";
  out_ << "  if (!value.empty() && value.back() == L'\\0') value.pop_back();\n";
  out_ << "  std::string text;\n";
  out_ << "  const int utf8Size = ::WideCharToMultiByte(\n";
  out_ << "      CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr,\n";
  out_ << "      0, nullptr, nullptr);\n";
  out_ << "  if (utf8Size > 0) {\n";
  out_ << "    text.resize(static_cast<std::size_t>(utf8Size));\n";
  out_ << "    ::WideCharToMultiByte(CP_UTF8, 0, value.data(),\n";
  out_ << "                          static_cast<int>(value.size()), text.data(),\n";
  out_ << "                          utf8Size, nullptr, nullptr);\n";
  out_ << "  }\n";
  out_ << "  return Result<std::string>::success(text);\n";
  out_ << "#else\n";
  out_ << "  const char* value = std::getenv(name.c_str());\n";
  out_ << "  if (value == nullptr) {\n";
  out_ << "    return Result<std::string>::failure(\n";
  out_ << "        \"environment variable is not set: \" + name);\n";
  out_ << "  }\n";
  out_ << "  return Result<std::string>::success(std::string(value));\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline bool setEnvironment(const std::string& name,\n";
  out_ << "                           const std::string& value) {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  return ::SetEnvironmentVariableW(utf8ToWide(name).c_str(),\n";
  out_ << "                                   utf8ToWide(value).c_str()) != 0;\n";
  out_ << "#else\n";
  out_ << "  return ::setenv(name.c_str(), value.c_str(), 1) == 0;\n";
  out_ << "#endif\n";
  out_ << "}\n";
  out_ << "inline std::string homeDirectory() {\n";
  out_ << "#if defined(_WIN32)\n";
  out_ << "  const Result<std::string> profile = environment(\"USERPROFILE\");\n";
  out_ << "  if (profile.ok) return profile.value;\n";
  out_ << "  const Result<std::string> drive = environment(\"HOMEDRIVE\");\n";
  out_ << "  const Result<std::string> home = environment(\"HOMEPATH\");\n";
  out_ << "  if (drive.ok && home.ok) return drive.value + home.value;\n";
  out_ << "#else\n";
  out_ << "  const Result<std::string> home = environment(\"HOME\");\n";
  out_ << "  if (home.ok) return home.value;\n";
  out_ << "#endif\n";
  out_ << "  return currentDirectory();\n";
  out_ << "}\n";
  out_ << "inline std::string pathJoin(const std::string& base,\n";
  out_ << "                           const std::string& child) {\n";
  out_ << "  return fromPath(toPath(base) / toPath(child));\n";
  out_ << "}\n";
  out_ << "inline std::string pathFileName(const std::string& path) {\n";
  out_ << "  return fromPath(toPath(path).filename());\n";
  out_ << "}\n";
  out_ << "inline std::string pathParent(const std::string& path) {\n";
  out_ << "  return fromPath(toPath(path).parent_path());\n";
  out_ << "}\n";
  out_ << "inline std::string pathExtension(const std::string& path) {\n";
  out_ << "  return fromPath(toPath(path).extension());\n";
  out_ << "}\n";
  out_ << "inline bool pathIsAbsolute(const std::string& path) {\n";
  out_ << "  return toPath(path).is_absolute();\n";
  out_ << "}\n";
  out_ << "inline std::string pathNormalize(const std::string& path) {\n";
  out_ << "  return fromPath(toPath(path).lexically_normal());\n";
  out_ << "}\n";
  out_ << "inline bool pathExists(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  return std::filesystem::exists(toPath(path), error);\n";
  out_ << "}\n";
  out_ << "inline bool createDirectory(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  std::filesystem::create_directory(toPath(path), error);\n";
  out_ << "  return !error;\n";
  out_ << "}\n";
  out_ << "inline bool createDirectories(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  std::filesystem::create_directories(toPath(path), error);\n";
  out_ << "  return !error;\n";
  out_ << "}\n";
  out_ << "inline bool deleteDirectory(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  return std::filesystem::remove(toPath(path), error) && !error;\n";
  out_ << "}\n";
  out_ << "inline Result<std::deque<std::string>> listDirectory(\n";
  out_ << "    const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  std::filesystem::directory_iterator iterator(toPath(path), error);\n";
  out_ << "  if (error) {\n";
  out_ << "    return Result<std::deque<std::string>>::failure(error.message());\n";
  out_ << "  }\n";
  out_ << "  std::deque<std::string> entries;\n";
  out_ << "  const std::filesystem::directory_iterator end;\n";
  out_ << "  while (iterator != end) {\n";
  out_ << "    entries.push_back(fromPath(iterator->path().filename()));\n";
  out_ << "    iterator.increment(error);\n";
  out_ << "    if (error) {\n";
  out_ << "      return Result<std::deque<std::string>>::failure(error.message());\n";
  out_ << "    }\n";
  out_ << "  }\n";
  out_ << "  std::sort(entries.begin(), entries.end());\n";
  out_ << "  return Result<std::deque<std::string>>::success(entries);\n";
  out_ << "}\n";
  out_ << "inline Result<std::string> readFile(const std::string& path) {\n";
  out_ << "  std::ifstream file(toPath(path), std::ios::binary);\n";
  out_ << "  if (!file) {\n";
  out_ << "    return Result<std::string>::failure(\"cannot read file: \" + path);\n";
  out_ << "  }\n";
  out_ << "  std::string content((std::istreambuf_iterator<char>(file)),\n";
  out_ << "                      std::istreambuf_iterator<char>());\n";
  out_ << "  return Result<std::string>::success(content);\n";
  out_ << "}\n";
  out_ << "inline Result<std::int64_t> writeFile(const std::string& path,\n";
  out_ << "                                       const std::string& content) {\n";
  out_ << "  std::ofstream file(toPath(path), std::ios::binary);\n";
  out_ << "  if (!file) {\n";
  out_ << "    return Result<std::int64_t>::failure(\"cannot write file: \" + path);\n";
  out_ << "  }\n";
  out_ << "  file << content;\n";
  out_ << "  if (!file) {\n";
  out_ << "    return Result<std::int64_t>::failure(\"cannot write file: \" + path);\n";
  out_ << "  }\n";
  out_ << "  return Result<std::int64_t>::success(1);\n";
  out_ << "}\n";
  out_ << "inline bool fileExists(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  return std::filesystem::is_regular_file(toPath(path), error);\n";
  out_ << "}\n";
  out_ << "inline bool deleteFile(const std::string& path) {\n";
  out_ << "  std::error_code error;\n";
  out_ << "  return std::filesystem::remove(toPath(path), error) && !error;\n";
  out_ << "}\n";
  out_ << "inline std::int64_t timeMillis() {\n";
  out_ << "  return std::chrono::duration_cast<std::chrono::milliseconds>(\n";
  out_ << "             std::chrono::system_clock::now().time_since_epoch())\n";
  out_ << "      .count();\n";
  out_ << "}\n";
  out_ << "inline void sleepMillis(std::int64_t ms) {\n";
  out_ << "  std::this_thread::sleep_for(std::chrono::milliseconds(ms));\n";
  out_ << "}\n";
  out_ << "}  // namespace coord_rt\n\n";
  out_ << "namespace coord_program {\n\n";
  out_ << "namespace coord_meta {\n";
  out_ << "inline bool hasAnnotation(const std::string& declaration,\n";
  out_ << "                          const std::string& annotation);\n";
  out_ << "inline std::string annotationValue(\n";
  out_ << "    const std::string& declaration,\n";
  out_ << "    const std::string& annotation);\n";
  out_ << "int runtimeFieldCount(const std::string& declaration);\n";
  out_ << "std::string runtimeFieldName(const std::string& declaration,\n";
  out_ << "                              int index);\n";
  out_ << "std::string runtimeFieldType(const std::string& declaration,\n";
  out_ << "                              int index);\n";
  out_ << "int runtimeMethodCount(const std::string& declaration);\n";
  out_ << "std::string runtimeMethodName(const std::string& declaration,\n";
  out_ << "                               int index);\n";
  out_ << "std::string runtimeMethodType(const std::string& declaration,\n";
  out_ << "                               int index);\n";
  out_ << "std::string runtimeBase(const std::string& declaration);\n";
  out_ << "bool runtimeHasMember(const std::string& declaration,\n";
  out_ << "                      const std::string& member);\n";
  out_ << "}  // namespace coord_meta\n\n";
}

void CppEmitter::emitParams(const std::vector<Param>& params) {
  out_ << '(';
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      out_ << ", ";
    }
    const bool smart =
        params[i].isUnique || params[i].isShare || params[i].isWeak;
    if (smart) {
      out_ << smartPointerOpen(params[i].isUnique, params[i].isShare,
                               params[i].isWeak);
    }
    emitType(params[i].type.get());
    if (smart) {
      out_ << '>';
    }
    if (!params[i].name.empty()) {
      out_ << ' ' << sanitize(params[i].name);
    }
  }
  out_ << ')';
}

void CppEmitter::emitClass(const Decl& decl) {
  const Decl* previousClass = currentClassDecl_;
  currentClassDecl_ = &decl;
  emitAnnotations(decl.annotations);
  emitInheritedAnnotations(decl);
  if (!decl.generics.empty()) {
    out_ << "template <";
    for (std::size_t i = 0; i < decl.generics.size(); ++i) {
      if (i != 0) {
        out_ << ", ";
      }
      out_ << "typename " << sanitize(decl.generics[i].name);
    }
    out_ << ">\n";
  }

  bool polymorphic = !decl.interfaces.empty();
  currentBase_.clear();
  if (decl.base != nullptr) {
    const std::string base = typeToString(decl.base.get());
    if (base != "class" && base != "void") {
      currentBase_ = base;
    }
  }
  for (const auto& member : decl.members) {
    if (member->kind == DeclKind::Method && member->isVirtual) {
      polymorphic = true;
    }
  }

  out_ << "class " << sanitize(decl.name);
  if (decl.isFinal) {
    out_ << " final";
  }
  bool firstBase = true;
  if (decl.base != nullptr) {
    const std::string base = typeToString(decl.base.get());
    if (base != "class" && base != "void") {
      out_ << " : public " << base;
      firstBase = false;
    }
  }
  for (const auto& iface : decl.interfaces) {
    out_ << (firstBase ? " : public " : ", public ")
         << typeToString(iface.get());
    firstBase = false;
  }
  out_ << " {\n";
  depth_ = 1;

  fields_.clear();
  for (const auto& member : decl.members) {
    if (member->kind == DeclKind::Field) {
      fields_.insert(member->name);
    }
  }

  Access currentAccess = Access::Private;
  for (const auto& member : decl.members) {
    indent();
    emitAnnotations(member->annotations);
    emitInheritedAnnotations(*member);
    const Access memberAccess =
        member->access == Access::Default ? Access::Private : member->access;
    if (memberAccess != currentAccess) {
      out_ << accessName(memberAccess) << ":\n";
      currentAccess = memberAccess;
    }

    if (member->kind == DeclKind::Field) {
      indent();
      const bool smart =
          member->isUnique || member->isShare || member->isWeak;
      if (smart) {
        out_ << smartPointerOpen(member->isUnique, member->isShare,
                                 member->isWeak);
        emitStorageType(member->type.get(), member->isUnsigned,
                        member->length);
        out_ << '>';
      } else {
        emitStorageType(member->type.get(), member->isUnsigned,
                        member->length);
      }
      out_ << ' ' << sanitize(member->name);
      if (member->init) {
        out_ << " = ";
        emitInitializer(member->init.get(), member->radix);
      } else {
        bool optional = false;
        for (const AnnotationUse& use : member->annotations) {
          if (use.name == "Optional") {
            optional = true;
          }
        }
        if (optional && member->type != nullptr &&
            member->type->resolved != nullptr &&
            member->type->resolved->kind == TypeKind::Pointer) {
          out_ << " = nullptr";
        } else {
          out_ << "{}";
        }
      }
      out_ << ";\n";
      continue;
    }

    if (member->kind == DeclKind::Constructor) {
      indent();
      out_ << sanitize(member->name);
      emitParams(member->params);
      if (!member->initializers.empty()) {
        out_ << " : ";
        for (std::size_t i = 0; i < member->initializers.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          const Initializer& initializer = member->initializers[i];
          if (initializer.kind == Initializer::Kind::Super) {
            out_ << currentBase_;
          } else {
            out_ << sanitize(initializer.name);
          }
          out_ << '(';
          for (std::size_t j = 0; j < initializer.args.size(); ++j) {
            if (j != 0) {
              out_ << ", ";
            }
            const Decl* field =
                initializer.kind == Initializer::Kind::Field
                    ? fieldOf(currentClassDecl_, initializer.name)
                    : nullptr;
            if (field != nullptr && (field->isUnique || field->isShare) &&
                initializer.args[j]->kind == ExprKind::Call &&
                initializer.args[j]->lhs != nullptr &&
                initializer.args[j]->lhs->kind == ExprKind::Name) {
              out_ << (field->isUnique ? "std::make_unique<"
                                       : "coord_rt::makeShared<")
                   << sanitize(initializer.args[j]->lhs->name) << ">(";
              for (std::size_t k = 0;
                   k < initializer.args[j]->args.size(); ++k) {
                if (k != 0) {
                  out_ << ", ";
                }
                emitExpr(initializer.args[j]->args[k].get());
              }
              out_ << ')';
            } else {
              emitExpr(initializer.args[j].get());
            }
          }
          out_ << ')';
        }
      }
      out_ << " {\n";
      for (const Initializer& initializer : member->initializers) {
        if (initializer.kind != Initializer::Kind::Field ||
            initializer.args.size() != 1) {
          continue;
        }
        const Decl* field = fieldOf(currentClassDecl_, initializer.name);
        if (field == nullptr || !field->isShare) {
          continue;
        }
        indent();
        out_ << "coord_rt::recordShareEdge(this, ";
        if (initializer.args[0]->kind == ExprKind::NullLit) {
          out_ << "nullptr";
        } else {
          out_ << "coord_rt::shareObjectAddress(";
          emitExpr(initializer.args[0].get());
          out_ << ')';
        }
        out_ << ");\n";
      }
      depth_ = 2;
      locals_.clear();
      locals_.emplace_back();
      for (const Param& param : member->params) {
        locals_.back().insert(param.name);
      }
      emitBlock(member->body.get());
      locals_.pop_back();
      depth_ = 1;
      indent();
      out_ << "}\n";
      continue;
    }

    if (member->kind == DeclKind::Destructor) {
      indent();
      const std::string virtualPrefix = polymorphic ? "virtual " : "";
      if (member->synthesizedDestructor) {
        out_ << "// synthesized destructor: ";
        if (member->destroyOrder.empty()) {
          out_ << "no members to destroy.\n";
        } else {
          out_ << "reverse declaration order:";
          for (const Decl* field : member->destroyOrder) {
            out_ << ' ' << (field != nullptr ? field->name : "?");
          }
          out_ << " (then the base class).\n";
        }
        indent();
        if (decl.isProxy && proxyRuntime(&decl)) {
          out_ << virtualPrefix << "~" << sanitize(decl.name) << "() {\n";
          depth_ = 2;
          line("coord_rt::proxyRelease<" + sanitize(decl.proxyHandler) +
               ">((const void*)this);");
          depth_ = 1;
          indent();
          out_ << "}\n";
        } else {
          out_ << virtualPrefix << "~" << sanitize(decl.name)
               << "() = default;\n";
        }
      } else {
        out_ << virtualPrefix << "~" << sanitize(decl.name) << "() {\n";
        depth_ = 2;
        emitBlock(member->body.get());
        if (decl.isProxy && proxyRuntime(&decl)) {
          line("coord_rt::proxyRelease<" + sanitize(decl.proxyHandler) +
               ">((const void*)this);");
        }
        depth_ = 1;
        indent();
        out_ << "}\n";
      }
      continue;
    }

    if (member->kind == DeclKind::Method) {
      indent();
      if (member->isStatic) {
        out_ << "static ";
      } else if (member->isVirtual) {
        out_ << "virtual ";
      }
      emitReturnType(*member);
      out_ << ' ' << sanitize(member->name);
      emitParams(member->params);
      if (member->isConstMethod) {
        out_ << " const";
      }
      if (member->isOverride) {
        out_ << " override";
      }
      if (member->isFinal) {
        out_ << " final";
      }
      if (member->body) {
        out_ << " {\n";
        depth_ = 2;
        const Decl* previousCallable = currentCallable_;
        currentCallable_ = member.get();
        locals_.clear();
        locals_.emplace_back();
        for (const Param& param : member->params) {
          locals_.back().insert(param.name);
        }
        if (decl.isProxy) {
          const Decl* handler = nullptr;
          for (const auto& candidate : module_.decls) {
            if ((candidate->kind == DeclKind::Class ||
                 candidate->kind == DeclKind::Annotation) &&
                candidate->name == decl.proxyHandler) {
              handler = candidate.get();
              break;
            }
          }
          if (handler != nullptr) {
            for (const auto& handlerMethod : handler->members) {
              if (handlerMethod->kind != DeclKind::Method) {
                continue;
              }
              if (handler->kind == DeclKind::Class &&
                  !handlerMethod->isStatic) {
                continue;
              }
              bool isCallTrap = false;
              for (const AnnotationUse& use :
                   handlerMethod->annotations) {
                if (use.name == "Call") {
                  isCallTrap = true;
                }
              }
              if (isCallTrap) {
                std::string call = trapCallName(handler, *handlerMethod) + "(";
                if (handlerMethod->params.size() == 1) {
                  call += "\"" + escapeString(member->name) + "\"";
                }
                call += ");";
                line(call);
                break;
              }
            }
          }
        }
        emitBlock(member->body.get());
        currentCallable_ = previousCallable;
        locals_.pop_back();
        depth_ = 1;
        indent();
        out_ << "}\n";
      } else {
        out_ << " = 0;\n";
      }
    }
  }

  depth_ = 0;
  out_ << "};\n\n";
  currentClassDecl_ = previousClass;
}

void CppEmitter::emitEnum(const Decl& decl) {
  emitAnnotations(decl.annotations);
  emitInheritedAnnotations(decl);
  const bool hasBehavior = !decl.members.empty() || !decl.interfaces.empty();
  if (!hasBehavior) {
    out_ << "enum class " << sanitize(decl.name) << " : std::int64_t {\n";
    depth_ = 1;
    for (const EnumVariant& variant : decl.variants) {
      line(sanitize(variant.name) + " = " +
           std::to_string(variant.resolvedValue) + ",");
    }
    depth_ = 0;
    out_ << "};\n\n";
    return;
  }

  // Enums with methods/interfaces are a same-layout wrapper: one 64-bit value,
  // static variant constants, methods and (optionally) an interface vtable.
  out_ << "class " << sanitize(decl.name);
  for (std::size_t i = 0; i < decl.interfaces.size(); ++i) {
    out_ << (i == 0 ? " : public " : ", public ")
         << typeToString(decl.interfaces[i].get());
  }
  out_ << " {\npublic:\n";
  out_ << "  enum class Raw : std::int64_t {\n";
  for (const EnumVariant& variant : decl.variants) {
    out_ << "    " << sanitize(variant.name) << " = "
         << variant.resolvedValue << ",\n";
  }
  out_ << "  };\n";
  out_ << "  Raw value_;\n";
  out_ << "  constexpr " << sanitize(decl.name)
       << "(Raw value) : value_(value) {}\n";
  for (const EnumVariant& variant : decl.variants) {
    out_ << "  static const " << sanitize(decl.name) << ' '
         << sanitize(variant.name) << ";\n";
  }
  out_ << "  std::int64_t raw() const {\n"
       << "    return static_cast<std::int64_t>(value_);\n"
       << "  }\n";
  out_ << "  operator std::int64_t() const { return raw(); }\n";
  if (!decl.interfaces.empty()) {
    out_ << "  virtual ~" << sanitize(decl.name) << "() = default;\n";
  }

  fields_.clear();
  currentBase_.clear();
  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Method) {
      continue;
    }
    depth_ = 1;
    indent();
    if (member->isVirtual || !decl.interfaces.empty()) {
      out_ << "virtual ";
    }
    emitReturnType(*member);
    out_ << ' ' << sanitize(member->name);
    emitParams(member->params);
    if (member->isConstMethod) {
      out_ << " const";
    }
    if (member->isOverride) {
      out_ << " override";
    }
    if (member->isFinal) {
      out_ << " final";
    }
    if (member->body) {
      out_ << " {\n";
      depth_ = 2;
      const Decl* previousCallable = currentCallable_;
      currentCallable_ = member.get();
      locals_.clear();
      locals_.emplace_back();
      for (const Param& param : member->params) {
        locals_.back().insert(param.name);
      }
      emitBlock(member->body.get());
      currentCallable_ = previousCallable;
      locals_.pop_back();
      depth_ = 1;
      indent();
      out_ << "}\n";
    } else {
      out_ << " = 0;\n";
    }
  }
  depth_ = 0;
  out_ << "};\n";

  for (const EnumVariant& variant : decl.variants) {
    out_ << "inline const " << sanitize(decl.name) << ' '
         << sanitize(decl.name) << "::" << sanitize(variant.name) << "{"
         << sanitize(decl.name) << "::Raw::" << sanitize(variant.name)
         << "};\n";
  }
  out_ << '\n';
}

void CppEmitter::emitInterface(const Decl& decl) {
  emitAnnotations(decl.annotations);
  emitInheritedAnnotations(decl);
  out_ << "class " << sanitize(decl.name) << " {\npublic:\n";
  depth_ = 1;
  indent();
  out_ << "virtual ~" << sanitize(decl.name) << "() = default;\n";
  locals_.clear();
  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Method) {
      continue;
    }
    indent();
    emitAnnotations(member->annotations);
    emitInheritedAnnotations(*member);
    out_ << "virtual ";
    emitReturnType(*member);
    out_ << ' ' << sanitize(member->name);
    emitParams(member->params);
    if (member->isConstMethod) {
      out_ << " const";
    }
    if (member->body) {
      out_ << " {\n";
      depth_ = 2;
      locals_.emplace_back();
      for (const Param& param : member->params) {
        locals_.back().insert(param.name);
      }
      emitBlock(member->body.get());
      locals_.pop_back();
      depth_ = 1;
      indent();
      out_ << "}\n";
    } else {
      out_ << " = 0;\n";
    }
  }
  depth_ = 0;
  out_ << "};\n\n";
}

void CppEmitter::emitNativeFunction(const Decl& decl) {
  emitReturnType(decl);
  out_ << ' ' << sanitize(decl.name);
  emitParams(decl.params);
  out_ << " {\n";
  const bool returnsVoid =
      decl.type == nullptr || decl.type->name == "void" ||
      (decl.type->resolved != nullptr &&
       decl.type->resolved->name == "void");
  const bool returnsString =
      decl.type != nullptr && decl.type->resolved != nullptr &&
      decl.type->resolved->kind == TypeKind::String;
  if (returnsString) {
    out_ << "  const char* __native_result = ::";
  } else if (!returnsVoid) {
    out_ << "  return ::";
  } else {
    out_ << "  ::";
  }
  out_ << decl.nativeSymbol << '(';
  for (std::size_t i = 0; i < decl.params.size(); ++i) {
    if (i != 0) {
      out_ << ", ";
    }
    const Param& param = decl.params[i];
    const bool string =
        param.type != nullptr && param.type->resolved != nullptr &&
        param.type->resolved->kind == TypeKind::String;
    out_ << sanitize(param.name);
    if (string) {
      out_ << ".c_str()";
    }
  }
  out_ << ");\n";
  if (returnsString) {
    out_ << "  if (__native_result == nullptr) return std::string();\n";
    out_ << "  return std::string(__native_result);\n";
  }
  out_ << "}\n";
}

void CppEmitter::emitFunction(const Decl& decl) {
  if (decl.isNative) {
    emitNativeFunction(decl);
    return;
  }
  emitAnnotations(decl.annotations);
  fields_.clear();
  typeParams_.clear();
  smartLocals_.clear();
  for (const GenericParam& param : decl.generics) {
    typeParams_.insert(param.name);
  }

  if (!decl.generics.empty()) {
    out_ << "template <";
    for (std::size_t i = 0; i < decl.generics.size(); ++i) {
      if (i != 0) {
        out_ << ", ";
      }
      out_ << "typename " << sanitize(decl.generics[i].name);
    }
    out_ << ">\n";

    std::string requirements;
    for (const GenericParam& param : decl.generics) {
      if (param.bound == nullptr) {
        continue;
      }
      const std::string name = sanitize(param.name);
      std::string condition;
      if (param.bound->kind == TypeExprKind::Named &&
          param.bound->name == "integer") {
        condition = "std::is_integral_v<" + name + ">";
      } else if (param.bound->kind == TypeExprKind::Named) {
        condition = "std::is_base_of_v<" +
                    typeToString(param.bound.get()) + ", " + name + ">";
      }
      if (!condition.empty()) {
        if (!requirements.empty()) {
          requirements += " && ";
        }
        requirements += condition;
      }
    }
    if (!requirements.empty()) {
      out_ << "requires (" << requirements << ")\n";
    }
  }

  emitReturnType(decl);
  out_ << ' ' << sanitize(decl.name);
  emitParams(decl.params);
  if (decl.body) {
    out_ << " {\n";
    depth_ = 1;
    currentResult_ = decl.isResult || decl.isExpect;
    currentReturnType_ = typeToString(decl.type.get());
    const Decl* previousCallable = currentCallable_;
    currentCallable_ = &decl;
    locals_.clear();
    locals_.emplace_back();
    for (const Param& param : decl.params) {
      locals_.back().insert(param.name);
    }
    emitBlock(decl.body.get());
    currentCallable_ = previousCallable;
    locals_.pop_back();
    depth_ = 0;
    currentResult_ = false;
    currentReturnType_.clear();
    out_ << "}\n\n";
  } else {
    out_ << ";\n\n";
  }
  typeParams_.clear();
}

void CppEmitter::emitConst(const Decl& decl) {
  emitAnnotations(decl.annotations);
  const Value* value = constant(decl.name);
  out_ << (value != nullptr ? "constexpr " : "const ");
  emitStorageType(decl.type.get(), decl.isUnsigned, decl.length);
  out_ << ' ' << sanitize(decl.name) << " = ";
  if (value != nullptr) {
    if (value->kind == Value::Kind::Int && decl.radix != 0) {
      out_ << "static_cast<std::int64_t>("
           << formatCppInteger(value->intValue, decl.radix) << ')';
    } else {
      out_ << literal(*value);
    }
  } else if (decl.init) {
    emitInitializer(decl.init.get(), decl.radix);
  } else {
    out_ << "{}";
  }
  out_ << ";\n\n";
}

void CppEmitter::emitMainWrapper() {
  const Decl* main = nullptr;
  for (const auto& decl : module_.decls) {
    if (decl->kind == DeclKind::Function && decl->name == "main") {
      main = decl.get();
      break;
    }
  }
  if (main == nullptr) {
    return;
  }
  smartLocals_.clear();

  const bool mainResult = main->isResult;
  typeParams_.clear();
  fields_.clear();
  locals_.clear();
  locals_.emplace_back();
  for (const Param& param : main->params) {
    locals_.back().insert(param.name);
  }

  if (mainResult) {
    out_ << "coord_rt::Result<std::int64_t>";
  } else {
    out_ << "std::int64_t";
  }
  out_ << " coord_main";
  emitParams(main->params);
  if (main->body) {
    out_ << " {\n";
    depth_ = 1;
    currentResult_ = mainResult;
    currentReturnType_ = "std::int64_t";
    emitBlock(main->body.get());
    depth_ = 0;
    currentResult_ = false;
    currentReturnType_.clear();
    out_ << "}\n";
  } else {
    out_ << " {}\n";
  }
  locals_.pop_back();
}

void CppEmitter::emitInitializer(const Expr* expr, int radix) {
  const int saved = literalRadix_;
  literalRadix_ = radix;
  emitExpr(expr);
  literalRadix_ = saved;
}

void CppEmitter::emitShareEdgeRecord(const Expr* assign) {
  if (assign == nullptr || assign->kind != ExprKind::Assign ||
      assign->lhs == nullptr || assign->rhs == nullptr) {
    return;
  }
  const Decl* field = nullptr;
  const Expr* owner = nullptr;
  if (assign->lhs->kind == ExprKind::Member &&
      assign->lhs->lhs != nullptr &&
      assign->lhs->lhs->resolvedType != nullptr) {
    field = fieldOf(assign->lhs->lhs->resolvedType->decl, assign->lhs->name);
    owner = assign->lhs->lhs.get();
  } else if (assign->lhs->kind == ExprKind::Name && currentClassDecl_ != nullptr) {
    field = fieldOf(currentClassDecl_, assign->lhs->name);
    owner = nullptr;
  }
  if (field == nullptr || !field->isShare) {
    return;
  }
  if (assign->rhs->kind != ExprKind::Name &&
      assign->rhs->kind != ExprKind::Member &&
      assign->rhs->kind != ExprKind::This &&
      assign->rhs->kind != ExprKind::NullLit) {
    return;
  }
  indent();
  out_ << "coord_rt::recordShareEdge(";
  if (owner != nullptr) {
    out_ << "coord_rt::shareObjectAddress(";
    emitExpr(owner);
    out_ << ')';
  } else {
    out_ << "this";
  }
  out_ << ", ";
  if (assign->rhs->kind == ExprKind::NullLit) {
    out_ << "nullptr";
  } else {
    out_ << "coord_rt::shareObjectAddress(";
    emitExpr(assign->rhs.get());
    out_ << ')';
  }
  out_ << ");\n";
}

void CppEmitter::emitBlock(const Stmt* block) {
  if (block == nullptr) {
    return;
  }
  const bool hasScope = block->kind == StmtKind::Block;
  if (hasScope) {
    locals_.emplace_back();
  }
  for (const auto& stmt : block->statements) {
    emitStmt(stmt.get());
  }
  if (hasScope) {
    locals_.pop_back();
  }
}

const Pattern* CppEmitter::patternInner(const Pattern* pattern) const {
  if (pattern != nullptr && pattern->kind == PatternKind::Guard) {
    return pattern->left.get();
  }
  return pattern;
}

void CppEmitter::emitPatternBindings(const Pattern* pattern,
                                     const std::string& value) {
  const Pattern* inner = patternInner(pattern);
  if (inner != nullptr && inner->kind == PatternKind::Bind) {
    indent();
    out_ << "auto " << sanitize(inner->name) << " = " << value << ";\n";
  }
}

void CppEmitter::emitPatternCondition(const Pattern* pattern,
                                      const std::string& value) {
  if (pattern == nullptr) {
    out_ << "true";
    return;
  }
  switch (pattern->kind) {
    case PatternKind::Wildcard:
    case PatternKind::Bind:
      out_ << "true";
      return;
    case PatternKind::Literal:
      out_ << '(' << value << " == ";
      emitExpr(pattern->value.get());
      out_ << ')';
      return;
    case PatternKind::Path:
      if (pattern->enumBehavior) {
        out_ << '(' << value << ".raw() == " << sanitize(pattern->enumName)
             << "::" << sanitize(pattern->variant) << ".raw())";
      } else {
        out_ << '(' << value << " == " << sanitize(pattern->enumName) << "::"
             << sanitize(pattern->variant) << ')';
      }
      return;
    case PatternKind::Range:
      out_ << "((" << value << " >= ";
      emitExpr(pattern->left != nullptr ? pattern->left->value.get()
                                        : nullptr);
      out_ << ") && (" << value << ' '
           << (pattern->inclusive ? "<=" : "<") << ' ';
      emitExpr(pattern->right != nullptr ? pattern->right->value.get()
                                         : nullptr);
      out_ << "))";
      return;
    case PatternKind::Or:
      out_ << '(';
      emitPatternCondition(pattern->left.get(), value);
      out_ << " || ";
      emitPatternCondition(pattern->right.get(), value);
      out_ << ')';
      return;
    case PatternKind::Guard:
      emitPatternCondition(pattern->left.get(), value);
      return;
  }
}

void CppEmitter::emitMatchBranches(const Stmt* stmt, std::size_t index,
                                   const std::string& value) {
  if (index >= stmt->matchPatterns.size() ||
      index >= stmt->switchBodies.size()) {
    return;
  }
  const Pattern* pattern = stmt->matchPatterns[index].get();
  const Pattern* inner = patternInner(pattern);
  const bool hasBinding =
      inner != nullptr && inner->kind == PatternKind::Bind;
  if (hasBinding) {
    indent();
    out_ << "{\n";
    ++depth_;
    emitPatternBindings(pattern, value);
  }
  const std::string matched = "__matchOk" + std::to_string(tempCounter_++);
  indent();
  out_ << "bool " << matched << " = ";
  emitPatternCondition(pattern, value);
  out_ << ";\n";
  if (pattern != nullptr && pattern->kind == PatternKind::Guard &&
      pattern->guard != nullptr) {
    indent();
    out_ << "if (" << matched << ") " << matched << " = ";
    emitExpr(pattern->guard.get());
    out_ << ";\n";
  }
  indent();
  out_ << "if (" << matched << ") {\n";
  ++depth_;
  if (stmt->switchBodies[index]->kind == StmtKind::Block) {
    emitBlock(stmt->switchBodies[index].get());
  } else {
    emitStmt(stmt->switchBodies[index].get());
  }
  --depth_;
  indent();
  out_ << "}\n";
  if (index + 1 < stmt->matchPatterns.size()) {
    indent();
    out_ << "else {\n";
    ++depth_;
    emitMatchBranches(stmt, index + 1, value);
    --depth_;
    indent();
    out_ << "}\n";
  }
  if (hasBinding) {
    --depth_;
    indent();
    out_ << "}\n";
  }
}

void CppEmitter::emitMatch(const Stmt* stmt) {
  const std::string value = "__match" + std::to_string(tempCounter_++);
  indent();
  out_ << "{\n";
  ++depth_;
  indent();
  out_ << "auto " << value << " = ";
  emitExpr(stmt->condition.get());
  out_ << ";\n";
  emitMatchBranches(stmt, 0, value);
  --depth_;
  indent();
  out_ << "}\n";
}

void CppEmitter::emitStmt(const Stmt* stmt) {
  if (stmt == nullptr) {
    return;
  }
  switch (stmt->kind) {
    case StmtKind::Block:
      if (stmt->blockRetention == BlockRetention::Compile) {
        line("// @Retention(Compile) block executed by the compiler");
        return;
      }
      line("{");
      ++depth_;
      emitBlock(stmt);
      --depth_;
      line("}");
      return;
    case StmtKind::Var: {
      if (stmt->constructureInit && stmt->constructureCtor != nullptr) {
        const Decl* owner =
            stmt->type != nullptr && stmt->type->resolved != nullptr
                ? stmt->type->resolved->decl
                : nullptr;
        const Decl* constructTrap = proxyTrap(owner, "Construct");
        if (constructTrap == nullptr && owner != nullptr && owner->isProxy &&
            owner->proxyPolicy == ProxyPolicy::Abort) {
          indent();
          out_ << "coord_rt::optionalCheck(nullptr, \"proxy construction "
                  "aborted\");\n";
        }
        if (constructTrap != nullptr) {
          indent();
          out_ << trapCallName(owner, *constructTrap) << '(';
          for (std::size_t i = 0; i < stmt->constructureOrder.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitInitializer(stmt->constructureArgs[stmt->constructureOrder[i]]
                                .value.get(),
                            stmt->radix);
          }
          out_ << ");\n";
        }
        indent();
        emitStorageType(stmt->type.get(), stmt->isUnsigned, stmt->length);
        out_ << ' ' << sanitize(stmt->name) << " = ";
        out_ << typeToString(stmt->type.get()) << '(';
        for (std::size_t i = 0; i < stmt->constructureOrder.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          emitInitializer(stmt->constructureArgs[stmt->constructureOrder[i]]
                              .value.get(),
                          stmt->radix);
        }
        out_ << ");\n";
        if (!locals_.empty()) {
          locals_.back().insert(stmt->name);
        }
        return;
      }
      if (stmt->expr != nullptr &&
          stmt->expr->kind == ExprKind::ObjectLit) {
        indent();
        emitStorageType(stmt->type.get(), stmt->isUnsigned, stmt->length);
        out_ << ' ' << sanitize(stmt->name) << "{};\n";
        for (ObjectField& field : stmt->expr->fields) {
          auto base = std::make_unique<Expr>();
          base->kind = ExprKind::Name;
          base->name = stmt->name;
          base->resolvedType = stmt->expr->resolvedType;
          auto member = std::make_unique<Expr>();
          member->kind = ExprKind::Member;
          member->name = field.name;
          member->lhs = std::move(base);
          auto assign = std::make_unique<Expr>();
          assign->kind = ExprKind::Assign;
          assign->op = "=";
          assign->lhs = std::move(member);
          assign->rhs = std::move(field.value);
          indent();
          emitExpr(assign.get());
          out_ << ";\n";
        }
        return;
      }
      if (stmt->isUnique || stmt->isShare || stmt->isWeak) {
        const bool unique = stmt->isUnique;
        const bool weak = stmt->isWeak;
        indent();
        out_ << smartPointerOpen(stmt->isUnique, stmt->isShare, weak)
             << typeToString(stmt->type.get()) << "> "
             << sanitize(stmt->name);
        const Expr* init = stmt->expr.get();
        if (weak) {
          if (init != nullptr && init->kind != ExprKind::NullLit) {
            out_ << " = ";
            emitInitializer(init, stmt->radix);
          } else {
            out_ << "{}";
          }
        } else if (stmt->braceInit) {
          out_ << " = " << (unique ? "std::make_unique<" : "coord_rt::makeShared<")
               << typeToString(stmt->type.get()) << ">(";
          for (std::size_t i = 0; i < stmt->initArgs.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitInitializer(stmt->initArgs[i].get(), stmt->radix);
          }
          out_ << ')';
        } else if (init != nullptr && init->kind == ExprKind::Name &&
                   smartLocals_.count(init->name) != 0) {
          if (unique) {
            out_ << " = std::move(" << sanitize(init->name) << ')';
          } else {
            out_ << " = " << sanitize(init->name);
          }
        } else if (init != nullptr && init->kind == ExprKind::Call &&
                   init->lhs != nullptr &&
                   init->lhs->kind == ExprKind::Name) {
          out_ << " = " << (unique ? "std::make_unique<" : "coord_rt::makeShared<")
               << typeToString(stmt->type.get()) << ">(";
          for (std::size_t i = 0; i < init->args.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitInitializer(init->args[i].get(), stmt->radix);
          }
          out_ << ')';
        } else if (init != nullptr && init->kind != ExprKind::NullLit) {
          out_ << " = " << (unique ? "std::unique_ptr<" : "std::shared_ptr<")
               << typeToString(stmt->type.get()) << ">(";
          emitInitializer(init, stmt->radix);
          out_ << ')';
        }
        out_ << ";\n";
        if (!weak) {
          smartLocals_[stmt->name] =
              (unique ? 1 : 2) | (stmt->isOptional ? 4 : 0);
        }
        if (!locals_.empty()) {
          locals_.back().insert(stmt->name);
        }
        return;
      }
      if (stmt->type != nullptr &&
          stmt->type->kind == TypeExprKind::Ref &&
          stmt->type->inner != nullptr) {
        const Expr* init = stmt->expr.get();
        const bool direct =
            init != nullptr &&
            (init->kind == ExprKind::Name || init->kind == ExprKind::Member ||
             init->kind == ExprKind::Index || init->kind == ExprKind::This ||
             init->kind == ExprKind::Super);
        if (direct) {
          indent();
          out_ << typeToString(stmt->type.get()) << ' '
               << sanitize(stmt->name) << " = ";
          emitInitializer(init, stmt->radix);
          out_ << ";\n";
        } else {
          const std::string temporary =
              "__ref" + std::to_string(refTemps_++);
          indent();
          emitStorageType(stmt->type->inner.get(), stmt->isUnsigned,
                          stmt->length);
          out_ << ' ' << temporary << " = ";
          if (init != nullptr) {
            emitInitializer(init, stmt->radix);
          } else {
            out_ << "{}";
          }
          out_ << ";\n";
          indent();
          out_ << typeToString(stmt->type.get()) << ' '
               << sanitize(stmt->name) << " = " << temporary << ";\n";
        }
        if (!locals_.empty()) {
          locals_.back().insert(stmt->name);
        }
        return;
      }
      indent();
      emitStorageType(stmt->type.get(), stmt->isUnsigned, stmt->length);
      out_ << ' ' << sanitize(stmt->name);
      if (stmt->braceInit) {
        out_ << '{';
        for (std::size_t i = 0; i < stmt->initArgs.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          emitInitializer(stmt->initArgs[i].get(), stmt->radix);
        }
        out_ << '}';
      } else if (stmt->directInit && stmt->expr != nullptr &&
          stmt->expr->kind == ExprKind::Call) {
        const auto& args = stmt->expr->args;
        if (args.empty()) {
          out_ << " = " << typeToString(stmt->type.get()) << "()";
        } else {
          out_ << '(';
          for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitInitializer(args[i].get(), stmt->radix);
          }
          out_ << ')';
        }
      } else if (stmt->expr != nullptr) {
        out_ << " = ";
        emitInitializer(stmt->expr.get(), stmt->radix);
      } else if (stmt->isOptional && stmt->type != nullptr &&
                 stmt->type->resolved != nullptr &&
                 stmt->type->resolved->kind == TypeKind::Pointer) {
        out_ << " = nullptr";
      } else {
        out_ << "{}";
      }
      out_ << ";\n";
      if (!locals_.empty()) {
        locals_.back().insert(stmt->name);
      }
      return;
    }
    case StmtKind::Expr:
      if (stmt->expr != nullptr && stmt->expr->kind == ExprKind::Call &&
          stmt->expr->lhs != nullptr &&
          stmt->expr->lhs->kind == ExprKind::Name &&
          stmt->expr->lhs->name == "line" && stmt->expr->args.size() == 2 &&
          stmt->expr->args[0] != nullptr &&
          stmt->expr->args[0]->kind == ExprKind::StringLit &&
          stmt->expr->args[1] != nullptr &&
          stmt->expr->args[1]->kind == ExprKind::IntLit) {
        out_ << "#line " << stmt->expr->args[1]->intValue << " \""
             << escapeString(stmt->expr->args[0]->stringValue) << "\"\n";
        return;
      }
      if (currentResult_ && isResultCall(stmt->expr.get())) {
        const std::string temporary =
            "__result" + std::to_string(tempCounter_++);
        indent();
        out_ << "auto " << temporary << " = ";
        emitExpr(stmt->expr.get());
        out_ << ";\n";
        indent();
        out_ << "if (!" << temporary << ".ok) { return " << temporary
             << "; }\n";
        return;
      }
      line("{");
      ++depth_;
      indent();
      emitExpr(stmt->expr.get());
      out_ << ";\n";
      emitShareEdgeRecord(stmt->expr.get());
      --depth_;
      line("}");
      return;
    case StmtKind::If:
      if (stmt->isLet) {
        const std::string temporary =
            "__let" + std::to_string(tempCounter_++);
        if (stmt->expr->resolvedType != nullptr &&
            stmt->expr->resolvedType->kind == TypeKind::Pointer) {
          indent();
          out_ << "if (auto " << temporary << " = ";
          emitExpr(stmt->expr.get());
          out_ << "; " << temporary << " != nullptr) {\n";
          ++depth_;
          indent();
          out_ << "auto " << sanitize(stmt->letName) << " = "
               << '*' << temporary << ";\n";
          emitStmt(stmt->thenBranch.get());
          --depth_;
          indent();
          out_ << "}";
          if (stmt->elseBranch) {
            out_ << " else ";
            emitStmt(stmt->elseBranch.get());
          } else {
            out_ << '\n';
          }
          return;
        }
        indent();
        out_ << "if (auto " << temporary << " = ";
        emitExpr(stmt->expr.get());
        out_ << "; " << temporary << ".ok) {\n";
        ++depth_;
        indent();
        out_ << "auto " << sanitize(stmt->letName) << " = " << temporary
             << ".value;\n";
        emitStmt(stmt->thenBranch.get());
        --depth_;
        indent();
        out_ << "}";
        if (stmt->elseBranch) {
          out_ << " else ";
          emitStmt(stmt->elseBranch.get());
        } else {
          out_ << '\n';
        }
        return;
      }
      indent();
      out_ << "if (";
      emitExpr(stmt->condition.get());
      out_ << ") ";
      emitStmt(stmt->thenBranch.get());
      if (stmt->elseBranch) {
        line("else");
        emitStmt(stmt->elseBranch.get());
      }
      return;
    case StmtKind::ForIn:
      indent();
      out_ << "for (const auto& " << sanitize(stmt->name) << " : ";
      emitExpr(stmt->expr.get());
      out_ << ") ";
      emitStmt(stmt->thenBranch.get());
      return;
    case StmtKind::Switch: {
      if (stmt->isMatch) {
        emitMatch(stmt);
        return;
      }
      const std::string temporary =
          "__switch" + std::to_string(tempCounter_++);
      indent();
      out_ << "{\n";
      ++depth_;
      indent();
      out_ << "auto " << temporary << " = ";
      emitExpr(stmt->condition.get());
      out_ << ";\n";
      bool first = true;
      for (std::size_t i = 0;
           i < stmt->switchValues.size() && i < stmt->switchBodies.size();
           ++i) {
        indent();
        if (stmt->switchValues[i] == nullptr) {
          out_ << "else ";
        } else {
          if (!first) {
            out_ << "else ";
          }
          out_ << "if (" << temporary << " == ";
          emitExpr(stmt->switchValues[i].get());
          out_ << ") ";
        }
        out_ << "{\n";
        ++depth_;
        if (stmt->switchBodies[i]->kind == StmtKind::Block) {
          emitBlock(stmt->switchBodies[i].get());
        } else {
          emitStmt(stmt->switchBodies[i].get());
        }
        --depth_;
        indent();
        out_ << "}\n";
        first = false;
      }
      --depth_;
      indent();
      out_ << "}\n";
      return;
    }
    case StmtKind::Destructure: {
      const std::string temporary =
          "__destructure" + std::to_string(tempCounter_++);
      if (stmt->optionalDestructure) {
        const std::string source = temporary + "_source";
        indent();
        out_ << "auto&& " << source << " = ";
        emitExpr(stmt->expr.get());
        out_ << ";\n";
        if (stmt->proxyPolicy == ProxyPolicy::Skip &&
            !stmt->optionalDestructureNarrowed) {
          for (const DestructureBinding& binding : stmt->destructureBindings) {
            indent();
            emitStorageType(binding.type.get(), false, 0);
            out_ << ' ' << sanitize(binding.name) << "{};\n";
          }
          return;
        }
        if (stmt->proxyPolicy == ProxyPolicy::Abort &&
            !stmt->optionalDestructureNarrowed) {
          indent();
          out_ << "coord_rt::optionalCheck(" << source
               << ", \"optional destructuring on a null value\");\n";
        }
        indent();
        out_ << "auto&& " << temporary << " = ";
        if (stmt->proxyPolicy == ProxyPolicy::Default &&
            !stmt->optionalDestructureNarrowed &&
            stmt->optionalDefaultRef != nullptr) {
          out_ << "((" << source << ") ? *" << source << " : (";
          emitExpr(stmt->optionalDefaultRef);
          out_ << "))";
        } else if (stmt->optionalDestructureNarrowed) {
          out_ << "*" << source;
        } else {
          out_ << "*" << source;
        }
        out_ << ";\n";
      } else {
        indent();
        out_ << "auto&& " << temporary << " = ";
        emitExpr(stmt->expr.get());
        out_ << ";\n";
      }
      const Decl* owner = proxyOwner(stmt->expr.get());
      const Decl* trap = proxyTrap(owner, "Destructure");
      const Decl* instanceTrap = proxyInstanceTrap(owner, "Destructure");
      const bool runtimeTrap = instanceTrap != nullptr;
      const Decl* selectedTrap =
          runtimeTrap ? instanceTrap : trap;
      if (selectedTrap == nullptr && owner != nullptr && owner->isProxy &&
          owner->proxyPolicy == ProxyPolicy::Abort) {
        indent();
        out_ << "coord_rt::optionalCheck(nullptr, \"proxy destructuring "
                "aborted\");\n";
      }
      const auto emitTrap = [&](const DestructureBinding& binding) {
        if (selectedTrap == nullptr) {
          return;
        }
        if (runtimeTrap && owner != nullptr) {
          const std::string handler = sanitize(owner->proxyHandler);
          indent();
          out_ << handler << "& __destructure_proxy = "
               << "coord_rt::proxyHandler<" << handler
               << ">((const void*)&" << temporary << ");\n";
          indent();
          out_ << "__destructure_proxy." << sanitize(selectedTrap->name)
               << '(';
          if (!selectedTrap->params.empty()) {
            out_ << '"' << escapeString(binding.name) << '"';
          }
          out_ << ");\n";
        } else {
          indent();
          out_ << trapCallName(owner, *selectedTrap) << '(';
          if (!selectedTrap->params.empty()) {
            out_ << '"' << escapeString(binding.name) << '"';
          }
          out_ << ");\n";
        }
      };
      if (stmt->destructureMethod != nullptr) {
        for (const DestructureBinding& binding :
             stmt->destructureBindings) {
          indent();
          emitStorageType(binding.type.get(), false, 0);
          out_ << ' ' << sanitize(binding.name) << "{};\n";
          emitTrap(binding);
        }
        indent();
        out_ << temporary << '.'
             << sanitize(stmt->destructureMethod->name) << '(';
        for (std::size_t i = 0; i < stmt->destructureBindings.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          out_ << sanitize(stmt->destructureBindings[i].name);
        }
        out_ << ");\n";
        return;
      }
      for (const DestructureBinding& binding : stmt->destructureBindings) {
        emitTrap(binding);
        indent();
        emitStorageType(binding.type.get(), false, 0);
        out_ << ' ' << sanitize(binding.name) << " = ";
        auto base = std::make_unique<Expr>();
        base->kind = ExprKind::Name;
        base->name = temporary;
        base->resolvedType = stmt->expr->resolvedType;
        Expr member;
        member.kind = ExprKind::Member;
        member.name = binding.name;
        member.lhs = std::move(base);
        emitExpr(&member);
        out_ << ";\n";
      }
      return;
    }
    case StmtKind::While:
      indent();
      out_ << "while (";
      emitExpr(stmt->condition.get());
      out_ << ") ";
      emitStmt(stmt->thenBranch.get());
      return;
    case StmtKind::Return:
      indent();
      out_ << "return";
      if (stmt->expr) {
        out_ << ' ';
        if (currentResult_ && isErrorCall(stmt->expr.get()) &&
            !stmt->expr->args.empty()) {
          out_ << "coord_rt::Result<" << currentReturnType_
               << ">::failure(";
          emitExpr(stmt->expr->args[0].get());
          out_ << ')';
        } else if (currentResult_ && !isResultCall(stmt->expr.get())) {
          out_ << "coord_rt::Result<" << currentReturnType_
               << ">::success(";
          emitExpr(stmt->expr.get());
          out_ << ')';
        } else if (currentCallable_ != nullptr &&
                   (currentCallable_->isUnique ||
                    currentCallable_->isShare) &&
                   stmt->expr->kind == ExprKind::Call &&
                   stmt->expr->lhs != nullptr &&
                   stmt->expr->lhs->kind == ExprKind::Name) {
          out_ << (currentCallable_->isUnique ? "std::make_unique<"
                                              : "coord_rt::makeShared<")
               << sanitize(stmt->expr->lhs->name) << ">(";
          for (std::size_t i = 0; i < stmt->expr->args.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitExpr(stmt->expr->args[i].get());
          }
          out_ << ')';
        } else {
          emitExpr(stmt->expr.get());
        }
      }
      out_ << ";\n";
      return;
    case StmtKind::Break:
      line("break;");
      return;
    case StmtKind::Continue:
      line("continue;");
      return;
    case StmtKind::Destroy:
      indent();
      out_ << "// destructor declaration erased; C++ destroys the member "
              "after the destructor body\n";
      return;
  }
}

bool CppEmitter::isResultCall(const Expr* expr) const {
  if (expr == nullptr || expr->kind != ExprKind::Call ||
      expr->lhs == nullptr || expr->lhs->kind != ExprKind::Name) {
    return false;
  }
  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Function || decl->name != expr->lhs->name ||
        decl->params.size() != expr->args.size()) {
      continue;
    }
    return decl->isResult || decl->isExpect;
  }
  return false;
}

bool CppEmitter::isErrorCall(const Expr* expr) const {
  return expr != nullptr && expr->kind == ExprKind::Call &&
         expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
         expr->lhs->name == "error";
}

void CppEmitter::emitExpr(const Expr* expr) {
  if (expr == nullptr) {
    return;
  }
  switch (expr->kind) {
    case ExprKind::IntLit:
      out_ << "static_cast<std::int64_t>("
           << formatCppInteger(expr->intValue, literalRadix_) << ')';
      return;
    case ExprKind::DecimalLit: {
      out_ << "coord_rt::decimalFromDouble(";
      std::ostringstream stream;
      stream << std::setprecision(17) << expr->decimalValue;
      out_ << stream.str() << ')';
      return;
    }
    case ExprKind::CharLit:
      out_ << charLiteral(expr->charValue);
      return;
    case ExprKind::BoolLit:
      out_ << (expr->boolValue ? "true" : "false");
      return;
    case ExprKind::NullLit:
      out_ << "nullptr";
      return;
    case ExprKind::StringLit:
      out_ << "std::string(\"" << escapeString(expr->stringValue) << "\")";
      return;
    case ExprKind::Name: {
      if (expr->optionalAccess) {
        const std::string name = sanitize(expr->name);
        if (expr->proxyPolicy == ProxyPolicy::Abort) {
          out_ << "(coord_rt::optionalAbort(" << name << ", \""
               << escapeString(expr->proxyMessage.empty()
                                   ? "optional value is null"
                                   : expr->proxyMessage)
               << "\"))";
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Skip) {
          out_ << "((" << name << ") ? *" << name
               << " : std::remove_reference_t<decltype(*" << name << ")>{} )";
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Default &&
            expr->optionalDefault != nullptr) {
          out_ << "((" << name << ") ? *" << name << " : (";
          emitExpr(expr->optionalDefault);
          out_ << "))";
          return;
        }
      }
      bool isLocal = false;
      for (const auto& scope : locals_) {
        if (scope.count(expr->name) != 0) {
          isLocal = true;
          break;
        }
      }
      if (!isLocal && fields_.count(expr->name) != 0) {
        out_ << "this->";
      }
      out_ << sanitize(expr->name);
      return;
    }
    case ExprKind::This:
      out_ << "(*this)";
      return;
    case ExprKind::Super:
      out_ << "(*this)";
      return;
    case ExprKind::Unary:
      if (expr->op == "-" && expr->lhs != nullptr &&
          expr->lhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int) {
        out_ << "coord_rt::checkedNeg(";
        emitExpr(expr->lhs.get());
        out_ << ", \"integer overflow\")";
        return;
      }
      if (expr->op == "*" && expr->optionalAccess) {
        const auto emitSource = [&]() { emitExpr(expr->lhs.get()); };
        if (expr->lhs != nullptr && expr->lhs->optionalCall) {
          if (expr->proxyPolicy == ProxyPolicy::Abort) {
            out_ << "coord_rt::optionalAbort(";
            emitSource();
            out_ << ", \"optional return value is null\")";
            return;
          }
          if (expr->proxyPolicy == ProxyPolicy::Skip) {
            out_ << "([&]() -> std::remove_reference_t<decltype(*";
            emitSource();
            out_ << ")> { auto&& __o = ";
            emitSource();
            out_ << "; return __o ? *__o : "
                    "std::remove_reference_t<decltype(*__o)>{}; }())";
            return;
          }
          if (expr->proxyPolicy == ProxyPolicy::Default &&
              expr->optionalDefault != nullptr) {
            out_ << "([&]() -> std::remove_reference_t<decltype(*";
            emitSource();
            out_ << ")> { auto&& __o = ";
            emitSource();
            out_ << "; return __o ? *__o : (";
            emitExpr(expr->optionalDefault);
            out_ << "); }())";
            return;
          }
        }
        if (expr->proxyPolicy == ProxyPolicy::Abort) {
          out_ << "(coord_rt::optionalAbort(";
          emitSource();
          out_ << ", \""
               << escapeString(expr->proxyMessage.empty()
                                   ? "optional value is null"
                                   : expr->proxyMessage)
               << "\"))";
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Skip) {
          out_ << "((";
          emitSource();
          out_ << ") ? *";
          emitSource();
          out_ << " : std::remove_reference_t<decltype(*";
          emitSource();
          out_ << ")>{} )";
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Default &&
            expr->optionalDefault != nullptr) {
          out_ << "((";
          emitSource();
          out_ << ") ? *";
          emitSource();
          out_ << " : (";
          emitExpr(expr->optionalDefault);
          out_ << "))";
          return;
        }
      }
      out_ << '(' << expr->op;
      emitExpr(expr->lhs.get());
      out_ << ')';
      return;
    case ExprKind::Binary:
      if ((expr->op == "/" || expr->op == "%") &&
          expr->lhs != nullptr && expr->rhs != nullptr &&
          expr->lhs->resolvedType != nullptr &&
          expr->rhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int &&
          expr->rhs->resolvedType->kind == TypeKind::Int) {
        out_ << (expr->op == "/" ? "coord_rt::checkedDiv("
                                 : "coord_rt::checkedMod(");
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"integer division by zero\")";
        return;
      }
      if ((expr->op == "<<" || expr->op == ">>") &&
          expr->lhs != nullptr && expr->rhs != nullptr &&
          expr->lhs->resolvedType != nullptr &&
          expr->rhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int &&
          expr->rhs->resolvedType->kind == TypeKind::Int) {
        out_ << (expr->op == "<<" ? "coord_rt::checkedShl("
                                  : "coord_rt::checkedShr(");
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"shift out of range\")";
        return;
      }
      if ((expr->op == "+" || expr->op == "-" || expr->op == "*") &&
          expr->lhs != nullptr && expr->rhs != nullptr &&
          expr->lhs->resolvedType != nullptr &&
          expr->rhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int &&
          expr->rhs->resolvedType->kind == TypeKind::Int) {
        out_ << (expr->op == "+" ? "coord_rt::checkedAdd("
                                 : expr->op == "-" ? "coord_rt::checkedSub("
                                                   : "coord_rt::checkedMul(");
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"integer overflow\")";
        return;
      }
      out_ << '(';
      emitExpr(expr->lhs.get());
      out_ << ' ' << expr->op << ' ';
      emitExpr(expr->rhs.get());
      out_ << ')';
      return;
    case ExprKind::Inc:
      if (expr->lhs != nullptr && expr->lhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int) {
        out_ << (expr->op == "pre" ? "coord_rt::checkedInc("
                                   : "coord_rt::checkedPostInc(");
        emitExpr(expr->lhs.get());
        out_ << ", " << (expr->intValue < 0 ? -1 : 1)
             << ", \"integer overflow\")";
        return;
      }
      out_ << '(';
      if (expr->op == "pre") {
        out_ << (expr->intValue < 0 ? "--" : "++");
        emitExpr(expr->lhs.get());
      } else {
        emitExpr(expr->lhs.get());
        out_ << (expr->intValue < 0 ? "--" : "++");
      }
      out_ << ')';
      return;
    case ExprKind::MethodRef: {
      if (expr->lhs == nullptr) {
        out_ << "nullptr";
        return;
      }
      if (expr->staticRef) {
        out_ << sanitize(expr->lhs->name) << "::" << sanitize(expr->name);
        return;
      }
      const std::string receiverName = expr->lhs->name;
      const std::string receiver = sanitize(receiverName);
      const bool smart = smartLocals_.count(receiverName) != 0;
      const std::size_t arity =
          expr->resolvedType != nullptr
              ? expr->resolvedType->alternatives.size()
              : 0;
      out_ << "[&" << receiver << "](";
      for (std::size_t i = 0; i < arity; ++i) {
        if (i != 0) {
          out_ << ", ";
        }
        out_ << "auto __arg" << i;
      }
      out_ << ") -> decltype(auto) { return " << receiver
           << (smart ? "->" : ".") << sanitize(expr->name) << '(';
      for (std::size_t i = 0; i < arity; ++i) {
        if (i != 0) {
          out_ << ", ";
        }
        out_ << "__arg" << i;
      }
      out_ << "); }";
      return;
    }
    case ExprKind::IfExpr: {
      if (expr->ifExpr == nullptr) {
        out_ << "0";
        return;
      }
      out_ << '(';
      emitExpr(expr->ifExpr->condition.get());
      out_ << " ? ";
      emitExpr(expr->ifExpr->thenValue.get());
      out_ << " : ";
      emitExpr(expr->ifExpr->elseValue.get());
      out_ << ')';
      return;
    }
    case ExprKind::Lambda: {
      const bool implicitCaptures =
          expr->lambda != nullptr && expr->lambda->captureList.empty();
      const std::string captures = !implicitCaptures
                                       ? expr->lambda->captureList
                                       : "[=]";
      out_ << captures << '(';
      if (expr->lambda != nullptr) {
        for (std::size_t i = 0; i < expr->lambda->params.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          out_ << typeToString(expr->lambda->params[i].type.get()) << ' '
               << sanitize(expr->lambda->params[i].name);
        }
      }
      out_ << ')';
      if (implicitCaptures) {
        // An omitted capture list copies by value and the copy is mutable.
        out_ << " mutable";
      }
      out_ << " -> ";
      if (expr->lambda != nullptr) {
        out_ << typeToString(expr->lambda->returnType.get());
        out_ << " {";
        if (expr->lambda->bodyExpr != nullptr) {
          out_ << " return ";
          emitExpr(expr->lambda->bodyExpr.get());
          out_ << ';';
        } else if (expr->lambda->body != nullptr) {
          emitStmt(expr->lambda->body.get());
        }
        out_ << '}';
      } else {
        out_ << "void {}";
      }
      return;
    }
    case ExprKind::ArrayLit:
      out_ << '{';
      for (std::size_t i = 0; i < expr->args.size(); ++i) {
        if (i != 0) {
          out_ << ", ";
        }
        emitExpr(expr->args[i].get());
      }
      out_ << '}';
      return;
    case ExprKind::Assign:
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Member &&
          expr->lhs->lhs != nullptr &&
          expr->lhs->lhs->kind == ExprKind::Name &&
          smartLocals_.count(expr->lhs->lhs->name) != 0) {
        out_ << '(' << sanitize(expr->lhs->lhs->name) << "->"
             << sanitize(expr->lhs->name) << ' ' << expr->op << ' ';
        emitExpr(expr->rhs.get());
        out_ << ')';
        return;
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Member &&
          expr->lhs->lhs != nullptr) {
        const Decl* owner = proxyOwner(expr->lhs->lhs.get());
        if (owner != nullptr && isProxyField(owner, expr->lhs->name)) {
          const Decl* runtimeSet = proxyInstanceTrap(owner, "Set");
          if (runtimeSet != nullptr) {
            const bool isPointer =
                (expr->lhs->lhs->resolvedType != nullptr &&
                 expr->lhs->lhs->resolvedType->kind == TypeKind::Pointer) ||
                expr->lhs->lhs->kind == ExprKind::This;
            out_ << "([&]() { auto&& __self = ";
            emitExpr(expr->lhs->lhs.get());
            out_ << "; auto __value = ";
            emitExpr(expr->rhs.get());
            out_ << "; " << sanitize(owner->proxyHandler)
                 << "& __proxy = coord_rt::proxyHandler<"
                 << sanitize(owner->proxyHandler) << ">((const void*)";
            if (isPointer) {
              out_ << "__self";
            } else {
              out_ << "&__self";
            }
            out_ << "); __proxy." << sanitize(runtimeSet->name) << '(';
            if (!runtimeSet->params.empty()) {
              out_ << "__value";
            }
            out_ << "); __self" << (isPointer ? "->" : ".")
                 << sanitize(expr->lhs->name)
                 << " = std::move(__value); }())";
            return;
          }
          const Decl* setTrap = proxyTrap(owner, "Set");
          if (setTrap != nullptr) {
            out_ << '(' << trapCallName(owner, *setTrap) << '(';
            if (setTrap->params.size() == 1) {
              emitExpr(expr->rhs.get());
            }
            out_ << "), ";
          } else {
            out_ << '(';
          }
          emitExpr(expr->lhs->lhs.get());
          out_ << '.' << sanitize(expr->lhs->name) << ' ' << expr->op << ' ';
          emitExpr(expr->rhs.get());
          out_ << ')';
          return;
        }
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
          expr->rhs != nullptr && expr->rhs->kind == ExprKind::Call &&
          expr->rhs->lhs != nullptr &&
          expr->rhs->lhs->kind == ExprKind::Name) {
        const Decl* field = fieldOf(currentClassDecl_, expr->lhs->name);
        if (field != nullptr && (field->isUnique || field->isShare)) {
          out_ << '(' << sanitize(expr->lhs->name) << " = "
               << (field->isUnique ? "std::make_unique<"
                                   : "coord_rt::makeShared<")
               << sanitize(expr->rhs->lhs->name) << ">(";
          for (std::size_t i = 0; i < expr->rhs->args.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitExpr(expr->rhs->args[i].get());
          }
          out_ << "))";
          return;
        }
      }
      if (expr->lhs != nullptr && expr->rhs != nullptr &&
          expr->lhs->resolvedType != nullptr &&
          expr->rhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::Int &&
          expr->rhs->resolvedType->kind == TypeKind::Int &&
          (expr->op == "+=" || expr->op == "-=" || expr->op == "*=" ||
           expr->op == "/=" || expr->op == "%=")) {
        out_ << "coord_rt::checked"
             << (expr->op == "+=" ? "Add" : expr->op == "-=" ? "Sub"
                                  : expr->op == "*=" ? "Mul"
                                  : expr->op == "/=" ? "Div"
                                                   : "Mod")
             << "Assign(";
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"integer overflow\")";
        return;
      }
      out_ << '(';
      emitExpr(expr->lhs.get());
      out_ << ' ' << expr->op << ' ';
      emitExpr(expr->rhs.get());
      out_ << ')';
      return;
    case ExprKind::Call: {
      const Expr* callee = expr->lhs.get();
      if (callee != nullptr && callee->kind == ExprKind::Name) {
        const std::string& builtin = callee->name;
        if (builtin == "readFile" || builtin == "writeFile" ||
            builtin == "fileExists" || builtin == "deleteFile" ||
            builtin == "timeMillis" || builtin == "sleepMillis" ||
            builtin == "operatingSystem" || builtin == "architecture" ||
            builtin == "targetTriple" || builtin == "pathSeparator" ||
            builtin == "lineSeparator" || builtin == "executableSuffix" ||
            builtin == "currentDirectory" || builtin == "temporaryDirectory" ||
            builtin == "homeDirectory" || builtin == "environment" ||
            builtin == "setEnvironment" || builtin == "pathJoin" ||
            builtin == "pathFileName" || builtin == "pathParent" ||
            builtin == "pathExtension" || builtin == "pathIsAbsolute" ||
            builtin == "pathNormalize" || builtin == "pathExists" ||
            builtin == "createDirectory" || builtin == "createDirectories" ||
            builtin == "deleteDirectory" || builtin == "listDirectory") {
          out_ << "coord_rt::" << builtin << '(';
          for (std::size_t i = 0; i < expr->args.size(); ++i) {
            if (i != 0) {
              out_ << ", ";
            }
            emitExpr(expr->args[i].get());
          }
          out_ << ')';
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->name == "getClass" && expr->args.empty()) {
        const std::string typeName =
            callee->lhs != nullptr && callee->lhs->resolvedType != nullptr
                ? callee->lhs->resolvedType->name
                : "Object";
        out_ << "Class{\"" << typeName << "\"}";
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          expr->args.empty() &&
          (callee->name == "getSuperClass" ||
           callee->name == "getSupperClass")) {
        out_ << "Class{\"Object\"}";
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          expr->args.empty() &&
          (callee->name == "getDescription" ||
           callee->name == "getDescriptor")) {
        if (callee->name == "getDescription") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").name";
          return;
        }
        Type* owner = callee->lhs != nullptr ? callee->lhs->resolvedType
                                             : nullptr;
        if (owner != nullptr && owner->name == "Class" &&
            callee->lhs->kind == ExprKind::Call &&
            callee->lhs->lhs != nullptr &&
            callee->lhs->lhs->kind == ExprKind::Member &&
            callee->lhs->lhs->name == "getClass") {
          owner = callee->lhs->lhs->lhs != nullptr
                      ? callee->lhs->lhs->lhs->resolvedType
                      : nullptr;
        }
        if (owner != nullptr && owner->name == "Class") {
          out_ << "Descriptor{(";
          emitExpr(callee->lhs.get());
          out_ << ").name, {}, {}}";
        } else if (owner != nullptr && owner->decl != nullptr) {
          out_ << "Descriptor{\"" << owner->name << "\", {";
          for (std::size_t i = 0; i < owner->fields.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << '"' << owner->fields[i].name << '"';
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->fields.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << '"' << owner->fields[i].type->name << '"';
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->fields.size(); ++i) {
            if (i != 0) out_ << ", ";
            int mask = 0;
            if (owner->fields[i].access == Access::Public) mask |= 1;
            if (owner->fields[i].access == Access::Private) mask |= 2;
            if (owner->fields[i].access == Access::Protected) mask |= 4;
            out_ << mask;
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << '"' << owner->methods[i].name << '"';
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << '"'
                 << (owner->methods[i].returnType != nullptr
                         ? owner->methods[i].returnType->name
                         : "void")
                 << '"';
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << owner->methods[i].paramTypes.size();
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            out_ << '"';
            for (std::size_t j = 0; j < owner->methods[i].paramTypes.size();
                 ++j) {
              if (j != 0) out_ << ',';
              out_ << owner->methods[i].paramTypes[j]->name;
            }
            out_ << '"';
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            int mask = 0;
            if (owner->methods[i].access == Access::Public) mask |= 1;
            if (owner->methods[i].access == Access::Private) mask |= 2;
            if (owner->methods[i].access == Access::Protected) mask |= 4;
            if (owner->methods[i].isVirtual) mask |= 32;
            if (owner->methods[i].isFinal) mask |= 16;
            out_ << mask;
          }
          out_ << "}, {";
          for (std::size_t i = 0; i < owner->methods.size(); ++i) {
            if (i != 0) out_ << ", ";
            std::string doc;
            if (owner->decl != nullptr) {
              for (const auto& member : owner->decl->members) {
                if (member->name == owner->methods[i].name) {
                  doc = member->doc;
                  break;
                }
              }
            }
            out_ << '"';
            for (char c : doc) {
              if (c == '\n' || c == '\r') continue;
              if (c == '"' || c == '\\') out_ << '\\';
              out_ << c;
            }
            out_ << '"';
          }
          out_ << "}, \"";
          if (owner->decl != nullptr) {
            for (char c : owner->decl->doc) {
              if (c == '\n' || c == '\r') {
                continue;
              } else if (c == '"' || c == '\\') {
                out_ << '\\' << c;
              } else {
                out_ << c;
              }
            }
          }
          out_ << "\", {";
          if (owner->base != nullptr && owner->base->name != "class" &&
              owner->base->name != "Object") {
            out_ << '"' << owner->base->name << '"';
          }
          for (std::size_t i = 0; i < owner->interfaces.size(); ++i) {
            if (i != 0 || (owner->base != nullptr &&
                           owner->base->name != "class" &&
                           owner->base->name != "Object")) {
              out_ << ", ";
            }
            out_ << '"' << owner->interfaces[i]->name << '"';
          }
          out_ << "}}";
        } else {
          out_ << "Descriptor{(";
          emitExpr(callee->lhs.get());
          out_ << ").name, {}, {}, {}, {}, {}, {}, {}, {}, {}}";
        }
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->kind == ExprKind::Name) {
        const auto smart = smartLocals_.find(callee->lhs->name);
        if (smart != smartLocals_.end() && (smart->second & 4) != 0) {
          const std::string& name = callee->name;
          if (name == "isPresent") {
            out_ << "((" << sanitize(callee->lhs->name) << ") != nullptr)";
            return;
          }
          if (name == "isEmpty") {
            out_ << "((" << sanitize(callee->lhs->name) << ") == nullptr)";
            return;
          }
          if (name == "get") {
            out_ << "(*" << sanitize(callee->lhs->name) << ")";
            return;
          }
          if (name == "orElse" && expr->args.size() == 1) {
            out_ << "((" << sanitize(callee->lhs->name)
                 << ") != nullptr ? (*" << sanitize(callee->lhs->name)
                 << ") : (";
            emitExpr(expr->args[0].get());
            out_ << "))";
            return;
          }
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Reflector) {
        Type* target = callee->lhs->resolvedType->alternatives.empty()
                           ? nullptr
                           : callee->lhs->resolvedType->alternatives[0];
        if (target == nullptr || target->decl == nullptr) {
          out_ << "0";
          return;
        }
        const descriptor::Declaration* info = nullptr;
        for (const auto& candidate : descriptors_) {
          if (candidate.origin == target->decl) {
            info = &candidate;
            break;
          }
        }
        if (info == nullptr) {
          out_ << "0";
          return;
        }
        const std::string& method = callee->name;
        if (method == "name") {
          out_ << "std::string(\"" << escapeString(info->name) << "\")";
          return;
        }
        if (method == "kind") {
          out_ << "std::string(\"" << escapeString(info->kind) << "\")";
          return;
        }
        if (method == "annotationCount") {
          out_ << info->annotations.size();
          return;
        }
        if (method == "fieldCount") {
          out_ << info->fields.size();
          return;
        }
        if (method == "methodCount") {
          out_ << info->methods.size();
          return;
        }
        if (method == "fieldName" || method == "fieldType") {
          std::string value;
          const std::int64_t index = expr->args[0]->intValue;
          if (index >= 0 &&
              static_cast<std::size_t>(index) < info->fields.size()) {
            const descriptor::Field& field =
                info->fields[static_cast<std::size_t>(index)];
            value = method == "fieldName" ? field.name : field.type;
          }
          out_ << "std::string(\"" << escapeString(value) << "\")";
          return;
        }
        if (method == "methodName" || method == "methodReturnType") {
          std::string value;
          const std::int64_t index = expr->args[0]->intValue;
          if (index >= 0 &&
              static_cast<std::size_t>(index) < info->methods.size()) {
            const descriptor::Method& item =
                info->methods[static_cast<std::size_t>(index)];
            value = method == "methodName" ? item.name : item.returnType;
          }
          out_ << "std::string(\"" << escapeString(value) << "\")";
          return;
        }
        if (method == "methodParamCount") {
          const std::int64_t index = expr->args[0]->intValue;
          if (index >= 0 &&
              static_cast<std::size_t>(index) < info->methods.size()) {
            out_ << info->methods[static_cast<std::size_t>(index)]
                        .parameterTypes.size();
          } else {
            out_ << 0;
          }
          return;
        }
        if (method == "methodParamType") {
          std::string value;
          const std::int64_t methodIndex = expr->args[0]->intValue;
          const std::int64_t paramIndex = expr->args[1]->intValue;
          if (methodIndex >= 0 &&
              static_cast<std::size_t>(methodIndex) < info->methods.size()) {
            const descriptor::Method& item =
                info->methods[static_cast<std::size_t>(methodIndex)];
            if (paramIndex >= 0 &&
                static_cast<std::size_t>(paramIndex) <
                    item.parameterTypes.size()) {
              value = item.parameterTypes[static_cast<std::size_t>(paramIndex)];
            }
          }
          out_ << "std::string(\"" << escapeString(value) << "\")";
          return;
        }
        if (method == "annotationName") {
          std::string value;
          const std::int64_t index = expr->args[0]->intValue;
          if (index >= 0 &&
              static_cast<std::size_t>(index) < info->annotations.size()) {
            value = info->annotations[static_cast<std::size_t>(index)];
          }
          out_ << "std::string(\"" << escapeString(value) << "\")";
          return;
        }
        if (method == "hasAnnotation" && expr->args.size() == 1 &&
            expr->args[0]->kind == ExprKind::StringLit) {
          bool found = false;
          for (const std::string& annotation : info->annotations) {
            if (annotation == expr->args[0]->stringValue) {
              found = true;
            }
          }
          out_ << (found ? "true" : "false");
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Name) {
        const Decl* invocationTarget = nullptr;
        for (const auto& decl : module_.decls) {
          if (decl->kind == DeclKind::Function &&
              decl->name == callee->name && decl->isInvocation) {
            invocationTarget = decl.get();
            break;
          }
        }
        if (invocationTarget != nullptr) {
          const bool isVoid = invocationTarget->type == nullptr ||
                              invocationTarget->type->name == "void";
          struct InvocationInfo {
            const Decl* before = nullptr;
            const Decl* after = nullptr;
            std::string name;
          };
          std::vector<InvocationInfo> infos;
          bool beforeArgs = false;
          bool afterResult = false;
          for (const std::string& handlerName :
               invocationTarget->invocationHandlers) {
            const Decl* handlerDecl = nullptr;
            for (const auto& candidate : module_.decls) {
              if (candidate->kind == DeclKind::Class &&
                  candidate->name == handlerName) {
                handlerDecl = candidate.get();
                break;
              }
            }
            if (handlerDecl == nullptr) {
              continue;
            }
            InvocationInfo info;
            info.name = sanitize(handlerName);
            for (const auto& member : handlerDecl->members) {
              if (member->kind != DeclKind::Method) {
                continue;
              }
              if (member->name == "before") {
                info.before = member.get();
                if (!member->params.empty()) {
                  beforeArgs = true;
                }
              } else if (member->name == "after") {
                info.after = member.get();
                if (!member->params.empty()) {
                  afterResult = true;
                }
              }
            }
            infos.push_back(std::move(info));
          }
          out_ << "([&]() { ";
          for (std::size_t i = 0; i < infos.size(); ++i) {
            out_ << infos[i].name << " __invocation" << i << "; ";
          }
          if (beforeArgs) {
            for (std::size_t i = 0; i < expr->args.size(); ++i) {
              out_ << "auto __arg" << i << " = ";
              emitExpr(expr->args[i].get());
              out_ << "; ";
            }
          }
          for (std::size_t i = 0; i < infos.size(); ++i) {
            if (infos[i].before == nullptr) {
              continue;
            }
            out_ << "__invocation" << i << ".before(";
            if (!infos[i].before->params.empty()) {
              for (std::size_t j = 0; j < expr->args.size(); ++j) {
                if (j != 0) {
                  out_ << ", ";
                }
                out_ << "__arg" << j;
              }
            }
            out_ << "); ";
          }
          const auto emitCallArgs = [&]() {
            for (std::size_t i = 0; i < expr->args.size(); ++i) {
              if (i != 0) {
                out_ << ", ";
              }
              if (beforeArgs) {
                out_ << "__arg" << i;
              } else {
                emitExpr(expr->args[i].get());
              }
            }
          };
          if (isVoid) {
            out_ << sanitize(callee->name) << '(';
            emitCallArgs();
            out_ << "); ";
            for (std::size_t i = infos.size(); i > 0; --i) {
              if (infos[i - 1].after != nullptr) {
                out_ << "__invocation" << (i - 1) << ".after(); ";
              }
            }
            out_ << "}())";
          } else {
            out_ << "auto __result = " << sanitize(callee->name) << '(';
            emitCallArgs();
            out_ << "); ";
            for (std::size_t i = infos.size(); i > 0; --i) {
              if (infos[i - 1].after == nullptr) {
                continue;
              }
              out_ << "__invocation" << (i - 1) << ".after(";
              if (afterResult && !infos[i - 1].after->params.empty()) {
                out_ << "__result";
              }
              out_ << "); ";
            }
            out_ << "return __result; }())";
          }
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Class &&
          callee->lhs->resolvedType->decl != nullptr) {
        const Decl* owner = callee->lhs->resolvedType->decl;
        const Decl* methodDecl = nullptr;
        for (const auto& member : owner->members) {
          if (member->kind == DeclKind::Method &&
              member->name == callee->name && member->isInvocation &&
              member->params.size() == expr->args.size()) {
            methodDecl = member.get();
            break;
          }
        }
        const Decl* proxyMethod = nullptr;
        for (const auto& member : owner->members) {
          if (member->kind == DeclKind::Method &&
              member->name == callee->name && member->isProxy &&
              member->params.size() == expr->args.size()) {
            proxyMethod = member.get();
            break;
          }
        }
        if (proxyMethod != nullptr) {
          const Decl* handlerDecl = nullptr;
          for (const auto& candidate : module_.decls) {
            if (candidate->kind == DeclKind::Class &&
                candidate->name == proxyMethod->proxyHandler) {
              handlerDecl = candidate.get();
              break;
            }
          }
          const Decl* trap = nullptr;
          if (handlerDecl != nullptr) {
            for (const auto& member : handlerDecl->members) {
              if (member->kind != DeclKind::Method) {
                continue;
              }
              for (const AnnotationUse& use : member->annotations) {
                if (use.name == "Call") {
                  trap = member.get();
                }
              }
            }
          }
          if (trap != nullptr) {
            const bool isVoid = proxyMethod->type == nullptr ||
                                proxyMethod->type->name == "void";
            const bool isPointer =
                callee->lhs->resolvedType->kind == TypeKind::Pointer;
            out_ << "([&]() { auto&& __self = ";
            emitExpr(callee->lhs.get());
            out_ << "; ";
            if (trap->isStatic) {
              out_ << sanitize(handlerDecl->name) << "::";
            } else {
              out_ << sanitize(handlerDecl->name)
                   << "& __proxy = coord_rt::proxyHandler<"
                   << sanitize(handlerDecl->name) << ">((const void*)";
              if (isPointer) {
                out_ << "__self";
              } else {
                out_ << "&__self";
              }
              out_ << "); __proxy.";
            }
            out_ << sanitize(trap->name) << '(';
            if (!trap->params.empty()) {
              out_ << '"' << escapeString(proxyMethod->name) << '"';
            }
            out_ << "); ";
            if (isVoid) {
              out_ << "__self" << (isPointer ? "->" : ".")
                   << sanitize(callee->name) << '(';
              for (std::size_t i = 0; i < expr->args.size(); ++i) {
                if (i != 0) {
                  out_ << ", ";
                }
                emitExpr(expr->args[i].get());
              }
              out_ << "); }())";
            } else {
              out_ << "auto __result = __self" << (isPointer ? "->" : ".")
                   << sanitize(callee->name) << '(';
              for (std::size_t i = 0; i < expr->args.size(); ++i) {
                if (i != 0) {
                  out_ << ", ";
                }
                emitExpr(expr->args[i].get());
              }
              out_ << "); return __result; }())";
            }
            return;
          }
        }
        if (owner->isProxy && !owner->proxyHandler.empty()) {
          const Decl* handlerDecl = nullptr;
          for (const auto& candidate : module_.decls) {
            if (candidate->kind == DeclKind::Class &&
                candidate->name == owner->proxyHandler) {
              handlerDecl = candidate.get();
              break;
            }
          }
          const Decl* trap = nullptr;
          if (handlerDecl != nullptr) {
            for (const auto& member : handlerDecl->members) {
              if (member->kind != DeclKind::Method || member->isStatic) {
                continue;
              }
              for (const AnnotationUse& use : member->annotations) {
                if (use.name == "Call") {
                  trap = member.get();
                }
              }
            }
          }
          if (trap != nullptr) {
            const Decl* classMethod = nullptr;
            for (const auto& member : owner->members) {
              if (member->kind == DeclKind::Method &&
                  member->name == callee->name &&
                  member->params.size() == expr->args.size()) {
                classMethod = member.get();
                break;
              }
            }
            const bool isVoid = classMethod == nullptr ||
                                classMethod->type == nullptr ||
                                classMethod->type->name == "void";
            const bool isPointer =
                callee->lhs->resolvedType->kind == TypeKind::Pointer;
            out_ << "([&]() { auto&& __self = ";
            emitExpr(callee->lhs.get());
            out_ << "; " << sanitize(handlerDecl->name)
                 << "& __proxy = coord_rt::proxyHandler<"
                 << sanitize(handlerDecl->name) << ">((const void*)";
            if (isPointer) {
              out_ << "__self";
            } else {
              out_ << "&__self";
            }
            out_ << "); __proxy." << sanitize(trap->name) << '(';
            if (!trap->params.empty()) {
              out_ << '"' << escapeString(callee->name) << '"';
            }
            out_ << "); ";
            if (isVoid) {
              out_ << "__self" << (isPointer ? "->" : ".")
                   << sanitize(callee->name) << '(';
              for (std::size_t i = 0; i < expr->args.size(); ++i) {
                if (i != 0) {
                  out_ << ", ";
                }
                emitExpr(expr->args[i].get());
              }
              out_ << "); }())";
            } else {
              out_ << "auto __result = __self" << (isPointer ? "->" : ".")
                   << sanitize(callee->name) << '(';
              for (std::size_t i = 0; i < expr->args.size(); ++i) {
                if (i != 0) {
                  out_ << ", ";
                }
                emitExpr(expr->args[i].get());
              }
              out_ << "); return __result; }())";
            }
            return;
          }
        }
        if (methodDecl != nullptr) {
          const bool isVoid = methodDecl->type == nullptr ||
                              methodDecl->type->name == "void";
          const bool isPointer =
              callee->lhs->resolvedType->kind == TypeKind::Pointer;
          struct MethodInvocationInfo {
            const Decl* before = nullptr;
            const Decl* after = nullptr;
            std::string name;
          };
          std::vector<MethodInvocationInfo> infos;
          bool beforeArgs = false;
          bool afterResult = false;
          for (const std::string& handlerName :
               methodDecl->invocationHandlers) {
            const Decl* handlerDecl = nullptr;
            for (const auto& candidate : module_.decls) {
              if (candidate->kind == DeclKind::Class &&
                  candidate->name == handlerName) {
                handlerDecl = candidate.get();
                break;
              }
            }
            if (handlerDecl == nullptr) {
              continue;
            }
            MethodInvocationInfo info;
            info.name = sanitize(handlerName);
            for (const auto& member : handlerDecl->members) {
              if (member->kind != DeclKind::Method) {
                continue;
              }
              if (member->name == "before") {
                info.before = member.get();
                if (!member->params.empty()) {
                  beforeArgs = true;
                }
              } else if (member->name == "after") {
                info.after = member.get();
                if (!member->params.empty()) {
                  afterResult = true;
                }
              }
            }
            infos.push_back(std::move(info));
          }
          out_ << "([&]() { auto&& __self = ";
          emitExpr(callee->lhs.get());
          out_ << "; ";
          for (std::size_t i = 0; i < infos.size(); ++i) {
            out_ << infos[i].name << " __invocation" << i << "; ";
          }
          if (beforeArgs) {
            for (std::size_t i = 0; i < expr->args.size(); ++i) {
              out_ << "auto __arg" << i << " = ";
              emitExpr(expr->args[i].get());
              out_ << "; ";
            }
          }
          for (std::size_t i = 0; i < infos.size(); ++i) {
            if (infos[i].before == nullptr) {
              continue;
            }
            out_ << "__invocation" << i << ".before(";
            if (!infos[i].before->params.empty()) {
              for (std::size_t j = 0; j < expr->args.size(); ++j) {
                if (j != 0) {
                  out_ << ", ";
                }
                out_ << "__arg" << j;
              }
            }
            out_ << "); ";
          }
          const auto emitArgs = [&]() {
            for (std::size_t i = 0; i < expr->args.size(); ++i) {
              if (i != 0) {
                out_ << ", ";
              }
              if (beforeArgs) {
                out_ << "__arg" << i;
              } else {
                emitExpr(expr->args[i].get());
              }
            }
          };
          if (isVoid) {
            out_ << "__self" << (isPointer ? "->" : ".")
                 << sanitize(callee->name) << '(';
            emitArgs();
            out_ << "); ";
            for (std::size_t i = infos.size(); i > 0; --i) {
              if (infos[i - 1].after != nullptr) {
                out_ << "__invocation" << (i - 1) << ".after(); ";
              }
            }
            out_ << "}())";
          } else {
            out_ << "auto __result = __self" << (isPointer ? "->" : ".")
                 << sanitize(callee->name) << '(';
            emitArgs();
            out_ << "); ";
            for (std::size_t i = infos.size(); i > 0; --i) {
              if (infos[i - 1].after == nullptr) {
                continue;
              }
              out_ << "__invocation" << (i - 1) << ".after(";
              if (afterResult && !infos[i - 1].after->params.empty()) {
                out_ << "__result";
              }
              out_ << "); ";
            }
            out_ << "return __result; }())";
          }
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          (callee->name == "runtimeFieldCount" ||
           callee->name == "runtimeMethodCount" ||
           callee->name == "runtimeBase" ||
           callee->name == "runtimeFieldName" ||
           callee->name == "runtimeMethodName" ||
           callee->name == "runtimeFieldType" ||
           callee->name == "runtimeMethodType" ||
           callee->name == "runtimeHasMember")) {
        out_ << "coord_meta::" << callee->name << '(';
        for (std::size_t i = 0; i < expr->args.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          emitExpr(expr->args[i].get());
        }
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "runtimeAnnotationValue" &&
          expr->args.size() == 2) {
        out_ << "coord_meta::annotationValue(";
        emitExpr(expr->args[0].get());
        out_ << ", ";
        emitExpr(expr->args[1].get());
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "hasRuntimeAnnotation" &&
          expr->args.size() == 2) {
        out_ << "coord_meta::hasAnnotation(";
        emitExpr(expr->args[0].get());
        out_ << ", ";
        emitExpr(expr->args[1].get());
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "assert" && expr->args.size() == 1) {
        out_ << "coord_rt::check(";
        emitExpr(expr->args[0].get());
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "assertTrue" && expr->args.size() == 1) {
        out_ << "coord_rt::check(";
        emitExpr(expr->args[0].get());
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "assertFalse" && expr->args.size() == 1) {
        out_ << "coord_rt::check(!(";
        emitExpr(expr->args[0].get());
        out_ << "))";
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "assertEq" && expr->args.size() == 2) {
        out_ << "coord_rt::checkEq(";
        emitExpr(expr->args[0].get());
        out_ << ", ";
        emitExpr(expr->args[1].get());
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Set) {
        const std::string& name = callee->name;
        if (name == "add" && expr->args.size() == 1) {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").insert(";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "contains" && expr->args.size() == 1) {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ").find(";
          emitExpr(expr->args[0].get());
          out_ << ") != (";
          emitExpr(callee->lhs.get());
          out_ << ").end())";
          return;
        }
        if (name == "remove" && expr->args.size() == 1) {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").erase(";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "length") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").size()";
          return;
        }
        if (name == "isEmpty") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").empty()";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Map) {
        const std::string& name = callee->name;
        if (name == "set" && expr->args.size() == 2) {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").insert_or_assign(";
          emitExpr(expr->args[0].get());
          out_ << ", ";
          emitExpr(expr->args[1].get());
          out_ << ')';
          return;
        }
        if (name == "get" && expr->args.size() == 1) {
          out_ << "coord_rt::mapFind(";
          emitExpr(callee->lhs.get());
          out_ << ", ";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "contains" && expr->args.size() == 1) {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ").find(";
          emitExpr(expr->args[0].get());
          out_ << ") != (";
          emitExpr(callee->lhs.get());
          out_ << ").end())";
          return;
        }
        if (name == "remove" && expr->args.size() == 1) {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").erase(";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "length") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").size()";
          return;
        }
        if (name == "isEmpty") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").empty()";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Queue) {
        const std::string& name = callee->name;
        if (name == "view") {
          out_ << "coord_rt::makeArrayView(";
          emitExpr(callee->lhs.get());
          out_ << ')';
          return;
        }
        if ((name == "pushBack" || name == "pushFront") &&
            expr->args.size() == 1) {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ")." << (name == "pushBack" ? "push_back" : "push_front")
               << '(';
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "popFront") {
          out_ << "coord_rt::queuePopFront(";
          emitExpr(callee->lhs.get());
          out_ << ')';
          return;
        }
        if (name == "popBack") {
          out_ << "coord_rt::queuePopBack(";
          emitExpr(callee->lhs.get());
          out_ << ')';
          return;
        }
        if (name == "length") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").size()";
          return;
        }
        if (name == "isEmpty") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").empty()";
          return;
        }
        if (name == "clear") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").clear()";
          return;
        }
        if (name == "get" || name == "at") {
          out_ << "coord_rt::arrayGet(";
          emitExpr(callee->lhs.get());
          out_ << ", ";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::ArrayView) {
        const std::string& name = callee->name;
        if (name == "get" || name == "at") {
          out_ << "coord_rt::arrayViewGet(";
          emitExpr(callee->lhs.get());
          out_ << ", ";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
        if (name == "length") {
          out_ << '(';
          emitExpr(callee->lhs.get());
          out_ << ").size";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ").size == 0)";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->optionalCall) {
        const std::string& name = callee->name;
        if (name == "isPresent") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") != nullptr)";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") == nullptr)";
          return;
        }
        if (name == "get") {
          out_ << "coord_rt::optionalAbort(";
          emitExpr(callee->lhs.get());
          out_ << ", \"optional return value is null\")";
          return;
        }
        if (name == "orElse" && expr->args.size() == 1) {
          out_ << "([&]() -> std::remove_reference_t<decltype(*";
          emitExpr(callee->lhs.get());
          out_ << ")> { auto&& __o = ";
          emitExpr(callee->lhs.get());
          out_ << "; return __o ? *__o : (";
          emitExpr(expr->args[0].get());
          out_ << "); }())";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->optionalField) {
        const std::string& name = callee->name;
        if (name == "isPresent") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") != nullptr)";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") == nullptr)";
          return;
        }
        if (name == "get") {
          out_ << "coord_rt::optionalAbort(";
          emitExpr(callee->lhs.get());
          out_ << ", \"optional field is null\")";
          return;
        }
        if (name == "orElse" && expr->args.size() == 1) {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") ? *";
          emitExpr(callee->lhs.get());
          out_ << " : (";
          emitExpr(expr->args[0].get());
          out_ << "))";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->weakValue) {
        const std::string& name = callee->name;
        if (name == "isPresent") {
          out_ << "(!(";
          emitExpr(callee->lhs.get());
          out_ << ").expired())";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ").expired())";
          return;
        }
        if (name == "lock" || name == "upgrade") {
          out_ << "(";
          emitExpr(callee->lhs.get());
          out_ << ").lock()";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::SharePtr) {
        const std::string& name = callee->name;
        if (name == "isPresent") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") != nullptr)";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") == nullptr)";
          return;
        }
        if (name == "get") {
          out_ << "coord_rt::optionalAbort(";
          emitExpr(callee->lhs.get());
          out_ << ", \"weak reference expired\")";
          return;
        }
        if (name == "orElse" && expr->args.size() == 1) {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") ? *";
          emitExpr(callee->lhs.get());
          out_ << " : (";
          emitExpr(expr->args[0].get());
          out_ << "))";
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Pointer) {
        const std::string& name = callee->name;
        if (name == "isPresent") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") != nullptr)";
          return;
        }
        if (name == "isEmpty") {
          out_ << "((";
          emitExpr(callee->lhs.get());
          out_ << ") == nullptr)";
          return;
        }
        if (name == "get") {
          out_ << "coord_rt::optionalAbort(";
          emitExpr(callee->lhs.get());
          out_ << ", \"optional value is null\")";
          return;
        }
        if (name == "orElse" && expr->args.size() == 1) {
          out_ << "coord_rt::optionalOrElse(";
          emitExpr(callee->lhs.get());
          out_ << ", ";
          emitExpr(expr->args[0].get());
          out_ << ')';
          return;
        }
      }
      if (callee != nullptr && callee->kind == ExprKind::Member &&
          callee->lhs != nullptr && callee->lhs->resolvedType != nullptr &&
          callee->lhs->resolvedType->kind == TypeKind::Function &&
          (callee->name == "apply" || callee->name == "accept" ||
           callee->name == "test" || callee->name == "get" ||
           callee->name == "run" || callee->name == "call")) {
        emitExpr(callee->lhs.get());
        out_ << '(';
        for (std::size_t i = 0; i < expr->args.size(); ++i) {
          if (i != 0) {
            out_ << ", ";
          }
          emitExpr(expr->args[i].get());
        }
        out_ << ')';
        return;
      }
      if (callee != nullptr && callee->kind == ExprKind::Name &&
          callee->name == "print") {
        out_ << "coord_rt::print";
      } else {
        emitExpr(callee);
      }
      out_ << '(';
      const Decl* callable = callableOf(callee);
      for (std::size_t i = 0; i < expr->args.size(); ++i) {
        if (i != 0) {
          out_ << ", ";
        }
        const Param* parameter =
            callable != nullptr && i < callable->params.size()
                ? &callable->params[i]
                : nullptr;
        if (parameter != nullptr &&
            (parameter->isUnique || parameter->isShare) &&
            expr->args[i]->kind == ExprKind::Call &&
            expr->args[i]->lhs != nullptr &&
            expr->args[i]->lhs->kind == ExprKind::Name) {
          out_ << (parameter->isUnique ? "std::make_unique<"
                                       : "coord_rt::makeShared<")
               << sanitize(expr->args[i]->lhs->name) << ">(";
          for (std::size_t j = 0; j < expr->args[i]->args.size(); ++j) {
            if (j != 0) {
              out_ << ", ";
            }
            emitExpr(expr->args[i]->args[j].get());
          }
          out_ << ')';
        } else if (parameter != nullptr &&
                   (parameter->isUnique || parameter->isShare) &&
                   expr->args[i]->kind == ExprKind::Name &&
                   smartLocals_.count(expr->args[i]->name) != 0) {
          out_ << "std::move(" << sanitize(expr->args[i]->name) << ')';
        } else {
          emitExpr(expr->args[i].get());
        }
      }
      out_ << ')';
      return;
    }
    case ExprKind::Member: {
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
          module_.importAliases.count(expr->lhs->name) != 0) {
        out_ << sanitize(expr->name);
        return;
      }
      if (expr->optionalAccess && !expr->optionalNarrowed &&
          expr->lhs != nullptr) {
        const std::string member = sanitize(expr->name);
        const auto emitSource = [&]() { emitExpr(expr->lhs.get()); };
        if (expr->lhs->optionalCall) {
          if (expr->proxyPolicy == ProxyPolicy::Abort) {
            out_ << "(coord_rt::optionalAbort(";
            emitSource();
            out_ << ", \"optional return value is null\")." << member
                 << ')';
            return;
          }
          if (expr->proxyPolicy == ProxyPolicy::Skip) {
            out_ << "([&]() -> std::remove_reference_t<decltype(*";
            emitSource();
            out_ << ")> { auto&& __o = ";
            emitSource();
            out_ << "; return __o ? *__o : "
                    "std::remove_reference_t<decltype(*__o)>{}; }()).";
            out_ << member;
            return;
          }
          if (expr->proxyPolicy == ProxyPolicy::Default &&
              expr->optionalDefault != nullptr) {
            out_ << "([&]() -> std::remove_reference_t<decltype(*";
            emitSource();
            out_ << ")> { auto&& __o = ";
            emitSource();
            out_ << "; return __o ? *__o : (";
            emitExpr(expr->optionalDefault);
            out_ << "); }())." << member;
            return;
          }
        }
        if (expr->proxyPolicy == ProxyPolicy::Abort) {
          out_ << "(coord_rt::optionalAbort(";
          emitSource();
          out_ << ", \""
               << escapeString(expr->proxyMessage.empty()
                                   ? "optional value is null"
                                   : expr->proxyMessage)
               << "\")." << member << ')';
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Skip) {
          out_ << "((";
          emitSource();
          out_ << ") ? (*";
          emitSource();
          out_ << ") : std::remove_reference_t<decltype(*";
          emitSource();
          out_ << ")>{})." << member;
          return;
        }
        if (expr->proxyPolicy == ProxyPolicy::Default &&
            expr->optionalDefault != nullptr) {
          out_ << "((";
          emitSource();
          out_ << ") ? (*";
          emitSource();
          out_ << ") : (";
          emitExpr(expr->optionalDefault);
          out_ << "))." << member;
          return;
        }
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
          smartLocals_.count(expr->lhs->name) != 0) {
        out_ << sanitize(expr->lhs->name) << "->"
             << sanitize(expr->name);
        return;
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name) {
        const Decl* field = fieldOf(currentClassDecl_, expr->lhs->name);
        if (field != nullptr && (field->isUnique || field->isShare)) {
          out_ << sanitize(expr->lhs->name) << "->"
               << sanitize(expr->name);
          return;
        }
        const Param* param =
            paramOf(currentCallable_, expr->lhs->name);
        if (param != nullptr && (param->isUnique || param->isShare)) {
          out_ << sanitize(expr->lhs->name) << "->"
               << sanitize(expr->name);
          return;
        }
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Member) {
        const Expr* inner = expr->lhs.get();
        const Decl* field = nullptr;
        if (inner->lhs != nullptr && inner->lhs->kind == ExprKind::Name) {
          field = fieldOf(currentClassDecl_, inner->lhs->name);
        } else if (inner->lhs != nullptr &&
                   inner->lhs->kind == ExprKind::This) {
          field = fieldOf(currentClassDecl_, inner->name);
        } else if (inner->lhs != nullptr &&
                   inner->lhs->resolvedType != nullptr &&
                   inner->lhs->resolvedType->decl != nullptr) {
          field = fieldOf(inner->lhs->resolvedType->decl, inner->name);
        }
        if (field != nullptr && (field->isUnique || field->isShare)) {
          emitExpr(inner);
          out_ << "->" << sanitize(expr->name);
          return;
        }
      }
      const Decl* owner = proxyOwner(expr->lhs.get());
      if (owner != nullptr && isProxyField(owner, expr->name)) {
        const Decl* runtimeGet = proxyInstanceTrap(owner, "Get");
        if (runtimeGet != nullptr) {
          const bool isPointer =
              (expr->lhs->resolvedType != nullptr &&
               expr->lhs->resolvedType->kind == TypeKind::Pointer) ||
              expr->lhs->kind == ExprKind::This;
          out_ << "([&]() -> decltype(auto) { auto&& __self = ";
          emitExpr(expr->lhs.get());
          out_ << "; " << sanitize(owner->proxyHandler)
               << "& __proxy = coord_rt::proxyHandler<"
               << sanitize(owner->proxyHandler) << ">((const void*)";
          if (isPointer) {
            out_ << "__self";
          } else {
            out_ << "&__self";
          }
          out_ << "); __proxy." << sanitize(runtimeGet->name) << '(';
          if (!runtimeGet->params.empty()) {
            out_ << '"' << escapeString(expr->name) << '"';
          }
          out_ << "); return (__self" << (isPointer ? "->" : ".")
               << sanitize(expr->name) << "); }())";
          return;
        }
      }
      const Decl* getTrap = proxyTrap(owner, "Get");
      if (getTrap != nullptr && isProxyField(owner, expr->name)) {
        out_ << '(' << trapCallName(owner, *getTrap) << '(';
        if (getTrap->params.size() == 1) {
          out_ << '"' << escapeString(expr->name) << '"';
        }
        out_ << "), ";
        emitExpr(expr->lhs.get());
        out_ << '.' << sanitize(expr->name) << ')';
        return;
      }
      if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Super) {
        out_ << "this->" << currentBase_ << "::" << sanitize(expr->name);
      } else if (expr->resolvedType != nullptr &&
                 expr->resolvedType->kind == TypeKind::Enum &&
                 expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name) {
        out_ << sanitize(expr->lhs->name) << "::" << sanitize(expr->name);
      } else if (expr->lhs != nullptr &&
                 expr->lhs->kind == ExprKind::This) {
        out_ << "this->" << sanitize(expr->name);
      } else {
        emitExpr(expr->lhs.get());
        out_ << '.' << sanitize(expr->name);
      }
      return;
    }
    case ExprKind::Index:
      if (expr->lhs != nullptr && expr->lhs->resolvedType != nullptr &&
          (expr->lhs->resolvedType->kind == TypeKind::Array ||
           expr->lhs->resolvedType->kind == TypeKind::Queue)) {
        out_ << "coord_rt::checkedIndex(";
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"index out of range\")";
        return;
      }
      if (expr->lhs != nullptr && expr->lhs->resolvedType != nullptr &&
          expr->lhs->resolvedType->kind == TypeKind::ArrayView) {
        out_ << "coord_rt::checkedIndexView(";
        emitExpr(expr->lhs.get());
        out_ << ", ";
        emitExpr(expr->rhs.get());
        out_ << ", \"index out of range\")";
        return;
      }
      emitExpr(expr->lhs.get());
      out_ << '[';
      emitExpr(expr->rhs.get());
      out_ << ']';
      return;
  }
}

const Decl* CppEmitter::proxyOwner(const Expr* base) const {
  if (base == nullptr || base->resolvedType == nullptr ||
      base->resolvedType->kind != TypeKind::Class) {
    return nullptr;
  }
  const Decl* decl = base->resolvedType->decl;
  return decl != nullptr && decl->isProxy ? decl : nullptr;
}

const Decl* CppEmitter::proxyTrap(const Decl* owner,
                                  const std::string& annotation) const {
  if (owner == nullptr || owner->proxyHandler.empty()) {
    return nullptr;
  }
  for (const auto& decl : module_.decls) {
    if ((decl->kind != DeclKind::Class &&
         decl->kind != DeclKind::Annotation) ||
        decl->name != owner->proxyHandler) {
      continue;
    }
    for (const auto& member : decl->members) {
      if (member->kind != DeclKind::Method) {
        continue;
      }
      if (decl->kind == DeclKind::Class &&
          (!member->isStatic || member->params.size() > 1)) {
        continue;
      }
      for (const AnnotationUse& use : member->annotations) {
        if (use.name == annotation) {
          return member.get();
        }
      }
    }
  }
  return nullptr;
}

const Decl* CppEmitter::fieldOf(const Decl* owner,
                                const std::string& name) const {
  if (owner == nullptr) {
    return nullptr;
  }
  for (const auto& member : owner->members) {
    if (member->kind == DeclKind::Field && member->name == name) {
      return member.get();
    }
  }
  return nullptr;
}

const Param* CppEmitter::paramOf(const Decl* callable,
                                 const std::string& name) const {
  if (callable == nullptr) {
    return nullptr;
  }
  for (const Param& param : callable->params) {
    if (param.name == name) {
      return &param;
    }
  }
  return nullptr;
}

const Decl* CppEmitter::callableOf(const Expr* callee) const {
  if (callee == nullptr) {
    return nullptr;
  }
  if (callee->kind == ExprKind::Name) {
    for (const auto& decl : module_.decls) {
      if (decl->kind == DeclKind::Function && decl->name == callee->name) {
        return decl.get();
      }
    }
    return nullptr;
  }
  if (callee->kind == ExprKind::Member && callee->lhs != nullptr &&
      callee->lhs->resolvedType != nullptr &&
      callee->lhs->resolvedType->decl != nullptr) {
    const Decl* owner = callee->lhs->resolvedType->decl;
    for (const auto& member : owner->members) {
      if (member->kind == DeclKind::Method &&
          member->name == callee->name) {
        return member.get();
      }
    }
  }
  return nullptr;
}

bool CppEmitter::proxyRuntime(const Decl* owner) const {
  if (owner == nullptr || owner->proxyHandler.empty()) {
    return false;
  }
  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Class ||
        decl->name != owner->proxyHandler) {
      continue;
    }
    for (const AnnotationUse& use : decl->annotations) {
      if (use.name == "Retention") {
        return true;
      }
    }
  }
  return false;
}

const Decl* CppEmitter::proxyInstanceTrap(
    const Decl* owner, const std::string& annotation) const {
  if (!proxyRuntime(owner)) {
    return nullptr;
  }
  if (owner == nullptr || owner->proxyHandler.empty()) {
    return nullptr;
  }
  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Class ||
        decl->name != owner->proxyHandler) {
      continue;
    }
    for (const auto& member : decl->members) {
      if (member->kind != DeclKind::Method || member->isStatic) {
        continue;
      }
      for (const AnnotationUse& use : member->annotations) {
        if (use.name == annotation) {
          return member.get();
        }
      }
    }
  }
  return nullptr;
}

std::string CppEmitter::trapCallName(const Decl* owner,
                                     const Decl& trap) const {
  if (owner == nullptr) {
    return sanitize(trap.name);
  }
  if (owner->kind == DeclKind::Annotation) {
    return sanitize(owner->name) + pascalCase(sanitize(trap.name));
  }
  if (owner->proxyHandler.empty()) {
    return sanitize(owner->name) + "::" + sanitize(trap.name);
  }
  for (const auto& decl : module_.decls) {
    if (decl->name != owner->proxyHandler) {
      continue;
    }
    if (decl->kind == DeclKind::Annotation) {
      return sanitize(decl->name) + pascalCase(sanitize(trap.name));
    }
    return sanitize(decl->name) + "::" + sanitize(trap.name);
  }
  return sanitize(trap.name);
}

bool CppEmitter::isProxyField(const Decl* owner,
                              const std::string& name) const {
  if (owner == nullptr) {
    return false;
  }
  for (const auto& member : owner->members) {
    if (member->kind == DeclKind::Field && member->name == name) {
      return true;
    }
  }
  return false;
}

void CppEmitter::emitAnnotationTraps(const Decl& decl) {
  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Method || member->body == nullptr) {
      continue;
    }
    bool trap = false;
    for (const AnnotationUse& use : member->annotations) {
      if (use.name == "Call" || use.name == "Get" || use.name == "Set") {
        trap = true;
      }
    }
    if (!trap || (member->type != nullptr &&
                  member->type->name != "void")) {
      continue;
    }
    out_ << "inline void " << sanitize(decl.name)
         << pascalCase(sanitize(member->name));
    emitParams(member->params);
    out_ << " {\n";
    depth_ = 1;
    locals_.clear();
    locals_.emplace_back();
    for (const Param& param : member->params) {
      locals_.back().insert(param.name);
    }
    emitBlock(member->body.get());
    locals_.pop_back();
    depth_ = 0;
    out_ << "}\n\n";
  }
}

void CppEmitter::emitRuntimeReflectionTables() {
  struct MemberRecord {
    std::string declaration;
    std::string kind;
    std::string name;
    std::string type;
  };
  struct BaseRecord {
    std::string declaration;
    std::string base;
  };
  std::map<std::string, const descriptor::Declaration*> index;
  for (const auto& declaration : descriptors_) {
    index[declaration.name] = &declaration;
  }
  std::vector<MemberRecord> members;
  std::vector<BaseRecord> bases;

  std::function<void(const std::string&, const std::string&,
                     std::set<std::string>&)>
      collect = [&](const std::string& name, const std::string& target,
                    std::set<std::string>& visited) {
        if (!visited.insert(name).second) {
          return;
        }
        const auto found = index.find(name);
        if (found == index.end() || found->second == nullptr) {
          return;
        }
        const descriptor::Declaration& declaration = *found->second;
        if (!declaration.base.empty()) {
          collect(declaration.base, target, visited);
        }
        for (const descriptor::Field& field : declaration.fields) {
          members.push_back(
              MemberRecord{target, "field", field.name, field.type});
        }
        for (const descriptor::Method& method : declaration.methods) {
          members.push_back(
              MemberRecord{target, "method", method.name, method.returnType});
        }
      };

  for (const auto& declaration : descriptors_) {
    if (declaration.kind != "class" && declaration.kind != "interface" &&
        declaration.kind != "annotation" && declaration.kind != "enumerate") {
      continue;
    }
    std::set<std::string> visited;
    collect(declaration.name, declaration.name, visited);
    if (!declaration.base.empty()) {
      bases.push_back(BaseRecord{declaration.name, declaration.base});
    }
  }

  out_ << "namespace coord_meta {\n";
  if (!members.empty()) {
    out_ << "struct MemberRecord {\n";
    out_ << "  const char* declaration;\n";
    out_ << "  const char* kind;\n";
    out_ << "  const char* name;\n";
    out_ << "  const char* type;\n";
    out_ << "};\n";
    out_ << "inline constexpr MemberRecord kRuntimeMembers[] = {\n";
    for (const MemberRecord& member : members) {
      out_ << "  {\"" << escapeString(member.declaration) << "\", \""
           << member.kind << "\", \"" << escapeString(member.name) << "\", \""
           << escapeString(member.type) << "\"},\n";
    }
    out_ << "};\n";
  }
  if (!bases.empty()) {
    out_ << "struct BaseRecord {\n";
    out_ << "  const char* declaration;\n";
    out_ << "  const char* base;\n";
    out_ << "};\n";
    out_ << "inline constexpr BaseRecord kRuntimeBases[] = {\n";
    for (const BaseRecord& base : bases) {
      out_ << "  {\"" << escapeString(base.declaration) << "\", \""
           << escapeString(base.base) << "\"},\n";
    }
    out_ << "};\n";
  }

  const auto emitCount = [&](const char* name, const char* kind) {
    out_ << "inline int " << name << "(const std::string& declaration) {\n";
    if (!members.empty()) {
      out_ << "  int count = 0;\n";
      out_ << "  for (const MemberRecord& member : kRuntimeMembers) {\n";
      out_ << "    if (declaration == member.declaration &&\n";
      out_ << "        std::string(member.kind) == \"" << kind << "\") {\n";
      out_ << "      ++count;\n";
      out_ << "    }\n";
      out_ << "  }\n";
      out_ << "  return count;\n";
    } else {
      out_ << "  (void)declaration;\n";
      out_ << "  return 0;\n";
    }
    out_ << "}\n";
  };
  emitCount("runtimeFieldCount", "field");
  emitCount("runtimeMethodCount", "method");

  const auto emitName = [&](const char* name, const char* kind) {
    out_ << "inline std::string " << name
         << "(const std::string& declaration, int index) {\n";
    if (!members.empty()) {
      out_ << "  int current = 0;\n";
      out_ << "  for (const MemberRecord& member : kRuntimeMembers) {\n";
      out_ << "    if (declaration == member.declaration &&\n";
      out_ << "        std::string(member.kind) == \"" << kind << "\") {\n";
      out_ << "      if (current == index) return member.name;\n";
      out_ << "      ++current;\n";
      out_ << "    }\n";
      out_ << "  }\n";
    } else {
      out_ << "  (void)declaration;\n";
      out_ << "  (void)index;\n";
    }
    out_ << "  return {};\n";
    out_ << "}\n";
  };
  emitName("runtimeFieldName", "field");
  emitName("runtimeMethodName", "method");

  const auto emitType = [&](const char* name, const char* kind) {
    out_ << "inline std::string " << name
         << "(const std::string& declaration, int index) {\n";
    if (!members.empty()) {
      out_ << "  int current = 0;\n";
      out_ << "  for (const MemberRecord& member : kRuntimeMembers) {\n";
      out_ << "    if (declaration == member.declaration &&\n";
      out_ << "        std::string(member.kind) == \"" << kind << "\") {\n";
      out_ << "      if (current == index) return member.type;\n";
      out_ << "      ++current;\n";
      out_ << "    }\n";
      out_ << "  }\n";
    } else {
      out_ << "  (void)declaration;\n";
      out_ << "  (void)index;\n";
    }
    out_ << "  return {};\n";
    out_ << "}\n";
  };
  emitType("runtimeFieldType", "field");
  emitType("runtimeMethodType", "method");

  out_ << "inline bool runtimeHasMember(const std::string& declaration,\n";
  out_ << "                              const std::string& member) {\n";
  if (!members.empty()) {
    out_ << "  for (const MemberRecord& record : kRuntimeMembers) {\n";
    out_ << "    if (declaration == record.declaration &&\n";
    out_ << "        member == record.name) {\n";
    out_ << "      return true;\n";
    out_ << "    }\n";
    out_ << "  }\n";
  } else {
    out_ << "  (void)declaration;\n";
    out_ << "  (void)member;\n";
  }
  out_ << "  return false;\n";
  out_ << "}\n";

  out_ << "inline std::string runtimeBase(const std::string& declaration) {\n";
  if (!bases.empty()) {
    out_ << "  for (const BaseRecord& record : kRuntimeBases) {\n";
    out_ << "    if (declaration == record.declaration) {\n";
    out_ << "      return record.base;\n";
    out_ << "    }\n";
    out_ << "  }\n";
  } else {
    out_ << "  (void)declaration;\n";
  }
  out_ << "  return {};\n";
  out_ << "}\n";
  out_ << "}  // namespace coord_meta\n\n";
}

void CppEmitter::emitRetentionTables() {
  std::set<std::string> retained;
  for (const auto& declaration : descriptors_) {
    if (declaration.kind != "annotation") {
      continue;
    }
    for (const std::string& annotation : declaration.annotations) {
      if (annotation == "Retention") {
        retained.insert(declaration.name);
      }
    }
  }

  struct Record {
    std::string declaration;
    std::string annotation;
    std::string value;
  };
  std::vector<Record> records;
  const auto add = [&](const std::string& path,
                       const std::vector<std::string>& annotations,
                       const std::vector<std::string>& values) {
    for (std::size_t i = 0; i < annotations.size(); ++i) {
      if (annotations[i] == "Retention") {
        continue;
      }
      if (retained.find(annotations[i]) != retained.end()) {
        records.push_back(Record{path, annotations[i],
                                 i < values.size() ? values[i] : ""});
      }
    }
  };

  for (const auto& declaration : descriptors_) {
    add(declaration.name, declaration.annotations,
        declaration.annotationValues);
    for (const descriptor::Field& field : declaration.fields) {
      add(declaration.name + "::" + field.name, field.annotations,
          field.annotationValues);
    }
    for (const descriptor::Method& method : declaration.methods) {
      add(declaration.name + "::" + method.name, method.annotations,
          method.annotationValues);
    }
    for (const descriptor::Variant& variant : declaration.variants) {
      add(declaration.name + "::" + variant.name, variant.annotations,
          variant.annotationValues);
    }
  }
  if (!records.empty()) {
    out_ << "namespace coord_meta {\n";
    out_ << "struct AnnotationRecord {\n";
    out_ << "  const char* declaration;\n";
    out_ << "  const char* annotation;\n";
    out_ << "  const char* value;\n";
    out_ << "};\n";
    out_ << "inline constexpr AnnotationRecord kRetainedAnnotations[] = {\n";
    for (const Record& record : records) {
      out_ << "  {\"" << record.declaration << "\", \"" << record.annotation
           << "\", \"" << escapeString(record.value) << "\"},\n";
    }
    out_ << "};\n";
    out_ << "inline bool hasAnnotation(const std::string& declaration,\n";
    out_ << "                          const std::string& annotation) {\n";
    out_ << "  for (const AnnotationRecord& record : kRetainedAnnotations) {\n";
    out_ << "    if (declaration == record.declaration &&\n";
    out_ << "        annotation == record.annotation) {\n";
    out_ << "      return true;\n";
    out_ << "    }\n";
    out_ << "  }\n";
    out_ << "  return false;\n";
    out_ << "}\n";
    out_ << "inline std::string annotationValue(\n";
    out_ << "    const std::string& declaration,\n";
    out_ << "    const std::string& annotation) {\n";
    out_ << "  for (const AnnotationRecord& record : kRetainedAnnotations) {\n";
    out_ << "    if (declaration == record.declaration &&\n";
    out_ << "        annotation == record.annotation) {\n";
    out_ << "      return record.value;\n";
    out_ << "    }\n";
    out_ << "  }\n";
    out_ << "  return {};\n";
    out_ << "}\n";
    out_ << "}  // namespace coord_meta\n\n";
  } else {
    out_ << "namespace coord_meta {\n";
    out_ << "inline bool hasAnnotation(const std::string&,\n";
    out_ << "                          const std::string&) {\n";
    out_ << "  return false;\n";
    out_ << "}\n";
    out_ << "inline std::string annotationValue(const std::string&,\n";
    out_ << "                                   const std::string&) {\n";
    out_ << "  return {};\n";
    out_ << "}\n";
    out_ << "}  // namespace coord_meta\n\n";
  }

  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Enum || !decl->retainEnum) {
      continue;
    }
    out_ << "namespace coord_meta {\n";
    out_ << "struct EnumVariantRecord {\n";
    out_ << "  const char* name;\n";
    out_ << "  std::int64_t value;\n";
    out_ << "  std::int64_t ordinal;\n";
    out_ << "};\n";
    out_ << "inline constexpr EnumVariantRecord k" << decl->name
         << "Variants[] = {\n";
    for (std::size_t i = 0; i < decl->variants.size(); ++i) {
      out_ << "  {\"" << decl->variants[i].name << "\", "
           << decl->variants[i].resolvedValue << ", " << i << "},\n";
    }
    out_ << "};\n";
    out_ << "}  // namespace coord_meta\n\n";
  }
}

void CppEmitter::emitTestRunner() {
  out_ << "int coord_test_main() {\n";
  depth_ = 1;
  bool any = false;
  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Function || !decl->isTest) {
      continue;
    }
    if (!testFilter_.empty() &&
        decl->name.find(testFilter_) == std::string::npos) {
      continue;
    }
    any = true;
    line("std::cout << \"[test] " + decl->name + "\\n\";");
    line(sanitize(decl->name) + "();");
  }
  if (!any) {
    line("std::cout << \"no tests\\n\";");
  }
  line("return 0;");
  depth_ = 0;
  out_ << "}\n";
}

void CppEmitter::emit() {
  descriptors_ = descriptor::Builder::build(module_);
  emitPrelude();

  for (const auto& decl : module_.decls) {
    switch (decl->kind) {
      case DeclKind::Class:
        emitClass(*decl);
        break;
      case DeclKind::Enum:
        emitEnum(*decl);
        break;
      case DeclKind::Interface: {
        bool functional = false;
        for (const AnnotationUse& use : decl->annotations) {
          if (use.name == "FunctionalInterface") {
            functional = true;
            break;
          }
        }
        if (!functional) {
          emitInterface(*decl);
        }
        break;
      }
      case DeclKind::Function:
        if (decl->name != "main") {
          emitFunction(*decl);
        }
        break;
      case DeclKind::Const:
        emitConst(*decl);
        break;
      case DeclKind::Annotation:
        if (decl->isCore || decl->isStandard) {
          break;
        }
        emitAnnotationTraps(*decl);
        out_ << "// annotation '" << decl->name
             << "' is compile-time only except its proxy trap hooks.\n\n";
        break;
      default:
        break;
    }
  }

  if (testMode_) {
    emitTestRunner();
  } else {
    emitMainWrapper();
  }
  emitRetentionTables();
  emitRuntimeReflectionTables();
  out_ << "}  // namespace coord_program\n\n";

  if (testMode_) {
    out_ << "int main() {\n";
    out_ << "  coord_rt::enableUtf8Console();\n";
    out_ << "  const int __exit = coord_program::coord_test_main();\n";
    out_ << "  coord_rt::reportShareLeaks();\n";
    out_ << "  return __exit;\n";
    out_ << "}\n";
    return;
  }

  const Decl* mainDecl = nullptr;
  for (const auto& decl : module_.decls) {
    if (decl->kind == DeclKind::Function && decl->name == "main") {
      mainDecl = decl.get();
      break;
    }
  }
  const bool mainHasArgs =
      mainDecl != nullptr && !mainDecl->params.empty();
  const bool mainArgcForm =
      mainDecl != nullptr && mainDecl->params.size() == 2;
  out_ << "namespace {\n";
  out_ << "int coord_entry(const std::deque<std::string>& __coord_args) {\n";
  const auto emitMainCall = [&]() {
    out_ << "coord_program::coord_main(";
    if (mainHasArgs) {
      if (mainArgcForm) {
        out_ << "static_cast<int>(__coord_args.size()), ";
      }
      out_ << "__coord_args";
    }
    out_ << ')';
  };
  if (mainDecl != nullptr && mainDecl->isResult) {
    out_ << "  auto __main_result = ";
    emitMainCall();
    out_ << ";\n";
    out_ << "  if (!__main_result.ok) {\n";
    out_ << "    std::cerr << __main_result.error << '\\n';\n";
    out_ << "    return 1;\n";
    out_ << "  }\n";
    out_ << "  const int __exit = static_cast<int>(__main_result.value);\n";
    out_ << "  coord_rt::reportShareLeaks();\n";
    out_ << "  return __exit;\n";
  } else {
    out_ << "  const int __exit = static_cast<int>(";
    emitMainCall();
    out_ << ");\n";
    out_ << "  coord_rt::reportShareLeaks();\n";
    out_ << "  return __exit;\n";
  }
  out_ << "}\n";
  out_ << "}  // namespace\n\n";
  out_ << "// Windows/MSVC uses wmain so argv is always UTF-8 in the program.\n";
  out_ << "#if defined(_WIN32) && defined(_MSC_VER)\n";
  out_ << "int wmain(int argc, wchar_t** argv) {\n";
  out_ << "  coord_rt::enableUtf8Console();\n";
  out_ << "  std::deque<std::string> __coord_args;\n";
  out_ << "  for (int i = 0; i < argc; ++i) {\n";
  out_ << "    __coord_args.emplace_back(coord_rt::wideToUtf8(argv[i]));\n";
  out_ << "  }\n";
  out_ << "  return coord_entry(__coord_args);\n";
  out_ << "}\n";
  out_ << "#else\n";
  out_ << "int main(int argc, char** argv) {\n";
  out_ << "  coord_rt::enableUtf8Console();\n";
  out_ << "  std::deque<std::string> __coord_args;\n";
  out_ << "  for (int i = 0; i < argc; ++i) {\n";
  out_ << "    __coord_args.emplace_back(argv[i]);\n";
  out_ << "  }\n";
  out_ << "  return coord_entry(__coord_args);\n";
  out_ << "}\n";
  out_ << "#endif\n";
}

}  // namespace coord
