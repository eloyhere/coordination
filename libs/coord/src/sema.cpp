#include "coord/sema.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace coord {

bool Type::isMeta() const {
  switch (kind) {
    case TypeKind::Annotation:
    case TypeKind::Descriptor:
    case TypeKind::Description:
    case TypeKind::Reflection:
    case TypeKind::Reflector:
    case TypeKind::Proxy:
      return true;
    default:
      return false;
  }
}

namespace {

bool numericLike(const Type* type) {
  if (type == nullptr) {
    return false;
  }
  return type->kind == TypeKind::Int || type->kind == TypeKind::Decimal ||
         type->kind == TypeKind::Float || type->name == "decimal";
}

void parseOptionalPolicy(const AnnotationUse& use, ProxyPolicy& policy,
                         const Expr*& fallback) {
  if (use.args.empty() || use.args[0] == nullptr) {
    return;
  }
  const Expr* argument = use.args[0].get();
  if (argument->kind == ExprKind::Name) {
    std::string name = argument->name;
    for (char& character : name) {
      character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    }
    if (name == "skip") {
      policy = ProxyPolicy::Skip;
    } else if (name == "abort") {
      policy = ProxyPolicy::Abort;
    } else if (name == "error") {
      policy = ProxyPolicy::Error;
    }
  } else if (argument->kind == ExprKind::Call &&
             argument->lhs != nullptr &&
             argument->lhs->kind == ExprKind::Name &&
             argument->args.size() == 1) {
    std::string name = argument->lhs->name;
    for (char& character : name) {
      character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    }
    if (name == "default") {
      policy = ProxyPolicy::Default;
      fallback = argument->args[0].get();
    }
  }
}

bool isComparisonOperator(const std::string& op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
         op == ">=";
}

/// Recognizes a condition that proves an @Optional variable is present (or
/// absent) in the corresponding branch.
bool optionalNarrowTarget(const Expr* expr, std::string& name,
                          bool& valueWhenTrue) {
  if (expr == nullptr) {
    return false;
  }
  if (expr->kind == ExprKind::Unary && expr->op == "!") {
    bool inner = false;
    if (optionalNarrowTarget(expr->lhs.get(), name, inner)) {
      valueWhenTrue = !inner;
      return true;
    }
    return false;
  }
  if (expr->kind == ExprKind::Binary &&
      (expr->op == "!=" || expr->op == "==")) {
    const Expr* left = expr->lhs.get();
    const Expr* right = expr->rhs.get();
    if (left != nullptr && left->kind == ExprKind::Name && right != nullptr &&
        right->kind == ExprKind::NullLit) {
      name = left->name;
      valueWhenTrue = expr->op == "!=";
      return true;
    }
    if (right != nullptr && right->kind == ExprKind::Name && left != nullptr &&
        left->kind == ExprKind::NullLit) {
      name = right->name;
      valueWhenTrue = expr->op == "!=";
      return true;
    }
    return false;
  }
  if (expr->kind == ExprKind::Call && expr->lhs != nullptr &&
      expr->lhs->kind == ExprKind::Member && expr->lhs->lhs != nullptr &&
      expr->lhs->lhs->kind == ExprKind::Name &&
      (expr->lhs->name == "isPresent" || expr->lhs->name == "isEmpty")) {
    name = expr->lhs->lhs->name;
    valueWhenTrue = expr->lhs->name == "isPresent";
    return true;
  }
  return false;
}

void collectOptionalNarrowings(
    const Expr* expr, bool truth,
    std::vector<std::pair<std::string, bool>>& targets) {
  if (expr == nullptr) {
    return;
  }
  if (expr->kind == ExprKind::Unary && expr->op == "!") {
    collectOptionalNarrowings(expr->lhs.get(), !truth, targets);
    return;
  }
  if (expr->kind == ExprKind::Binary && expr->op == "&&" && truth) {
    collectOptionalNarrowings(expr->lhs.get(), true, targets);
    collectOptionalNarrowings(expr->rhs.get(), true, targets);
    return;
  }
  if (expr->kind == ExprKind::Binary && expr->op == "||" && !truth) {
    collectOptionalNarrowings(expr->lhs.get(), false, targets);
    collectOptionalNarrowings(expr->rhs.get(), false, targets);
    return;
  }
  std::string name;
  bool valueWhenTrue = true;
  if (optionalNarrowTarget(expr, name, valueWhenTrue) && valueWhenTrue == truth) {
    targets.emplace_back(std::move(name), truth);
  }
}

bool isAnnotationTargetName(std::string_view name) {
  return name == "class" || name == "interface" || name == "annotation" ||
         name == "function" || name == "method" || name == "field" ||
         name == "variable" || name == "parameter" || name == "module" ||
         name == "enumerate" || name == "enum_value";
}

std::optional<std::int64_t> evalEnumConstant(const Expr* expr) {
  if (expr == nullptr) {
    return std::nullopt;
  }
  switch (expr->kind) {
    case ExprKind::IntLit:
      return expr->intValue;
    case ExprKind::Unary: {
      const std::optional<std::int64_t> value =
          evalEnumConstant(expr->lhs.get());
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (expr->op == "-") {
        return -*value;
      }
      if (expr->op == "+") {
        return *value;
      }
      if (expr->op == "~") {
        return ~*value;
      }
      return std::nullopt;
    }
    case ExprKind::Binary: {
      const std::optional<std::int64_t> lhs =
          evalEnumConstant(expr->lhs.get());
      const std::optional<std::int64_t> rhs =
          evalEnumConstant(expr->rhs.get());
      if (!lhs.has_value() || !rhs.has_value()) {
        return std::nullopt;
      }
      if ((expr->op == "/" || expr->op == "%") && *rhs == 0) {
        return std::nullopt;
      }
      if (expr->op == "+") {
        return *lhs + *rhs;
      }
      if (expr->op == "-") {
        return *lhs - *rhs;
      }
      if (expr->op == "*") {
        return *lhs * *rhs;
      }
      if (expr->op == "/") {
        return *lhs / *rhs;
      }
      if (expr->op == "%") {
        return *lhs % *rhs;
      }
      if (expr->op == "&") {
        return *lhs & *rhs;
      }
      if (expr->op == "|") {
        return *lhs | *rhs;
      }
      if (expr->op == "^") {
        return *lhs ^ *rhs;
      }
      return std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

std::string joinTypes(const std::vector<Type*>& types, const char* separator) {
  std::string result;
  for (std::size_t i = 0; i < types.size(); ++i) {
    if (i != 0) {
      result += separator;
    }
    result += types[i] != nullptr ? types[i]->name : "<error>";
  }
  return result;
}

}  // namespace

Checker::Checker(SourceManager& sources, DiagnosticEngine& diagnostics)
    : sources_(sources), diagnostics_(diagnostics) {}

void Checker::initializeBuiltins() {
  errorType_ = makeType(TypeKind::Error, "<error>", nullptr);
  classType_ = makeType(TypeKind::Class, "class", nullptr);

  Type* decimalRoot = makeType(TypeKind::Class, "decimalRoot", classType_);
  Type* characterRoot = makeType(TypeKind::Class, "character", classType_);
  Type* booleanRoot = makeType(TypeKind::Class, "boolean", classType_);
  enumerationType_ = makeType(TypeKind::Class, "Enumeration", classType_);

  voidType_ = makeType(TypeKind::Void, "void", nullptr);
  intType_ = makeType(TypeKind::Int, "integer", classType_);
  decimalType_ = makeType(TypeKind::Decimal, "decimal", decimalRoot);
  makeType(TypeKind::Decimal, "f32", decimalRoot);
  floatType_ = makeType(TypeKind::Float, "float", decimalRoot);
  recordType_ = makeType(TypeKind::Record, "Record", classType_);
  charType_ = makeType(TypeKind::Char, "char", characterRoot);
  boolType_ = makeType(TypeKind::Bool, "bool", booleanRoot);
  stringType_ = makeType(TypeKind::String, "string", classType_);
  nullType_ = makeType(TypeKind::Null, "null", nullptr);
  errorValueType_ = makeType(TypeKind::ErrorValue, "Error", nullptr);
  typeMap_["Integer"] = intType_;
  typeMap_["String"] = stringType_;
  typeMap_["Character"] = charType_;
  typeMap_["Boolean"] = boolType_;
  typeMap_["Float"] = floatType_;
  typeMap_["Decimal"] = decimalType_;
  typeMap_["Record"] = recordType_;
  // The keyword spellings must resolve to the primitives together with their
  // uppercase aliases; the internal base classes that carry the same names are
  // implementation details and must stay unreachable from source.
  typeMap_["boolean"] = boolType_;
  typeMap_["character"] = charType_;

  annotationType_ = makeType(TypeKind::Annotation, "annotation", classType_);
  functionType_ = makeType(TypeKind::Function, "function", classType_);
  descriptionType_ = makeType(TypeKind::Description, "Description", classType_);
  reflectorType_ = makeType(TypeKind::Reflector, "Reflector", classType_);

  threadType_ = makeType(TypeKind::Thread, "Thread", classType_);
  const auto addThreadMethod = [&](const char* name, Type* returnType) {
    MethodInfo method;
    method.name = name;
    method.returnType = returnType;
    method.owner = threadType_;
    method.access = Access::Public;
    threadType_->methods.push_back(method);
  };
  addThreadMethod("join", voidType_);
  addThreadMethod("detach", voidType_);
  addThreadMethod("joinable", boolType_);

  scopedThreadType_ = makeType(TypeKind::ScopedThread, "ScopedThread",
                               classType_);
  const auto addScopedThreadMethod = [&](const char* name, Type* returnType) {
    MethodInfo method;
    method.name = name;
    method.returnType = returnType;
    method.owner = scopedThreadType_;
    method.access = Access::Public;
    scopedThreadType_->methods.push_back(method);
  };
  addScopedThreadMethod("join", voidType_);
  addScopedThreadMethod("detach", voidType_);
  addScopedThreadMethod("joinable", boolType_);

  mutexType_ = makeType(TypeKind::Mutex, "Mutex", classType_);
  const auto addMutexMethod = [&](const char* name, Type* returnType) {
    MethodInfo method;
    method.name = name;
    method.returnType = returnType;
    method.owner = mutexType_;
    method.access = Access::Public;
    mutexType_->methods.push_back(method);
  };
  addMutexMethod("lock", voidType_);
  addMutexMethod("unlock", voidType_);
  addMutexMethod("tryLock", boolType_);

  scopedLockType_ = makeType(TypeKind::ScopedLock, "ScopedLock", classType_);

  atomicType_ = makeType(TypeKind::Atomic, "Atomic", classType_);
  const auto addAtomicMethod = [&](const char* name, Type* returnType,
                                   std::vector<Type*> params) {
    MethodInfo method;
    method.name = name;
    method.returnType = returnType;
    method.paramTypes = std::move(params);
    method.owner = atomicType_;
    method.access = Access::Public;
    atomicType_->methods.push_back(method);
  };
  addAtomicMethod("load", intType_, {});
  addAtomicMethod("store", voidType_, {intType_});
  addAtomicMethod("fetchAdd", intType_, {intType_});
  addAtomicMethod("fetchSub", intType_, {intType_});
  addAtomicMethod("exchange", intType_, {intType_});
  addAtomicMethod("compareExchange", boolType_, {intType_, intType_});
}

Type* Checker::makeType(TypeKind kind, std::string name, Type* base) {
  auto type = std::make_unique<Type>();
  type->kind = kind;
  type->name = std::move(name);
  type->base = base;
  type->builtin = true;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  if (!raw->name.empty()) {
    typeMap_[raw->name] = raw;
  }
  return raw;
}

Type* Checker::makeArrayType(Type* element, std::int64_t length) {
  const std::string name = (element != nullptr ? element->name : "<error>") +
                           "[" + std::to_string(length) + "]";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Array;
  type->name = name;
  type->base = classType_;
  type->pointee = element;
  type->arrayLength = length;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  typeMap_[name] = raw;
  compositeTypes_[name] = raw;
  return raw;
}

Type* Checker::makeFunctionType(std::vector<Type*> params, Type* returnType) {
  const std::string name = "(" + joinTypes(params, ", ") + ") -> " +
                           (returnType != nullptr ? returnType->name : "void");
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Function;
  type->name = name;
  type->base = classType_;
  type->alternatives = std::move(params);
  type->returnType = returnType;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeQueueType(Type* element) {
  const std::string name =
      "Array<" + (element != nullptr ? element->name : "<error>") + ">";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Queue;
  type->name = name;
  type->base = classType_;
  type->pointee = element;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeArrayViewType(Type* element) {
  const std::string name =
      "ArrayView<" + (element != nullptr ? element->name : "<error>") + ">";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::ArrayView;
  type->name = name;
  type->base = classType_;
  type->pointee = element;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeMapType(Type* key, Type* value, bool unordered) {
  const std::string name =
      (unordered ? "UnorderedMap<" : "OrderedMap<") +
      (key != nullptr ? key->name : "<error>") + ", " +
      (value != nullptr ? value->name : "<error>") + ">";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Map;
  type->name = name;
  type->base = classType_;
  type->unordered = unordered;
  type->alternatives = {key, value};
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeSetType(Type* element, bool unordered) {
  const std::string name =
      (unordered ? "UnorderedSet<" : "OrderedSet<") +
      (element != nullptr ? element->name : "<error>") + ">";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Set;
  type->name = name;
  type->base = classType_;
  type->unordered = unordered;
  type->pointee = element;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeResultType(Type* success, bool expect) {
  const std::string name = (expect ? "Expect<" : "Result<") +
                           (success != nullptr ? success->name : "<error>") +
                           ">";
  const auto found = compositeTypes_.find(name);
  if (found != compositeTypes_.end()) {
    return found->second;
  }
  auto type = std::make_unique<Type>();
  type->kind = expect ? TypeKind::Expect : TypeKind::Result;
  type->name = name;
  type->base = classType_;
  type->pointee = success;
  Type* raw = type.get();
  types_.push_back(std::move(type));
  compositeTypes_[name] = raw;
  typeMap_[name] = raw;
  return raw;
}

Type* Checker::makeSharePtrType(Type* pointee) {
  const std::string key = pointee != nullptr ? pointee->name : "<error>";
  const std::string cacheKey = "share:" + key;
  const auto it = compositeTypes_.find(cacheKey);
  if (it != compositeTypes_.end()) {
    return it->second;
  }
  Type* type = makeType(TypeKind::SharePtr, "@Share " + key, classType_);
  type->pointee = pointee;
  compositeTypes_[cacheKey] = type;
  return type;
}

Type* Checker::makePointerType(Type* pointee) {
  const std::string key = pointee != nullptr ? pointee->name : "<error>";
  const auto it = pointerTypes_.find(key);
  if (it != pointerTypes_.end()) {
    return it->second;
  }
  Type* type = makeType(TypeKind::Pointer, key + "*", nullptr);
  type->pointee = pointee;
  pointerTypes_[key] = type;
  return type;
}

Type* Checker::makeUnionType(std::vector<Type*> alternatives) {
  const std::string name = joinTypes(alternatives, " | ");
  const std::string key = "union:" + name;
  const auto it = compositeTypes_.find(key);
  if (it != compositeTypes_.end()) {
    return it->second;
  }
  Type* type = makeType(TypeKind::Class, name, classType_);
  type->isUnion = true;
  type->alternatives = std::move(alternatives);
  compositeTypes_[key] = type;
  return type;
}

Type* Checker::makeIntersectType(std::vector<Type*> parts) {
  const std::string name = joinTypes(parts, " & ");
  const std::string key = "intersect:" + name;
  const auto it = compositeTypes_.find(key);
  if (it != compositeTypes_.end()) {
    return it->second;
  }
  Type* type = makeType(TypeKind::Class, name, classType_);
  type->alternatives = std::move(parts);
  compositeTypes_[key] = type;
  return type;
}

Type* Checker::lookupType(std::string_view name) const {
  const auto it = typeMap_.find(std::string(name));
  return it != typeMap_.end() ? it->second : nullptr;
}

bool Checker::check(ModuleAst& module) {
  importAliases_ = module.importAliases;
  initializeBuiltins();
  pushScope(Scope::Kind::Module);
  declareTopLevel(module);
  registerAnnotations(module);
  moduleUnsafe_ = module.moduleUnsafe && !module.moduleSafe;

  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Interface) {
      checkInterface(*decl);
    }
  }
  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Enum) {
      checkEnum(*decl);
    }
  }
  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Annotation) {
      checkAnnotation(*decl);
    }
  }
  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Class) {
      checkClass(*decl);
    }
  }
  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Const) {
      checkConst(*decl);
    }
  }
  for (const auto& decl : module.decls) {
    if (decl->kind == DeclKind::Function) {
      checkFunction(*decl);
    }
  }

  checkShareTypeCycles(module);

  const auto mainIt = functions_.find("main");
  if (mainIt != functions_.end()) {
    if (mainIt->second.size() > 1) {
      diagnostics_.error(mainIt->second[1]->span, "E2011",
                         "multiple definitions of 'main'");
    }
    const Decl* main = mainIt->second.front();
    if (main->resolvedType != nullptr &&
        main->resolvedType->name != "integer") {
      diagnostics_.error(main->span, "E2011",
                         "'main' must return integer");
    }
  }

  popScope();
  return !diagnostics_.hasErrors();
}

void Checker::declareTopLevel(ModuleAst& module) {
  for (const auto& decl : module.decls) {
    orderedDecls_.push_back(decl.get());
    switch (decl->kind) {
      case DeclKind::Class:
      case DeclKind::Interface:
      case DeclKind::Enum:
      case DeclKind::Annotation: {
        if (typeMap_.find(decl->name) != typeMap_.end()) {
          diagnostics_.error(decl->span, "E2001",
                             "duplicate type declaration '" + decl->name + "'");
          continue;
        }
        TypeKind kind = TypeKind::Class;
        Type* base = classType_;
        if (decl->kind == DeclKind::Interface) {
          kind = TypeKind::Interface;
        } else if (decl->kind == DeclKind::Annotation) {
          kind = TypeKind::Annotation;
          base = annotationType_;
        } else if (decl->kind == DeclKind::Enum) {
          kind = TypeKind::Enum;
          base = enumerationType_;
        }
        Type* type = makeType(kind, decl->name, base);
        type->builtin = false;
        type->decl = decl.get();
        decl->resolvedType = type;
        typeDecls_[decl->name] = decl.get();
        break;
      }
      case DeclKind::Function:
        functions_[decl->name].push_back(decl.get());
        break;
      case DeclKind::Const:
        if (consts_.find(decl->name) != consts_.end()) {
          diagnostics_.error(decl->span, "E2001",
                             "duplicate constant '" + decl->name + "'");
        } else {
          consts_[decl->name] = decl.get();
        }
        break;
      default:
        diagnostics_.error(decl->span, "E2002",
                           "declaration is not allowed at module scope");
        break;
    }
  }
}

void Checker::pushScope(Scope::Kind kind) {
  scopes_.push_back(Scope{kind, {}});
}

void Checker::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Checker::pushTypeScope() { typeScopes_.emplace_back(); }

void Checker::popTypeScope() {
  if (!typeScopes_.empty()) {
    typeScopes_.pop_back();
  }
}

void Checker::bindGenericParams(const std::vector<GenericParam>& generics,
                                bool allowMeta) {
  if (typeScopes_.empty()) {
    pushTypeScope();
  }
  auto& scope = typeScopes_.back();
  for (const GenericParam& param : generics) {
    Type* bound = classType_;
    if (param.bound) {
      bound = resolveType(param.bound.get(), allowMeta);
    }
    scope[param.name] = bound;
  }
}

void Checker::declareVariable(const std::string& name, Type* type,
                              bool isConst, const Decl* field, Span span,
                              bool isOptional, ProxyPolicy proxyPolicy,
                              const Expr* optionalDefault) {
  if (scopes_.empty()) {
    pushScope(Scope::Kind::Module);
  }
  auto& variables = scopes_.back().variables;
  if (variables.find(name) != variables.end()) {
    diagnostics_.error(span, "E2002",
                       "redeclaration of '" + name + "' in the same scope");
  }
  Variable variable;
  variable.type = type;
  variable.isConst = isConst;
  variable.isOptional = isOptional;
  variable.proxyPolicy = proxyPolicy;
  variable.optionalDefault = optionalDefault;
  variable.field = field;
  variable.scopeDepth = static_cast<int>(scopes_.size());
  variables[name] = variable;
}

Checker::Variable* Checker::lookupVariable(std::string_view name,
                                           std::size_t* scopeIndex) {
  for (std::size_t i = scopes_.size(); i > 0; --i) {
    const auto found = scopes_[i - 1].variables.find(std::string(name));
    if (found != scopes_[i - 1].variables.end()) {
      if (scopeIndex != nullptr) {
        *scopeIndex = i - 1;
      }
      return &found->second;
    }
  }
  return nullptr;
}

bool Checker::checkOptionalDirectAccess(Expr* expr, std::string_view action) {
  if (expr == nullptr) {
    return false;
  }
  if (expr->lhs != nullptr && expr->lhs->optionalCall) {
    const Expr* call = expr->lhs.get();
    if (call->proxyPolicy == ProxyPolicy::Error) {
      diagnostics_.error(
          expr->span, "E2048",
          "un-narrowed @Optional return value is used by " +
              std::string(action) +
              "; configure @Optional(Skip|Abort|Default(expr)) or use "
              "isPresent()/get()");
      return false;
    }
    expr->optionalAccess = true;
    expr->proxyPolicy = call->proxyPolicy;
    expr->optionalDefault = call->optionalDefault;
    expr->optionalSmart = call->optionalSmart;
    return true;
  }
  if (expr->lhs != nullptr && expr->lhs->optionalField) {
    const Expr* field = expr->lhs.get();
    const Expr* defaultExpr = field->optionalDefault;
    if (field->proxyPolicy == ProxyPolicy::Error) {
      diagnostics_.error(
          expr->span, "E2048",
          "un-narrowed @Optional field is used by " + std::string(action) +
              "; configure @Optional(Skip|Abort|Default(expr)) or use "
              "isPresent()/get()");
      return false;
    }
    expr->optionalAccess = true;
    expr->proxyPolicy = field->proxyPolicy;
    expr->optionalDefault = defaultExpr;
    expr->optionalSmart = field->optionalSmart;
    return true;
  }
  Variable* variable = nullptr;
  Expr* marked = nullptr;
  std::string name;
  if (expr->kind == ExprKind::Name) {
    variable = lookupVariable(expr->name);
    marked = expr;
    name = expr->name;
  } else if (expr->lhs != nullptr &&
             expr->lhs->kind == ExprKind::Name) {
    variable = lookupVariable(expr->lhs->name);
    marked = expr;
    name = expr->lhs->name;
  }
  if (variable == nullptr || !variable->isOptional) {
    return false;
  }
  if (variable->narrowed) {
    marked->optionalNarrowed = true;
    return true;
  }
  if (variable->proxyPolicy == ProxyPolicy::Error) {
    diagnostics_.error(
        expr->span, "E2048",
        "un-narrowed @Optional value '" + name + "' is used by " +
            std::string(action) +
            "; narrow it with `!= null`/`isPresent()` or use `.get()`/"
            "`.orElse(...)`, or configure @Optional(Skip|Abort|Default(expr))");
    return false;
  }
  marked->optionalAccess = true;
  marked->proxyPolicy = variable->proxyPolicy;
  marked->optionalDefault = variable->optionalDefault;
  marked->optionalSmart = variable->isUnique || variable->isShare;
  const LineCol location = sources_.lineCol(expr->span);
  marked->proxyMessage =
      sources_.file(expr->span.file).path + ":" +
      std::to_string(location.line) + ":" + std::to_string(location.column) +
      ": optional value '" + name + "' is null";
  return true;
}

void Checker::applyOptionalNarrowing(
    const Expr* condition, bool truth,
    std::vector<std::pair<std::string, bool>>& undo) {
  std::vector<std::pair<std::string, bool>> targets;
  collectOptionalNarrowings(condition, truth, targets);
  for (const auto& [name, value] : targets) {
    Variable* variable = lookupVariable(name);
    if (variable == nullptr || !variable->isOptional) {
      continue;
    }
    undo.emplace_back(name, variable->narrowed);
    variable->narrowed = value;
  }
}

void Checker::restoreOptionalNarrowing(
    std::vector<std::pair<std::string, bool>>& undo) {
  for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
    Variable* variable = lookupVariable(it->first);
    if (variable != nullptr) {
      variable->narrowed = it->second;
    }
  }
  undo.clear();
}

const Decl* Checker::lookupFunction(std::string_view name,
                                    std::size_t argc) const {
  const auto it = functions_.find(std::string(name));
  if (it == functions_.end()) {
    return nullptr;
  }
  for (const Decl* decl : it->second) {
    if (decl->params.size() == argc) {
      return decl;
    }
  }
  return nullptr;
}

const FieldInfo* Checker::lookupField(Type* type, std::string_view name) const {
  if (type == nullptr) {
    return nullptr;
  }
  for (const FieldInfo& field : type->fields) {
    if (field.name == name) {
      return &field;
    }
  }
  if (type->base != nullptr) {
    if (const FieldInfo* field = lookupField(type->base, name)) {
      return field;
    }
  }
  return nullptr;
}

const MethodInfo* Checker::lookupMethod(Type* type, std::string_view name,
                                        std::size_t argc) const {
  if (type == nullptr) {
    return nullptr;
  }
  for (const MethodInfo& method : type->methods) {
    if (method.name == name && method.paramTypes.size() == argc) {
      return &method;
    }
  }
  for (Type* iface : type->interfaces) {
    if (const MethodInfo* method = lookupMethod(iface, name, argc)) {
      return method;
    }
  }
  if (type->base != nullptr) {
    return lookupMethod(type->base, name, argc);
  }
  return nullptr;
}

Type* Checker::resolveNamedType(TypeExpr* type, bool allowMeta) {
  const auto checkContainerArgument = [&](const TypeExpr* argument,
                                         const char* container) {
    if (argument != nullptr && argument->kind == TypeExprKind::Ref) {
      diagnostics_.error(
          argument->span, "E2039",
          "borrowed references cannot be stored in " +
              std::string(container) +
              "; store a value, pointer, or @Share/@Unique owner");
    }
  };
  std::string resolvedName = type->name;
  const std::size_t dot = resolvedName.find('.');
  if (dot != std::string::npos &&
      importAliases_.count(resolvedName.substr(0, dot)) != 0) {
    resolvedName = resolvedName.substr(dot + 1);
  }
  if (resolvedName == "Array" && type->args.size() == 1) {
    checkContainerArgument(type->args[0].get(), "Array");
    return makeQueueType(resolveType(type->args[0].get(), allowMeta));
  }
  if (type->name == "ArrayView" && type->args.size() == 1) {
    checkContainerArgument(type->args[0].get(), "ArrayView");
    return makeArrayViewType(resolveType(type->args[0].get(), allowMeta));
  }
  if (type->name == "OrderedMap" && type->args.size() == 2) {
    checkContainerArgument(type->args[0].get(), "map keys");
    checkContainerArgument(type->args[1].get(), "map values");
    return makeMapType(resolveType(type->args[0].get(), allowMeta),
                       resolveType(type->args[1].get(), allowMeta), false);
  }
  if (type->name == "UnorderedMap" && type->args.size() == 2) {
    checkContainerArgument(type->args[0].get(), "map keys");
    checkContainerArgument(type->args[1].get(), "map values");
    return makeMapType(resolveType(type->args[0].get(), allowMeta),
                       resolveType(type->args[1].get(), allowMeta), true);
  }
  if (type->name == "OrderedSet" && type->args.size() == 1) {
    checkContainerArgument(type->args[0].get(), "set elements");
    return makeSetType(resolveType(type->args[0].get(), allowMeta), false);
  }
  if (type->name == "UnorderedSet" && type->args.size() == 1) {
    checkContainerArgument(type->args[0].get(), "set elements");
    return makeSetType(resolveType(type->args[0].get(), allowMeta), true);
  }
  Type* found = nullptr;
  for (auto it = typeScopes_.rbegin(); it != typeScopes_.rend(); ++it) {
    const auto scoped = it->find(resolvedName);
    if (scoped != it->end()) {
      found = scoped->second;
      break;
    }
  }
  if (found == nullptr) {
    const auto it = typeMap_.find(resolvedName);
    if (it != typeMap_.end()) {
      found = it->second;
    }
  }
  if (found == nullptr) {
    diagnostics_.error(type->span, "E2003",
                       "unknown type '" + type->name + "'");
    return errorType_;
  }
  if (found->kind == TypeKind::Interface && found->functional) {
    return instantiateFunctionalInterface(found, type, allowMeta);
  }
  if (!type->args.empty()) {
    if (resolvedName == "Reflector") {      std::vector<Type*> arguments;
      for (const auto& arg : type->args) {
        arguments.push_back(resolveType(arg.get(), allowMeta));
      }
      const bool isReflector = true;
      Type* instance = makeType(
          isReflector ? TypeKind::Reflector : TypeKind::Reflector,
          resolvedName + "<" + joinTypes(arguments, ", ") + ">",
          isReflector ? reflectorType_ : proxyType_);
      instance->builtin = false;
      instance->alternatives = std::move(arguments);
      return instance;
    }
    for (const auto& arg : type->args) {
      resolveType(arg.get(), allowMeta);
    }
  }
  return found;
}

Type* Checker::instantiateFunctionalInterface(Type* iface, TypeExpr* type,
                                              bool allowMeta) {
  const Decl* decl = iface->decl;
  if (decl == nullptr) {
    return errorType_;
  }
  const std::size_t expected = decl->generics.size();
  if (type->args.size() != expected) {
    diagnostics_.error(
        type->span, "E2089",
        "functional interface '" + iface->name + "' expects " +
            std::to_string(expected) + " type argument(s)");
    return errorType_;
  }
  std::vector<Type*> arguments;
  arguments.reserve(type->args.size());
  for (const auto& argument : type->args) {
    arguments.push_back(resolveType(argument.get(), allowMeta));
  }
  const Decl* method = nullptr;
  for (const auto& member : decl->members) {
    if (member->kind == DeclKind::Method && member->body == nullptr) {
      method = member.get();
      break;
    }
  }
  if (method == nullptr) {
    diagnostics_.error(type->span, "E2089",
                       "functional interface '" + iface->name +
                           "' has no abstract method");
    return errorType_;
  }
  pushTypeScope();
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    typeScopes_.back()[decl->generics[i].name] = arguments[i];
  }
  std::vector<Type*> parameters;
  parameters.reserve(method->params.size());
  for (const Param& parameter : method->params) {
    parameters.push_back(resolveType(parameter.type.get(), false));
  }
  Type* returnType =
      method->type ? resolveType(method->type.get(), false) : voidType_;
  popTypeScope();
  return makeFunctionType(std::move(parameters), returnType);
}

Type* Checker::resolveType(TypeExpr* type, bool allowMeta) {
  if (type == nullptr) {
    return voidType_;
  }
  Type* result = nullptr;
  switch (type->kind) {
    case TypeExprKind::Named:
      result = resolveNamedType(type, allowMeta);
      break;
    case TypeExprKind::Pointer:
      result = makePointerType(resolveType(type->inner.get(), allowMeta));
      break;
    case TypeExprKind::Ref:
    case TypeExprKind::Const:
      result = resolveType(type->inner.get(), allowMeta);
      break;
    case TypeExprKind::Union: {
      std::vector<Type*> alternatives;
      for (const auto& part : type->args) {
        alternatives.push_back(resolveType(part.get(), allowMeta));
      }
      result = alternatives.size() == 1 ? alternatives.front()
                                        : makeUnionType(std::move(alternatives));
      break;
    }
    case TypeExprKind::Intersect: {
      std::vector<Type*> parts;
      for (const auto& part : type->args) {
        parts.push_back(resolveType(part.get(), allowMeta));
      }
      result = parts.size() == 1 ? parts.front()
                                 : makeIntersectType(std::move(parts));
      break;
    }
    case TypeExprKind::Function: {
      std::vector<Type*> parameters;
      for (const auto& arg : type->args) {
        parameters.push_back(resolveType(arg.get(), allowMeta));
      }
      Type* resultType = resolveType(type->returnType.get(), allowMeta);
      result = makeFunctionType(std::move(parameters), resultType);
      break;
    }
    case TypeExprKind::Array: {
      Type* element = resolveType(type->inner.get(), allowMeta);
      if (type->arraySize == nullptr ||
          type->arraySize->kind != ExprKind::IntLit ||
          type->arraySize->intValue <= 0) {
        diagnostics_.error(type->span, "E2012",
                           "array length must be a positive integer literal");
        result = errorType_;
      } else {
        result = makeArrayType(element, type->arraySize->intValue);
      }
      break;
    }
  }
  if (result != nullptr && result->isMeta() && !allowMeta &&
      result->kind != TypeKind::Reflector) {
    diagnostics_.error(type->span, "E2009",
                       "meta type '" + result->name +
                           "' cannot be used in runtime declarations");
    return errorType_;
  }
  type->resolved = result;
  return result;
}

bool Checker::isMetaAnnotationName(std::string_view name) {
  return name == "Inherit" || name == "Repeatable" || name == "Target" ||
         name == "Meta" || name == "Retention" || name == "Documented";
}

bool Checker::isBuiltinAnnotationName(std::string_view name) {
  return isMetaAnnotationName(name);
}

void Checker::registerAnnotations(ModuleAst& module) {
  for (const auto& declPtr : module.decls) {
    Decl& decl = *declPtr;
    if (decl.kind != DeclKind::Annotation) {
      continue;
    }

    for (const AnnotationUse& use : decl.annotations) {
      if (!isMetaAnnotationName(use.name)) {
        diagnostics_.error(
            use.span, "E2040",
            "only built-in meta-annotations may annotate an annotation "
            "declaration");
      }
    }

    const annotation::Descriptor* descriptor =
        annotationRegistry_.build(decl, diagnostics_);
    if (descriptor == nullptr) {
      continue;
    }

    AnnotationInfo info;
    info.name = decl.name;
    info.isMeta = descriptor->isMeta;
    info.repeatable = descriptor->repeatable;
    info.inherit = descriptor->inherit;
    info.documented = descriptor->documented;
    info.targetAll = descriptor->targetAll;
    info.targets = descriptor->targets;
    info.retentionRuntime =
        descriptor->retention != annotation::Retention::Compile;
    info.retentionInvocation =
        descriptor->retention == annotation::Retention::Invocation;
    info.decl = &decl;
    annotations_[decl.name] = std::move(info);
  }
}

void Checker::validateAnnotations(
    const std::vector<AnnotationUse>& annotations, std::string_view target,
    Span span, bool allowTypeTarget) {
  std::set<std::string> seen;
  for (const AnnotationUse& use : annotations) {
    if (isMetaAnnotationName(use.name)) {
      const bool retentionOnEnum =
          target == "enumerate" && use.name == "Retention";
      const bool retentionOnClass =
          target == "class" && use.name == "Retention";
      const bool retentionOnBlock =
          target == "block" && use.name == "Retention";
      if (target != "annotation" && !retentionOnEnum && !retentionOnClass &&
          !retentionOnBlock) {
        diagnostics_.error(use.span, "E2040",
                           "meta-annotation '@" + use.name +
                               "' is only allowed on an annotation "
                               "declaration");
      }
      continue;
    }

    if (use.name == "Processor") {
      diagnostics_.error(
          use.span, "E2061",
          "@Processor method annotation was removed; make the annotation "
          "implement AnnotationProcessor and override process(Description)");
      continue;
    }

    const auto it = annotations_.find(use.name);
    if (it != annotations_.end()) {
      const AnnotationInfo& info = it->second;
      const bool targetMatches =
          info.targetAll || info.targets.find(std::string(target)) !=
                                info.targets.end() ||
          (allowTypeTarget && info.targets.count("type") != 0);
      if (!targetMatches) {
        diagnostics_.error(use.span, "E2040",
                           "annotation '@" + use.name +
                               "' cannot be applied to " + std::string(target));
      }
      if (!seen.insert(use.name).second && !info.repeatable) {
        diagnostics_.error(use.span, "E2042",
                           "annotation '@" + use.name +
                               "' is not @Repeatable");
      }
      continue;
    }

    if (isBuiltinAnnotationName(use.name)) {
      continue;
    }

    diagnostics_.error(use.span, "E2041",
                       "unknown annotation '@" + use.name + "'");
  }
}

void Checker::inheritAnnotations(Decl& target, const Decl* source) {
  if (source == nullptr) {
    return;
  }

  std::set<std::string> direct;
  for (const AnnotationUse& use : target.annotations) {
    direct.insert(use.name);
  }

  const auto add = [&](const std::string& name) {
    if (direct.find(name) != direct.end()) {
      return;
    }
    if (std::find(target.inheritedAnnotationNames.begin(),
                  target.inheritedAnnotationNames.end(),
                  name) != target.inheritedAnnotationNames.end()) {
      return;
    }
    const auto it = annotations_.find(name);
    if (it == annotations_.end() || !it->second.inherit) {
      return;
    }
    target.inheritedAnnotationNames.push_back(name);
  };

  for (const AnnotationUse& use : source->annotations) {
    add(use.name);
  }
  for (const std::string& name : source->inheritedAnnotationNames) {
    add(name);
  }
}

int Checker::resolveLength(const std::vector<AnnotationUse>& annotations,
                           Type* type, Span span) {
  const AnnotationUse* lengthUse = nullptr;
  for (const AnnotationUse& use : annotations) {
    if (use.name == "Length") {
      lengthUse = &use;
      break;
    }
  }
  if (lengthUse == nullptr) {
    return 0;
  }
  if (type == nullptr ||
      (type->name != "integer" && type->name != "character" &&
       type->name != "decimal" && type->name != "boolean")) {
    diagnostics_.error(span, "E2057",
                       "@Length is only valid on basic numeric types");
    return 0;
  }
  if (lengthUse->args.size() != 1 || lengthUse->args[0] == nullptr ||
      lengthUse->args[0]->kind != ExprKind::IntLit) {
    diagnostics_.error(lengthUse->span, "E2056",
                       "@Length requires an integer literal width");
    return 0;
  }
  const std::int64_t width = lengthUse->args[0]->intValue;
  if (width != 8 && width != 16 && width != 32 && width != 64 &&
      width != 128) {
    diagnostics_.error(lengthUse->span, "E2056",
                       "@Length width must be 8, 16, 32, 64 or 128");
    return 0;
  }
  if (width == 128 &&
      (type == nullptr || type->name != "integer")) {
    diagnostics_.error(lengthUse->span, "E2057",
                       "@Length(128) is only supported for integer");
    return 0;
  }
  return static_cast<int>(width);
}

namespace {

bool hasMarkerInterface(Type* type, std::string_view name) {
  if (type == nullptr) {
    return false;
  }
  for (Type* interfaceType : type->interfaces) {
    if (interfaceType != nullptr &&
        (interfaceType->name == name ||
         hasMarkerInterface(interfaceType, name))) {
      return true;
    }
  }
  return type->base != nullptr && hasMarkerInterface(type->base, name);
}

}  // namespace

void Checker::validateInvocationHandler(Decl& decl) {
  const AnnotationUse* invocation = nullptr;
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Invocation") {
      invocation = &use;
      break;
    }
  }
  if (invocation == nullptr) {
    return;
  }
  if (invocation->args.empty()) {
    diagnostics_.error(invocation->span, "E2066",
                       "@Invocation requires at least one handler class name");
    return;
  }
  const bool returnsVoid =
      decl.type == nullptr || decl.type->name == "void";
  for (const auto& argument : invocation->args) {
    if (argument == nullptr || argument->kind != ExprKind::Name) {
      diagnostics_.error(invocation->span, "E2066",
                         "@Invocation handlers must be class names");
      continue;
    }
    const std::string handlerName = argument->name;
    Type* handler = lookupType(handlerName);
    if (handler == nullptr || handler->kind != TypeKind::Class ||
        handler->decl == nullptr) {
      diagnostics_.error(invocation->span, "E2066",
                         "@Invocation handler '" + handlerName +
                             "' must be a class");
      continue;
    }
    if (!hasMarkerInterface(handler, "InvocationHandler")) {
      diagnostics_.error(invocation->span, "E2075",
                         "@Invocation handler '" + handlerName +
                             "' must implement InvocationHandler");
      continue;
    }
    bool hasBefore = false;
    bool hasAfter = false;
    for (const auto& member : handler->decl->members) {
      if (member->kind != DeclKind::Method ||
          member->access != Access::Public ||
          (member->type != nullptr && member->type->name != "void")) {
        continue;
      }
      if (member->name == "before") {
        if (member->params.size() != 0 &&
            member->params.size() != decl.params.size()) {
          diagnostics_.error(
              member->span, "E2066",
              "@Invocation before() must take zero or all target parameters");
        }
        hasBefore = true;
      } else if (member->name == "after") {
        if (member->params.size() > 1 ||
            (returnsVoid && !member->params.empty())) {
          diagnostics_.error(
              member->span, "E2066",
              "@Invocation after() takes at most the return value");
        }
        hasAfter = true;
      }
    }
    if (!hasBefore || !hasAfter) {
      diagnostics_.error(
          invocation->span, "E2066",
          "@Invocation handler '" + handlerName +
              "' must declare public void before()/after()");
      continue;
    }
    decl.invocationHandlers.push_back(handlerName);
  }
  if (decl.invocationHandlers.empty()) {
    return;
  }
  decl.isInvocation = true;
  decl.invocationHandler = decl.invocationHandlers.front();
}

void Checker::validateProxyHandler(Decl& decl) {
  const AnnotationUse* proxy = nullptr;
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Proxy") {
      proxy = &use;
      break;
    }
  }
  if (proxy == nullptr) {
    return;
  }
  if (proxy->args.empty() || proxy->args.size() > 2 ||
      proxy->args[0] == nullptr ||
      proxy->args[0]->kind != ExprKind::Name) {
    diagnostics_.error(proxy->span, "E2063",
                       "@Proxy requires a handler type name and an optional "
                       "Error|Skip|Abort|Default(expr) policy");
    return;
  }
  if (proxy->args.size() == 2 && proxy->args[1] != nullptr) {
    const Expr* policyArg = proxy->args[1].get();
    if (policyArg->kind == ExprKind::Name) {
      const std::string policy = policyArg->name;
      if (policy == "Error" || policy == "error") {
        decl.proxyPolicy = ProxyPolicy::Error;
      } else if (policy == "Skip" || policy == "skip") {
        decl.proxyPolicy = ProxyPolicy::Skip;
      } else if (policy == "Abort" || policy == "abort") {
        decl.proxyPolicy = ProxyPolicy::Abort;
      } else {
        diagnostics_.error(policyArg->span, "E2063",
                           "unknown @Proxy policy '" + policyArg->name + "'");
      }
    } else if (policyArg->kind == ExprKind::Call &&
               policyArg->lhs != nullptr &&
               policyArg->lhs->kind == ExprKind::Name &&
               (policyArg->lhs->name == "Default" ||
                policyArg->lhs->name == "default") &&
               policyArg->args.size() == 1) {
      decl.proxyPolicy = ProxyPolicy::Default;
      decl.proxyDefault = policyArg->args[0].get();
    } else {
      diagnostics_.error(policyArg->span, "E2063",
                         "@Proxy policy must be Error, Skip, Abort or "
                         "Default(expr)");
    }
  }
  const std::string handlerName = proxy->args[0]->name;
  Type* handler = lookupType(handlerName);
  if (handler == nullptr) {
    diagnostics_.error(proxy->span, "E2063",
                       "unknown @Proxy handler '" + handlerName + "'");
    return;
  }
  if (handler->kind != TypeKind::Annotation &&
      handler->kind != TypeKind::Class) {
    diagnostics_.error(proxy->span, "E2063",
                       "@Proxy handler must be an annotation or class");
    return;
  }
  if (!hasMarkerInterface(handler, "ProxyHandler")) {
    diagnostics_.error(proxy->span, "E2074",
                       "@Proxy handler '" + handler->name +
                           "' must implement ProxyHandler");
    return;
  }
  decl.isProxy = true;
  decl.proxyHandler = handlerName;

  bool handlerRuntime = false;
  if (handler->kind == TypeKind::Class) {
    for (const AnnotationUse& use : handler->decl->annotations) {
      if (use.name == "Retention") {
        handlerRuntime = true;
      }
    }
  }
  int traps = 0;
  if (handler->decl != nullptr) {
    for (const auto& member : handler->decl->members) {
      if (member->kind != DeclKind::Method) {
        continue;
      }
      for (const AnnotationUse& use : member->annotations) {
        if (use.name == "Get" || use.name == "Set" || use.name == "Has" ||
            use.name == "Delete" || use.name == "Keys" ||
            use.name == "Describe" || use.name == "Call" ||
            use.name == "Construct") {
          ++traps;
          break;
        }
      }
    }
  }
  if (traps == 0) {
    diagnostics_.error(proxy->span, "E2063",
                       "@Proxy handler '" + handlerName +
                           "' declares no trap methods");
    return;
  }
  if (handler->kind == TypeKind::Class &&
      decl.kind == DeclKind::Method && !handlerRuntime) {
    diagnostics_.error(
        proxy->span, "E2063",
        "method-level @Proxy requires @Retention(runtime) on the handler");
  }
  bool runtime = handlerRuntime;
  if (handler->kind == TypeKind::Class) {
    for (const AnnotationUse& use : handler->decl->annotations) {
      if (use.name == "Retention") {
        runtime = true;
        handlerRuntime = true;
      }
    }
    for (const auto& member : handler->decl->members) {
      if (member->kind != DeclKind::Method) {
        continue;
      }
      for (const AnnotationUse& use : member->annotations) {
        if (use.name != "Call" && use.name != "Get" && use.name != "Set") {
          continue;
        }
        if (member->params.size() > 1) {
          diagnostics_.error(
              member->span, "E2063",
              "class @Proxy trap must take zero or one arguments");
          continue;
        }
        if (runtime && member->isStatic) {
          diagnostics_.error(
              member->span, "E2063",
              "@Retention(runtime) class traps must be non-static");
        }
        if (!runtime && !member->isStatic) {
          diagnostics_.error(
              member->span, "E2063",
              "non-static class traps require @Retention(runtime) on the "
              "handler");
        }
        if ((use.name == "Call" || use.name == "Get") &&
            member->params.size() == 1 &&
            (member->params[0].type == nullptr ||
             member->params[0].type->name != "string")) {
          diagnostics_.error(
              member->span, "E2063",
              "@Proxy @Call/@Get trap argument must be a string");
        }
      }
    }
  }
}

bool Checker::classHasOwningFields(const Type* type) const {
  if (type == nullptr || type->kind != TypeKind::Class) {
    return false;
  }
  for (const FieldInfo& field : type->fields) {
    if (field.decl != nullptr &&
        (field.decl->isUnique || field.decl->isShare)) {
      return true;
    }
    if (field.type != nullptr && field.type->kind == TypeKind::Pointer) {
      return true;
    }
  }
  return false;
}

bool Checker::needsExplicitCopy(const Type* type,
                                std::set<const Type*>& visiting) const {
  if (type == nullptr || type->kind != TypeKind::Class ||
      type->decl == nullptr) {
    return false;
  }
  if (type->decl->copyKind == Decl::CopyKind::Deep ||
      type->decl->copyKind == Decl::CopyKind::Shallow) {
    return false;
  }
  if (!visiting.insert(type).second) {
    return false;
  }
  bool needs = classHasOwningFields(type);
  if (!needs && type->base != nullptr && type->base->name != "class") {
    needs = needsExplicitCopy(type->base, visiting);
  }
  if (!needs) {
    for (const FieldInfo& field : type->fields) {
      if (field.type != nullptr && field.type->kind == TypeKind::Class &&
          needsExplicitCopy(field.type, visiting)) {
        needs = true;
        break;
      }
    }
  }
  visiting.erase(type);
  return needs;
}

bool Checker::classRequiresExplicitCopy(const Type* type) const {
  std::set<const Type*> visiting;
  return needsExplicitCopy(type, visiting);
}

bool Checker::isDeepCopyable(const Type* type,
                             std::set<const Type*>& visiting) {
  if (type == nullptr) {
    return false;
  }
  switch (type->kind) {
    case TypeKind::Int:
    case TypeKind::Decimal:
    case TypeKind::Float:
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::String:
    case TypeKind::Enum:
      return true;
    case TypeKind::Class:
      break;
    default:
      return false;
  }
  const auto cached = deepCopyCache_.find(type);
  if (cached != deepCopyCache_.end()) {
    return cached->second;
  }
  if (type->decl == nullptr ||
      type->decl->copyKind == Decl::CopyKind::Shallow ||
      type->decl->copyKind == Decl::CopyKind::None) {
    return false;
  }
  if (classHasOwningFields(type) &&
      type->decl->copyKind != Decl::CopyKind::Deep) {
    return false;
  }
  if (!visiting.insert(type).second) {
    return false;
  }
  bool copyable = true;
  for (const FieldInfo& field : type->fields) {
    if (!copyable) {
      break;
    }
    if (field.decl != nullptr && field.decl->isShare) {
      copyable = false;
    } else if (field.decl != nullptr && field.decl->isUnique) {
      if (field.type == nullptr || field.type->kind != TypeKind::Class) {
        copyable = false;
      } else if (field.type == type &&
                 type->decl->copyKind == Decl::CopyKind::Deep) {
        // Self-recursive @Unique chain: finite at runtime (null terminates).
      } else if (!isDeepCopyable(field.type, visiting)) {
        copyable = false;
      }
    } else if (field.type != nullptr &&
               (field.type->kind == TypeKind::Pointer ||
                field.type->kind == TypeKind::Thread ||
                field.type->kind == TypeKind::ScopedThread ||
                field.type->kind == TypeKind::Mutex ||
                field.type->kind == TypeKind::ScopedLock ||
                field.type->kind == TypeKind::Atomic)) {
      copyable = false;
    } else if (field.type != nullptr &&
               field.type->kind == TypeKind::Class &&
               !isDeepCopyable(field.type, visiting)) {
      copyable = false;
    }
  }
  if (copyable && type->base != nullptr && type->base->name != "class" &&
      type->base->kind == TypeKind::Class) {
    copyable = isDeepCopyable(type->base, visiting);
  }
  visiting.erase(type);
  deepCopyCache_[type] = copyable;
  return copyable;
}

void Checker::validateCopyClass(Decl& decl) {
  Type* type = decl.resolvedType;
  if (type == nullptr) {
    return;
  }
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name != "Copy") {
      continue;
    }
    if (use.args.size() != 1 || use.args[0] == nullptr ||
        use.args[0]->kind != ExprKind::Name) {
      diagnostics_.error(use.span, "E2095",
                         "@Copy requires Deep or Shallow");
      continue;
    }
    const std::string& mode = use.args[0]->name;
    if (mode == "Deep") {
      decl.copyKind = Decl::CopyKind::Deep;
    } else if (mode == "Shallow") {
      decl.copyKind = Decl::CopyKind::Shallow;
    } else {
      diagnostics_.error(use.span, "E2095",
                         "@Copy requires Deep or Shallow");
    }
  }
  const bool owning = classHasOwningFields(type);
  if (decl.copyKind == Decl::CopyKind::Implicit && owning) {
    decl.copyKind = Decl::CopyKind::None;
  }
  if (decl.copyKind == Decl::CopyKind::Deep) {
    for (const FieldInfo& field : type->fields) {
      if (field.decl != nullptr && field.decl->isShare) {
        diagnostics_.error(field.decl->span, "E2095",
                           "@Copy(Deep) cannot copy @Share field '" +
                               field.name + "'");
      }
      if (field.type != nullptr &&
          (field.type->kind == TypeKind::Pointer ||
           field.type->kind == TypeKind::Thread ||
           field.type->kind == TypeKind::ScopedThread ||
           field.type->kind == TypeKind::Mutex ||
           field.type->kind == TypeKind::ScopedLock ||
           field.type->kind == TypeKind::Atomic)) {
        diagnostics_.error(field.decl != nullptr ? field.decl->span : decl.span,
                           "E2095",
                           "@Copy(Deep) cannot copy non-value field '" +
                               field.name + "'");
      } else if (field.type != nullptr &&
                 field.type->kind == TypeKind::Class &&
                 field.type->decl != nullptr &&
                 field.type->decl->copyKind == Decl::CopyKind::Shallow) {
        diagnostics_.error(field.decl != nullptr ? field.decl->span : decl.span,
                           "E2095",
                           "@Copy(Deep) cannot contain @Copy(Shallow) field '" +
                               field.name + "'");
      }
    }
    std::set<const Type*> visiting;
    if (type->base != nullptr && type->base->name != "class" &&
        !isDeepCopyable(type->base, visiting)) {
      diagnostics_.error(decl.span, "E2095",
                         "@Copy(Deep) base class is not deep-copyable");
    }
    for (const FieldInfo& field : type->fields) {
      if (field.type != nullptr && field.type->kind == TypeKind::Class &&
          !isDeepCopyable(field.type, visiting)) {
        diagnostics_.error(
            field.decl != nullptr ? field.decl->span : decl.span, "E2095",
            "@Copy(Deep) field '" + field.name +
                "' is not deep-copyable (needs value fields or @Unique "
                "chains, no @Share/pointer/thread/mutex/atomic fields)");
      }
    }
  } else if (decl.copyKind == Decl::CopyKind::Shallow) {
    for (const FieldInfo& field : type->fields) {
      if (field.decl != nullptr && field.decl->isUnique) {
        diagnostics_.error(field.decl->span, "E2095",
                           "@Copy(Shallow) cannot copy @Unique field '" +
                               field.name + "'");
      }
    }
  }
}

void Checker::checkClass(Decl& decl) {
  validateDoc(decl);
  validateAnnotations(decl.annotations, "class", decl.span);
  validateProxyHandler(decl);

  Type* type = decl.resolvedType;
  if (type == nullptr) {
    return;
  }
  Type* savedClass = currentClass_;
  Decl* savedClassDecl = currentClassDecl_;
  currentClass_ = type;
  currentClassDecl_ = &decl;
  pushScope(Scope::Kind::Class);
  bool ownsUnsafe = false;
  bool ownsSafe = false;
  for (const AnnotationUse& use : decl.annotations) {
    ownsUnsafe = ownsUnsafe || use.name == "Unsafe";
    ownsSafe = ownsSafe || use.name == "Safe";
  }
  const bool savedClassUnsafe = classUnsafe_;
  classUnsafe_ = ownsSafe ? false : (ownsUnsafe || moduleUnsafe_);
  decl.isUnsafe = classUnsafe_;
  decl.isSafe = ownsSafe;

  for (const auto& member : decl.members) {
    if (member->access == Access::Default) {
      member->access = Access::Private;
    }
  }

  pushTypeScope();
  bindGenericParams(decl.generics, true);

  if (decl.base) {
    Type* base = resolveType(decl.base.get(), true);
    if (base->kind != TypeKind::Class && base->kind != TypeKind::Interface) {
      diagnostics_.error(decl.base->span, "E2004",
                         "base type must be a class");
    } else if (base->kind == TypeKind::Interface) {
      diagnostics_.error(decl.base->span, "E2004",
                         "a class cannot extend an interface; use 'implements'");
    } else {
      if (base->decl != nullptr && base->decl->isFinal) {
        diagnostics_.error(decl.base->span, "E2031",
                           "cannot extend final class '" + base->name + "'");
      }
      type->base = base;
    }
  }

  for (const auto& ifaceExpr : decl.interfaces) {
    Type* iface = resolveType(ifaceExpr.get(), true);
    if (iface->kind != TypeKind::Interface) {
      diagnostics_.error(ifaceExpr->span, "E2004",
                         "implemented type must be an interface");
    } else {
      if (iface->name == "AnnotationProcessor") {
        diagnostics_.error(
            ifaceExpr->span, "E2060",
            "only annotation declarations can implement AnnotationProcessor");
      }
      type->interfaces.push_back(iface);
    }
  }

  for (Type* base = type->base; base != nullptr; base = base->base) {
    if (base->decl != nullptr) {
      inheritAnnotations(decl, base->decl);
    }
  }
  for (Type* iface : type->interfaces) {
    if (iface->decl != nullptr) {
      inheritAnnotations(decl, iface->decl);
    }
  }

  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Field) {
      continue;
    }
    validateAnnotations(member->annotations, "field", member->span, true);
    for (const AnnotationUse& use : member->annotations) {
      if (use.name == "Unique") {
        member->isUnique = true;
      } else if (use.name == "Share") {
        member->isShare = true;
      } else if (use.name == "Weak") {
        member->isWeak = true;
      } else if (use.name == "Optional") {
        member->isOptional = true;
        member->proxyPolicy = ProxyPolicy::Error;
        parseOptionalPolicy(use, member->proxyPolicy, member->proxyDefault);
      }
    }
    if (member->isUnique && member->isShare) {
      diagnostics_.error(member->span, "E2065",
                         "a field cannot be both @Unique and @Share");
    }
    if (member->isWeak && (member->isUnique || member->isShare)) {
      diagnostics_.error(member->span, "E2067",
                         "@Weak cannot be combined with @Unique or @Share");
    }
    if (member->type != nullptr &&
        member->type->kind == TypeExprKind::Ref) {
      diagnostics_.error(member->span, "E2039",
                         "reference fields are not allowed; use @Share/@Unique "
                         "or store a value");
    }
    for (const AnnotationUse& use : member->annotations) {
      if (use.name == "Unsigned") {
        member->isUnsigned = true;
      } else if (use.name == "Signed") {
        member->isSigned = true;
      }
    }
    if (member->isUnsigned && member->isSigned) {
      diagnostics_.error(member->span, "E2053",
                         "a field cannot be both @Signed and @Unsigned");
    }
    Type* fieldType = resolveType(member->type.get(), false);
    if (member->radix != 0) {
      if (member->radix != 2 && member->radix != 8 && member->radix != 10 &&
          member->radix != 16) {
        diagnostics_.error(member->span, "E2066",
                           "@Radix must be 2, 8, 10 or 16");
      } else if (fieldType != nullptr && !fieldType->isInteger()) {
        diagnostics_.error(member->span, "E2066",
                           "@Radix is only supported for integer fields");
      }
    }
    if ((member->isUnique || member->isShare) && fieldType != nullptr &&
        fieldType->kind != TypeKind::Class &&
        fieldType->kind != TypeKind::Interface) {
      diagnostics_.error(
          member->span, "E2065",
          "@Unique/@Share fields require a class or interface type");
    }
    if (member->isWeak && fieldType != nullptr &&
        fieldType->kind != TypeKind::Class &&
        fieldType->kind != TypeKind::Interface) {
      diagnostics_.error(member->span, "E2067",
                         "@Weak fields require a class or interface type");
    }
    if ((member->isUnsigned || member->isSigned) && fieldType != nullptr &&
        fieldType->name != "integer") {
      diagnostics_.error(member->span, "E2054",
                         "@Unsigned/@Signed require an integer field");
    }
    member->length = resolveLength(member->annotations, fieldType,
                                   member->span);
    bool optionalField = member->isOptional;
    for (const AnnotationUse& use : member->annotations) {
      if (use.name == "Optional") {
        optionalField = true;
      }
    }
    if (optionalField && fieldType != nullptr &&
        fieldType->kind != TypeKind::Pointer &&
        fieldType->kind != TypeKind::Class) {
      diagnostics_.error(member->span, "E2047",
                         "@Optional requires a pointer, @Unique or @Share");
    }
    if (optionalField && member->proxyPolicy == ProxyPolicy::Default &&
        member->proxyDefault != nullptr && fieldType != nullptr) {
      Type* expected = fieldType->kind == TypeKind::Pointer
                           ? fieldType->pointee
                           : fieldType;
      Type* fallback = checkExpr(const_cast<Expr*>(member->proxyDefault));
      requireAssignable(fallback, expected, member->proxyDefault->span,
                        "@Optional Default(expr)");
    }
    bool duplicate = false;
    for (const FieldInfo& existing : type->fields) {
      if (existing.name == member->name) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      diagnostics_.error(member->span, "E2002",
                         "duplicate member '" + member->name + "'");
      continue;
    }
    if (member->init) {
      Type* initType = checkExpr(member->init.get());
      bool optionalField = false;
      for (const AnnotationUse& use : member->annotations) {
        if (use.name == "Optional") {
          optionalField = true;
        }
      }
      if (initType != nullptr && initType->kind == TypeKind::Null) {
        if (!optionalField) {
          diagnostics_.error(member->span, "E2046",
                             "null requires an @Optional field");
        }
      } else {
        requireAssignable(initType, fieldType, member->span,
                          "field initializer");
      }
    }
    type->fields.push_back(
        FieldInfo{member->name, fieldType, member.get(), member->access, type});
    member->resolvedType = fieldType;
    if (type->base != nullptr) {
      const FieldInfo* hidden = lookupField(type->base, member->name);
      if (hidden != nullptr && hidden->decl != nullptr &&
          hidden->decl->isFinal) {
        diagnostics_.error(member->span, "E2032",
                           "cannot hide final field '" + member->name + "'");
      }
    }
  }

  validateCopyClass(decl);

  for (const auto& member : decl.members) {
    if (member->kind == DeclKind::Method) {
      checkMethod(*member, type);
    } else if (member->kind == DeclKind::Constructor) {
      checkConstructorDecl(*member);
    } else if (member->kind == DeclKind::Destructor) {
      checkDestructorBody(decl, *member);
    }
  }

  for (const MethodInfo& method : type->methods) {
    const MethodInfo* base = lookupMethod(
        type->base, method.name, method.paramTypes.size());
    if (base == nullptr) {
      for (Type* iface : type->interfaces) {
        base = lookupMethod(iface, method.name, method.paramTypes.size());
        if (base != nullptr) {
          break;
        }
      }
    }
    if (base == nullptr) {
      if (method.decl->isOverride) {
        diagnostics_.error(method.decl->span, "E2033",
                           "'override' requires a virtual base method");
      }
      continue;
    }
    inheritAnnotations(*const_cast<Decl*>(method.decl), base->decl);
    if (method.decl->isOverride) {
      if (!base->isVirtual) {
        diagnostics_.error(method.decl->span, "E2033",
                           "'override' requires a virtual base method");
      }
      if (base->isFinal) {
        diagnostics_.error(method.decl->span, "E2032",
                           "cannot override final method '" + method.name +
                               "'");
      }
      if (!isSubtype(method.returnType, base->returnType)) {
        diagnostics_.error(method.decl->span, "E2034",
                           "covariant return type expected for '" + method.name +
                               "'");
      }
    } else if (base->isVirtual && !method.decl->isStatic) {
      diagnostics_.error(method.decl->span, "E2035",
                         "method '" + method.name + "' overrides a virtual "
                         "method and must be marked 'override'");
    }
  }

  for (Type* iface : type->interfaces) {
    for (const MethodInfo& required : iface->methods) {
      if (required.decl != nullptr && required.decl->body != nullptr) {
        continue;  // default method
      }
      if (lookupMethod(type, required.name, required.paramTypes.size()) ==
          nullptr) {
        diagnostics_.error(decl.span, "E2010",
                           "class '" + decl.name + "' does not implement '" +
                               required.name + "' from interface '" +
                               iface->name + "'");
      } else {
        const MethodInfo* implementation = lookupMethod(
            type, required.name, required.paramTypes.size());
        if (implementation != nullptr && implementation->owner == type &&
            !implementation->decl->isOverride) {
          diagnostics_.error(implementation->decl->span, "E2035",
                             "interface method '" + required.name +
                                 "' must be marked 'override'");
        }
        if (implementation != nullptr &&
            !isSubtype(implementation->returnType, required.returnType)) {
          diagnostics_.error(implementation->decl->span, "E2034",
                             "covariant return type expected for interface "
                             "method '" +
                                 required.name + "'");
        }
      }
    }
  }

  if (!decl.hasUserDestructor) {
    synthesizeDestructor(decl);
  }

  popTypeScope();
  popScope();
  currentClass_ = savedClass;
  currentClassDecl_ = savedClassDecl;
  classUnsafe_ = savedClassUnsafe;
}

void Checker::checkEnum(Decl& decl) {
  validateDoc(decl);
  validateAnnotations(decl.annotations, "enumerate", decl.span);
  Type* type = decl.resolvedType;
  if (type == nullptr) {
    return;
  }

  Type* savedClass = currentClass_;
  Decl* savedClassDecl = currentClassDecl_;
  currentClass_ = type;
  currentClassDecl_ = &decl;
  pushScope(Scope::Kind::Class);
  pushTypeScope();

  for (const auto& ifaceExpr : decl.interfaces) {
    Type* iface = resolveType(ifaceExpr.get(), true);
    if (iface->kind != TypeKind::Interface) {
      diagnostics_.error(ifaceExpr->span, "E2004",
                         "implemented type must be an interface");
    } else {
      if (iface->name == "AnnotationProcessor") {
        diagnostics_.error(
            ifaceExpr->span, "E2060",
            "only annotation declarations can implement AnnotationProcessor");
      }
      type->interfaces.push_back(iface);
    }
  }

  std::unordered_map<std::string, std::int64_t> names;
  std::unordered_map<std::int64_t, std::string> values;
  std::int64_t next = 0;

  for (EnumVariant& variant : decl.variants) {
    validateAnnotations(variant.annotations, "enum_value", variant.span);
    std::int64_t value = next;
    if (variant.value) {
      const std::optional<std::int64_t> evaluated =
          evalEnumConstant(variant.value.get());
      if (!evaluated.has_value()) {
        diagnostics_.error(variant.span, "E2020",
                           "enum variant value must be an integer constant "
                           "expression");
        value = next;
      } else {
        value = *evaluated;
      }
    }
    variant.resolvedValue = value;

    if (names.find(variant.name) != names.end()) {
      diagnostics_.error(variant.span, "E2022",
                         "duplicate enum variant '" + variant.name + "'");
    } else if (values.find(value) != values.end()) {
      diagnostics_.error(variant.span, "E2023",
                         "duplicate enum value " + std::to_string(value));
    } else {
      names[variant.name] = value;
      values[value] = variant.name;
      type->variants.push_back(EnumVariantInfo{variant.name, value});
    }
    next = value + 1;
  }

  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Method) {
      diagnostics_.error(member->span, "E2012",
                         "only methods are allowed in an enumerate body");
      continue;
    }
    member->access = Access::Public;
    checkMethod(*member, type);
  }

  for (Type* iface : type->interfaces) {
    for (const MethodInfo& required : iface->methods) {
      if (required.decl != nullptr && required.decl->body != nullptr) {
        continue;
      }
      const MethodInfo* implementation =
          lookupMethod(type, required.name, required.paramTypes.size());
      if (implementation == nullptr) {
        diagnostics_.error(decl.span, "E2010",
                           "enumerate '" + decl.name + "' does not implement '" +
                               required.name + "' from interface '" +
                               iface->name + "'");
        continue;
      }
      if (implementation->owner == type &&
          !implementation->decl->isOverride) {
        diagnostics_.error(implementation->decl->span, "E2035",
                           "interface method '" + required.name +
                               "' must be marked 'override'");
      }
      if (!isSubtype(implementation->returnType, required.returnType)) {
        diagnostics_.error(implementation->decl->span, "E2034",
                           "covariant return type expected for interface "
                           "method '" +
                               required.name + "'");
      }
    }
  }

  for (const AnnotationUse& use : decl.annotations) {
    if (use.name != "Retention") {
      continue;
    }
    const bool valid =
        use.args.size() == 1 &&
        use.args[0] != nullptr &&
        use.args[0]->kind == ExprKind::Name &&
        use.args[0]->name == "enum";
    if (!valid) {
      diagnostics_.error(
          use.span, "E2021",
          "@Retention on an enumerate declaration must be @Retention(enum)");
    } else {
      decl.retainEnum = true;
    }
  }

  popTypeScope();
  popScope();
  currentClass_ = savedClass;
  currentClassDecl_ = savedClassDecl;
}

void Checker::checkInterface(Decl& decl) {
  validateAnnotations(decl.annotations, "interface", decl.span);
  Type* type = decl.resolvedType;
  if (type == nullptr) {
    return;
  }
  if (decl.base != nullptr) {
    Type* parent = resolveType(decl.base.get(), false);
    if (parent == nullptr || parent->kind != TypeKind::Interface) {
      diagnostics_.error(decl.base->span, "E2010",
                         "an interface can only extend another interface");
    } else {
      type->interfaces.push_back(parent);
    }
  }
  for (const auto& ifaceExpr : decl.interfaces) {
    Type* iface = resolveType(ifaceExpr.get(), true);
    if (iface != nullptr && iface->kind == TypeKind::Interface &&
        iface->name == "AnnotationProcessor") {
      diagnostics_.error(
          ifaceExpr->span, "E2060",
          "only annotation declarations can implement AnnotationProcessor");
    }
  }
  Type* savedClass = currentClass_;
  Decl* savedClassDecl = currentClassDecl_;
  currentClass_ = type;
  currentClassDecl_ = &decl;
  pushScope(Scope::Kind::Class);

  pushTypeScope();
  bindGenericParams(decl.generics, true);

  for (const auto& member : decl.members) {
    if (member->kind == DeclKind::Field) {
      diagnostics_.error(member->span, "E2013",
                         "interfaces cannot declare fields");
      continue;
    }
    if (member->kind == DeclKind::Method) {
      member->isVirtual = true;
      // AnnotationProcessor declares the meta-typed `Description` parameter.
      checkMethod(*member, type, decl.name == "AnnotationProcessor");
    }
  }

  bool functional = false;
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "FunctionalInterface") {
      functional = true;
    }
  }
  if (functional) {
    const Decl* abstractMethod = nullptr;
    int abstractCount = 0;
    for (const auto& member : decl.members) {
      if (member->kind == DeclKind::Method && member->body == nullptr) {
        ++abstractCount;
        abstractMethod = member.get();
      }
    }
    if (abstractCount != 1) {
      diagnostics_.error(
          decl.span, "E2089",
          "@FunctionalInterface requires exactly one abstract method, but '" +
              decl.name + "' declares " + std::to_string(abstractCount));
    } else {
      type->functional = true;
      type->functionalMethod = abstractMethod->name;
    }
  }

  popTypeScope();
  popScope();
  currentClass_ = savedClass;
  currentClassDecl_ = savedClassDecl;
}

void Checker::checkAnnotation(Decl& decl) {
  validateDoc(decl);
  validateAnnotations(decl.annotations, "annotation", decl.span);
  Type* type = decl.resolvedType;
  if (type == nullptr) {
    return;
  }
  bool annotationProcessor = false;
  for (const auto& iface : decl.interfaces) {
    Type* interfaceType = resolveType(iface.get(), false);
    if (interfaceType != nullptr &&
        interfaceType->kind == TypeKind::Interface) {
      type->interfaces.push_back(interfaceType);
      if (interfaceType->name == "AnnotationProcessor") {
        annotationProcessor = true;
      }
    } else {
      diagnostics_.error(iface->span, "E2010",
                         "annotation can only implement interfaces");
    }
  }
  Type* savedClass = currentClass_;
  Decl* savedClassDecl = currentClassDecl_;
  currentClass_ = type;
  currentClassDecl_ = &decl;
  pushScope(Scope::Kind::Class);

  pushTypeScope();
  bindGenericParams(decl.generics, true);

  int processCount = 0;
  for (const auto& member : decl.members) {
    if (member->kind != DeclKind::Method) {
      diagnostics_.error(member->span, "E2014",
                         "only methods are allowed in an annotation body");
      continue;
    }

    const bool process = annotationProcessor && member->name == "process";
    if (!process) {
      checkMethod(*member, type);
      continue;
    }

    ++processCount;
    checkMethod(*member, type, true);
    if (!member->isOverride) {
      diagnostics_.error(
          member->span, "E2060",
          "AnnotationProcessor.process must be declared 'override'");
    }
    if (member->body == nullptr) {
      diagnostics_.error(member->span, "E2060",
                         "AnnotationProcessor.process must have a body");
    }
    if (member->resolvedType != nullptr &&
        member->resolvedType->kind != TypeKind::Void) {
      diagnostics_.error(member->span, "E2058",
                         "AnnotationProcessor.process must return void");
    }
    if (member->params.size() != 1 ||
        member->params[0].type == nullptr ||
        member->params[0].type->name != "Description") {
      diagnostics_.error(
          member->span, "E2059",
          "AnnotationProcessor.process must take one Description parameter");
    }
  }
  if (annotationProcessor) {
    if (processCount == 0) {
      diagnostics_.error(
          decl.span, "E2060",
          "annotation '" + decl.name +
              "' implements AnnotationProcessor but does not override "
              "process(Description)");
    } else if (processCount > 1) {
      diagnostics_.error(
          decl.span, "E2060",
          "annotation '" + decl.name +
              "' declares more than one process method; AnnotationProcessor "
              "requires exactly one override process(Description)");
    } else {
      decl.processorMethod = "process";
    }
  }

  const auto annotationInfo = annotations_.find(decl.name);
  if (annotationInfo != annotations_.end()) {
    annotationInfo->second.processorMethod = decl.processorMethod;
  }

  popTypeScope();
  popScope();
  currentClass_ = savedClass;
  currentClassDecl_ = savedClassDecl;
}

void Checker::validateDoc(Decl& decl) {
  if (decl.doc.empty()) {
    return;
  }
  std::set<std::string> documentedParams;
  std::set<std::string> documentedTemplates;
  bool documentedReturn = false;
  std::istringstream lines(decl.doc);
  std::string line;
  while (std::getline(lines, line)) {
    std::size_t begin = line.find_first_not_of(" \t\r*");
    if (begin == std::string::npos) {
      continue;
    }
    line = line.substr(begin);
    if (line.empty() || line[0] != '@') {
      continue;
    }
    const std::size_t space = line.find_first_of(" \t");
    const std::string tag = line.substr(1, space == std::string::npos
                                               ? std::string::npos
                                               : space - 1);
    std::string rest =
        space == std::string::npos ? std::string() : line.substr(space + 1);
    if (tag == "parameter" || tag == "param") {
      const std::size_t end = rest.find_first_of(" \t");
      const std::string name = rest.substr(0, end);
      if (name.empty()) {
        diagnostics_.error(decl.span, "E2076",
                           "@" + tag + " requires a parameter name");
        continue;
      }
      bool found = false;
      for (const Param& param : decl.params) {
        if (param.name == name) {
          found = true;
          break;
        }
      }
      if (!found) {
        diagnostics_.error(decl.span, "E2076",
                           "@" + tag + " documents unknown parameter '" +
                               name + "'");
      }
      if (!documentedParams.insert(name).second) {
        diagnostics_.error(decl.span, "E2076",
                           "duplicate @" + tag + " for '" + name + "'");
      }
    } else if (tag == "template") {
      const std::size_t end = rest.find_first_of(" \t");
      const std::string name = rest.substr(0, end);
      bool found = false;
      for (const GenericParam& generic : decl.generics) {
        if (generic.name == name) {
          found = true;
          break;
        }
      }
      if (name.empty() || !found) {
        diagnostics_.error(decl.span, "E2076",
                           "@template documents unknown parameter '" + name +
                               "'");
      }
      if (!documentedTemplates.insert(name).second) {
        diagnostics_.error(decl.span, "E2076",
                           "duplicate @template for '" + name + "'");
      }
    } else if (tag == "return") {
      if (decl.type == nullptr || decl.type->name == "void") {
        diagnostics_.error(decl.span, "E2076",
                           "@return is not allowed on a void declaration");
      }
      if (documentedReturn) {
        diagnostics_.error(decl.span, "E2076",
                           "duplicate @return documentation");
      }
      documentedReturn = true;
    } else {
      diagnostics_.error(decl.span, "E2076",
                         "unknown documentation tag '@" + tag + "'");
    }
  }
}

void Checker::checkMethod(Decl& decl, Type* owner, bool allowMeta) {
  validateDoc(decl);
  validateAnnotations(decl.annotations, "method", decl.span, true);
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Unique") {
      decl.isUnique = true;
    } else if (use.name == "Share") {
      decl.isShare = true;
    } else if (use.name == "Weak") {
      decl.isWeak = true;
    } else if (use.name == "Optional") {
      decl.isOptional = true;
      decl.proxyPolicy = ProxyPolicy::Error;
      parseOptionalPolicy(use, decl.proxyPolicy, decl.proxyDefault);
    }
  }
  if (decl.isUnique && decl.isShare) {
    diagnostics_.error(decl.span, "E2065",
                       "a declaration cannot be both @Unique and @Share");
  }
  if (decl.isWeak && (decl.isUnique || decl.isShare)) {
    diagnostics_.error(decl.span, "E2067",
                       "@Weak cannot be combined with @Unique or @Share");
  }
  validateProxyHandler(decl);
  validateInvocationHandler(decl);
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Result") {
      decl.isResult = true;
    } else if (use.name == "Expect") {
      decl.isExpect = true;
    } else if (use.name == "Test") {
      decl.isTest = true;
    } else if (use.name == "Unsigned") {
      decl.isUnsigned = true;
    } else if (use.name == "Signed") {
      decl.isSigned = true;
    }
  }
  if (decl.isTest) {
    if (!decl.params.empty()) {
      diagnostics_.error(decl.span, "E2064",
                         "@Test function must not declare parameters");
    }
    if (decl.type != nullptr && decl.type->name != "void") {
      diagnostics_.error(decl.span, "E2064",
                         "@Test function must return void");
    }
  }
  if (decl.isResult && decl.isExpect) {
    diagnostics_.error(decl.span, "E2051",
                       "a declaration cannot be both @Result and @Expect");
  }
  if (decl.isUnsigned && decl.isSigned) {
    diagnostics_.error(decl.span, "E2053",
                       "a declaration cannot be both @Signed and @Unsigned");
  }
  pushTypeScope();
  bindGenericParams(decl.generics, false);

  Type* returnType =
      decl.type ? resolveType(decl.type.get(), allowMeta) : voidType_;
  const AnnotationUse* resultAnnotation = nullptr;
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Result" || use.name == "Expect") {
      resultAnnotation = &use;
      break;
    }
  }
  if (resultAnnotation != nullptr) {
    if (resultAnnotation->typeArgs.size() != 1) {
      diagnostics_.error(
          resultAnnotation->span, "E2073",
          "@" + resultAnnotation->name +
              " must carry the expected success type, e.g. @" +
              resultAnnotation->name + "<integer>");
    } else {
      Type* expected =
          resolveType(resultAnnotation->typeArgs[0].get(), false);
      if (expected != returnType) {
        diagnostics_.error(
            resultAnnotation->span, "E2073",
            "expected type of @" + resultAnnotation->name +
                " must match the declared return type");
      }
    }
  }
  if ((decl.isUnique || decl.isShare) && returnType != nullptr &&
      returnType->kind != TypeKind::Class &&
      returnType->kind != TypeKind::Interface) {
    diagnostics_.error(
        decl.span, "E2065",
        "@Unique/@Share return requires a class or interface type");
  }
  decl.length = resolveLength(decl.annotations, returnType, decl.span);
  if ((decl.isUnsigned || decl.isSigned) && returnType != nullptr &&
      returnType->name != "integer") {
    diagnostics_.error(decl.span, "E2054",
                       "@Unsigned/@Signed require an integer return type");
  }

  MethodInfo info;
  info.name = decl.name;
  info.returnType = returnType;
  info.decl = &decl;
  info.isVirtual = decl.isVirtual;
  info.isFinal = decl.isFinal;
  info.access = decl.access;
  info.owner = owner;

  pushScope(Scope::Kind::Function);
  for (Param& param : decl.params) {
    validateAnnotations(param.annotations, "parameter", param.span, true);
    if (param.isUnique && param.isShare) {
      diagnostics_.error(param.span, "E2065",
                         "a parameter cannot be both @Unique and @Share");
    }
    Type* paramType = resolveType(param.type.get(), allowMeta);
    if (param.isOptional && paramType != nullptr &&
        paramType->kind != TypeKind::Pointer &&
        paramType->kind != TypeKind::Class) {
      diagnostics_.error(param.span, "E2047",
                         "@Optional parameters require a pointer or class type");
    }
    if ((param.isUnique || param.isShare) && paramType != nullptr &&
        paramType->kind != TypeKind::Class &&
        paramType->kind != TypeKind::Interface) {
      diagnostics_.error(
          param.span, "E2065",
          "@Unique/@Share parameters require a class or interface type");
    }
    info.paramTypes.push_back(paramType);
    declareVariable(param.name, paramType, false, nullptr, param.span, param.isOptional, param.proxyPolicy, param.optionalDefault);
    if (Variable* declared = lookupVariable(param.name)) {
      declared->isWeak = param.isWeak;
      if (paramType != nullptr && paramType->kind == TypeKind::Function) {
        // A function-typed parameter is only guaranteed to live for the
        // duration of the call; it must not be returned or stored.
        declared->borrowDepth = -1;
      }
    }
  }
  owner->methods.push_back(info);

  decl.resolvedType = returnType;

  bool ownUnsafeScope = false;
  bool ownSafeScope = false;
  for (const AnnotationUse& use : decl.annotations) {
    ownUnsafeScope = ownUnsafeScope || use.name == "Unsafe";
    ownSafeScope = ownSafeScope || use.name == "Safe";
  }
  const bool effectiveUnsafe =
      ownSafeScope ? false
                   : (ownUnsafeScope || classUnsafe_ || moduleUnsafe_);
  decl.isUnsafe = effectiveUnsafe;
  decl.isSafe = ownSafeScope;
  const bool savedUnsafeScope = unsafeScope_;
  unsafeScope_ = effectiveUnsafe;

  Type* savedReturn = currentReturnType_;
  Decl* savedFunction = currentFunction_;
  const bool savedDestructor = inDestructor_;
  currentReturnType_ = returnType;
  currentFunction_ = &decl;
  inDestructor_ = false;
  if (decl.body) {
    checkStmt(decl.body.get());
  }
  unsafeScope_ = savedUnsafeScope;
  currentReturnType_ = savedReturn;
  currentFunction_ = savedFunction;
  inDestructor_ = savedDestructor;

  popScope();
  popTypeScope();
}

void Checker::checkConstructorDecl(Decl& decl) {
  validateAnnotations(decl.annotations, "constructor", decl.span, true);
  if (currentClassDecl_ != nullptr && decl.name != currentClassDecl_->name) {
    diagnostics_.error(decl.span, "E2006",
                       "constructor name must match class name '" +
                           currentClassDecl_->name + "'");
  }
  pushScope(Scope::Kind::Function);
  for (Param& param : decl.params) {
    validateAnnotations(param.annotations, "parameter", param.span, true);
    if (param.isUnique && param.isShare) {
      diagnostics_.error(param.span, "E2065",
                         "a parameter cannot be both @Unique and @Share");
    }
    Type* paramType = resolveType(param.type.get(), false);
    if (param.isOptional && paramType != nullptr &&
        paramType->kind != TypeKind::Pointer &&
        paramType->kind != TypeKind::Class) {
      diagnostics_.error(param.span, "E2047",
                         "@Optional parameters require a pointer or class type");
    }
    if ((param.isUnique || param.isShare) && paramType != nullptr &&
        paramType->kind != TypeKind::Class &&
        paramType->kind != TypeKind::Interface) {
      diagnostics_.error(
          param.span, "E2065",
          "@Unique/@Share parameters require a class or interface type");
    }
    declareVariable(param.name, paramType, false, nullptr, param.span, param.isOptional, param.proxyPolicy, param.optionalDefault);
    if (Variable* declared = lookupVariable(param.name)) {
      declared->isWeak = param.isWeak;
      if (paramType != nullptr && paramType->kind == TypeKind::Function) {
        // A function-typed parameter is only guaranteed to live for the
        // duration of the call; it must not be returned or stored.
        declared->borrowDepth = -1;
      }
    }
  }

  const auto findConstructor = [](Type* type, std::size_t argc) -> const Decl* {
    if (type == nullptr || type->decl == nullptr) {
      return nullptr;
    }
    for (const auto& member : type->decl->members) {
      if (member->kind == DeclKind::Constructor &&
          member->params.size() == argc) {
        return member.get();
      }
    }
    return nullptr;
  };

  bool sawSuper = false;
  std::set<std::string> initializedFields;
  for (const Initializer& initializer : decl.initializers) {
    if (initializer.kind == Initializer::Kind::Super) {
      if (sawSuper) {
        diagnostics_.error(initializer.span, "E2037",
                           "duplicate 'super' initializer");
      }
      sawSuper = true;
      Type* base = currentClass_ != nullptr ? currentClass_->base : nullptr;
      if (base == nullptr || base->name == "class") {
        diagnostics_.error(initializer.span, "E2036",
                           "'super' initializer requires a base class");
        continue;
      }
      const Decl* constructor = findConstructor(base, initializer.args.size());
      if (constructor == nullptr) {
        diagnostics_.error(initializer.span, "E2005",
                           "base class has no matching constructor");
        continue;
      }
      if (!canAccess(constructor->access, base)) {
        diagnostics_.error(initializer.span, "E2030",
                           "cannot access base constructor");
      }
      for (std::size_t i = 0; i < initializer.args.size(); ++i) {
        Type* argType = checkExpr(initializer.args[i].get());
        Type* paramType =
            resolveType(constructor->params[i].type.get(), false);
        requireAssignable(argType, paramType, initializer.args[i]->span,
                          "super argument");
      }
      continue;
    }

    const FieldInfo* field = nullptr;
    if (currentClass_ != nullptr) {
      for (const FieldInfo& candidate : currentClass_->fields) {
        if (candidate.name == initializer.name) {
          field = &candidate;
          break;
        }
      }
    }
    if (field == nullptr) {
      diagnostics_.error(initializer.span, "E2038",
                         "unknown field '" + initializer.name +
                             "' in initializer list");
      continue;
    }
    if (!initializedFields.insert(initializer.name).second) {
      diagnostics_.error(initializer.span, "E2037",
                         "duplicate initializer for field '" +
                             initializer.name + "'");
    }
    if (field->decl != nullptr && field->decl->init != nullptr) {
      diagnostics_.error(initializer.span, "E2037",
                         "field '" + initializer.name +
                             "' already has a default initializer");
    }
    if (field->decl != nullptr &&
        (field->decl->isUnique || field->decl->isShare)) {
      if (initializer.args.size() != 1) {
        diagnostics_.error(
            initializer.span, "E2005",
            "ownership field initializer requires one class value");
        continue;
      }
      Type* argType = checkExpr(initializer.args[0].get());
      requireAssignable(argType, field->type, initializer.args[0]->span,
                        "ownership field initializer");
      continue;
    }
    if (field->type != nullptr &&
        field->type->kind == TypeKind::Class &&
        field->type->decl != nullptr) {
      const Decl* constructor =
          findConstructor(field->type, initializer.args.size());
      if (constructor == nullptr) {
        diagnostics_.error(initializer.span, "E2005",
                           "field type has no matching constructor");
        continue;
      }
      for (std::size_t i = 0; i < initializer.args.size(); ++i) {
        Type* argType = checkExpr(initializer.args[i].get());
        Type* paramType =
            resolveType(constructor->params[i].type.get(), false);
        requireAssignable(argType, paramType, initializer.args[i]->span,
                          "field argument");
      }
    } else {
      if (initializer.args.size() != 1) {
        diagnostics_.error(initializer.span, "E2005",
                           "field initializer requires one argument");
        continue;
      }
      Type* argType = checkExpr(initializer.args[0].get());
      requireAssignable(argType, field->type, initializer.args[0]->span,
                        "field initializer");
    }
  }

  if (!sawSuper && currentClass_ != nullptr &&
      currentClass_->base != nullptr && currentClass_->base->name != "class") {
    const Decl* constructor = findConstructor(currentClass_->base, 0);
    if (constructor != nullptr &&
        !canAccess(constructor->access, currentClass_->base)) {
      diagnostics_.error(decl.span, "E2030",
                         "cannot access default base constructor");
    }
  }

  decl.resolvedType = voidType_;
  bool ownUnsafeScope = false;
  bool ownSafeScope = false;
  for (const AnnotationUse& use : decl.annotations) {
    ownUnsafeScope = ownUnsafeScope || use.name == "Unsafe";
    ownSafeScope = ownSafeScope || use.name == "Safe";
  }
  const bool effectiveUnsafe =
      ownSafeScope ? false
                   : (ownUnsafeScope || classUnsafe_ || moduleUnsafe_);
  decl.isUnsafe = effectiveUnsafe;
  decl.isSafe = ownSafeScope;
  const bool savedUnsafeScope = unsafeScope_;
  unsafeScope_ = effectiveUnsafe;
  Type* savedReturn = currentReturnType_;
  Decl* savedFunction = currentFunction_;
  currentReturnType_ = voidType_;
  currentFunction_ = &decl;
  if (decl.body) {
    checkStmt(decl.body.get());
  }
  unsafeScope_ = savedUnsafeScope;
  currentReturnType_ = savedReturn;
  currentFunction_ = savedFunction;

  popScope();
}

void Checker::checkDestructorBody(Decl& classDecl, Decl& destructor) {
  validateAnnotations(destructor.annotations, "method", destructor.span);
  if (destructor.name != classDecl.name) {
    diagnostics_.error(destructor.span, "E2007",
                       "destructor name must match class name '" +
                           classDecl.name + "'");
  }
  if (!destructor.params.empty()) {
    diagnostics_.error(destructor.span, "E2007",
                       "a destructor cannot declare parameters");
  }

  destroyedFields_.clear();
  const bool savedDestructor = inDestructor_;
  Type* savedReturn = currentReturnType_;
  Decl* savedFunction = currentFunction_;
  inDestructor_ = true;
  currentReturnType_ = voidType_;
  currentFunction_ = &destructor;

  pushScope(Scope::Kind::Function);
  if (destructor.body) {
    checkStmt(destructor.body.get());
  }
  popScope();

  inDestructor_ = savedDestructor;
  currentReturnType_ = savedReturn;
  currentFunction_ = savedFunction;

  classDecl.hasUserDestructor = true;

  if (currentClass_ != nullptr) {
    for (const FieldInfo& field : currentClass_->fields) {
      if (field.type == nullptr || !isUserClass(field.type)) {
        continue;
      }
      const bool destroyed =
          std::find(destroyedFields_.begin(), destroyedFields_.end(),
                    field.name) != destroyedFields_.end();
      if (!destroyed) {
        diagnostics_.warning(
            destructor.span, "W0001",
            "destructor of '" + classDecl.name + "' does not declare '~this." +
                field.name + ";'; the member still gets destroyed because "
                                "Coordination reuses C++ RAII");
      }
    }
  }
}

void Checker::synthesizeDestructor(Decl& classDecl) {
  auto destructor = std::make_unique<Decl>();
  destructor->kind = DeclKind::Destructor;
  destructor->name = classDecl.name;
  destructor->span = classDecl.span;
  destructor->synthesizedDestructor = true;
  destructor->access = Access::Public;
  if (currentClass_ != nullptr) {
    for (auto it = currentClass_->fields.rbegin();
         it != currentClass_->fields.rend(); ++it) {
      destructor->destroyOrder.push_back(it->decl);
    }
  }
  classDecl.members.push_back(std::move(destructor));
}

void Checker::checkFunction(Decl& decl) {
  validateDoc(decl);
  validateAnnotations(decl.annotations, "function", decl.span, true);
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Unique") {
      decl.isUnique = true;
    } else if (use.name == "Share") {
      decl.isShare = true;
    } else if (use.name == "Weak") {
      decl.isWeak = true;
    } else if (use.name == "Optional") {
      decl.isOptional = true;
      decl.proxyPolicy = ProxyPolicy::Error;
      parseOptionalPolicy(use, decl.proxyPolicy, decl.proxyDefault);
    } else if (use.name == "Native") {
      decl.isNative = true;
      if (use.args.empty() || use.args[0] == nullptr ||
          use.args[0]->kind != ExprKind::StringLit) {
        diagnostics_.error(use.span, "E2078",
                           "@Native requires a C symbol string, e.g. "
                           "@Native(\"getpid\")");
      } else {
        decl.nativeSymbol = use.args[0]->stringValue;
      }
    }
  }
  if (decl.isNative) {
    bool validSymbol = !decl.nativeSymbol.empty();
    for (std::size_t i = 0; i < decl.nativeSymbol.size(); ++i) {
      const char c = decl.nativeSymbol[i];
      const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      const bool digit = c >= '0' && c <= '9';
      if (!alpha && c != '_' && (i == 0 || !digit)) {
        validSymbol = false;
      }
    }
    if (!validSymbol) {
      diagnostics_.error(decl.span, "E2078",
                         "@Native symbol must be a C identifier");
    }
    if (decl.body != nullptr) {
      diagnostics_.error(decl.span, "E2078",
                         "@Native functions must not have a body");
    }
    if (!decl.generics.empty()) {
      diagnostics_.error(decl.span, "E2078",
                         "@Native functions cannot declare generics");
    }
    if (decl.isResult || decl.isExpect || decl.isTest ||
        decl.isUnique || decl.isShare || decl.isWeak || decl.isOptional) {
      diagnostics_.error(decl.span, "E2078",
                         "@Native cannot be combined with "
                         "@Result/@Expect/@Test/@Unique/@Share/@Weak/@Optional");
    }
  }
  if (decl.isUnique && decl.isShare) {
    diagnostics_.error(decl.span, "E2065",
                       "a declaration cannot be both @Unique and @Share");
  }
  if (decl.isWeak && (decl.isUnique || decl.isShare)) {
    diagnostics_.error(decl.span, "E2067",
                       "@Weak cannot be combined with @Unique or @Share");
  }
  validateInvocationHandler(decl);
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Result") {
      decl.isResult = true;
    } else if (use.name == "Expect") {
      decl.isExpect = true;
    } else if (use.name == "Test") {
      decl.isTest = true;
    } else if (use.name == "Unsigned") {
      decl.isUnsigned = true;
    } else if (use.name == "Signed") {
      decl.isSigned = true;
    }
  }
  if (decl.isResult && decl.isExpect) {
    diagnostics_.error(decl.span, "E2051",
                       "a declaration cannot be both @Result and @Expect");
  }
  if (decl.isUnsigned && decl.isSigned) {
    diagnostics_.error(decl.span, "E2053",
                       "a declaration cannot be both @Signed and @Unsigned");
  }
  if (decl.isTest) {
    if (!decl.params.empty()) {
      diagnostics_.error(decl.span, "E2064",
                         "@Test function must not declare parameters");
    }
    if (decl.type != nullptr && decl.type->name != "void") {
      diagnostics_.error(decl.span, "E2064",
                         "@Test function must return void");
    }
  }
  pushTypeScope();
  bindGenericParams(decl.generics, false);

  Type* returnType =
      decl.type ? resolveType(decl.type.get(), false) : voidType_;
  const AnnotationUse* resultAnnotation = nullptr;
  for (const AnnotationUse& use : decl.annotations) {
    if (use.name == "Result" || use.name == "Expect") {
      resultAnnotation = &use;
      break;
    }
  }
  if (resultAnnotation != nullptr) {
    if (resultAnnotation->typeArgs.size() != 1) {
      diagnostics_.error(
          resultAnnotation->span, "E2073",
          "@" + resultAnnotation->name +
              " must carry the expected success type, e.g. @" +
              resultAnnotation->name + "<integer>");
    } else {
      Type* expected =
          resolveType(resultAnnotation->typeArgs[0].get(), false);
      if (expected != returnType) {
        diagnostics_.error(
            resultAnnotation->span, "E2073",
            "expected type of @" + resultAnnotation->name +
                " must match the declared return type");
      }
    }
  }
  if ((decl.isUnique || decl.isShare) && returnType != nullptr &&
      returnType->kind != TypeKind::Class &&
      returnType->kind != TypeKind::Interface) {
    diagnostics_.error(
        decl.span, "E2065",
        "@Unique/@Share return requires a class or interface type");
  }

  pushScope(Scope::Kind::Function);
  for (Param& param : decl.params) {
    validateAnnotations(param.annotations, "parameter", param.span, true);
    if (param.isUnique && param.isShare) {
      diagnostics_.error(param.span, "E2065",
                         "a parameter cannot be both @Unique and @Share");
    }
    Type* paramType = resolveType(param.type.get(), false);
    if (param.isOptional && paramType != nullptr &&
        paramType->kind != TypeKind::Pointer &&
        paramType->kind != TypeKind::Class) {
      diagnostics_.error(param.span, "E2047",
                         "@Optional parameters require a pointer or class type");
    }
    if ((param.isUnique || param.isShare) && paramType != nullptr &&
        paramType->kind != TypeKind::Class &&
        paramType->kind != TypeKind::Interface) {
      diagnostics_.error(
          param.span, "E2065",
          "@Unique/@Share parameters require a class or interface type");
    }
    declareVariable(param.name, paramType, false, nullptr, param.span, param.isOptional, param.proxyPolicy, param.optionalDefault);
    if (Variable* declared = lookupVariable(param.name)) {
      declared->isWeak = param.isWeak;
      if (paramType != nullptr && paramType->kind == TypeKind::Function) {
        // A function-typed parameter is only guaranteed to live for the
        // duration of the call; it must not be returned or stored.
        declared->borrowDepth = -1;
      }
    }
  }

  decl.resolvedType = returnType;
  decl.length = resolveLength(decl.annotations, returnType, decl.span);
  if (decl.isNative) {
    const auto supported = [](Type* type) {
      return type != nullptr &&
             (type->kind == TypeKind::Int || type->kind == TypeKind::Bool ||
              type->kind == TypeKind::Float || type->kind == TypeKind::String ||
              type->name == "void");
    };
    if (!supported(returnType)) {
      diagnostics_.error(
          decl.span, "E2078",
          "@Native supports only integer/boolean/float/string/void signatures");
    }
    for (const Param& param : decl.params) {
      Type* paramType = param.type ? param.type->resolved : nullptr;
      if (!supported(paramType) || paramType->name == "void") {
        diagnostics_.error(
            param.span, "E2078",
            "@Native parameters must be integer, boolean, float or string");
      }
    }
  }
  if ((decl.isUnsigned || decl.isSigned) && returnType != nullptr &&
      returnType->name != "integer") {
    diagnostics_.error(decl.span, "E2054",
                       "@Unsigned/@Signed require an integer return type");
  }
  if (decl.name == "main") {
    if (returnType == nullptr || returnType->name != "integer") {
      diagnostics_.error(decl.span, "E2052",
                         "main must return integer");
    }
    const auto isStringArray = [](Type* type) {
      return type != nullptr && type->kind == TypeKind::Queue &&
             type->pointee != nullptr && type->pointee->name == "string";
    };
    if (decl.params.size() == 1) {
      Type* paramType = resolveType(decl.params[0].type.get(), false);
      if (!isStringArray(paramType)) {
        diagnostics_.error(
            decl.params[0].span, "E2052",
            "main(Array<string>) is the only single-parameter form");
      }
    } else if (decl.params.size() == 2) {
      Type* first = resolveType(decl.params[0].type.get(), false);
      Type* second = resolveType(decl.params[1].type.get(), false);
      if (first == nullptr || first->name != "integer" ||
          !isStringArray(second)) {
        diagnostics_.error(
            decl.span, "E2052",
            "main parameters must be (integer argc, Array<string> argv)");
      }
    } else if (decl.params.size() > 2) {
      diagnostics_.error(
          decl.span, "E2052",
          "main takes no parameters, Array<string>, or "
          "(integer, Array<string>)");
    }
  }
  if (decl.name == "main" && decl.isResult &&
      (returnType == nullptr || returnType->name != "integer")) {
    diagnostics_.error(decl.span, "E2052",
                       "@Result main must return integer");
  }

  bool ownUnsafeScope = false;
  bool ownSafeScope = false;
  for (const AnnotationUse& use : decl.annotations) {
    ownUnsafeScope = ownUnsafeScope || use.name == "Unsafe";
    ownSafeScope = ownSafeScope || use.name == "Safe";
  }
  const bool effectiveUnsafe =
      ownSafeScope ? false : (ownUnsafeScope || moduleUnsafe_);
  decl.isUnsafe = effectiveUnsafe;
  decl.isSafe = ownSafeScope;
  const bool savedUnsafeScope = unsafeScope_;
  unsafeScope_ = effectiveUnsafe;

  Type* savedReturn = currentReturnType_;
  Decl* savedFunction = currentFunction_;
  const bool savedDestructor = inDestructor_;
  currentReturnType_ = returnType;
  currentFunction_ = &decl;
  inDestructor_ = false;
  if (decl.body) {
    checkStmt(decl.body.get());
  }
  unsafeScope_ = savedUnsafeScope;
  currentReturnType_ = savedReturn;
  currentFunction_ = savedFunction;
  inDestructor_ = savedDestructor;

  popScope();
  popTypeScope();
}

void Checker::checkConst(Decl& decl) {
  validateAnnotations(decl.annotations, "variable", decl.span, true);
  Type* declared = decl.type ? resolveType(decl.type.get(), true) : nullptr;
  Type* initType = decl.init ? checkExpr(decl.init.get()) : errorType_;
  if (declared == nullptr) {
    declared = initType;
  } else if (decl.init) {
    requireAssignable(initType, declared, decl.span, "constant initializer");
  }
  decl.resolvedType = declared;
  if (decl.radix != 0) {
    if (decl.radix != 2 && decl.radix != 8 && decl.radix != 10 &&
        decl.radix != 16) {
      diagnostics_.error(decl.span, "E2066",
                         "@Radix must be 2, 8, 10 or 16");
    } else if (declared != nullptr && !declared->isInteger()) {
      diagnostics_.error(decl.span, "E2066",
                         "@Radix is only supported for integer constants");
    }
  }
  if (decl.type == nullptr && declared != nullptr) {
    decl.type = std::make_unique<TypeExpr>();
    decl.type->kind = TypeExprKind::Named;
    decl.type->name = declared->name;
    decl.type->resolved = declared;
  }
}

bool Checker::isSubtype(Type* from, Type* to) const {
  if (from == nullptr || to == nullptr) {
    return false;
  }
  if (from->kind == TypeKind::Error || to->kind == TypeKind::Error) {
    return true;
  }
  if (from == to) {
    return true;
  }
  if (from->kind == TypeKind::Array && to->kind == TypeKind::Array) {
    return from->arrayLength == to->arrayLength &&
           isSubtype(from->pointee, to->pointee);
  }
  if ((from->kind == TypeKind::Result && to->kind == TypeKind::Result) ||
      (from->kind == TypeKind::Expect && to->kind == TypeKind::Expect)) {
    return isSubtype(from->pointee, to->pointee);
  }
  if (to->name == "class") {
    return true;
  }
  if (to->isUnion) {
    for (Type* alternative : to->alternatives) {
      if (isSubtype(from, alternative)) {
        return true;
      }
    }
    return false;
  }
  if (from->isUnion) {
    for (Type* alternative : from->alternatives) {
      if (isSubtype(alternative, to)) {
        return true;
      }
    }
    return false;
  }
  if (from->kind == TypeKind::Pointer && to->kind == TypeKind::Pointer) {
    return isSubtype(from->pointee, to->pointee);
  }
  for (Type* base = from->base; base != nullptr; base = base->base) {
    if (base == to) {
      return true;
    }
  }
  for (Type* iface : from->interfaces) {
    if (iface == to || isSubtype(iface, to)) {
      return true;
    }
  }
  return false;
}

bool Checker::assignable(Type* from, Type* to) const {
  if (isSubtype(from, to)) {
    return true;
  }
  if (from != nullptr && to != nullptr) {
    const bool fromNumeric = from->kind == TypeKind::Int ||
                             from->kind == TypeKind::Decimal ||
                             from->kind == TypeKind::Float;
    const bool toNumeric = to->kind == TypeKind::Int ||
                           to->kind == TypeKind::Decimal ||
                           to->kind == TypeKind::Float;
    if (fromNumeric && toNumeric && to->kind != TypeKind::Int) {
      return true;
    }
  }
  return false;
}

bool Checker::convertible(Type* from, Type* to) {
  if (!isUserClass(from) || !isUserClass(to) || to->decl == nullptr) {
    return false;
  }
  for (const auto& member : to->decl->members) {
    if (member->kind != DeclKind::Constructor || member->params.size() != 1) {
      continue;
    }
    if (!canAccess(member->access, to)) {
      continue;
    }
    Type* parameterType = resolveType(member->params[0].type.get(), false);
    if (assignable(from, parameterType)) {
      return true;
    }
  }
  return false;
}

bool Checker::canAccess(Access access, Type* owner) const {
  if (access == Access::Public || access == Access::Default) {
    return true;
  }
  if (currentClass_ == nullptr) {
    return false;
  }
  if (currentClass_ == owner) {
    return true;
  }
  if (access == Access::Protected) {
    for (Type* base = currentClass_->base; base != nullptr;
         base = base->base) {
      if (base == owner) {
        return true;
      }
    }
  }
  return false;
}

void Checker::requireAssignable(Type* from, Type* to, Span span,
                                const std::string& context) {
  if (assignable(from, to) || convertible(from, to)) {
    return;
  }
  const std::string fromName = from != nullptr ? from->name : "<error>";
  const std::string toName = to != nullptr ? to->name : "<error>";
  diagnostics_.error(span, "E2005",
                     "type mismatch in " + context + ": expected '" + toName +
                         "', found '" + fromName + "'");
}

void Checker::checkClosureEscape(const Expr* value, std::string_view where) {
  if (value == nullptr || value->borrowDepth == 0) {
    return;
  }
  diagnostics_.error(
      value->span, "E2039",
      "a closure that captures by reference cannot " + std::string(where) +
          "; capture by value, return a new closure, or keep the closure "
          "inside the borrowed scope");
}

void Checker::resolveLambdaCaptures(Expr* expr) {
  LambdaContext context;
  context.explicitList = !expr->lambda->captureList.empty();
  bool sawDefault = false;
  for (std::size_t i = 0; i < expr->lambda->captures.size(); ++i) {
    const Capture& capture = expr->lambda->captures[i];
    if (capture.kind == Capture::Kind::This) {
      if (currentClass_ == nullptr) {
        diagnostics_.error(capture.span, "E2087",
                           "'this' can only be captured inside a class");
        continue;
      }
      if (context.capturesThis) {
        diagnostics_.error(capture.span, "E2087",
                           "duplicate 'this' capture");
      }
      context.capturesThis = true;
      continue;
    }
    if (capture.name.empty()) {
      if (sawDefault || i != 0) {
        diagnostics_.error(
            capture.span, "E2087",
            "the default capture ('=' or '&') must appear once and first");
        continue;
      }
      sawDefault = true;
      context.hasDefault = true;
      context.defaultByRef = capture.kind == Capture::Kind::ByRef;
      continue;
    }
    if (context.namedCaptures.find(capture.name) !=
        context.namedCaptures.end()) {
      diagnostics_.error(capture.span, "E2087",
                         "duplicate capture of '" + capture.name + "'");
      continue;
    }
    const bool byRef = capture.kind == Capture::Kind::ByRef;
    if (context.hasDefault && context.defaultByRef == byRef) {
      diagnostics_.error(capture.span, "E2087",
                         "capture '" + capture.name +
                             "' cannot repeat the default capture mode");
      continue;
    }
    Variable* variable = lookupVariable(capture.name);
    if (variable == nullptr) {
      diagnostics_.error(capture.span, "E2087",
                         "cannot capture unknown identifier '" +
                             capture.name + "'");
      continue;
    }
    LambdaCaptureRef info{variable->scopeDepth, variable->borrowDepth};
    context.namedCaptures[capture.name] = byRef;
    context.captured[capture.name] = byRef;
    context.namedRefs[capture.name] = info;
    context.refs[capture.name] = info;
  }
  lambdaContexts_.push_back(std::move(context));
}

void Checker::recordCaptureUse(const Expr* expr, const std::string& name,
                               const Variable& variable,
                               std::size_t scopeIndex) {
  if (!lambdaContexts_.empty() && findLockCapture(name) != nullptr) {
    diagnostics_.error(
        expr->span, "E2039",
        "@Lock capture '" + name +
            "' cannot be captured by a nested closure; it is scope-bound");
  }
  for (auto it = lambdaContexts_.rbegin(); it != lambdaContexts_.rend(); ++it) {
    if (scopeIndex >= it->scopeBoundary) {
      break;
    }
    if (it->explicitList &&
        it->namedCaptures.find(name) == it->namedCaptures.end() &&
        !it->hasDefault) {
      diagnostics_.error(
          expr->span, "E2087",
          "outer variable '" + name +
              "' is not in the lambda capture list; list it explicitly or "
              "use a default capture");
      return;
    }
    const bool byRef = it->namedCaptures.find(name) != it->namedCaptures.end()
                           ? it->namedCaptures[name]
                           : it->defaultByRef;
    it->refs[name] = LambdaCaptureRef{variable.scopeDepth,
                                      variable.borrowDepth};
    it->captured[name] = byRef;
  }
}

void Checker::recordThisCapture(Span span) {
  for (auto it = lambdaContexts_.rbegin(); it != lambdaContexts_.rend(); ++it) {
    it->usesThis = true;
    if (!it->explicitList || it->capturesThis) {
      continue;
    }
    diagnostics_.error(
        span, "E2087",
        "'this' is used inside a lambda with an explicit capture list but is "
        "not captured; add [this]");
    it->capturesThis = true;
  }
}

void Checker::checkCaptureMutation(const Expr* target, Span span) {
  if (target == nullptr || target->kind != ExprKind::Name) {
    return;
  }
  std::size_t scopeIndex = 0;
  Variable* variable = lookupVariable(target->name, &scopeIndex);
  if (variable == nullptr) {
    return;
  }
  for (auto it = lambdaContexts_.rbegin(); it != lambdaContexts_.rend(); ++it) {
    if (scopeIndex >= it->scopeBoundary) {
      break;
    }
    if (!it->explicitList) {
      continue;
    }
    const auto found = it->captured.find(target->name);
    if (found != it->captured.end() && !found->second) {
      diagnostics_.error(
          span, "E2088",
          "by-value capture '" + target->name +
              "' is read-only in v0; capture it by reference with [&" +
              target->name + "]");
      return;
    }
  }
}

int Checker::evaluateLambdaBorrow(const LambdaContext& context) const {
  int depth = 0;
  const auto apply = [&depth](const LambdaCaptureRef& ref, bool byRef) {
    int candidate = 0;
    if (byRef) {
      candidate = ref.borrowDepth != 0 ? ref.borrowDepth : ref.scopeDepth;
    } else if (ref.borrowDepth != 0) {
      candidate = ref.borrowDepth;
    }
    if (candidate == 0) {
      return;
    }
    if (candidate < 0 || depth < 0) {
      depth = -1;
      return;
    }
    if (candidate > depth) {
      depth = candidate;
    }
  };
  for (const auto& entry : context.namedRefs) {
    const auto mode = context.namedCaptures.find(entry.first);
    apply(entry.second,
          mode != context.namedCaptures.end() && mode->second);
  }
  for (const auto& entry : context.refs) {
    if (context.namedCaptures.find(entry.first) !=
        context.namedCaptures.end()) {
      continue;
    }
    apply(entry.second, context.defaultByRef);
  }
  return depth;
}

void Checker::checkLambda(Expr* expr, Type* expected) {
  if (expr == nullptr || expr->lambda == nullptr) {
    return;
  }
  if (expected == nullptr || expected->kind != TypeKind::Function) {
    diagnostics_.error(expr->span, "E2079",
                       "lambda requires a function target type");
    expr->resolvedType = errorType_;
    return;
  }
  if (expr->lambda->params.size() != expected->alternatives.size()) {
    diagnostics_.error(expr->span, "E2079",
                       "lambda parameter count does not match the target");
  }
  resolveLambdaCaptures(expr);
  pushScope(Scope::Kind::Function);
  lambdaContexts_.back().scopeBoundary = scopes_.size() - 1;
  const std::size_t count =
      std::min(expr->lambda->params.size(), expected->alternatives.size());
  for (std::size_t i = 0; i < count; ++i) {
    Param& param = expr->lambda->params[i];
    Type* paramType = param.type ? resolveType(param.type.get(), false)
                                 : errorType_;
    requireAssignable(paramType, expected->alternatives[i], param.span,
                      "lambda parameter");
    declareVariable(param.name, paramType, false, nullptr, param.span, false,
                    ProxyPolicy::Error, nullptr);
    if (Variable* declared = lookupVariable(param.name);
        declared != nullptr && paramType != nullptr &&
        paramType->kind == TypeKind::Function) {
      declared->borrowDepth = -1;
    }
  }
  Type* savedReturn = currentReturnType_;
  Decl* savedFunction = currentFunction_;
  const bool savedDestructor = inDestructor_;
  currentReturnType_ = expected->returnType;
  currentFunction_ = nullptr;
  inDestructor_ = false;
  if (expr->lambda->bodyExpr != nullptr) {
    Type* body = checkExpr(expr->lambda->bodyExpr.get());
    requireAssignable(body, expected->returnType,
                      expr->lambda->bodyExpr->span, "lambda return");
  } else if (expr->lambda->body != nullptr) {
    checkStmt(expr->lambda->body.get());
  }
  currentReturnType_ = savedReturn;
  currentFunction_ = savedFunction;
  inDestructor_ = savedDestructor;
  expr->lambda->borrowDepth = evaluateLambdaBorrow(lambdaContexts_.back());
  expr->lambda->usesThis = lambdaContexts_.back().usesThis;
  expr->borrowDepth = expr->lambda->borrowDepth;
  popScope();
  lambdaContexts_.pop_back();
  expr->resolvedType = expected;
}

Type* Checker::checkIfExpr(Expr* expr) {
  if (expr->ifExpr == nullptr) {
    expr->resolvedType = errorType_;
    return errorType_;
  }
  IfExprInfo& info = *expr->ifExpr;
  requireAssignable(checkExpr(info.condition.get()), boolType_,
                    info.condition->span, "if expression condition");
  Type* thenType = nullptr;
  Type* elseType = nullptr;
  std::vector<std::pair<std::string, bool>> undo;
  applyOptionalNarrowing(info.condition.get(), true, undo);
  thenType = checkExpr(info.thenValue.get());
  restoreOptionalNarrowing(undo);
  applyOptionalNarrowing(info.condition.get(), false, undo);
  elseType = checkExpr(info.elseValue.get());
  restoreOptionalNarrowing(undo);
  Type* result = thenType;
  if (thenType == nullptr || elseType == nullptr) {
    result = errorType_;
  } else if (thenType != elseType && !assignable(thenType, elseType) &&
             !assignable(elseType, thenType)) {
    diagnostics_.error(expr->span, "E2005",
                       "if expression branches have incompatible types ('" +
                           thenType->name + "' and '" + elseType->name + "')");
    result = errorType_;
  } else if (thenType != elseType && assignable(thenType, elseType) &&
             !assignable(elseType, thenType)) {
    result = elseType;
  }
  if (result != nullptr && result->kind == TypeKind::Void) {
    diagnostics_.error(expr->span, "E2005",
                       "an if expression must produce a value; use an 'if' "
                       "statement for side effects");
    result = errorType_;
  }
  const int thenBorrow = info.thenValue != nullptr
                             ? info.thenValue->borrowDepth
                             : 0;
  const int elseBorrow = info.elseValue != nullptr
                             ? info.elseValue->borrowDepth
                             : 0;
  if (thenBorrow == 0) {
    expr->borrowDepth = elseBorrow;
  } else if (elseBorrow == 0) {
    expr->borrowDepth = thenBorrow;
  } else if (thenBorrow < 0 || elseBorrow < 0) {
    expr->borrowDepth = -1;
  } else {
    expr->borrowDepth = std::max(thenBorrow, elseBorrow);
  }
  expr->resolvedType = result;
  return result;
}

const MethodInfo* Checker::findMethodByName(Type* type, const std::string& name,
                                            int& count) const {
  if (type == nullptr) {
    return nullptr;
  }
  const MethodInfo* found = nullptr;
  for (const MethodInfo& method : type->methods) {
    if (method.name == name) {
      ++count;
      found = &method;
    }
  }
  if (type->base != nullptr && type->base->kind != TypeKind::Error &&
      type->base->name != "class") {
    if (const MethodInfo* base =
            findMethodByName(type->base, name, count)) {
      found = base;
    }
  }
  for (Type* iface : type->interfaces) {
    if (const MethodInfo* inherited = findMethodByName(iface, name, count)) {
      found = inherited;
    }
  }
  return found;
}

Type* Checker::checkMethodRef(Expr* expr) {
  if (expr->lhs == nullptr) {
    diagnostics_.error(expr->span, "E2005", "malformed method reference");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  if (expr->lhs->kind == ExprKind::Name &&
      lookupVariable(expr->lhs->name) == nullptr) {
    Type* type = lookupType(expr->lhs->name);
    if (type != nullptr) {
      int count = 0;
      const MethodInfo* method = findMethodByName(type, expr->name, count);
      if (count == 0) {
        diagnostics_.error(expr->span, "E2004",
                           "type '" + type->name + "' has no method '" +
                               expr->name + "'");
        expr->resolvedType = errorType_;
        return errorType_;
      }
      if (count > 1) {
        diagnostics_.error(expr->span, "E2005",
                           "method reference '" + type->name + "::" +
                               expr->name + "' is ambiguous over overloads");
        expr->resolvedType = errorType_;
        return errorType_;
      }
      if (method->decl == nullptr || !method->decl->isStatic) {
        diagnostics_.error(
            expr->span, "E2005",
            "unbound instance method references are not supported in v0; use "
            "'object::method'");
        expr->resolvedType = errorType_;
        return errorType_;
      }
      if (!canAccess(method->access, method->owner)) {
        diagnostics_.error(expr->span, "E2030",
                           "cannot access " +
                               std::string(accessName(method->access)) +
                               " method '" + expr->name + "'");
      }
      Type* function = makeFunctionType(method->paramTypes, method->returnType);
      expr->staticRef = true;
      expr->resolvedType = function;
      return function;
    }
  }
  if (expr->lhs->kind != ExprKind::Name) {
    diagnostics_.error(
        expr->span, "E2005",
        "bound method references require a named receiver in v0");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  Type* receiverType = checkExpr(expr->lhs.get());
  std::size_t receiverScope = 0;
  Variable* receiver =
      lookupVariable(expr->lhs->name, &receiverScope);
  if (receiver == nullptr || receiverType == nullptr ||
      (receiverType->kind != TypeKind::Class &&
       receiverType->kind != TypeKind::Interface)) {
    diagnostics_.error(expr->span, "E2005",
                       "method receiver '" + expr->lhs->name +
                           "' must be a class or interface value");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  int count = 0;
  const MethodInfo* method =
      findMethodByName(receiverType, expr->name, count);
  if (count == 0) {
    diagnostics_.error(expr->span, "E2004",
                       "type '" + receiverType->name + "' has no method '" +
                           expr->name + "'");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  if (count > 1) {
    diagnostics_.error(expr->span, "E2005",
                       "method reference '" + expr->lhs->name + "::" +
                           expr->name + "' is ambiguous over overloads");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  if (method->decl != nullptr && method->decl->isStatic) {
    diagnostics_.error(expr->span, "E2005",
                       "static methods must be referenced through the type, "
                       "not an object");
  }
  if (!canAccess(method->access, method->owner)) {
    diagnostics_.error(expr->span, "E2030",
                       "cannot access " +
                           std::string(accessName(method->access)) +
                           " method '" + expr->name + "'");
  }
  Type* function = makeFunctionType(method->paramTypes, method->returnType);
  expr->boundRef = true;
  expr->borrowDepth = receiver->scopeDepth;
  expr->resolvedType = function;
  return function;
}

Type* Checker::checkExpr(Expr* expr) {
  if (expr == nullptr) {
    return errorType_;
  }
  switch (expr->kind) {
    case ExprKind::Lambda:
      diagnostics_.error(expr->span, "E2079",
                         "lambda requires an explicit function target type");
      expr->resolvedType = errorType_;
      return errorType_;
    case ExprKind::IfExpr:
      return checkIfExpr(expr);
    case ExprKind::MethodRef:
      return checkMethodRef(expr);
    case ExprKind::IntLit:
      expr->resolvedType = intType_;
      return intType_;
    case ExprKind::DecimalLit:
      expr->resolvedType = decimalType_;
      return decimalType_;
    case ExprKind::CharLit:
      expr->resolvedType = charType_;
      return charType_;
    case ExprKind::BoolLit:
      expr->resolvedType = boolType_;
      return boolType_;
    case ExprKind::StringLit:
      expr->resolvedType = stringType_;
      return stringType_;
    case ExprKind::NullLit:
      expr->resolvedType = nullType_;
      return nullType_;
    case ExprKind::Name: {
      std::size_t scopeIndex = 0;
      if (Variable* variable = lookupVariable(expr->name, &scopeIndex)) {
        if (variable->moved) {
          diagnostics_.error(expr->span, "E2045",
                             "@Unique value '" + expr->name +
                                 "' was moved and can no longer be used");
        }
        recordCaptureUse(expr, expr->name, *variable, scopeIndex);
        expr->resolvedType = variable->type;
        expr->borrowDepth = variable->borrowDepth;
        expr->weakValue = variable->isWeak;
        return variable->type;
      }
      const auto constant = consts_.find(expr->name);
      if (constant != consts_.end()) {
        expr->resolvedType = constant->second->resolvedType;
        return constant->second->resolvedType;
      }
      if (currentClass_ != nullptr) {
        if (const FieldInfo* field = lookupField(currentClass_, expr->name)) {
          if (!canAccess(field->access, field->owner)) {
            diagnostics_.error(expr->span, "E2030",
                               "cannot access " +
                                   std::string(accessName(field->access)) +
                                   " field '" + expr->name + "'");
            expr->resolvedType = errorType_;
            return errorType_;
          }
          recordThisCapture(expr->span);
          expr->resolvedType = field->type;
          return field->type;
        }
      }
      diagnostics_.error(expr->span, "E2004",
                         "unknown identifier '" + expr->name + "'");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    case ExprKind::This:
      if (currentClass_ == nullptr) {
        diagnostics_.error(expr->span, "E2004",
                           "'this' is only valid inside a class");
        expr->resolvedType = errorType_;
        return errorType_;
      }
      recordThisCapture(expr->span);
      expr->resolvedType = currentClass_;
      return currentClass_;
    case ExprKind::Super:
      if (currentClass_ == nullptr || currentClass_->base == nullptr ||
          currentClass_->base->name == "class") {
        diagnostics_.error(expr->span, "E2036",
                           "'super' requires a base class");
        expr->resolvedType = errorType_;
        return errorType_;
      }
      expr->resolvedType = currentClass_->base;
      return currentClass_->base;
    case ExprKind::Unary:
      return checkUnary(expr);
    case ExprKind::Binary:
      return checkBinary(expr);
    case ExprKind::Call:
      return checkCall(expr);
    case ExprKind::Member:
      return checkMember(expr);
    case ExprKind::Index: {
      Type* base = checkExpr(expr->lhs.get());
      Type* index = checkExpr(expr->rhs.get());
      if (index != nullptr && !index->isInteger()) {
        diagnostics_.error(expr->rhs->span, "E2005",
                           "array index must be an integer");
      }
      if (base != nullptr &&
          (base->kind == TypeKind::Array ||
           base->kind == TypeKind::Queue ||
           base->kind == TypeKind::ArrayView)) {
        expr->resolvedType = base->pointee;
        return base->pointee;
      }
      if (base != nullptr && base->kind == TypeKind::Pointer) {
        expr->resolvedType = base->pointee;
        return base->pointee;
      }
      diagnostics_.error(expr->span, "E2005",
                         "type '" + (base != nullptr ? base->name : "<error>") +
                             "' cannot be indexed");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    case ExprKind::Assign:
      return checkAssign(expr);
    case ExprKind::Inc: {
      Type* target = checkExpr(expr->lhs.get());
      checkCaptureMutation(expr->lhs.get(), expr->span);
      checkLockWrite(expr->lhs.get(), expr->span);
      if (target != nullptr && !target->isInteger()) {
        diagnostics_.error(expr->span, "E2005",
                           "operator '++' requires an integer lvalue");
      }
      expr->resolvedType = intType_;
      return intType_;
    }
  }
  return errorType_;
}

Type* Checker::checkUnary(Expr* expr) {
  Type* operand = checkExpr(expr->lhs.get());
  if (expr->op == "&") {
    Type* pointer = makePointerType(operand);
    expr->resolvedType = pointer;
    return pointer;
  }
  if (expr->op == "*") {
    checkOptionalDirectAccess(expr, "dereference");
    if (!unsafeScope_) {
      diagnostics_.error(
          expr->span, "E2096",
          "raw pointer dereference requires an @Unsafe scope");
    }
    if (operand->kind != TypeKind::Pointer) {
      diagnostics_.error(expr->span, "E2005",
                         "cannot dereference a non-pointer type");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    expr->resolvedType = operand->pointee;
    return operand->pointee;
  }
  if (expr->op == "!") {
    requireAssignable(operand, boolType_, expr->span, "logical not");
    expr->resolvedType = boolType_;
    return boolType_;
  }
  if (!numericLike(operand)) {
    diagnostics_.error(expr->span, "E2005",
                       "unary '" + expr->op + "' requires a numeric operand");
    expr->resolvedType = errorType_;
    return errorType_;
  }
  expr->resolvedType = operand;
  return operand;
}

Type* Checker::checkBinary(Expr* expr) {
  Type* lhs = checkExpr(expr->lhs.get());
  std::vector<std::pair<std::string, bool>> undo;
  if (expr->op == "&&") {
    applyOptionalNarrowing(expr->lhs.get(), true, undo);
  }
  Type* rhs = checkExpr(expr->rhs.get());
  restoreOptionalNarrowing(undo);

  if (expr->op == "&&" || expr->op == "||") {
    requireAssignable(lhs, boolType_, expr->span, "logical operator");
    requireAssignable(rhs, boolType_, expr->span, "logical operator");
    expr->resolvedType = boolType_;
    return boolType_;
  }

  if (isComparisonOperator(expr->op)) {
    if ((expr->op == "==" || expr->op == "!=") &&
        ((lhs != nullptr && lhs->kind == TypeKind::Null) ||
         (rhs != nullptr && rhs->kind == TypeKind::Null))) {
      expr->resolvedType = boolType_;
      return boolType_;
    }
    const bool compatible =
        assignable(lhs, rhs) || assignable(rhs, lhs) ||
        (numericLike(lhs) && numericLike(rhs));
    if (!compatible) {
      diagnostics_.error(expr->span, "E2005",
                         "cannot compare '" + lhs->name + "' with '" +
                             rhs->name + "'");
    }
    expr->resolvedType = boolType_;
    return boolType_;
  }

  if (expr->op == "&" || expr->op == "|" || expr->op == "^") {
    if (lhs != nullptr && rhs != nullptr &&
        (lhs->kind == TypeKind::Float || rhs->kind == TypeKind::Float)) {
      diagnostics_.error(expr->span, "E2005",
                         "bitwise operators are not defined for float");
      expr->resolvedType = errorType_;
      return errorType_;
    }
  }

  if (expr->op == "+" && lhs->kind == TypeKind::String &&
      rhs->kind == TypeKind::String) {
    expr->resolvedType = stringType_;
    return stringType_;
  }

  if (expr->op == "<<" || expr->op == ">>") {
    const bool integerShift = lhs != nullptr && rhs != nullptr &&
                              lhs->kind == TypeKind::Int &&
                              rhs->kind == TypeKind::Int;
    const bool decimalShift = lhs != nullptr && rhs != nullptr &&
                              lhs->kind == TypeKind::Decimal &&
                              rhs->kind == TypeKind::Int;
    if (!integerShift && !decimalShift) {
      diagnostics_.error(expr->span, "E2005",
                         "operator '" + expr->op +
                             "' requires integer, or decimal << integer");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    Type* result = decimalShift ? decimalType_ : intType_;
    expr->resolvedType = result;
    return result;
  }

  if (!numericLike(lhs) || !numericLike(rhs)) {
    diagnostics_.error(expr->span, "E2005",
                       "operator '" + expr->op +
                           "' requires numeric operands");
    expr->resolvedType = errorType_;
    return errorType_;
  }

  Type* result = intType_;
  if (lhs->kind == TypeKind::Float || rhs->kind == TypeKind::Float) {
    result = floatType_;
  } else if (lhs->kind == TypeKind::Decimal ||
             rhs->kind == TypeKind::Decimal) {
    result = decimalType_;
  } else if (lhs->kind != TypeKind::Int) {
    result = lhs;
  }
  expr->resolvedType = result;
  return result;
}

Type* Checker::checkAssign(Expr* expr) {
  Type* lhs = checkExpr(expr->lhs.get());
  checkCaptureMutation(expr->lhs.get(), expr->span);
  checkLockWrite(expr->lhs.get(), expr->span);
  if (lhs != nullptr && lhs->kind == TypeKind::Class &&
      classRequiresExplicitCopy(lhs)) {
    bool referenceField = false;
    if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Member &&
        expr->lhs->lhs != nullptr) {
      Type* baseType = expr->lhs->lhs->resolvedType;
      const FieldInfo* field = lookupField(baseType, expr->lhs->name);
      if (field != nullptr && field->decl != nullptr &&
          (field->decl->isShare || field->decl->isWeak ||
           field->decl->isUnique)) {
        referenceField = true;
      }
    } else if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
               lookupVariable(expr->lhs->name) == nullptr &&
               currentClass_ != nullptr) {
      const FieldInfo* field =
          lookupField(currentClass_, expr->lhs->name);
      if (field != nullptr && field->decl != nullptr &&
          (field->decl->isShare || field->decl->isWeak ||
           field->decl->isUnique)) {
        referenceField = true;
      }
    }
    if (!referenceField) {
      diagnostics_.error(
          expr->span, "E2095",
          "class '" + lhs->name +
              "' requires an explicit @Copy(Deep) or @Copy(Shallow) before it "
              "can be assigned/copied");
    }
  }
  if (expr->rhs != nullptr && expr->rhs->kind == ExprKind::Name &&
      expr->lhs != nullptr &&
      (expr->lhs->kind == ExprKind::Member ||
       expr->lhs->kind == ExprKind::Index) &&
      findLockCapture(expr->rhs->name) != nullptr) {
    diagnostics_.error(
        expr->span, "E2039",
        "@Lock capture '" + expr->rhs->name +
            "' cannot be stored in a field or container; it is scope-bound");
  }
  if (lhs != nullptr && (lhs->kind == TypeKind::Thread ||
                          lhs->kind == TypeKind::ScopedThread)) {
    diagnostics_.error(expr->span, "E2090",
                       "Thread/ScopedThread values cannot be assigned in v0; "
                       "join/detach the current thread instead");
  }
  if (lhs != nullptr && lhs->kind == TypeKind::Mutex) {
    diagnostics_.error(expr->span, "E2091",
                       "Mutex values cannot be assigned or copied");
  }
  if (lhs != nullptr &&
      (lhs->kind == TypeKind::ScopedLock || lhs->kind == TypeKind::Atomic)) {
    diagnostics_.error(expr->span, "E2092",
                       "ScopedLock/Atomic values cannot be assigned or copied");
  }
  bool optional = false;
  if (expr->lhs->kind == ExprKind::Name) {
    if (Variable* variable = lookupVariable(expr->lhs->name)) {
      optional = variable->isOptional;
      if (variable->isConst) {
        diagnostics_.error(expr->span, "E2005",
                           "cannot assign to constant '" + expr->lhs->name +
                               "'");
      }
    }
  } else if (expr->lhs->kind == ExprKind::Member) {
    if (const FieldInfo* field = lookupField(lhs, expr->lhs->name)) {
      (void)field;
    }
  }
  if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
      expr->rhs != nullptr && expr->rhs->kind == ExprKind::Name &&
      expr->lhs->name == expr->rhs->name) {
    if (Variable* variable = lookupVariable(expr->lhs->name)) {
      if (variable->isUnique) {
        diagnostics_.error(expr->span, "E2045",
                           "@Unique value '" + expr->lhs->name +
                               "' cannot be assigned/moved to itself");
      }
    }
  }
  Type* rhs = nullptr;
  if (lhs != nullptr && lhs->kind == TypeKind::Function &&
      expr->rhs != nullptr && expr->rhs->kind == ExprKind::Lambda) {
    checkLambda(expr->rhs.get(), lhs);
    rhs = lhs;
  } else {
    rhs = checkExpr(expr->rhs.get());
  }
  if (!optional && expr->rhs != nullptr &&
      expr->rhs->kind == ExprKind::Name) {
    checkOptionalDirectAccess(expr->rhs.get(), "implicit conversion");
  }
  if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Member &&
      expr->lhs->lhs != nullptr && expr->rhs != nullptr &&
      expr->lhs->lhs->resolvedType != nullptr) {
    const FieldInfo* field =
        lookupField(expr->lhs->lhs->resolvedType, expr->lhs->name);
    if (field != nullptr && field->decl != nullptr &&
        field->decl->isShare) {
      const bool selfName =
          expr->rhs->kind == ExprKind::Name &&
          expr->lhs->lhs->kind == ExprKind::Name &&
          expr->rhs->name == expr->lhs->lhs->name;
      const bool selfThis =
          expr->rhs->kind == ExprKind::This &&
          expr->lhs->lhs->kind == ExprKind::This;
      if (selfName || selfThis) {
        diagnostics_.warning(
            expr->span, "W0005",
            "self-cycle through @Share field '" + expr->lhs->name +
                "'; use @Weak or restructure the ownership graph");
      }
    }
  }
  if (rhs != nullptr && rhs->kind == TypeKind::Null) {
    if (!optional) {
      diagnostics_.error(expr->span, "E2046",
                         "null cannot be assigned to a non-optional target");
    }
  } else {
    requireAssignable(rhs, lhs, expr->span, "assignment");
  }
  if (expr->rhs != nullptr && expr->lhs != nullptr) {
    const int borrowDepth = expr->rhs->borrowDepth;
    if (borrowDepth != 0 &&
        (expr->lhs->kind == ExprKind::Member ||
         expr->lhs->kind == ExprKind::Index)) {
      checkClosureEscape(expr->rhs.get(),
                         expr->lhs->kind == ExprKind::Member
                             ? "be stored in a field"
                             : "be stored in a container element");
    } else if (expr->lhs->kind == ExprKind::Name) {
      std::size_t destinationScope = 0;
      Variable* destination =
          lookupVariable(expr->lhs->name, &destinationScope);
      if (destination != nullptr) {
        if (borrowDepth > 0 &&
            static_cast<int>(destinationScope) < borrowDepth) {
          diagnostics_.error(
              expr->span, "E2039",
              "closure captures a local by reference and would outlive it in "
              "'" + expr->lhs->name +
                  "'; declare the target in the same or a deeper scope");
        }
        destination->borrowDepth = borrowDepth;
      }
    }
  }
  expr->resolvedType = lhs;
  return lhs;
}

Type* Checker::checkCall(Expr* expr) {
  Expr* callee = expr->lhs.get();
  if (callee == nullptr) {
    expr->resolvedType = errorType_;
    return errorType_;
  }
  if (callee->kind == ExprKind::Name) {
    const std::string& builtin = callee->name;
    const auto arg = [&](std::size_t index) -> Type* {
      return index < expr->args.size() ? checkExpr(expr->args[index].get())
                                       : errorType_;
    };
    if (builtin == "readFile" && expr->args.size() == 1) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "readFile path");
      Type* result = makeResultType(stringType_, false);
      expr->resolvedType = result;
      return result;
    }
    if (builtin == "writeFile" && expr->args.size() == 2) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "writeFile path");
      requireAssignable(arg(1), stringType_, expr->args[1]->span,
                        "writeFile content");
      Type* result = makeResultType(intType_, false);
      expr->resolvedType = result;
      return result;
    }
    if (builtin == "fileExists" && expr->args.size() == 1) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "fileExists path");
      expr->resolvedType = boolType_;
      return boolType_;
    }
    if (builtin == "deleteFile" && expr->args.size() == 1) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "deleteFile path");
      expr->resolvedType = boolType_;
      return boolType_;
    }
    if (builtin == "timeMillis" && expr->args.empty()) {
      expr->resolvedType = intType_;
      return intType_;
    }
    if (builtin == "sleepMillis" && expr->args.size() == 1) {
      requireAssignable(arg(0), intType_, expr->args[0]->span,
                        "sleepMillis duration");
      expr->resolvedType = voidType_;
      return voidType_;
    }
    // Platform and portable path runtime. These map to coord_rt helpers in the
    // generated C++ and use only standard C++ plus the Windows Unicode API.
    const auto stringResult = [&](Type* element, const char* name) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span, name);
      Type* result = makeResultType(element, false);
      expr->resolvedType = result;
      return result;
    };
    if ((builtin == "operatingSystem" || builtin == "architecture" ||
         builtin == "targetTriple" || builtin == "lineSeparator" ||
         builtin == "executableSuffix" || builtin == "homeDirectory" ||
         builtin == "temporaryDirectory" || builtin == "currentDirectory") &&
        expr->args.empty()) {
      expr->resolvedType = stringType_;
      return stringType_;
    }
    if (builtin == "pathSeparator" && expr->args.empty()) {
      expr->resolvedType = charType_;
      return charType_;
    }
    if ((builtin == "pathFileName" || builtin == "pathParent" ||
         builtin == "pathExtension" || builtin == "pathNormalize") &&
        expr->args.size() == 1) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span, builtin);
      expr->resolvedType = stringType_;
      return stringType_;
    }
    if (builtin == "pathJoin" && expr->args.size() == 2) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "pathJoin base");
      requireAssignable(arg(1), stringType_, expr->args[1]->span,
                        "pathJoin child");
      expr->resolvedType = stringType_;
      return stringType_;
    }
    if ((builtin == "pathIsAbsolute" || builtin == "pathExists" ||
         builtin == "createDirectory" || builtin == "createDirectories" ||
         builtin == "deleteDirectory") &&
        expr->args.size() == 1) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span, builtin);
      expr->resolvedType = boolType_;
      return boolType_;
    }
    if (builtin == "setEnvironment" && expr->args.size() == 2) {
      requireAssignable(arg(0), stringType_, expr->args[0]->span,
                        "setEnvironment name");
      requireAssignable(arg(1), stringType_, expr->args[1]->span,
                        "setEnvironment value");
      expr->resolvedType = boolType_;
      return boolType_;
    }
    if (builtin == "environment" && expr->args.size() == 1) {
      return stringResult(stringType_, "environment name");
    }
    if (builtin == "listDirectory" && expr->args.size() == 1) {
      return stringResult(makeQueueType(stringType_), "listDirectory path");
    }
  }

  if (callee->kind == ExprKind::Member && callee->name == "getClass" &&
      expr->args.empty()) {
    checkExpr(callee->lhs.get());
    Type* classInfo = lookupType("Class");
    if (classInfo == nullptr) {
      diagnostics_.error(expr->span, "E2005",
                         "getClass requires standard library Class");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    expr->resolvedType = classInfo;
    return classInfo;
  }

  if (callee->kind == ExprKind::Member && expr->args.empty() &&
      (callee->name == "getSuperClass" ||
       callee->name == "getSupperClass")) {
    checkExpr(callee->lhs.get());
    Type* classInfo = lookupType("Class");
    if (classInfo == nullptr) {
      diagnostics_.error(expr->span, "E2005",
                         callee->name + " requires standard library Class");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    expr->resolvedType = classInfo;
    return classInfo;
  }

  if (callee->kind == ExprKind::Member && expr->args.empty() &&
      (callee->name == "getDescription" || callee->name == "getDescriptor")) {
    Type* base = checkExpr(callee->lhs.get());
    if (base == nullptr) {
      diagnostics_.error(expr->span, "E2005",
                         callee->name + " requires a value or Class");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    if (callee->name == "getDescription") {
      expr->resolvedType = descriptionType_;
      return descriptionType_;
    }
    Type* descriptor = lookupType("Descriptor");
    if (descriptor == nullptr) {
      diagnostics_.error(expr->span, "E2005",
                         "getDescriptor requires standard library Descriptor");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    expr->resolvedType = descriptor;
    return descriptor;
  }

  if (callee->kind == ExprKind::Member && callee->lhs != nullptr &&
      callee->lhs->kind == ExprKind::Name &&
      importAliases_.count(callee->lhs->name) != 0) {
    Type* aliasedType = lookupType(callee->name);
    if (aliasedType != nullptr && aliasedType->kind == TypeKind::Class) {
      const auto constructor = [&](std::size_t argc) -> const Decl* {
        if (aliasedType->decl == nullptr) {
          return nullptr;
        }
        for (const auto& member : aliasedType->decl->members) {
          if (member->kind == DeclKind::Constructor &&
              member->params.size() == argc) {
            return member.get();
          }
        }
        return nullptr;
      };
      const Decl* found = constructor(expr->args.size());
      if (found == nullptr && expr->args.empty()) {
        found = constructor(0);
      }
      if (found != nullptr) {
        for (std::size_t i = 0; i < expr->args.size(); ++i) {
          Type* paramType = resolveType(found->params[i].type.get(), false);
          Type* argType = checkExpr(expr->args[i].get());
          requireAssignable(argType, paramType, expr->args[i]->span,
                            "constructor argument");
        }
      } else if (!expr->args.empty()) {
        diagnostics_.error(expr->span, "E2005",
                           "no constructor for aliased type '" +
                               callee->name + "'");
      }
      expr->resolvedType = aliasedType;
      return aliasedType;
    }
    const Decl* function =
        lookupFunction(callee->name, expr->args.size());
    if (function == nullptr) {
      diagnostics_.error(
          expr->span, "E2004",
          "no aliased function named '" + callee->name + "' takes " +
              std::to_string(expr->args.size()) + " argument(s)");
      expr->resolvedType = errorType_;
      return errorType_;
    }
    for (std::size_t i = 0; i < expr->args.size(); ++i) {
      Type* paramType = resolveType(function->params[i].type.get(), false);
      Type* argType = nullptr;
      if (expr->args[i] != nullptr &&
          expr->args[i]->kind == ExprKind::Lambda &&
          paramType != nullptr && paramType->kind == TypeKind::Function) {
        checkLambda(expr->args[i].get(), paramType);
        argType = paramType;
      } else {
        argType = checkExpr(expr->args[i].get());
      }
      if (!(argType != nullptr && argType->kind == TypeKind::Null &&
            function->params[i].isOptional)) {
        requireAssignable(argType, paramType, expr->args[i]->span,
                          "argument " + std::to_string(i + 1));
      }
    }
    Type* returnType = function->type
                           ? resolveType(function->type.get(), false)
                           : voidType_;
    if (function->isExpect) {
      returnType = makeResultType(returnType, true);
    } else if (function->isResult) {
      returnType = makeResultType(returnType, false);
    }
    if (function->isOptional) {
      expr->optionalCall = true;
      expr->proxyPolicy = function->proxyPolicy;
      expr->optionalDefault = function->proxyDefault;
      expr->optionalSmart = function->isUnique || function->isShare;
    }
    expr->resolvedType = returnType;
    return returnType;
  }

  if (callee->kind == ExprKind::Name) {
    if (callee->name == "runtimeFieldCount" ||
        callee->name == "runtimeMethodCount" ||
        callee->name == "runtimeBase") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2005",
                           callee->name + "() takes one declaration name");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "declaration name");
      }
      expr->resolvedType =
          callee->name == "runtimeBase" ? stringType_ : intType_;
      return expr->resolvedType;
    }

    if (callee->name == "runtimeFieldName" ||
        callee->name == "runtimeMethodName" ||
        callee->name == "runtimeFieldType" ||
        callee->name == "runtimeMethodType") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2005",
                           callee->name +
                               "() takes a declaration name and an index");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        Type* index = checkExpr(expr->args[1].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "declaration name");
        requireAssignable(index, intType_, expr->args[1]->span,
                          "member index");
      }
      expr->resolvedType = stringType_;
      return stringType_;
    }

    if (callee->name == "runtimeHasMember") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2005",
                           "runtimeHasMember() takes a declaration and a "
                           "member name");
      } else {
        Type* declaration = checkExpr(expr->args[0].get());
        Type* member = checkExpr(expr->args[1].get());
        requireAssignable(declaration, stringType_, expr->args[0]->span,
                          "declaration name");
        requireAssignable(member, stringType_, expr->args[1]->span,
                          "member name");
      }
      expr->resolvedType = boolType_;
      return boolType_;
    }

    if (callee->name == "runtimeAnnotationValue") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2005",
                           "runtimeAnnotationValue() takes a declaration and "
                           "an annotation name");
      } else {
        Type* declaration = checkExpr(expr->args[0].get());
        Type* annotation = checkExpr(expr->args[1].get());
        requireAssignable(declaration, stringType_, expr->args[0]->span,
                          "declaration name");
        requireAssignable(annotation, stringType_, expr->args[1]->span,
                          "annotation name");
      }
      expr->resolvedType = stringType_;
      return stringType_;
    }

    if (callee->name == "hasRuntimeAnnotation") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2005",
                           "hasRuntimeAnnotation() takes a declaration and an "
                           "annotation name");
      } else {
        Type* declaration = checkExpr(expr->args[0].get());
        Type* annotation = checkExpr(expr->args[1].get());
        requireAssignable(declaration, stringType_, expr->args[0]->span,
                          "declaration name");
        requireAssignable(annotation, stringType_, expr->args[1]->span,
                          "annotation name");
      }
      expr->resolvedType = boolType_;
      return boolType_;
    }

    if (callee->name == "addFunction") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2067",
                           "addFunction() takes one name");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "generated function name");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "addAnnotatedFunction") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2067",
                           "addAnnotatedFunction() takes a function name and "
                           "an annotation name");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "generated function name");
        Type* annotation = checkExpr(expr->args[1].get());
        requireAssignable(annotation, stringType_, expr->args[1]->span,
                          "generated annotation name");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "addFunctionReturning") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2067",
                           "addFunctionReturning() takes a name and an "
                           "integer");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "generated function name");
        Type* value = checkExpr(expr->args[1].get());
        requireAssignable(value, intType_, expr->args[1]->span,
                          "generated return value");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "addConstant") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2067",
                           "addConstant() takes a name and an integer");
      } else {
        Type* name = checkExpr(expr->args[0].get());
        requireAssignable(name, stringType_, expr->args[0]->span,
                          "generated constant name");
        Type* value = checkExpr(expr->args[1].get());
        requireAssignable(value, intType_, expr->args[1]->span,
                          "generated constant value");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "assert") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2064",
                           "assert() takes one boolean argument");
      } else {
        Type* condition = checkExpr(expr->args[0].get());
        requireAssignable(condition, boolType_, expr->args[0]->span,
                          "assert condition");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "assertTrue" || callee->name == "assertFalse") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2064",
                           callee->name + "() takes one boolean argument");
      } else {
        Type* condition = checkExpr(expr->args[0].get());
        requireAssignable(condition, boolType_, expr->args[0]->span,
                          "assert condition");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "assertEq") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2064",
                           "assertEq() takes two arguments");
      } else {
        Type* left = checkExpr(expr->args[0].get());
        Type* right = checkExpr(expr->args[1].get());
        if (!assignable(left, right) && !assignable(right, left)) {
          diagnostics_.error(expr->span, "E2064",
                             "assertEq arguments are not comparable");
        }
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "line") {
      if (expr->args.size() != 2) {
        diagnostics_.error(expr->span, "E2062",
                           "line() takes a file string and a line number");
      } else {
        Type* fileType = checkExpr(expr->args[0].get());
        requireAssignable(fileType, stringType_, expr->args[0]->span,
                          "line file");
        Type* lineType = checkExpr(expr->args[1].get());
        requireAssignable(lineType, intType_, expr->args[1]->span,
                          "line number");
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "print") {
      for (const auto& arg : expr->args) {
        Type* argType = checkExpr(arg.get());
        if (argType != nullptr && argType->kind == TypeKind::Expect) {
          diagnostics_.error(arg->span, "E2055",
                             "@Expect result must be consumed");
        } else if (argType != nullptr &&
                   argType->kind == TypeKind::Result) {
          diagnostics_.error(arg->span, "E2050",
                             "@Result must be handled explicitly");
        }
      }
      expr->resolvedType = voidType_;
      return voidType_;
    }

    if (callee->name == "error") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2052",
                           "error() takes exactly one string message");
      } else {
        Type* messageType = checkExpr(expr->args[0].get());
        requireAssignable(messageType, stringType_, expr->args[0]->span,
                          "error message");
      }
      expr->resolvedType = errorValueType_;
      return errorValueType_;
    }

    if (callee->name == "hardwareConcurrency") {
      if (!expr->args.empty()) {
        diagnostics_.error(expr->span, "E2090",
                           "hardwareConcurrency() takes no arguments");
      }
      expr->resolvedType = intType_;
      return intType_;
    }

    if (callee->name == "Thread" || callee->name == "ScopedThread") {
      Type* entryType = makeFunctionType({}, voidType_);
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2090",
                           "Thread() takes exactly one entry function");
        for (const auto& arg : expr->args) {
          checkExpr(arg.get());
        }
      } else {
        Expr* entry = expr->args[0].get();
        if (entry != nullptr && entry->kind == ExprKind::Lambda) {
          checkLambda(entry, entryType);
          for (const Capture& capture : entry->lambda->captures) {
            if (capture.kind == Capture::Kind::This) {
              diagnostics_.error(
                  capture.span, "E2090",
                  "a thread entry closure cannot capture 'this'; capture the "
                  "needed values by copy");
            } else if (capture.kind == Capture::Kind::ByRef) {
              diagnostics_.error(
                  capture.span, "E2090",
                  "a thread entry closure cannot capture by reference; "
                  "capture by value ([=], [x]) instead");
            }
          }
          if (entry->lambda->usesThis) {
            diagnostics_.error(entry->span, "E2090",
                               "a thread entry closure cannot use 'this'");
          }
        } else {
          Type* checked = checkExpr(entry);
          if (checked == nullptr || checked->kind != TypeKind::Function ||
              !checked->alternatives.empty() ||
              checked->returnType == nullptr ||
              checked->returnType->kind != TypeKind::Void) {
            diagnostics_.error(
                expr->span, "E2090",
                "Thread() entry must have type () -> void");
          }
        }
        if (entry != nullptr && entry->borrowDepth != 0) {
          diagnostics_.error(
              entry->span, "E2090",
              "a thread entry closure must own its captures; by-reference or "
              "call-scoped closures cannot cross threads");
        }
      }
      Type* threadResult = callee->name == "ScopedThread"
                               ? scopedThreadType_
                               : threadType_;
      expr->resolvedType = threadResult;
      return threadResult;
    }

    if (callee->name == "ScopedLock") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2092",
                           "ScopedLock() takes exactly one Mutex value");
        for (const auto& arg : expr->args) {
          checkExpr(arg.get());
        }
      } else {
        Type* mutex = checkExpr(expr->args[0].get());
        if (mutex == nullptr || mutex->kind != TypeKind::Mutex) {
          diagnostics_.error(expr->args[0]->span, "E2092",
                             "ScopedLock requires a Mutex value");
        }
        if (expr->args[0]->kind != ExprKind::Name &&
            expr->args[0]->kind != ExprKind::Member) {
          diagnostics_.error(expr->args[0]->span, "E2092",
                             "ScopedLock requires a named Mutex lvalue");
        }
      }
      expr->resolvedType = scopedLockType_;
      return scopedLockType_;
    }

    if (callee->name == "Atomic") {
      if (expr->args.size() != 1) {
        diagnostics_.error(expr->span, "E2092",
                           "Atomic() takes exactly one integer initial value");
        for (const auto& arg : expr->args) {
          checkExpr(arg.get());
        }
      } else {
        Type* initial = checkExpr(expr->args[0].get());
        requireAssignable(initial, intType_, expr->args[0]->span,
                          "Atomic initial value");
      }
      expr->resolvedType = atomicType_;
      return atomicType_;
    }

    const auto typeIt = typeMap_.find(callee->name);
    if (typeIt != typeMap_.end() &&
        (typeIt->second->kind == TypeKind::Class ||
         typeIt->second->kind == TypeKind::Interface ||
         typeIt->second->kind == TypeKind::Annotation)) {
      Type* constructed = typeIt->second;
      const Decl* constructor = nullptr;
      if (constructed->decl != nullptr) {
        for (const auto& member : constructed->decl->members) {
          if (member->kind == DeclKind::Constructor &&
              member->params.size() == expr->args.size()) {
            constructor = member.get();
            break;
          }
        }
      }
      if (constructor == nullptr) {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "no constructor for '" + constructed->name +
                                 "' takes " +
                                 std::to_string(expr->args.size()) +
                                 " argument(s)");
        }
      } else {
        if (!canAccess(constructor->access, constructed)) {
          diagnostics_.error(expr->span, "E2030",
                             "cannot access " +
                                 std::string(accessName(constructor->access)) +
                                 " constructor of '" + constructed->name +
                                 "'");
        }
        for (std::size_t i = 0; i < expr->args.size(); ++i) {
          Type* paramType =
              resolveType(constructor->params[i].type.get(), false);
          Type* argType = checkExpr(expr->args[i].get());
          requireAssignable(argType, paramType, expr->args[i]->span,
                            "constructor argument");
        }
      }
      expr->resolvedType = constructed;
      return constructed;
    }

    if (Variable* variable = lookupVariable(callee->name);
        variable != nullptr && variable->type != nullptr &&
        variable->type->kind == TypeKind::Function) {
      Type* functionType = variable->type;
      if (expr->args.size() != functionType->alternatives.size()) {
        diagnostics_.error(
            expr->span, "E2004",
            "function value '" + callee->name + "' takes " +
                std::to_string(functionType->alternatives.size()) +
                " argument(s)");
      }
      const std::size_t count =
          std::min(expr->args.size(), functionType->alternatives.size());
      for (std::size_t i = 0; i < count; ++i) {
        if (expr->args[i] != nullptr &&
            expr->args[i]->kind == ExprKind::Lambda) {
          checkLambda(expr->args[i].get(), functionType->alternatives[i]);
        } else {
          Type* argType = checkExpr(expr->args[i].get());
          requireAssignable(argType, functionType->alternatives[i],
                            expr->args[i]->span, "function argument");
        }
      }
      expr->resolvedType = functionType->returnType;
      return functionType->returnType;
    }

    const Decl* function = lookupFunction(callee->name, expr->args.size());
    if (function == nullptr) {
      diagnostics_.error(expr->span, "E2004",
                         "no function named '" + callee->name +
                             "' takes " + std::to_string(expr->args.size()) +
                             " argument(s)");
      for (const auto& arg : expr->args) {
        checkExpr(arg.get());
      }
      expr->resolvedType = errorType_;
      return errorType_;
    }
    if ((function->isResult || function->isExpect) &&
        currentFunction_ != nullptr && !currentFunction_->isResult &&
        !currentFunction_->isExpect) {
      diagnostics_.error(expr->span, "E2050",
                         "@Result/@Expect function '" + callee->name +
                             "' must be called from a @Result/@Expect "
                             "function or handled explicitly");
    }

    bool nativeDecl = function->isNative;
    for (const AnnotationUse& use : function->annotations) {
      nativeDecl = nativeDecl || use.name == "Native";
    }
    if ((nativeDecl || function->isUnsafe) && !unsafeScope_) {
      diagnostics_.error(
          expr->span, "E2096",
          "calling @Unsafe/@Native function '" + callee->name +
              "' requires an @Unsafe scope");
    }

    pushTypeScope();
    bindGenericParams(function->generics, false);
    for (std::size_t i = 0; i < expr->args.size(); ++i) {
      Type* paramType = resolveType(function->params[i].type.get(), false);
      Type* argType = nullptr;
      if (expr->args[i] != nullptr &&
          expr->args[i]->kind == ExprKind::Lambda &&
          paramType != nullptr && paramType->kind == TypeKind::Function) {
        checkLambda(expr->args[i].get(), paramType);
        argType = paramType;
      } else {
        argType = checkExpr(expr->args[i].get());
      }
      if (!(argType != nullptr && argType->kind == TypeKind::Null &&
            function->params[i].isOptional)) {
        requireAssignable(argType, paramType, expr->args[i]->span,
                          "argument " + std::to_string(i + 1));
      }
      if ((function->params[i].isUnique ||
           function->params[i].isShare) &&
          expr->args[i]->kind == ExprKind::Name) {
        if (Variable* variable =
                lookupVariable(expr->args[i]->name)) {
          if (variable->isUnique || variable->isShare) {
            variable->moved = true;
          }
        }
      }
    }
    Type* returnType = function->type
                           ? resolveType(function->type.get(), false)
                           : voidType_;
    popTypeScope();
    if (function->isExpect) {
      returnType = makeResultType(returnType, true);
    } else if (function->isResult) {
      returnType = makeResultType(returnType, false);
    }
    if (function->isOptional) {
      expr->optionalCall = true;
      expr->proxyPolicy = function->proxyPolicy;
      expr->optionalDefault = function->proxyDefault;
      expr->optionalSmart = function->isUnique || function->isShare;
    }
    expr->resolvedType = returnType;
    return returnType;
  }

  if (callee->kind == ExprKind::Member) {
    Type* baseType = checkExpr(callee->lhs.get());
    if (baseType != nullptr && baseType->kind == TypeKind::Function &&
        (callee->name == "apply" || callee->name == "accept" ||
         callee->name == "test" || callee->name == "get" ||
         callee->name == "run" || callee->name == "call")) {
      const std::size_t count =
          std::min(expr->args.size(), baseType->alternatives.size());
      if (expr->args.size() != baseType->alternatives.size()) {
        diagnostics_.error(
            expr->span, "E2004",
            "functional interface method '" + callee->name + "' takes " +
                std::to_string(baseType->alternatives.size()) +
                " argument(s)");
      }
      for (std::size_t i = 0; i < count; ++i) {
        if (expr->args[i] != nullptr &&
            expr->args[i]->kind == ExprKind::Lambda) {
          checkLambda(expr->args[i].get(), baseType->alternatives[i]);
        } else {
          Type* argType = checkExpr(expr->args[i].get());
          requireAssignable(argType, baseType->alternatives[i],
                            expr->args[i]->span,
                            "functional interface argument");
        }
      }
      expr->resolvedType = baseType->returnType;
      return baseType->returnType;
    }
    if (callee->lhs != nullptr && callee->lhs->optionalCall) {
      Type* fieldType = callee->lhs->resolvedType;
      Type* valueType =
          fieldType != nullptr && fieldType->kind == TypeKind::Pointer
              ? fieldType->pointee
              : fieldType;
      if (callee->name == "isPresent" || callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "get") {
        expr->resolvedType = valueType;
        return valueType;
      }
      if (callee->name == "orElse" && expr->args.size() == 1) {
        expr->resolvedType = valueType;
        return valueType;
      }
    }
    if (callee->lhs != nullptr && callee->lhs->optionalField) {
      Type* fieldType = callee->lhs->resolvedType;
      Type* valueType =
          fieldType != nullptr && fieldType->kind == TypeKind::Pointer
              ? fieldType->pointee
              : fieldType;
      if (callee->name == "isPresent" || callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "get") {
        expr->resolvedType = valueType;
        return valueType;
      }
      if (callee->name == "orElse" && expr->args.size() == 1) {
        expr->resolvedType = valueType;
        return valueType;
      }
    }
    if (callee->lhs != nullptr && callee->lhs->weakValue) {
      if (callee->name == "isPresent" || callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "lock" || callee->name == "upgrade") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        Type* result = makeSharePtrType(callee->lhs->resolvedType);
        expr->resolvedType = result;
        return result;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::SharePtr) {
      if (callee->name == "isPresent" || callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "get") {
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
      if (callee->name == "orElse" && expr->args.size() == 1) {
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Reflector) {
      if (callee->name == "name" || callee->name == "kind") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = stringType_;
        return stringType_;
      }
      if (callee->name == "fieldCount" || callee->name == "methodCount") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "fieldName" || callee->name == "fieldType" ||
          callee->name == "methodName" ||
          callee->name == "methodReturnType" ||
          callee->name == "annotationName") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one index");
        } else {
          Type* index = checkExpr(expr->args[0].get());
          requireAssignable(index, intType_, expr->args[0]->span,
                            "reflection index");
          if (expr->args[0]->kind == ExprKind::IntLit &&
              !baseType->alternatives.empty() &&
              baseType->alternatives[0] != nullptr &&
              baseType->alternatives[0]->decl != nullptr) {
            const Decl& target = *baseType->alternatives[0]->decl;
            std::int64_t count = 0;
            const bool fieldQuery =
                callee->name == "fieldName" || callee->name == "fieldType";
            const bool methodQuery =
                callee->name == "methodName" ||
                callee->name == "methodReturnType";
            for (const auto& member : target.members) {
              if ((fieldQuery && member->kind == DeclKind::Field) ||
                  (methodQuery && member->kind == DeclKind::Method)) {
                ++count;
              }
            }
            if (callee->name == "annotationName") {
              count = static_cast<std::int64_t>(target.annotations.size());
            }
            const std::int64_t requested = expr->args[0]->intValue;
            if (requested < 0 || requested >= count) {
              diagnostics_.error(
                  expr->args[0]->span, "E2069",
                  "reflection index " + std::to_string(requested) +
                      " is out of range (count " + std::to_string(count) + ")");
            }
          }
        }
        expr->resolvedType = stringType_;
        return stringType_;
      }
      if (callee->name == "methodParamCount") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'methodParamCount' takes one index");
        } else {
          Type* index = checkExpr(expr->args[0].get());
          requireAssignable(index, intType_, expr->args[0]->span,
                            "method index");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "methodParamType") {
        if (expr->args.size() != 2) {
          diagnostics_.error(expr->span, "E2005",
                             "'methodParamType' takes a method index and a "
                             "parameter index");
        } else {
          Type* method = checkExpr(expr->args[0].get());
          Type* parameter = checkExpr(expr->args[1].get());
          requireAssignable(method, intType_, expr->args[0]->span,
                            "method index");
          requireAssignable(parameter, intType_, expr->args[1]->span,
                            "parameter index");
        }
        expr->resolvedType = stringType_;
        return stringType_;
      }
      if (callee->name == "annotationCount") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'annotationCount' takes no arguments");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "hasAnnotation") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'hasAnnotation' takes one name");
        } else {
          Type* name = checkExpr(expr->args[0].get());
          requireAssignable(name, stringType_, expr->args[0]->span,
                            "annotation name");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Description) {
      if (callee->name == "name" || callee->name == "kind") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = stringType_;
        return stringType_;
      }
      if (callee->name == "hasAnnotation") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'hasAnnotation' takes one name argument");
        } else {
          Type* name = checkExpr(expr->args[0].get());
          requireAssignable(name, stringType_, expr->args[0]->span,
                            "annotation name");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "fieldCount" || callee->name == "methodCount") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Set) {
      if (callee->name == "add" || callee->name == "contains" ||
          callee->name == "remove") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one argument");
        } else {
          Type* value = checkExpr(expr->args[0].get());
          requireAssignable(value, baseType->pointee, expr->args[0]->span,
                            "set element");
        }
        expr->resolvedType =
            callee->name == "contains" ? boolType_ : voidType_;
        return expr->resolvedType;
      }
      if (callee->name == "length") {
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Map) {
      Type* keyType = baseType->alternatives[0];
      Type* valueType = baseType->alternatives[1];
      if (callee->name == "set") {
        if (expr->args.size() != 2) {
          diagnostics_.error(expr->span, "E2005",
                             "'set' takes a key and a value");
        } else {
          Type* key = checkExpr(expr->args[0].get());
          Type* value = checkExpr(expr->args[1].get());
          requireAssignable(key, keyType, expr->args[0]->span, "map key");
          requireAssignable(value, valueType, expr->args[1]->span,
                            "map value");
        }
        expr->resolvedType = voidType_;
        return voidType_;
      }
      if (callee->name == "get") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'get' takes one key");
        } else {
          Type* key = checkExpr(expr->args[0].get());
          requireAssignable(key, keyType, expr->args[0]->span, "map key");
        }
        Type* pointer = makePointerType(valueType);
        expr->resolvedType = pointer;
        return pointer;
      }
      if (callee->name == "contains" || callee->name == "remove") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one key");
        } else {
          Type* key = checkExpr(expr->args[0].get());
          requireAssignable(key, keyType, expr->args[0]->span, "map key");
        }
        expr->resolvedType =
            callee->name == "contains" ? boolType_ : voidType_;
        return expr->resolvedType;
      }
      if (callee->name == "length") {
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "isEmpty") {
        expr->resolvedType = boolType_;
        return boolType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Class &&
        callee->lhs != nullptr && callee->lhs->kind == ExprKind::Name) {
      Variable* variable = lookupVariable(callee->lhs->name);
      if (variable != nullptr && variable->isOptional &&
          (variable->isUnique || variable->isShare)) {
        if (callee->name == "isPresent" || callee->name == "isEmpty") {
          if (!expr->args.empty()) {
            diagnostics_.error(expr->span, "E2005",
                               "'" + callee->name + "' takes no arguments");
          }
          expr->resolvedType = boolType_;
          return boolType_;
        }
        if (callee->name == "get") {
          if (!expr->args.empty()) {
            diagnostics_.error(expr->span, "E2005",
                               "'get' takes no arguments");
          }
          expr->resolvedType = baseType;
          return baseType;
        }
        if (callee->name == "orElse") {
          if (expr->args.size() != 1) {
            diagnostics_.error(expr->span, "E2005",
                               "'orElse' takes one argument");
          } else {
            Type* fallback = checkExpr(expr->args[0].get());
            requireAssignable(fallback, baseType, expr->args[0]->span,
                              "orElse fallback");
          }
          expr->resolvedType = baseType;
          return baseType;
        }
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Queue) {
      if (callee->name == "get" || callee->name == "at") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one index");
        } else {
          Type* index = checkExpr(expr->args[0].get());
          requireAssignable(index, intType_, expr->args[0]->span,
                            "element index");
        }
        Type* pointer = makePointerType(baseType->pointee);
        expr->resolvedType = pointer;
        return pointer;
      }
      if (callee->name == "view") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'view' takes no arguments");
        }
        Type* view = makeArrayViewType(baseType->pointee);
        expr->resolvedType = view;
        return view;
      }
      if (callee->name == "pushBack" || callee->name == "pushFront") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one argument");
        } else {
          Type* value = checkExpr(expr->args[0].get());
          requireAssignable(value, baseType->pointee, expr->args[0]->span,
                            "queue element");
        }
        expr->resolvedType = voidType_;
        return voidType_;
      }
      if (callee->name == "popFront" || callee->name == "popBack") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
      if (callee->name == "length") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'length' takes no arguments");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "isEmpty") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'isEmpty' takes no arguments");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "clear") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'clear' takes no arguments");
        }
        expr->resolvedType = voidType_;
        return voidType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::ArrayView) {
      if (callee->name == "get" || callee->name == "at") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes one index");
        } else {
          Type* index = checkExpr(expr->args[0].get());
          requireAssignable(index, intType_, expr->args[0]->span,
                            "element index");
        }
        Type* pointer = makePointerType(baseType->pointee);
        expr->resolvedType = pointer;
        return pointer;
      }
      if (callee->name == "length") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'length' takes no arguments");
        }
        expr->resolvedType = intType_;
        return intType_;
      }
      if (callee->name == "isEmpty") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'isEmpty' takes no arguments");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
    }
    if (baseType != nullptr && baseType->kind == TypeKind::Pointer) {
      if (callee->name == "isPresent" || callee->name == "isEmpty") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "get") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'get' takes no arguments");
        }
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
      if (callee->name == "orElse") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'orElse' takes one argument");
        } else {
          Type* fallback = checkExpr(expr->args[0].get());
          requireAssignable(fallback, baseType->pointee, expr->args[0]->span,
                            "orElse fallback");
        }
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
    }
    if (baseType != nullptr &&
        (baseType->kind == TypeKind::Result ||
         baseType->kind == TypeKind::Expect)) {
      if (callee->name == "isOk" || callee->name == "isErr") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'" + callee->name + "' takes no arguments");
        }
        expr->resolvedType = boolType_;
        return boolType_;
      }
      if (callee->name == "unwrap") {
        if (!expr->args.empty()) {
          diagnostics_.error(expr->span, "E2005",
                             "'unwrap' takes no arguments");
        }
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
      if (callee->name == "expect") {
        if (expr->args.size() != 1) {
          diagnostics_.error(expr->span, "E2005",
                             "'expect' takes one message argument");
        } else {
          Type* messageType = checkExpr(expr->args[0].get());
          requireAssignable(messageType, stringType_, expr->args[0]->span,
                            "expect message");
        }
        expr->resolvedType = baseType->pointee;
        return baseType->pointee;
      }
      diagnostics_.error(expr->span, "E2004",
                         "no method '" + callee->name + "' on '" +
                             baseType->name + "'");
      for (const auto& arg : expr->args) {
        checkExpr(arg.get());
      }
      expr->resolvedType = errorType_;
      return errorType_;
    }
    const MethodInfo* method =
        lookupMethod(baseType, callee->name, expr->args.size());
    if (method == nullptr) {
      const FieldInfo* functionField = lookupField(baseType, callee->name);
      if (functionField != nullptr && functionField->type != nullptr &&
          functionField->type->kind == TypeKind::Function) {
        Type* functionType = functionField->type;
        const std::size_t count =
            std::min(expr->args.size(), functionType->alternatives.size());
        if (expr->args.size() != functionType->alternatives.size()) {
          diagnostics_.error(
              expr->span, "E2004",
              "function value '" + callee->name + "' takes " +
                  std::to_string(functionType->alternatives.size()) +
                  " argument(s)");
        }
        for (std::size_t i = 0; i < count; ++i) {
          if (expr->args[i] != nullptr &&
              expr->args[i]->kind == ExprKind::Lambda) {
            checkLambda(expr->args[i].get(), functionType->alternatives[i]);
          } else {
            Type* argType = checkExpr(expr->args[i].get());
            requireAssignable(argType, functionType->alternatives[i],
                              expr->args[i]->span, "function argument");
          }
        }
        expr->resolvedType = functionType->returnType;
        return functionType->returnType;
      }
      diagnostics_.error(expr->span, "E2004",
                         "no method '" + callee->name + "' on type '" +
                             baseType->name + "' with " +
                             std::to_string(expr->args.size()) +
                             " argument(s)");
      for (const auto& arg : expr->args) {
        checkExpr(arg.get());
      }
      expr->resolvedType = errorType_;
      return errorType_;
    }
    for (std::size_t i = 0; i < expr->args.size(); ++i) {
      Type* argType = checkExpr(expr->args[i].get());
      requireAssignable(argType, method->paramTypes[i], expr->args[i]->span,
                        "argument " + std::to_string(i + 1));
      if (method->decl != nullptr && i < method->decl->params.size() &&
          (method->decl->params[i].isUnique ||
           method->decl->params[i].isShare) &&
          expr->args[i]->kind == ExprKind::Name) {
        if (Variable* variable =
                lookupVariable(expr->args[i]->name)) {
          if (variable->isUnique || variable->isShare) {
            variable->moved = true;
          }
        }
      }
    }
    if (!canAccess(method->access, method->owner)) {
      diagnostics_.error(expr->span, "E2030",
                         "cannot access " +
                             std::string(accessName(method->access)) +
                             " method '" + method->name + "'");
    }
    checkLockCall(callee->lhs.get(), method, expr->span);
    if (method->decl != nullptr &&
        (method->decl->isNative || method->decl->isUnsafe) &&
        !unsafeScope_) {
      diagnostics_.error(
          expr->span, "E2096",
          "calling @Unsafe/@Native method '" + method->name +
              "' requires an @Unsafe scope");
    }
    expr->resolvedType = method->returnType;
    return method->returnType;
  }

  Type* calleeType = checkExpr(callee);
  if (calleeType != nullptr && calleeType->kind == TypeKind::Function) {
    const std::size_t count =
        std::min(expr->args.size(), calleeType->alternatives.size());
    if (expr->args.size() != calleeType->alternatives.size()) {
      diagnostics_.error(
          expr->span, "E2004",
          "function value takes " +
              std::to_string(calleeType->alternatives.size()) +
              " argument(s), but " + std::to_string(expr->args.size()) +
              " were given");
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (expr->args[i] != nullptr &&
          expr->args[i]->kind == ExprKind::Lambda) {
        checkLambda(expr->args[i].get(), calleeType->alternatives[i]);
      } else {
        Type* argType = checkExpr(expr->args[i].get());
        requireAssignable(argType, calleeType->alternatives[i],
                          expr->args[i]->span, "function argument");
      }
    }
    expr->resolvedType = calleeType->returnType;
    return calleeType->returnType;
  }
  diagnostics_.error(expr->span, "E2005", "expression is not callable");
  expr->resolvedType = errorType_;
  return errorType_;
}

Type* Checker::checkMember(Expr* expr) {
  if (expr->lhs != nullptr && expr->lhs->kind == ExprKind::Name &&
      lookupVariable(expr->lhs->name) == nullptr) {
    Type* candidate = lookupType(expr->lhs->name);
    if (candidate != nullptr && candidate->kind == TypeKind::Enum) {
      for (const EnumVariantInfo& variant : candidate->variants) {
        if (variant.name == expr->name) {
          expr->resolvedType = candidate;
          return candidate;
        }
      }
      diagnostics_.error(expr->span, "E2024",
                         "enumerate '" + candidate->name +
                             "' has no variant '" + expr->name + "'");
      expr->resolvedType = errorType_;
      return errorType_;
    }
  }

  Type* baseType = checkExpr(expr->lhs.get());
  if (baseType != nullptr && baseType->kind == TypeKind::Class &&
      expr->lhs != nullptr &&
      expr->name != "isPresent" && expr->name != "isEmpty" &&
      expr->name != "get" && expr->name != "orElse") {
    checkOptionalDirectAccess(expr, "member access");
  }
  if (const FieldInfo* field = lookupField(baseType, expr->name)) {
    if (!canAccess(field->access, field->owner)) {
      diagnostics_.error(expr->span, "E2030",
                         "cannot access " +
                             std::string(accessName(field->access)) +
                             " field '" + expr->name + "'");
    }
    if (field->decl != nullptr && field->decl->isWeak) {
      expr->weakValue = true;
    }
    if (field->decl != nullptr && field->decl->isOptional) {
      expr->optionalField = true;
      expr->proxyPolicy = field->decl->proxyPolicy;
      expr->optionalDefault = field->decl->proxyDefault;
      expr->optionalSmart =
          field->decl->isUnique || field->decl->isShare;
    }
    expr->resolvedType = field->type;
    return field->type;
  }
  diagnostics_.error(expr->span, "E2004",
                     "type '" + baseType->name + "' has no member '" +
                         expr->name + "'");
  expr->resolvedType = errorType_;
  return errorType_;
}

bool Checker::lockCaptureRoot(const Expr* expr, std::string& name,
                              bool& isThis) const {
  if (expr == nullptr) {
    return false;
  }
  if (expr->kind == ExprKind::Name) {
    name = expr->name;
    return true;
  }
  if (expr->kind == ExprKind::This) {
    isThis = true;
    return true;
  }
  if (expr->kind == ExprKind::Member || expr->kind == ExprKind::Index) {
    return lockCaptureRoot(expr->lhs.get(), name, isThis);
  }
  return false;
}

const Checker::LockCaptureInfo* Checker::findLockCapture(
    const std::string& name) const {
  for (auto it = lockBlocks_.rbegin(); it != lockBlocks_.rend(); ++it) {
    const auto found = it->captures.find(name);
    if (found != it->captures.end()) {
      return &found->second;
    }
  }
  return nullptr;
}

void Checker::validateLockBlock(Stmt* stmt) {
  if (!lockBlocks_.empty()) {
    diagnostics_.error(stmt->span, "E2094",
                       "nested @Lock blocks are not supported in v0");
  }
  if (stmt->lockMode != Stmt::LockMode::Exclusive) {
    diagnostics_.error(
        stmt->span, "E2094",
        "legacy @Lock form; migrate to "
        "@Lock(Exclusive, mutex, [captures])");
    return;
  }
  if (stmt->lockMutex == nullptr) {
    diagnostics_.error(stmt->span, "E2094",
                       "@Lock(Exclusive, ...) requires a Mutex value");
    return;
  }
  Type* mutex = checkExpr(stmt->lockMutex.get());
  if (mutex == nullptr || mutex->kind != TypeKind::Mutex) {
    diagnostics_.error(stmt->lockMutex->span, "E2094",
                       "@Lock requires a Mutex value");
  } else if (stmt->lockMutex->kind != ExprKind::Name &&
             stmt->lockMutex->kind != ExprKind::Member) {
    diagnostics_.error(stmt->lockMutex->span, "E2094",
                       "@Lock requires a named Mutex lvalue");
  }
  LockBlockContext context;
  context.blockDepth = scopes_.size();
  std::set<std::string> seen;
  for (const Stmt::LockCaptureSpec& capture : stmt->lockCaptures) {
    if (!seen.insert(capture.name).second) {
      diagnostics_.error(capture.span, "E2094",
                         "duplicate @Lock capture '" + capture.name + "'");
      continue;
    }
    std::size_t scopeIndex = 0;
    Variable* variable = lookupVariable(capture.name, &scopeIndex);
    if (variable == nullptr) {
      diagnostics_.error(capture.span, "E2094",
                         "cannot capture unknown identifier '" +
                             capture.name + "'");
      continue;
    }
    if (capture.kind == Stmt::LockCaptureKind::Value) {
      Type* type = variable->type;
      const bool valueType =
          type != nullptr &&
          (type->kind == TypeKind::Int || type->kind == TypeKind::Decimal ||
           type->kind == TypeKind::Float || type->kind == TypeKind::Bool ||
           type->kind == TypeKind::Char || type->kind == TypeKind::String ||
           type->kind == TypeKind::Enum);
      const bool deepCopyable =
          type != nullptr && type->kind == TypeKind::Class &&
          type->decl != nullptr &&
          type->decl->copyKind == Decl::CopyKind::Deep;
      if (!valueType && !deepCopyable) {
        diagnostics_.error(
            capture.span, "E2094",
            "value capture of class/container/pointer '" + capture.name +
                "' requires @Copy(Deep) on the class (shallow copies share "
                "@Share/pointer fields); use [&name] or [const&name]");
        continue;
      }
    }
    context.captures[capture.name] =
        LockCaptureInfo{capture.kind,
                        static_cast<std::size_t>(variable->scopeDepth)};
  }
  lockBlocks_.push_back(std::move(context));
}

void Checker::checkLockWrite(Expr* target, Span span) {
  if (lockBlocks_.empty() || target == nullptr) {
    return;
  }
  std::string name;
  bool isThis = false;
  if (!lockCaptureRoot(target, name, isThis) || isThis) {
    return;
  }
  std::size_t scopeIndex = 0;
  if (lookupVariable(name, &scopeIndex) == nullptr) {
    return;
  }
  const LockBlockContext& context = lockBlocks_.back();
  if (scopeIndex >= context.blockDepth) {
    return;
  }
  const auto found = context.captures.find(name);
  if (found == context.captures.end()) {
    diagnostics_.error(
        span, "E2094",
        "object '" + name +
            "' is not in the @Lock capture list and cannot be modified");
    return;
  }
  if (found->second.kind == Stmt::LockCaptureKind::ConstRef) {
    diagnostics_.error(span, "E2094",
                       "const& capture '" + name +
                           "' is read-only and cannot be modified");
  }
}

void Checker::checkLockCall(const Expr* base, const MethodInfo* method,
                            Span span) {
  if (lockBlocks_.empty() || base == nullptr) {
    return;
  }
  std::string name;
  bool isThis = false;
  if (!lockCaptureRoot(base, name, isThis) || isThis) {
    return;
  }
  std::size_t scopeIndex = 0;
  if (lookupVariable(name, &scopeIndex) == nullptr) {
    return;
  }
  const LockBlockContext& context = lockBlocks_.back();
  if (scopeIndex >= context.blockDepth) {
    return;
  }
  const auto found = context.captures.find(name);
  bool requireReadOnly = false;
  bool uncaptured = false;
  if (found == context.captures.end()) {
    uncaptured = true;
  } else if (found->second.kind == Stmt::LockCaptureKind::MutRef ||
             found->second.kind == Stmt::LockCaptureKind::Value) {
    return;
  } else {
    requireReadOnly = true;
  }
  if (uncaptured) {
    Variable* variable = lookupVariable(name);
    if (variable == nullptr || variable->type == nullptr ||
        (variable->type->kind != TypeKind::Class &&
         variable->type->kind != TypeKind::Interface)) {
      return;
    }
    requireReadOnly = true;
  }
  bool readOnly = false;
  if (method != nullptr && method->decl != nullptr) {
    for (const AnnotationUse& use : method->decl->annotations) {
      if (use.name == "ReadOnly") {
        readOnly = true;
        break;
      }
    }
  }
  if (!readOnly) {
    if (uncaptured) {
      diagnostics_.error(
          span, "E2094",
          "object '" + name +
              "' is not in the @Lock capture list; only @ReadOnly methods "
              "may be called on it");
    } else {
      diagnostics_.error(
          span, "E2094",
          "const& capture '" + name +
              "' can only call @ReadOnly methods; annotate the method or use "
              "[&" + name + "]");
    }
  }
}

void Checker::checkStmt(Stmt* stmt) {
  if (stmt == nullptr) {
    return;
  }
  switch (stmt->kind) {
    case StmtKind::Block: {
      validateAnnotations(stmt->annotations, "block", stmt->span);
      for (const AnnotationUse& use : stmt->annotations) {
        if (use.name != "Retention") {
          continue;
        }
        const Expr* argument = use.args.empty() ? nullptr : use.args[0].get();
        if (argument == nullptr || argument->kind != ExprKind::Name) {
          diagnostics_.error(use.span, "E2044",
                             "@Retention on a block requires Compile or "
                             "Runtime");
          continue;
        }
        if (argument->name == "Compile" || argument->name == "compile") {
          stmt->blockRetention = BlockRetention::Compile;
        } else if (argument->name == "Runtime" ||
                   argument->name == "runtime") {
          stmt->blockRetention = BlockRetention::Runtime;
        } else {
          diagnostics_.error(argument->span, "E2044",
                             "@Retention on a block requires Compile or "
                             "Runtime");
        }
      }
      if (stmt->hasLock) {
        validateLockBlock(stmt);
      }
      const bool savedBlockUnsafe = unsafeScope_;
      if (stmt->unsafeBlock) {
        unsafeScope_ = true;
      } else if (stmt->safeBlock) {
        unsafeScope_ = false;
      }
      checkBlock(stmt);
      unsafeScope_ = savedBlockUnsafe;
      if (stmt->hasLock && !lockBlocks_.empty()) {
        lockBlocks_.pop_back();
      }
      return;
    }
    case StmtKind::Var: {
      Type* type = stmt->type ? resolveType(stmt->type.get(), false) : nullptr;
      if (stmt->radix != 0) {
        if (stmt->radix != 2 && stmt->radix != 8 && stmt->radix != 10 &&
            stmt->radix != 16) {
          diagnostics_.error(stmt->span, "E2066",
                             "@Radix must be 2, 8, 10 or 16");
        } else if (type != nullptr && !type->isInteger()) {
          diagnostics_.error(stmt->span, "E2066",
                             "@Radix is only supported for integer types");
        }
      }
      if (stmt->isOptional && stmt->proxyPolicy == ProxyPolicy::Default &&
          stmt->optionalDefault != nullptr) {
        Type* expected = type;
        if (type != nullptr && type->kind == TypeKind::Pointer) {
          expected = type->pointee;
        }
        Type* fallback = checkExpr(stmt->optionalDefault.get());
        if (expected != nullptr) {
          requireAssignable(fallback, expected, stmt->optionalDefault->span,
                            "@Optional Default(expr)");
        }
      }
      if (stmt->constructureInit) {
        if (type == nullptr || type->kind != TypeKind::Class ||
            type->decl == nullptr) {
          diagnostics_.error(stmt->span, "E2076",
                             "@Constructure requires a class target type");
          return;
        }
        std::vector<const Decl*> candidates;
        for (const auto& member : type->decl->members) {
          if (member->kind != DeclKind::Constructor) {
            continue;
          }
          bool marked = false;
          for (const AnnotationUse& use : member->annotations) {
            if (use.name == "Constructure") {
              marked = true;
            }
          }
          if ((!marked && !type->decl->isProxy) ||
              member->params.size() != stmt->constructureArgs.size()) {
            continue;
          }
          std::vector<std::size_t> order(member->params.size(), 0);
          std::vector<bool> used(stmt->constructureArgs.size(), false);
          bool matched = true;
          for (std::size_t p = 0; p < member->params.size(); ++p) {
            bool found = false;
            for (std::size_t a = 0; a < stmt->constructureArgs.size(); ++a) {
              if (!used[a] && !stmt->constructureArgs[a].name.empty() &&
                  stmt->constructureArgs[a].name == member->params[p].name) {
                order[p] = a;
                used[a] = true;
                found = true;
                break;
              }
            }
            if (!found) {
              for (std::size_t a = 0; a < stmt->constructureArgs.size(); ++a) {
                if (!used[a]) {
                  order[p] = a;
                  used[a] = true;
                  found = true;
                  break;
                }
              }
            }
            if (!found) {
              matched = false;
              break;
            }
          }
          if (!matched) {
            continue;
          }
          bool typesMatch = true;
          for (std::size_t p = 0; p < member->params.size(); ++p) {
            Type* parameterType =
                resolveType(member->params[p].type.get(), false);
            Type* valueType = checkExpr(
                stmt->constructureArgs[order[p]].value.get());
            Type* argumentType =
                stmt->constructureArgs[order[p]].type != nullptr
                    ? resolveType(
                          stmt->constructureArgs[order[p]].type.get(), false)
                    : valueType;
            if (parameterType == nullptr || argumentType == nullptr ||
                valueType == nullptr ||
                !assignable(argumentType, parameterType) ||
                !assignable(valueType, parameterType)) {
              typesMatch = false;
              break;
            }
          }
          if (!typesMatch) {
            continue;
          }
          if (!canAccess(member->access, type)) {
            continue;
          }
          candidates.push_back(member.get());
          if (candidates.size() > 1) {
            break;
          }
        }
        if (candidates.empty()) {
          diagnostics_.error(
              stmt->span, "E2076",
              "no @Constructure constructor of '" + type->name +
                  "' matches the typed brace initializer");
          return;
        }
        if (candidates.size() > 1) {
          diagnostics_.error(stmt->span, "E2076",
                             "ambiguous @Constructure constructor for '" +
                                 type->name + "'");
          return;
        }
        stmt->constructureCtor = candidates.front();
        // Recompute the order for the selected constructor.
        std::vector<bool> used(stmt->constructureArgs.size(), false);
        stmt->constructureOrder.assign(stmt->constructureCtor->params.size(), 0);
        for (std::size_t p = 0; p < stmt->constructureCtor->params.size(); ++p) {
          std::size_t selected = 0;
          bool found = false;
          for (std::size_t a = 0; a < stmt->constructureArgs.size(); ++a) {
            if (!used[a] && !stmt->constructureArgs[a].name.empty() &&
                stmt->constructureArgs[a].name ==
                    stmt->constructureCtor->params[p].name) {
              selected = a;
              found = true;
              break;
            }
          }
          if (!found) {
            for (std::size_t a = 0; a < stmt->constructureArgs.size(); ++a) {
              if (!used[a]) {
                selected = a;
                found = true;
                break;
              }
            }
          }
          used[selected] = true;
          stmt->constructureOrder[p] = selected;
        }
        declareVariable(stmt->name, type, stmt->isConst, nullptr, stmt->span,
                        stmt->isOptional, stmt->proxyPolicy,
                        stmt->optionalDefault.get());
        return;
      }

      if (stmt->isUnique && stmt->isShare) {
        diagnostics_.error(stmt->span, "E2065",
                           "a variable cannot be both @Unique and @Share");
      }
      if (stmt->isWeak && (stmt->isUnique || stmt->isShare)) {
        diagnostics_.error(stmt->span, "E2067",
                           "@Weak cannot be combined with @Unique or @Share");
      }
      if ((stmt->isUnique || stmt->isShare) && type != nullptr &&
          type->kind != TypeKind::Class &&
          type->kind != TypeKind::Pointer) {
        diagnostics_.error(
            stmt->span, "E2065",
            "@Unique/@Share requires a class or pointer type; use a value "
            "for primitive data");
      }
      if (stmt->isWeak && type != nullptr && type->kind != TypeKind::Class &&
          type->kind != TypeKind::Interface) {
        diagnostics_.error(stmt->span, "E2067",
                           "@Weak requires a class or interface type");
      }
      if (stmt->isWeak && stmt->expr != nullptr &&
          stmt->expr->kind != ExprKind::NullLit) {
        if (stmt->expr->kind == ExprKind::Name) {
          Variable* source = lookupVariable(stmt->expr->name);
          if (source == nullptr ||
              (!source->isShare && !source->isWeak)) {
            diagnostics_.error(
                stmt->span, "E2067",
                "@Weak can only bind an @Share owner or another @Weak value");
          }
        } else {
          diagnostics_.error(
              stmt->span, "E2067",
              "@Weak initialization requires an @Share/@Weak value or null");
        }
      }

      if (stmt->expr != nullptr &&
          stmt->expr->kind == ExprKind::ObjectLit) {
        if (type == nullptr || type->kind != TypeKind::Class ||
            type->decl == nullptr) {
          diagnostics_.error(stmt->span, "E2005",
                             "object literal requires a class target type");
        } else {
          std::set<std::string> seen;
          for (const ObjectField& field : stmt->expr->fields) {
            if (!seen.insert(field.name).second) {
              diagnostics_.error(field.span, "E2002",
                                 "duplicate object field '" + field.name + "'");
              continue;
            }
            const FieldInfo* info = lookupField(type, field.name);
            if (info == nullptr) {
              diagnostics_.error(field.span, "E2008",
                                 "class '" + type->name +
                                     "' has no field '" + field.name + "'");
              continue;
            }
            if (!canAccess(info->access, info->owner)) {
              diagnostics_.error(field.span, "E2030",
                                 "cannot initialize private field '" +
                                     field.name + "'");
            }
            Type* value = checkExpr(field.value.get());
            requireAssignable(value, info->type, field.span, "object field");
            checkClosureEscape(field.value.get(),
                               "be stored in an object field");
          }
          stmt->expr->resolvedType = type;
        }
        declareVariable(stmt->name, type, stmt->isConst, nullptr, stmt->span, stmt->isOptional, stmt->proxyPolicy, stmt->optionalDefault.get());
        return;
      }

      if (stmt->braceInit) {
        if (type == nullptr) {
          diagnostics_.error(stmt->span, "E2005",
                             "brace initialization requires a type");
        } else if (type->kind != TypeKind::Class ||
                   type->decl == nullptr) {
          diagnostics_.error(stmt->span, "E2049",
                             "brace initialization requires a class type");
        } else {
          bool aggregate =
              type->base == nullptr || type->base->name == "class";
          if (!type->interfaces.empty()) {
            aggregate = false;
          }
          for (const auto& member : type->decl->members) {
            if (member->kind == DeclKind::Constructor) {
              aggregate = false;
            }
            if (member->kind == DeclKind::Field &&
                member->access != Access::Public) {
              aggregate = false;
            }
            if (member->kind == DeclKind::Method &&
                (member->isVirtual || member->isOverride)) {
              aggregate = false;
            }
          }
          if (!aggregate) {
            diagnostics_.error(
                stmt->span, "E2049",
                "brace initialization requires an aggregate class "
                "(no constructors, public fields, no base/interface/virtual)");
          } else if (stmt->initArgs.size() > type->fields.size()) {
            diagnostics_.error(stmt->span, "E2049",
                               "too many brace initializers for '" +
                                   type->name + "'");
          } else {
            for (std::size_t i = 0; i < stmt->initArgs.size(); ++i) {
              Type* argType = checkExpr(stmt->initArgs[i].get());
              requireAssignable(argType, type->fields[i].type,
                                stmt->initArgs[i]->span,
                                "brace initializer");
              checkClosureEscape(stmt->initArgs[i].get(),
                                 "be stored in a field");
            }
          }
        }
        declareVariable(stmt->name, type, stmt->isConst, nullptr, stmt->span, stmt->isOptional, stmt->proxyPolicy, stmt->optionalDefault.get());
        if (Variable* declared = lookupVariable(stmt->name)) {
          declared->isUnique = stmt->isUnique;
          declared->isShare = stmt->isShare;
          declared->isWeak = stmt->isWeak;
        }
        return;
      }

      Type* initType = nullptr;
      if (stmt->expr != nullptr &&
          stmt->expr->kind == ExprKind::ArrayLit) {
        if (type == nullptr ||
            (type->kind != TypeKind::Queue &&
             type->kind != TypeKind::Array)) {
          diagnostics_.error(stmt->span, "E2005",
                             "array literal requires an Array or fixed "
                             "array target type");
        } else {
          for (const auto& element : stmt->expr->args) {
            Type* elementType = checkExpr(element.get());
            requireAssignable(elementType, type->pointee, element->span,
                              "array element");
            checkClosureEscape(element.get(),
                               "be stored in a container literal");
          }
          stmt->expr->resolvedType = type;
        }
        initType = type;
      } else if (stmt->expr != nullptr &&
                 stmt->expr->kind == ExprKind::Lambda && type != nullptr &&
                 type->kind == TypeKind::Function) {
        checkLambda(stmt->expr.get(), type);
        initType = type;
      } else {
        initType = stmt->expr ? checkExpr(stmt->expr.get()) : nullptr;
      }
      if (!stmt->isOptional && stmt->expr != nullptr &&
          stmt->expr->kind == ExprKind::Name) {
        checkOptionalDirectAccess(stmt->expr.get(), "implicit conversion");
      }
      if (stmt->expr != nullptr && stmt->expr->kind == ExprKind::Name) {
        if (Variable* source = lookupVariable(stmt->expr->name)) {
          if (source->isUnique && !stmt->expr->optionalAccess) {
            if (!stmt->isUnique || type == nullptr ||
                source->type != type) {
              diagnostics_.error(
                  stmt->span, "E2045",
                  "@Unique value '" + stmt->expr->name +
                      "' can only be moved into another @Unique variable");
            } else {
              source->moved = true;
            }
          } else if (source->isShare && !stmt->isShare &&
                     !stmt->isWeak) {
            diagnostics_.error(
                stmt->span, "E2045",
                "@Share value '" + stmt->expr->name +
                    "' can only be copied into another @Share/@Weak variable");
          }
        }
      }
      if (type == nullptr) {
        type = initType != nullptr ? initType : errorType_;
      } else if (initType != nullptr) {
        if (initType->kind == TypeKind::Null) {
          if (!stmt->isOptional) {
            diagnostics_.error(stmt->span, "E2046",
                               "null requires an @Optional variable");
          }
        } else {
          requireAssignable(initType, type, stmt->span,
                            "variable initializer");
        }
      }
      if (stmt->type == nullptr && type != nullptr) {
        stmt->type = std::make_unique<TypeExpr>();
        stmt->type->kind = TypeExprKind::Named;
        stmt->type->name = type->name;
        stmt->type->resolved = type;
      }
      if (stmt->isUnique && stmt->isShare) {
        diagnostics_.error(stmt->span, "E2065",
                           "a variable cannot be both @Unique and @Share");
      }
      if ((stmt->isUnique || stmt->isShare) && type != nullptr &&
          type->kind != TypeKind::Class &&
          type->kind != TypeKind::Pointer) {
        diagnostics_.error(
            stmt->span, "E2065",
            "@Unique/@Share requires a class or pointer type; use a value "
            "for primitive data");
      }
      if (stmt->isOptional && type != nullptr &&
          type->kind != TypeKind::Pointer && type->kind != TypeKind::Class) {
        diagnostics_.error(stmt->span, "E2047",
                           "@Optional requires a pointer, @Unique or @Share");
      }
      if (stmt->isUnsigned && stmt->isSigned) {
        diagnostics_.error(stmt->span, "E2053",
                           "a variable cannot be both @Signed and @Unsigned");
      }
      if ((stmt->isUnsigned || stmt->isSigned) && type != nullptr &&
          type->name != "integer") {
        diagnostics_.error(stmt->span, "E2054",
                           "@Unsigned/@Signed require an integer variable");
      }
      if (stmt->length != 0) {
        if (stmt->length != 8 && stmt->length != 16 &&
            stmt->length != 32 && stmt->length != 64 &&
            stmt->length != 128) {
          diagnostics_.error(stmt->span, "E2056",
                             "@Length width must be 8, 16, 32, 64 or 128");
        } else if (stmt->length == 128 &&
                   (type == nullptr || type->name != "integer")) {
          diagnostics_.error(stmt->span, "E2057",
                             "@Length(128) is only supported for integer");
        } else if (type == nullptr ||
                   (type->name != "integer" && type->name != "character" &&
                    type->name != "decimal" && type->name != "boolean")) {
          diagnostics_.error(stmt->span, "E2057",
                             "@Length is only valid on basic numeric types");
        }
      }
      if (stmt->expr == nullptr && type != nullptr &&
          type->kind == TypeKind::Class && type->decl != nullptr) {
        for (const auto& member : type->decl->members) {
          if (member->kind == DeclKind::Constructor &&
              member->params.empty()) {
            if (!canAccess(member->access, type)) {
              diagnostics_.error(stmt->span, "E2030",
                                 "cannot access default constructor of '" +
                                     type->name + "'");
            }
            break;
          }
        }
        for (const auto& member : type->decl->members) {
          if (member->kind == DeclKind::Destructor) {
            if (!canAccess(member->access, type)) {
              diagnostics_.error(stmt->span, "E2030",
                                 "cannot access destructor of '" + type->name +
                                     "'");
            }
            break;
          }
        }
      }
      if (type != nullptr &&
          (type->kind == TypeKind::Thread ||
           type->kind == TypeKind::ScopedThread) &&
          stmt->expr != nullptr &&
          stmt->expr->kind != ExprKind::Call) {
        diagnostics_.error(
            stmt->span, "E2090",
            "Thread values are move-only: construct the thread in place with "
            "Thread(entry) instead of copying another thread");
      }
      if (type != nullptr && type->kind == TypeKind::Mutex &&
          stmt->expr != nullptr) {
        diagnostics_.error(
            stmt->span, "E2091",
            "Mutex values cannot be copied; declare the mutex in place or "
            "keep it as a class field");
      }
      if (type != nullptr && type->kind == TypeKind::ScopedLock) {
        const bool constructed =
            stmt->expr != nullptr && stmt->expr->kind == ExprKind::Call &&
            stmt->expr->lhs != nullptr &&
            stmt->expr->lhs->kind == ExprKind::Name &&
            stmt->expr->lhs->name == "ScopedLock";
        if (!constructed) {
          diagnostics_.error(
              stmt->span, "E2092",
              "ScopedLock must be initialized with ScopedLock(mutex) and "
              "cannot be copied");
        }
      }
      if (type != nullptr && type->kind == TypeKind::Atomic &&
          (stmt->expr == nullptr || stmt->expr->kind != ExprKind::Call)) {
        diagnostics_.error(
            stmt->span, "E2092",
            "Atomic values cannot be copied; initialize in place with "
            "Atomic(initial)");
      }
      if (type != nullptr && type->kind == TypeKind::Class &&
          stmt->expr != nullptr && stmt->expr->kind != ExprKind::Call &&
          classRequiresExplicitCopy(type)) {
        diagnostics_.error(
            stmt->span, "E2095",
            "class '" + type->name +
                "' contains owning/pointer fields and requires an explicit "
                "@Copy(Deep) or @Copy(Shallow) before it can be copied");
      }
      declareVariable(stmt->name, type, stmt->isConst, nullptr, stmt->span, stmt->isOptional, stmt->proxyPolicy, stmt->optionalDefault.get());
      if (Variable* declared = lookupVariable(stmt->name)) {
        declared->isUnique = stmt->isUnique;
        declared->isShare = stmt->isShare;
          declared->isWeak = stmt->isWeak;
          declared->borrowDepth =
              stmt->expr != nullptr ? stmt->expr->borrowDepth : 0;
      }
      return;
    }
    case StmtKind::Expr: {
      Type* type = checkExpr(stmt->expr.get());
      if (type != nullptr && type->kind == TypeKind::ErrorValue) {
        diagnostics_.error(stmt->span, "E2052",
                           "error(...) can only be returned from a "
                           "@Result/@Expect function");
      }
      if (stmt->expr->kind == ExprKind::Call && stmt->expr->lhs != nullptr &&
          stmt->expr->lhs->kind == ExprKind::Name) {
        const Decl* function = lookupFunction(stmt->expr->lhs->name,
                                              stmt->expr->args.size());
        if (function != nullptr && function->isExpect) {
          diagnostics_.error(stmt->span, "E2055",
                             "@Expect result must be consumed");
        }
      }
      return;
    }
    case StmtKind::If:
      if (stmt->isLet) {
        Type* value = checkExpr(stmt->expr.get());
        if (value != nullptr && value->kind == TypeKind::Pointer) {
          pushScope(Scope::Kind::Block);
          declareVariable(stmt->letName, value->pointee, true, nullptr,
                          stmt->span);
          checkStmt(stmt->thenBranch.get());
          popScope();
          checkStmt(stmt->elseBranch.get());
          return;
        }
        if (value == nullptr ||
            (value->kind != TypeKind::Result &&
             value->kind != TypeKind::Expect)) {
          diagnostics_.error(stmt->span, "E2005",
                             "if let requires a Result or Expect value");
        } else {
          pushScope(Scope::Kind::Block);
          declareVariable(stmt->letName, value->pointee, true, nullptr,
                          stmt->span);
          checkStmt(stmt->thenBranch.get());
          popScope();
          checkStmt(stmt->elseBranch.get());
          return;
        }
        checkStmt(stmt->thenBranch.get());
        checkStmt(stmt->elseBranch.get());
        return;
      }
      requireAssignable(checkExpr(stmt->condition.get()), boolType_,
                        stmt->span, "if condition");
      {
        std::vector<std::pair<std::string, bool>> undo;
        applyOptionalNarrowing(stmt->condition.get(), true, undo);
        checkStmt(stmt->thenBranch.get());
        restoreOptionalNarrowing(undo);
        applyOptionalNarrowing(stmt->condition.get(), false, undo);
        checkStmt(stmt->elseBranch.get());
        restoreOptionalNarrowing(undo);
      }
      return;
    case StmtKind::ForIn: {
      Type* iterable = checkExpr(stmt->expr.get());
      if (iterable == nullptr ||
          (iterable->kind != TypeKind::Array &&
           iterable->kind != TypeKind::Queue)) {
        diagnostics_.error(stmt->span, "E2005",
                           "for-in requires an Array or fixed array");
      } else {
        pushScope(Scope::Kind::Block);
        declareVariable(stmt->name, iterable->pointee, true, nullptr,
                        stmt->span);
        ++loopDepth_;
        checkStmt(stmt->thenBranch.get());
        --loopDepth_;
        popScope();
        return;
      }
      ++loopDepth_;
      checkStmt(stmt->thenBranch.get());
      --loopDepth_;
      return;
    }
    case StmtKind::Switch: {
      Type* value = checkExpr(stmt->condition.get());
      if (stmt->isMatch) {
        const bool supported =
            value != nullptr &&
            (value->kind == TypeKind::Enum || value->kind == TypeKind::Int ||
             value->kind == TypeKind::String ||
             value->kind == TypeKind::Char || value->kind == TypeKind::Bool);
        if (!supported) {
          diagnostics_.error(
              stmt->span, "E2077",
              "match requires an enumerate, integer, string, character or "
              "boolean value");
        }
        if (stmt->matchPatterns.size() != stmt->switchBodies.size()) {
          diagnostics_.error(stmt->span, "E2005", "malformed match branches");
        }
        MatchCoverage coverage;
        for (std::size_t i = 0;
             i < stmt->matchPatterns.size() && i < stmt->switchBodies.size();
             ++i) {
          pushScope(Scope::Kind::Block);
          checkMatchPattern(stmt->matchPatterns[i].get(), value, coverage,
                            false, stmt->matchPatterns[i]->span);
          checkStmt(stmt->switchBodies[i].get());
          popScope();
        }
        if (supported) {
          if (value->kind == TypeKind::Enum) {
            if (!coverage.exhaustive) {
              for (const EnumVariantInfo& variant : value->variants) {
                if (coverage.variants.count(variant.name) == 0) {
                  diagnostics_.error(stmt->span, "E2077",
                                     "match does not cover variant '" +
                                         variant.name + "'");
                }
              }
            }
          } else if (value->kind == TypeKind::Bool) {
            if (!coverage.exhaustive &&
                (coverage.variants.count("true") == 0 ||
                 coverage.variants.count("false") == 0)) {
              diagnostics_.error(
                  stmt->span, "E2077",
                  "match on a boolean value must cover 'true' and 'false' or "
                  "add a '_' branch");
            }
          } else if (!coverage.exhaustive) {
            diagnostics_.error(stmt->span, "E2077",
                               "match on a non-enumerate value requires a "
                               "'_' branch");
          }
        }
        return;
      }
      if (value != nullptr && value->kind != TypeKind::Int &&
          value->kind != TypeKind::String && value->kind != TypeKind::Enum &&
          value->kind != TypeKind::Char && value->kind != TypeKind::Bool) {
        diagnostics_.error(stmt->span, "E2005",
                           "switch requires an integer, string, "
                           "character, boolean, or enum value");
      }
      bool sawDefault = false;
      if (stmt->switchValues.size() != stmt->switchBodies.size()) {
        diagnostics_.error(stmt->span, "E2005",
                           "malformed switch branches");
      }
      for (std::size_t i = 0;
           i < stmt->switchValues.size() && i < stmt->switchBodies.size();
           ++i) {
        if (stmt->switchValues[i] == nullptr) {
          if (sawDefault) {
            diagnostics_.error(stmt->span, "E2005",
                               "duplicate default branch");
          }
          sawDefault = true;
        } else {
          Type* caseType = checkExpr(stmt->switchValues[i].get());
          requireAssignable(caseType, value, stmt->switchValues[i]->span,
                            "switch case");
        }
        checkStmt(stmt->switchBodies[i].get());
      }
      return;
    }
    case StmtKind::Destructure: {
      Type* declaredTarget = checkExpr(stmt->expr.get());
      Type* targetType = declaredTarget;
      if (stmt->expr->kind == ExprKind::Name) {
        Variable* variable = lookupVariable(stmt->expr->name);
        if (variable != nullptr && variable->isOptional) {
          const bool narrowed = variable->narrowed;
          const ProxyPolicy policy = variable->proxyPolicy;
          const Expr* fallback = variable->optionalDefault;
          if (declaredTarget != nullptr &&
              declaredTarget->kind == TypeKind::Pointer) {
            targetType = declaredTarget->pointee;
          }
          if (!narrowed && policy == ProxyPolicy::Error) {
            diagnostics_.error(
                stmt->span, "E2048",
                "un-narrowed @Optional value '" + stmt->expr->name +
                    "' cannot be destructured; narrow it with `!= null`/"
                    "`isPresent()` or configure @Optional(Skip|Abort|Default("
                    "expr))");
            return;
          }
          stmt->optionalDestructure = true;
          stmt->optionalDestructureNarrowed = narrowed;
          stmt->proxyPolicy = policy;
          stmt->optionalDefaultRef = fallback;
          if (!narrowed && policy == ProxyPolicy::Default &&
              fallback != nullptr && targetType != nullptr) {
            Type* fallbackType = checkExpr(const_cast<Expr*>(fallback));
            requireAssignable(fallbackType, targetType, fallback->span,
                              "@Optional Default(expr)");
          }
        }
      }
      if (targetType == nullptr ||
          (targetType->kind != TypeKind::Class &&
           targetType->kind != TypeKind::Interface)) {
        diagnostics_.error(stmt->span, "E2005",
                           "destructuring requires a class or interface value");
        return;
      }
      if (targetType->decl != nullptr && targetType->decl->isProxy &&
          targetType->decl->proxyPolicy == ProxyPolicy::Error) {
        bool hasDestructure = false;
        for (const auto& member : targetType->decl->members) {
          for (const AnnotationUse& use : member->annotations) {
            if (use.name == "Destructure") {
              hasDestructure = true;
            }
          }
        }
        if (!hasDestructure && !targetType->decl->proxyHandler.empty()) {
          Type* handlerType =
              lookupType(targetType->decl->proxyHandler);
          if (handlerType != nullptr && handlerType->decl != nullptr) {
            for (const auto& member : handlerType->decl->members) {
              for (const AnnotationUse& use : member->annotations) {
                if (use.name == "Destructure") {
                  hasDestructure = true;
                }
              }
            }
          }
        }
        if (!hasDestructure) {
          diagnostics_.error(
              stmt->span, "E2048",
              "proxy type '" + targetType->name +
                  "' has no @Destructure handler; configure @Proxy(handler, "
                  "Skip|Abort|Default(expr)) or add a @Destructure method");
          return;
        }
      }
      if (targetType->decl != nullptr) {
        for (const auto& member : targetType->decl->members) {
          if (member->kind != DeclKind::Method || member->isStatic ||
              member->params.size() != stmt->destructureBindings.size()) {
            continue;
          }
          bool marked = false;
          for (const AnnotationUse& use : member->annotations) {
            if (use.name == "Destructure") {
              marked = true;
            }
          }
          if (!marked) {
            continue;
          }
          bool matches = true;
          for (std::size_t i = 0; i < member->params.size(); ++i) {
            if (member->params[i].type == nullptr ||
                member->params[i].type->kind != TypeExprKind::Ref) {
              matches = false;
              break;
            }
            Type* parameterType =
                resolveType(member->params[i].type.get(), false);
            Type* bindingType = resolveType(
                stmt->destructureBindings[i].type.get(), false);
            if (parameterType == nullptr || bindingType == nullptr ||
                !assignable(bindingType, parameterType)) {
              matches = false;
              break;
            }
          }
          if (!canAccess(member->access, targetType)) {
            matches = false;
          }
          if (matches) {
            stmt->destructureMethod = member.get();
            break;
          }
        }
      }
      if (stmt->destructureMethod != nullptr) {
        for (const DestructureBinding& binding :
             stmt->destructureBindings) {
          Type* declared = resolveType(binding.type.get(), false);
          declareVariable(binding.name, declared, false, nullptr,
                          binding.span);
        }
        return;
      }
      for (const DestructureBinding& binding : stmt->destructureBindings) {
        Type* declared = resolveType(binding.type.get(), false);
        const FieldInfo* field = lookupField(targetType, binding.name);
        if (field == nullptr) {
          diagnostics_.error(binding.span, "E2008",
                             "type '" + targetType->name +
                                 "' has no field '" + binding.name + "'");
          continue;
        }
        if (!canAccess(field->access, field->owner)) {
          diagnostics_.error(binding.span, "E2030",
                             "cannot destructure " +
                                 std::string(accessName(field->access)) +
                                 " field '" + binding.name + "'");
        }
        requireAssignable(field->type, declared, binding.span,
                          "destructured field");
        declareVariable(binding.name, declared, false, nullptr, binding.span);
      }
      return;
    }
    case StmtKind::While:
      requireAssignable(checkExpr(stmt->condition.get()), boolType_,
                        stmt->span, "while condition");
      ++loopDepth_;
      {
        std::vector<std::pair<std::string, bool>> undo;
        applyOptionalNarrowing(stmt->condition.get(), true, undo);
        checkStmt(stmt->thenBranch.get());
        restoreOptionalNarrowing(undo);
      }
      --loopDepth_;
      return;
    case StmtKind::Return: {
      if (currentFunction_ != nullptr &&
          (currentFunction_->kind == DeclKind::Constructor ||
           currentFunction_->kind == DeclKind::Destructor)) {
        if (stmt->expr) {
          diagnostics_.error(stmt->span, "E2005",
                             "constructors and destructors cannot return a "
                             "value");
        }
        return;
      }
      Type* expected =
          currentReturnType_ != nullptr ? currentReturnType_ : voidType_;
      if (stmt->expr) {
        Type* actual = nullptr;
        if (stmt->expr->kind == ExprKind::Name) {
          const LockCaptureInfo* capture =
              findLockCapture(stmt->expr->name);
          if (capture != nullptr &&
              capture->kind != Stmt::LockCaptureKind::Value) {
            diagnostics_.error(
                stmt->span, "E2039",
                "@Lock borrow capture '" + stmt->expr->name +
                    "' cannot be returned; it is scope-bound");
          }
        }
        if (stmt->expr->kind == ExprKind::Lambda &&
            expected->kind == TypeKind::Function) {
          checkLambda(stmt->expr.get(), expected);
          actual = expected;
        } else {
          actual = checkExpr(stmt->expr.get());
        }
        checkClosureEscape(stmt->expr.get(), "be returned from the function");
        if (stmt->expr->kind == ExprKind::Unary && stmt->expr->op == "&") {
          diagnostics_.error(stmt->span, "E2039",
                             "returning '&' is not allowed; return the value "
                             "or an explicit @Share/@Unique owner");
        }
        if (actual != nullptr && actual->kind == TypeKind::ErrorValue) {
          if (currentFunction_ == nullptr ||
              (!currentFunction_->isResult && !currentFunction_->isExpect)) {
            diagnostics_.error(
                stmt->span, "E2052",
                "error(...) can only be returned from a @Result/@Expect "
                "function");
          }
        } else if (actual != nullptr &&
                   (actual->kind == TypeKind::Result ||
                    actual->kind == TypeKind::Expect)) {
          const bool sameFlavor =
              currentFunction_ != nullptr &&
              ((actual->kind == TypeKind::Result &&
                currentFunction_->isResult) ||
               (actual->kind == TypeKind::Expect &&
                currentFunction_->isExpect));
          if (!sameFlavor ||
              !isSubtype(actual->pointee, expected)) {
            diagnostics_.error(stmt->span, "E2005",
                               "cannot return '" + actual->name +
                                   "' as '" + expected->name + "'");
          }
        } else if (currentFunction_ != nullptr &&
                   currentFunction_->isOptional && actual != nullptr &&
                   actual->kind == TypeKind::Null) {
          // Nullable return accepts null explicitly.
        } else {
          requireAssignable(actual, expected, stmt->span, "return statement");
        }
      } else if (expected->kind != TypeKind::Void) {
        diagnostics_.error(stmt->span, "E2005",
                           "non-void function must return a value");
      }
      return;
    }
    case StmtKind::Break:
      if (loopDepth_ == 0) {
        diagnostics_.error(stmt->span, "E2005",
                           "'break' outside of a loop");
      }
      return;
    case StmtKind::Continue:
      if (loopDepth_ == 0) {
        diagnostics_.error(stmt->span, "E2005",
                           "'continue' outside of a loop");
      }
      return;
    case StmtKind::Destroy:
      checkDestroy(stmt);
      return;
  }
}

namespace {

bool patternBindsName(const Pattern* pattern) {
  if (pattern == nullptr) {
    return false;
  }
  if (pattern->kind == PatternKind::Bind) {
    return true;
  }
  if (pattern->kind == PatternKind::Or) {
    return patternBindsName(pattern->left.get()) ||
           patternBindsName(pattern->right.get());
  }
  if (pattern->kind == PatternKind::Guard) {
    return patternBindsName(pattern->left.get());
  }
  return false;
}

}  // namespace

void Checker::checkMatchPattern(Pattern* pattern, Type* value,
                                MatchCoverage& coverage, bool guarded,
                                Span span) {
  (void)span;
  if (pattern == nullptr) {
    return;
  }
  switch (pattern->kind) {
    case PatternKind::Wildcard:
      if (!guarded) {
        coverage.exhaustive = true;
      }
      return;
    case PatternKind::Bind: {
      const bool copyable =
          value != nullptr &&
          (value->kind == TypeKind::Enum || value->kind == TypeKind::Int ||
           value->kind == TypeKind::String || value->kind == TypeKind::Char ||
           value->kind == TypeKind::Bool);
      if (!copyable) {
        diagnostics_.error(
            pattern->span, "E2077",
            "binding patterns require an enumerate, integer, string, "
            "character or boolean value");
      } else {
        declareVariable(pattern->name, value, true, nullptr, pattern->span);
      }
      if (!guarded) {
        coverage.exhaustive = true;
      }
      return;
    }
    case PatternKind::Literal: {
      Type* literalType = checkExpr(pattern->value.get());
      requireAssignable(literalType, value, pattern->span, "match pattern");
      if (value != nullptr && value->kind == TypeKind::Bool &&
          pattern->value != nullptr &&
          pattern->value->kind == ExprKind::BoolLit) {
        coverage.variants.insert(pattern->value->boolValue ? "true" : "false");
      }
      return;
    }
    case PatternKind::Path: {
      if (value != nullptr && value->kind == TypeKind::Enum) {
        std::string variantName = pattern->name;
        std::string prefix;
        const std::size_t dot = pattern->name.rfind('.');
        if (dot != std::string::npos) {
          prefix = pattern->name.substr(0, dot);
          variantName = pattern->name.substr(dot + 1);
        }
        if (!prefix.empty() && prefix != value->name) {
          diagnostics_.error(pattern->span, "E2077",
                             "pattern '" + pattern->name +
                                 "' does not match enumerate '" + value->name +
                                 "'");
          return;
        }
        bool found = false;
        for (const EnumVariantInfo& variant : value->variants) {
          if (variant.name == variantName) {
            found = true;
            break;
          }
        }
        if (found) {
          pattern->enumName = value->name;
          pattern->variant = variantName;
          pattern->enumBehavior =
              value->decl != nullptr &&
              (!value->decl->members.empty() ||
               !value->decl->interfaces.empty());
          if (!guarded) {
            coverage.variants.insert(variantName);
          }
          return;
        }
        if (prefix.empty()) {
          // A bare name that is not a variant is a binding pattern.
          pattern->kind = PatternKind::Bind;
          declareVariable(pattern->name, value, true, nullptr, pattern->span);
          if (!guarded) {
            coverage.exhaustive = true;
          }
          return;
        }
        diagnostics_.error(pattern->span, "E2024",
                           "enumerate '" + value->name +
                               "' has no variant '" + variantName + "'");
        return;
      }
      if (pattern->name.find('.') != std::string::npos) {
        diagnostics_.error(pattern->span, "E2077",
                           "path patterns require an enumerate value");
        return;
      }
      const bool copyable =
          value != nullptr &&
          (value->kind == TypeKind::Int || value->kind == TypeKind::String ||
           value->kind == TypeKind::Char || value->kind == TypeKind::Bool);
      if (!copyable) {
        diagnostics_.error(
            pattern->span, "E2077",
            "binding patterns require an enumerate, integer, string, "
            "character or boolean value");
        return;
      }
      pattern->kind = PatternKind::Bind;
      declareVariable(pattern->name, value, true, nullptr, pattern->span);
      if (!guarded) {
        coverage.exhaustive = true;
      }
      return;
    }
    case PatternKind::Range: {
      if (value == nullptr ||
          (value->kind != TypeKind::Int && value->kind != TypeKind::Char)) {
        diagnostics_.error(
            pattern->span, "E2077",
            "range patterns require an integer or character value");
      }
      const auto checkBound = [&](Pattern* bound, const char* side) {
        if (bound == nullptr || bound->kind != PatternKind::Literal) {
          diagnostics_.error(pattern->span, "E2077",
                             std::string("range ") + side +
                                 " bound must be a literal");
          return;
        }
        Type* boundType = checkExpr(bound->value.get());
        if (boundType != nullptr && !boundType->isInteger() &&
            boundType->kind != TypeKind::Char) {
          diagnostics_.error(pattern->span, "E2077",
                             "range bounds must be integers or characters");
          return;
        }
        requireAssignable(boundType, value, bound->span, "range bound");
      };
      checkBound(pattern->left.get(), "lower");
      checkBound(pattern->right.get(), "upper");
      return;
    }
    case PatternKind::Or: {
      if (patternBindsName(pattern)) {
        diagnostics_.error(
            pattern->span, "E2077",
            "binding patterns cannot be combined with '|'; use separate "
            "branches or a wildcard");
      }
      checkMatchPattern(pattern->left.get(), value, coverage, guarded, span);
      checkMatchPattern(pattern->right.get(), value, coverage, guarded, span);
      return;
    }
    case PatternKind::Guard:
      checkMatchPattern(pattern->left.get(), value, coverage, true, span);
      requireAssignable(checkExpr(pattern->guard.get()), boolType_,
                        pattern->guard->span, "match guard");
      return;
  }
}

void Checker::checkBlock(Stmt* block) {
  pushScope(Scope::Kind::Block);
  for (const auto& stmt : block->statements) {
    checkStmt(stmt.get());
  }
  popScope();
}

void Checker::checkDestroy(Stmt* stmt) {
  if (!inDestructor_) {
    diagnostics_.error(stmt->span, "E2008",
                       "'~this.member' is only allowed inside a destructor");
    return;
  }
  Expr* target = stmt->expr.get();
  if (target == nullptr || target->kind != ExprKind::Member ||
      target->lhs == nullptr || target->lhs->kind != ExprKind::This) {
    diagnostics_.error(stmt->span, "E2008",
                       "destruction declaration must name a member, e.g. "
                       "'~this.path;'");
    return;
  }
  if (currentClass_ == nullptr) {
    return;
  }
  const FieldInfo* field = lookupField(currentClass_, target->name);
  if (field == nullptr) {
    diagnostics_.error(stmt->span, "E2008",
                       "class '" + currentClass_->name + "' has no member '" +
                           target->name + "'");
    return;
  }
  if (!isUserClass(field->type)) {
    diagnostics_.warning(stmt->span, "W0002",
                         "'~this." + target->name +
                             "' has no effect: the member type is not a "
                             "user-defined class");
  }
  const bool duplicate =
      std::find(destroyedFields_.begin(), destroyedFields_.end(),
                target->name) != destroyedFields_.end();
  if (duplicate) {
    diagnostics_.warning(stmt->span, "W0003",
                         "duplicate destruction declaration for '" +
                             target->name +
                             "'; only the last declaration takes effect");
  } else {
    destroyedFields_.push_back(target->name);
  }
}

void Checker::checkShareTypeCycles(ModuleAst& module) {
  struct Edge {
    const Decl* target = nullptr;
    std::string field;
  };
  std::unordered_map<const Decl*, std::vector<Edge>> graph;
  for (const auto& decl : module.decls) {
    if (decl->kind != DeclKind::Class || decl->isStandard) {
      continue;
    }
    for (const auto& member : decl->members) {
      if (member->kind != DeclKind::Field || !member->isShare ||
          member->type == nullptr || member->type->resolved == nullptr ||
          member->type->resolved->decl == nullptr) {
        continue;
      }
      graph[decl.get()].push_back(
          Edge{member->type->resolved->decl, member->name});
    }
  }
  if (graph.empty()) {
    return;
  }
  std::unordered_map<const Decl*, int> color;
  std::function<void(const Decl*)> visit = [&](const Decl* node) {
    color[node] = 1;
    const auto found = graph.find(node);
    if (found != graph.end()) {
      for (const Edge& edge : found->second) {
        const int state = color[edge.target];
        if (state == 1) {
          diagnostics_.warning(
              node->span, "W0005",
              "possible @Share cycle through field '" + edge.field +
                  "' of '" + node->name +
                  "'; use @Weak or restructure the ownership graph");
        } else if (state == 0) {
          visit(edge.target);
        }
      }
    }
    color[node] = 2;
  };
  for (const auto& entry : graph) {
    if (color[entry.first] == 0) {
      visit(entry.first);
    }
  }
}

}  // namespace coord
