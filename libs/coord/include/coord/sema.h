#pragma once

#include "coord/ast.h"
#include "coord/modules/annotation_registry.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace coord {

enum class TypeKind {
  Error,
  Void,
  Bool,
  Int,
  Decimal,
  Float,
  Record,
  Char,
  String,
  Null,
  ErrorValue,
  Description,
  Class,
  Enum,
  Interface,
  Annotation,
  Function,
  Descriptor,
  Reflection,
  Reflector,
  Proxy,
  SharePtr,
  Pointer,
  Array,
  Queue,
  ArrayView,
  Map,
  Set,
  Result,
  Expect,
};

struct Type;

struct FieldInfo {
  std::string name;
  Type* type = nullptr;
  const Decl* decl = nullptr;
  Access access = Access::Default;
  Type* owner = nullptr;
};

struct MethodInfo {
  std::string name;
  Type* returnType = nullptr;
  std::vector<Type*> paramTypes;
  const Decl* decl = nullptr;
  bool isVirtual = false;
  bool isFinal = false;
  Access access = Access::Default;
  Type* owner = nullptr;
};

struct EnumVariantInfo {
  std::string name;
  std::int64_t value = 0;
};

struct AnnotationInfo {
  std::string name;
  bool isMeta = false;
  bool repeatable = false;
  bool inherit = false;
  bool documented = false;
  bool targetAll = true;
  bool retentionRuntime = false;
  bool retentionInvocation = false;
  bool isIntrinsic = false;
  std::string processorMethod;
  std::set<std::string> targets;
  const Decl* decl = nullptr;
};

struct Type {
  TypeKind kind = TypeKind::Error;
  std::string name;
  Type* base = nullptr;
  Type* pointee = nullptr;
  std::int64_t arrayLength = 0;
  const Decl* decl = nullptr;
  Type* returnType = nullptr;
  bool builtin = false;
  bool unordered = false;
  bool isUnion = false;
  std::vector<FieldInfo> fields;
  std::vector<MethodInfo> methods;
  std::vector<Type*> interfaces;
  std::vector<Type*> alternatives;
  std::vector<EnumVariantInfo> variants;

  [[nodiscard]] bool isMeta() const;
  [[nodiscard]] bool isInteger() const {
    return kind == TypeKind::Int;
  }
  [[nodiscard]] bool isNumeric() const {
    return kind == TypeKind::Int || kind == TypeKind::Decimal ||
           kind == TypeKind::Float;
  }
};

class Checker {
public:
  Checker(SourceManager& sources, DiagnosticEngine& diagnostics);

  bool check(ModuleAst& module);

  std::map<std::string, std::vector<std::string>> importAliases_;

  [[nodiscard]] Type* lookupType(std::string_view name) const;
  [[nodiscard]] Type* errorType() const { return errorType_; }
  [[nodiscard]] Type* intType() const { return intType_; }
  [[nodiscard]] Type* stringType() const { return stringType_; }
  [[nodiscard]] Type* boolType() const { return boolType_; }
  [[nodiscard]] const std::vector<std::unique_ptr<Type>>& types() const {
    return types_;
  }
  [[nodiscard]] const std::vector<const Decl*>& orderedDecls() const {
    return orderedDecls_;
  }

private:
  struct Variable {
    Type* type = nullptr;
    bool isConst = false;
    bool isOptional = false;
    bool isUnique = false;
    bool isShare = false;
    bool isWeak = false;
    bool moved = false;
    bool narrowed = false;
    ProxyPolicy proxyPolicy = ProxyPolicy::Error;
    const Expr* optionalDefault = nullptr;
    const Decl* field = nullptr;
  };

  /// A lexical scope corresponds to one `{ ... }` block. Class bodies and the
  /// module top level are scopes too; they contain declarations rather than
  /// local variables, but they participate in the same lookup chain.
  struct Scope {
    enum class Kind { Module, Class, Function, Block };
    Kind kind = Kind::Block;
    std::unordered_map<std::string, Variable> variables;
  };

  void initializeBuiltins();
  Type* makeType(TypeKind kind, std::string name, Type* base);
  Type* makePointerType(Type* pointee);
  Type* makeSharePtrType(Type* pointee);
  Type* makeArrayType(Type* element, std::int64_t length);
  Type* makeQueueType(Type* element);
  Type* makeArrayViewType(Type* element);
  Type* makeMapType(Type* key, Type* value, bool unordered);
  Type* makeSetType(Type* element, bool unordered);
  Type* makeResultType(Type* success, bool expect);
  Type* makeUnionType(std::vector<Type*> alternatives);
  Type* makeIntersectType(std::vector<Type*> parts);

  void declareTopLevel(ModuleAst& module);
  void registerAnnotations(ModuleAst& module);
  void validateAnnotations(const std::vector<AnnotationUse>& annotations,
                           std::string_view target, Span span,
                           bool allowTypeTarget = false);
  void validateDoc(Decl& decl);
  Type* makeFunctionType(std::vector<Type*> params, Type* returnType);
  void checkLambda(Expr* expr, Type* expected);
  void checkShareTypeCycles(ModuleAst& module);
  int resolveLength(const std::vector<AnnotationUse>& annotations, Type* type,
                    Span span);
  void inheritAnnotations(Decl& target, const Decl* source);
  [[nodiscard]] static bool isMetaAnnotationName(std::string_view name);
  [[nodiscard]] static bool isBuiltinAnnotationName(std::string_view name);
  void checkClass(Decl& decl);
  void checkEnum(Decl& decl);
  void checkInterface(Decl& decl);
  void checkAnnotation(Decl& decl);
  void checkFunction(Decl& decl);
  void checkConst(Decl& decl);
  void checkMethod(Decl& decl, Type* owner, bool allowMeta = false);
  void checkConstructorDecl(Decl& decl);
  void checkDestructorBody(Decl& classDecl, Decl& destructor);
  void synthesizeDestructor(Decl& classDecl);
  void validateProxyHandler(Decl& decl);
  void validateInvocationHandler(Decl& decl);

  Type* resolveType(TypeExpr* type, bool allowMeta);
  Type* resolveNamedType(TypeExpr* type, bool allowMeta);

  void pushScope(Scope::Kind kind);
  void popScope();
  void pushTypeScope();
  void popTypeScope();
  void bindGenericParams(const std::vector<GenericParam>& generics,
                         bool allowMeta);
  void declareVariable(const std::string& name, Type* type, bool isConst,
                       const Decl* field, Span span,
                       bool isOptional = false,
                       ProxyPolicy proxyPolicy = ProxyPolicy::Error,
                       const Expr* optionalDefault = nullptr);

  /// Applies the @Optional policy when a value is used without narrowing.
  /// Returns true when the access is allowed (narrowed or non-Error policy).
  bool checkOptionalDirectAccess(Expr* expr, std::string_view action);
  void applyOptionalNarrowing(
      const Expr* condition, bool truth,
      std::vector<std::pair<std::string, bool>>& undo);
  void restoreOptionalNarrowing(
      std::vector<std::pair<std::string, bool>>& undo);

  [[nodiscard]] Variable* lookupVariable(std::string_view name);
  [[nodiscard]] const Decl* lookupFunction(std::string_view name,
                                           std::size_t argc) const;
  [[nodiscard]] const FieldInfo* lookupField(Type* type,
                                             std::string_view name) const;
  [[nodiscard]] const MethodInfo* lookupMethod(Type* type,
                                               std::string_view name,
                                               std::size_t argc) const;

  Type* checkExpr(Expr* expr);
  Type* checkCall(Expr* expr);
  Type* checkMember(Expr* expr);
  Type* checkUnary(Expr* expr);
  Type* checkBinary(Expr* expr);
  Type* checkAssign(Expr* expr);

  void checkStmt(Stmt* stmt);
  void checkBlock(Stmt* block);
  void checkDestroy(Stmt* stmt);

  [[nodiscard]] bool isSubtype(Type* from, Type* to) const;
  [[nodiscard]] bool assignable(Type* from, Type* to) const;
  [[nodiscard]] bool convertible(Type* from, Type* to);
  [[nodiscard]] bool canAccess(Access access, Type* owner) const;
  void requireAssignable(Type* from, Type* to, Span span,
                         const std::string& context);

  [[nodiscard]] bool isUserClass(Type* type) const {
    return type != nullptr && type->kind == TypeKind::Class &&
           type->decl != nullptr;
  }

  SourceManager& sources_;
  DiagnosticEngine& diagnostics_;

  std::vector<std::unique_ptr<Type>> types_;
  std::unordered_map<std::string, Type*> typeMap_;
  std::unordered_map<std::string, Type*> pointerTypes_;
  std::unordered_map<std::string, Type*> compositeTypes_;

  Type* errorType_ = nullptr;
  Type* voidType_ = nullptr;
  Type* classType_ = nullptr;
  Type* enumerationType_ = nullptr;
  Type* intType_ = nullptr;
  Type* decimalType_ = nullptr;
  Type* floatType_ = nullptr;
  Type* recordType_ = nullptr;
  Type* charType_ = nullptr;
  Type* boolType_ = nullptr;
  Type* stringType_ = nullptr;
  Type* nullType_ = nullptr;
  Type* errorValueType_ = nullptr;
  Type* descriptionType_ = nullptr;
  Type* annotationType_ = nullptr;
  Type* functionType_ = nullptr;
  Type* descriptorType_ = nullptr;
  Type* reflectionType_ = nullptr;
  Type* reflectorType_ = nullptr;
  Type* proxyType_ = nullptr;

  std::unordered_map<std::string, Decl*> typeDecls_;
  std::unordered_map<std::string, std::vector<Decl*>> functions_;
  std::unordered_map<std::string, Decl*> consts_;
  std::unordered_map<std::string, AnnotationInfo> annotations_;
  annotation::Registry annotationRegistry_;
  std::vector<const Decl*> orderedDecls_;

  std::vector<Scope> scopes_;
  std::vector<std::unordered_map<std::string, Type*>> typeScopes_;

  Type* currentClass_ = nullptr;
  Decl* currentClassDecl_ = nullptr;
  Decl* currentFunction_ = nullptr;
  Type* currentReturnType_ = nullptr;
  bool inDestructor_ = false;
  int loopDepth_ = 0;

  std::vector<std::string> destroyedFields_;
};

}  // namespace coord
