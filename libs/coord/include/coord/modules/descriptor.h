#pragma once

#include "coord/ast.h"
#include "coord/modules/access.h"

#include <string>
#include <vector>

namespace coord::descriptor {

struct Field {
  const Decl* origin = nullptr;
  std::string name;
  std::string type;
  std::string access;
  std::string signature;
  Span span;
  bool isConst = false;
  std::vector<std::string> annotations;
  std::vector<std::string> annotationValues;
};

struct Method {
  const Decl* origin = nullptr;
  std::string name;
  std::string returnType;
  std::vector<std::string> parameterTypes;
  std::vector<std::string> parameterNames;
  std::string access;
  std::string signature;
  Span span;
  bool isStatic = false;
  bool isVirtual = false;
  bool isOverride = false;
  bool isFinal = false;
  bool isConstMethod = false;
  std::vector<std::string> annotations;
  std::vector<std::string> annotationValues;
};

struct Variant {
  const EnumVariant* origin = nullptr;
  std::string name;
  std::int64_t value = 0;
  Span span;
  std::vector<std::string> annotations;
  std::vector<std::string> annotationValues;
};

struct Declaration {
  const Decl* origin = nullptr;
  std::string name;
  std::string kind;
  std::string signature;
  Span span;
  std::string base;
  std::vector<std::string> interfaces;
  bool isProxy = false;
  std::string proxyHandler;
  bool isFinal = false;
  std::vector<std::string> annotations;
  std::vector<std::string> annotationValues;
  std::vector<Field> fields;
  std::vector<Method> methods;
  std::vector<Method> constructors;
  std::vector<Method> destructors;
  std::vector<Variant> variants;
};

/// AST-derived, read-only declaration model. This is the single source that
/// outline, hint, reflector and description should consume; R7 exposes the
/// model and uses it for consistency tests, R8 moves codegen consumers over.
class Builder {
 public:
  [[nodiscard]] static std::vector<Declaration> build(
      const ModuleAst& module) {
    std::vector<Declaration> declarations;
    for (const auto& decl : module.decls) {
      if (decl->isCore || decl->isStandard) {
        continue;
      }
      if (decl->kind != DeclKind::Class &&
          decl->kind != DeclKind::Interface &&
          decl->kind != DeclKind::Annotation &&
          decl->kind != DeclKind::Enum &&
          decl->kind != DeclKind::Function &&
          decl->kind != DeclKind::Const) {
        continue;
      }
      Declaration descriptor;
      descriptor.origin = decl.get();
      descriptor.name = decl->name;
      descriptor.kind = kindName(decl->kind);
      descriptor.span = decl->span;
      descriptor.signature = signatureText(*decl);
      descriptor.base = decl->base != nullptr
                            ? describeType(decl->base.get())
                            : std::string();
      for (const auto& iface : decl->interfaces) {
        descriptor.interfaces.push_back(
            iface != nullptr ? describeType(iface.get()) : std::string());
      }
      descriptor.isProxy = decl->isProxy;
      descriptor.proxyHandler = decl->proxyHandler;
      descriptor.isFinal = decl->isFinal;
      appendAnnotations(decl->annotations, descriptor.annotations,
                        descriptor.annotationValues);
      for (const auto& member : decl->members) {
        if (member->kind == DeclKind::Field) {
          Field field;
          field.origin = member.get();
          field.name = member->name;
          field.type = member->type != nullptr
                           ? describeType(member->type.get())
                           : "void";
          field.access = accessName(member->access);
          field.signature = field.type + " " + field.name;
          field.span = member->span;
          field.isConst = member->isConstField;
          appendAnnotations(member->annotations, field.annotations,
                            field.annotationValues);
          descriptor.fields.push_back(std::move(field));
        } else if (member->kind == DeclKind::Method) {
          Method method;
          method.origin = member.get();
          method.name = member->name;
          method.returnType = member->type != nullptr
                                  ? describeType(member->type.get())
                                  : "void";
          for (const Param& param : member->params) {
            method.parameterTypes.push_back(
                param.type != nullptr ? describeType(param.type.get())
                                      : "void");
            method.parameterNames.push_back(param.name);
          }
          method.access = accessName(member->access);
          method.signature =
              method.returnType + " " + method.name + "(" +
              parameterText(member->params) + ")";
          method.span = member->span;
          method.isStatic = member->isStatic;
          method.isVirtual = member->isVirtual;
          method.isOverride = member->isOverride;
          method.isFinal = member->isFinal;
          method.isConstMethod = member->isConstMethod;
          appendAnnotations(member->annotations, method.annotations,
                            method.annotationValues);
          descriptor.methods.push_back(std::move(method));
        } else if (member->kind == DeclKind::Constructor) {
          Method constructor;
          constructor.origin = member.get();
          constructor.name = member->name;
          constructor.returnType = "void";
          for (const Param& param : member->params) {
            constructor.parameterTypes.push_back(
                param.type != nullptr ? describeType(param.type.get())
                                      : "void");
            constructor.parameterNames.push_back(param.name);
          }
          constructor.access = accessName(member->access);
          constructor.signature =
              "void " + constructor.name + "(" +
              parameterText(member->params) + ")";
          constructor.span = member->span;
          appendAnnotations(member->annotations, constructor.annotations,
                            constructor.annotationValues);
          descriptor.constructors.push_back(std::move(constructor));
        } else if (member->kind == DeclKind::Destructor) {
          if (member->synthesizedDestructor) {
            continue;
          }
          Method destructor;
          destructor.origin = member.get();
          destructor.name = member->name;
          destructor.returnType = "void";
          destructor.access = accessName(member->access);
          destructor.signature = "void " + destructor.name + "()";
          destructor.span = member->span;
          appendAnnotations(member->annotations, destructor.annotations,
                            destructor.annotationValues);
          descriptor.destructors.push_back(std::move(destructor));
        }
      }
      for (const EnumVariant& source : decl->variants) {
        Variant variant;
        variant.origin = &source;
        variant.name = source.name;
        variant.value = source.resolvedValue;
        variant.span = source.span;
        appendAnnotations(source.annotations, variant.annotations,
                          variant.annotationValues);
        descriptor.variants.push_back(std::move(variant));
      }
      declarations.push_back(std::move(descriptor));
    }
    return declarations;
  }

 private:
  static void appendAnnotations(
      const std::vector<AnnotationUse>& source,
      std::vector<std::string>& names,
      std::vector<std::string>& values) {
    for (const AnnotationUse& use : source) {
      names.push_back(use.name);
      values.push_back(annotationValue(use));
    }
  }

  [[nodiscard]] static std::string annotationValue(
      const AnnotationUse& use) {
    std::string result;
    for (const auto& arg : use.args) {
      if (arg == nullptr) {
        continue;
      }
      if (!result.empty()) {
        result += ',';
      }
      switch (arg->kind) {
        case ExprKind::StringLit:
          result += arg->stringValue;
          break;
        case ExprKind::IntLit:
          result += std::to_string(arg->intValue);
          break;
        case ExprKind::BoolLit:
          result += arg->boolValue ? "true" : "false";
          break;
        case ExprKind::CharLit:
          result.push_back(static_cast<char>(arg->charValue));
          break;
        default:
          break;
      }
    }
    return result;
  }

  [[nodiscard]] static std::string parameterText(
      const std::vector<Param>& params) {
    std::string result;
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (i != 0) {
        result += ", ";
      }
      if (params[i].type != nullptr) {
        result += describeType(params[i].type.get());
      }
      if (!params[i].name.empty()) {
        result += " " + params[i].name;
      }
    }
    return result;
  }

  [[nodiscard]] static std::string signatureText(const Decl& decl) {
    const std::string resultType =
        decl.type != nullptr ? describeType(decl.type.get()) : "void";
    if (decl.kind == DeclKind::Function) {
      return resultType + " " + decl.name + "(" +
             parameterText(decl.params) + ")";
    }
    if (decl.kind == DeclKind::Const) {
      return resultType + " " + decl.name;
    }
    return "void " + decl.name;
  }

  [[nodiscard]] static std::string kindName(DeclKind kind) {
    switch (kind) {
      case DeclKind::Class: return "class";
      case DeclKind::Interface: return "interface";
      case DeclKind::Annotation: return "annotation";
      case DeclKind::Enum: return "enumerate";
      case DeclKind::Function: return "function";
      case DeclKind::Const: return "const";
      default: return "unknown";
    }
  }
};

}  // namespace coord::descriptor
