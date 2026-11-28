#pragma once

#include "coord/modules/access.h"
#include "coord/support.h"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace coord {

struct Type;
struct Decl;

struct Node {
  Span span;
  virtual ~Node() = default;
};

/// Policy shared by all proxy objects (Optional is one proxy kind). It applies
/// when a proxy value is used directly (dereference, member access, implicit
/// conversion, construction or destructuring) without narrowing/handling.
enum class ProxyPolicy {
  Error,   // compile-time E2048 (default for @Optional)
  Skip,    // continue with a default value / default bindings
  Abort,   // abort at runtime with the source location
  Default  // use the expression supplied as Default(expr)
};

/// Compile-time/runtime retention of a statement block.
enum class BlockRetention { None, Compile, Runtime };

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct ObjectField {
  Span span;
  std::string name;
  ExprPtr value;
};

enum class ExprKind {
  IntLit,
  DecimalLit,
  CharLit,
  BoolLit,
  StringLit,
  NullLit,
  Name,
  This,
  Super,
  Unary,
  Binary,
  Call,
  Member,
  Index,
  Assign,
  Inc,
  ArrayLit,
  ObjectLit,
};

struct Expr : Node {
  ExprKind kind = ExprKind::IntLit;
  std::string name;  // Name / Member / callee
  std::string op;    // Unary / Binary / Assign
  std::int64_t intValue = 0;
  double decimalValue = 0.0;
  char32_t charValue = 0;
  bool boolValue = false;
  std::string stringValue;
  ExprPtr lhs;
  ExprPtr rhs;
  std::vector<ExprPtr> args;
  std::vector<ObjectField> fields;
  Type* resolvedType = nullptr;
  // Filled by Sema for an @Optional direct access.
  bool optionalAccess = false;
  bool optionalNarrowed = false;
  bool optionalSmart = false;
  ProxyPolicy proxyPolicy = ProxyPolicy::Error;
  const Expr* optionalDefault = nullptr;
  std::string proxyMessage;
  int radix = 0;
  bool weakValue = false;
  bool optionalField = false;
  bool optionalCall = false;
};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

struct TypeExpr;
using TypeExprPtr = std::unique_ptr<TypeExpr>;

enum class TypeExprKind {
  Named,   // int, string, File, N, reflector<File>
  Pointer, // T*
  Ref,     // T&  / const T&
  Const,   // const T
  Union,   // A | B
  Intersect,  // A & B
  Function,   // (A, B) -> R
  Array,      // T[N]
};

struct TypeExpr : Node {
  TypeExprKind kind = TypeExprKind::Named;
  std::string name;
  TypeExprPtr inner;
  std::vector<TypeExprPtr> args;
  TypeExprPtr returnType;
  ExprPtr arraySize;
  bool constRef = false;
  Type* resolved = nullptr;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

struct AnnotationUse {
  Span span;
  std::string name;
  std::vector<ExprPtr> args;
  std::vector<TypeExprPtr> typeArgs;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct DestructureBinding {
  Span span;
  TypeExprPtr type;
  std::string name;
};

/// One `A a`, `A 1` or `A a = expr` entry of a @Constructure brace initializer.
struct ConstructureArgument {
  Span span;
  TypeExprPtr type;
  std::string name;
  ExprPtr value;
};

enum class StmtKind {
  Block,
  Var,
  Expr,
  If,
  ForIn,
  While,
  Return,
  Break,
  Continue,
  Destroy,  // ~this.member;
  Switch,
  Destructure,
};

struct Stmt : Node {
  StmtKind kind = StmtKind::Block;
  std::vector<AnnotationUse> annotations;
  BlockRetention blockRetention = BlockRetention::None;
  std::vector<StmtPtr> statements;
  TypeExprPtr type;
  std::string name;
  bool isConst = false;
  bool isOptional = false;
  bool isUnsigned = false;
  bool isSigned = false;
  bool isUnique = false;
  bool isShare = false;
  bool isWeak = false;
  bool isLet = false;
  bool isMatch = false;
  bool optionalDestructure = false;
  bool optionalDestructureNarrowed = false;
  ProxyPolicy proxyPolicy = ProxyPolicy::Error;
  int radix = 0;
  ExprPtr optionalDefault;
  const Expr* optionalDefaultRef = nullptr;
  std::string letName;
  int length = 0;
  bool directInit = false;
  bool braceInit = false;
  bool constructureInit = false;
  std::vector<ConstructureArgument> constructureArgs;
  const Decl* constructureCtor = nullptr;
  std::vector<std::size_t> constructureOrder;
  ExprPtr expr;
  std::vector<ExprPtr> initArgs;
  std::vector<ExprPtr> switchValues;
  std::vector<StmtPtr> switchBodies;
  std::vector<DestructureBinding> destructureBindings;
  const Decl* destructureMethod = nullptr;
  ExprPtr condition;
  StmtPtr thenBranch;
  StmtPtr elseBranch;

  ~Stmt() override;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

struct GenericParam {
  Span span;
  std::string name;
  TypeExprPtr bound;
};

struct Param {
  Span span;
  TypeExprPtr type;
  std::string name;
  ExprPtr defaultValue;
  std::vector<AnnotationUse> annotations;
  int radix = 0;
  bool isUnique = false;
  bool isShare = false;
  bool isWeak = false;
  bool isOptional = false;
  ProxyPolicy proxyPolicy = ProxyPolicy::Error;
  const Expr* optionalDefault = nullptr;
};

struct Initializer {
  enum class Kind { Super, Field };
  Kind kind = Kind::Field;
  std::string name;
  std::vector<ExprPtr> args;
  Span span;
};

struct EnumVariant {
  Span span;
  std::string name;
  ExprPtr value;
  std::vector<AnnotationUse> annotations;
  std::int64_t resolvedValue = 0;
};

struct Decl;
using DeclPtr = std::unique_ptr<Decl>;

enum class DeclKind {
  Class,
  Interface,
  Enum,
  Field,
  Method,
  Constructor,
  Destructor,
  Function,
  Annotation,
  Const,
};

struct Decl : Node {
  DeclKind kind = DeclKind::Class;
  std::string name;
  bool exported = false;
  bool isStatic = false;
  bool isVirtual = false;
  bool isOverride = false;
  bool isFinal = false;
  bool isConstMethod = false;
  bool isConstField = false;
  bool isResult = false;
  bool isExpect = false;
  bool isTest = false;
  bool isUnsigned = false;
  bool isSigned = false;
  bool isUnique = false;
  bool isShare = false;
  bool isWeak = false;
  bool isOptional = false;
  int length = 0;
  int radix = 0;
  bool isCore = false;
  bool isStandard = false;
  std::string doc;
  bool isProxy = false;
  std::string proxyHandler;
  ProxyPolicy proxyPolicy = ProxyPolicy::Skip;
  const Expr* proxyDefault = nullptr;
  bool isInvocation = false;
  std::string invocationHandler;
  std::vector<std::string> invocationHandlers;
  std::string processorMethod;
  Access access = Access::Default;
  bool hasUserDestructor = false;
  bool synthesizedDestructor = false;
  bool isNative = false;
  std::string nativeSymbol;

  std::vector<AnnotationUse> annotations;
  std::vector<std::string> inheritedAnnotationNames;
  std::vector<GenericParam> generics;
  std::vector<Param> params;
  std::vector<Initializer> initializers;
  bool hasInitializerList = false;
  TypeExprPtr type;  // field type or return type
  TypeExprPtr base;
  std::vector<TypeExprPtr> interfaces;
  std::vector<DeclPtr> members;
  std::vector<EnumVariant> variants;
  bool retainEnum = false;
  StmtPtr body;
  ExprPtr init;
  Type* resolvedType = nullptr;
  std::vector<const Decl*> destroyOrder;

  ~Decl() override;
};

struct ImportDecl {
  Span span;
  std::vector<std::string> path;
  std::string alias;
  bool hasAlias = false;
  std::vector<std::string> items;
};

struct ModuleAst {
  std::uint32_t fileId = 0;
  std::vector<std::string> modulePath;
  std::vector<ImportDecl> imports;
  std::vector<std::string> exportedNames;
  std::map<std::string, std::vector<std::string>> importAliases;
  std::vector<DeclPtr> decls;
};

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

[[nodiscard]] std::string describeType(const TypeExpr* type);
[[nodiscard]] std::string describeDeclKind(DeclKind kind);
[[nodiscard]] std::string describeExprKind(ExprKind kind);

void dumpAst(const ModuleAst& module, std::ostream& out);

}  // namespace coord
