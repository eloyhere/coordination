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
  FractionLit,
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
  Lambda,
  IfExpr,
  MethodRef,
};

struct LambdaInfo;
struct IfExprInfo;

struct Expr : Node {
  ExprKind kind = ExprKind::IntLit;
  std::string name;  // Name / Member / callee
  std::string op;    // Unary / Binary / Assign
  std::int64_t intValue = 0;
  double fractionValue = 0.0;
  /// Cleaned source spelling of a fractional literal, used to encode an exact
  /// fixed-point raw value when Sema lowers the literal to `fix`.
  std::string fractionText;
  /// Set by Sema when a fractional literal is converted to a static-scale fix
  /// type: the encoded raw integer and the resolved target type.
  std::int64_t fixRaw = 0;
  /// @Length(128) raw bits (two's complement); used when `fixRawWide` is set.
  std::uint64_t fixRawHigh = 0;
  std::uint64_t fixRawLow = 0;
  bool fixRawWide = false;
  bool fixLiteral = false;
  /// Target fix type for a division whose power is overridden by an explicit
  /// target annotation (`@Power(n) fix y = x / 2;`).
  Type* fixTarget = nullptr;
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
  /// Propagated borrow lifetime of a closure value (see LambdaInfo).
  int borrowDepth = 0;
  bool weakValue = false;
  bool optionalField = false;
  bool optionalCall = false;
  std::shared_ptr<LambdaInfo> lambda;
  std::shared_ptr<IfExprInfo> ifExpr;
  /// Method reference flags filled by Sema: `Type::staticMethod` vs
  /// `object::method`.
  bool staticRef = false;
  bool boundRef = false;
};

/// Value branches of an `if` expression: `if (c) a else b`.
struct IfExprInfo {
  ExprPtr condition;
  ExprPtr thenValue;
  ExprPtr elseValue;
};

// ---------------------------------------------------------------------------
// Match patterns
// ---------------------------------------------------------------------------

enum class PatternKind {
  Wildcard,  // _
  Literal,   // 1, "x", 'a', true
  Path,      // Color.Red (enumerate variant); a bare unknown name is a binding
  Bind,      // x / x if guard -> binds the matched value
  Range,     // 1..10, 1..=10
  Or,        // p1 | p2
  Guard,     // p if condition
};

struct Pattern;
using PatternPtr = std::unique_ptr<Pattern>;

struct Pattern : Node {
  PatternKind kind = PatternKind::Wildcard;
  ExprPtr value;      // Literal
  std::string name;   // Path / Bind
  PatternPtr left;    // Or left, Range lower, Guard inner
  PatternPtr right;   // Or right, Range upper
  bool inclusive = true;
  ExprPtr guard;      // Guard condition
  // Resolved by Sema for Path patterns.
  std::string enumName;
  std::string variant;
  bool enumBehavior = false;

  ~Pattern() override;
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
  /// Numeric metadata attached to this type expression by a leading
  /// @Length/@Radix/@Power annotation. The values are 0 when absent; a
  /// negative sentinel means the annotation was present but its argument was
  /// not an integer literal (reported by Sema).
  int numericLength = 0;
  int numericRadix = 0;
  int numericPower = 0;
  Span numericLengthSpan;
  Span numericRadixSpan;
  Span numericPowerSpan;
  bool hasNumericLength = false;
  bool hasNumericRadix = false;
  bool hasNumericPower = false;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

struct AnnotationUse {
  Span span;
  std::string name;
  std::vector<ExprPtr> args;
  /// Parallel to `args`: the argument name for `name: value` arguments and an
  /// empty string for positional arguments.
  std::vector<std::string> argNames;
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

enum class StmtKind {  Block,
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

struct Stmt : Node {  StmtKind kind = StmtKind::Block;
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
  bool unsafeBlock = false;
  bool safeBlock = false;
  /// `@Lock(Exclusive, mutex, [captures]) { ... }` block specification.
  enum class LockMode { None, Exclusive };
  enum class LockCaptureKind { Value, MutRef, ConstRef };
  struct LockCaptureSpec {
    Span span;
    std::string name;
    LockCaptureKind kind = LockCaptureKind::Value;
  };
  bool hasLock = false;
  LockMode lockMode = LockMode::None;
  ExprPtr lockMutex;
  std::vector<LockCaptureSpec> lockCaptures;
  bool optionalDestructure = false;
  bool optionalDestructureNarrowed = false;
  ProxyPolicy proxyPolicy = ProxyPolicy::Error;
  int radix = 0;
  int power = 0;
  bool hasPower = false;
  bool hasLength = false;
  bool hasRadix = false;
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
  std::vector<PatternPtr> matchPatterns;
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
  int power = 0;
  bool hasPower = false;
  bool hasLength = false;
  bool hasRadix = false;
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

/// One entry of a lambda capture list. A default capture (`[=]` / `[&]`) has an
/// empty `name`; `[this]` uses `Kind::This`.
struct Capture {
  enum class Kind { ByValue, ByRef, This };
  Span span;
  std::string name;
  Kind kind = Kind::ByValue;
};

struct LambdaInfo {
  std::string captureList;
  std::vector<Capture> captures;
  std::vector<Param> params;
  TypeExprPtr returnType;
  StmtPtr body;
  ExprPtr bodyExpr;
  /// Non-zero when the closure holds a borrow: -1 means a call-scoped borrow
  /// (a function-typed parameter), a positive value is the lexical scope depth
  /// of the borrowed local. 0 means the closure owns everything it uses.
  int borrowDepth = 0;
  /// Set when the lambda body uses `this` (explicitly or implicitly).
  bool usesThis = false;
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
  int power = 0;
  bool hasPower = false;
  bool hasLength = false;
  bool hasRadix = false;
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
  /// `@Native(file: ..., abi: "c" | "c++")` project interface: the header
  /// declared by the annotation and the selected ABI ("c" or "c++").
  std::string nativeFile;
  std::string nativeAbi;
  bool isUnsafe = false;
  bool isSafe = false;
  /// Copyability of a class: implicit for pure value classes, explicit through
  /// `@Copy(Deep|Shallow)`, and None when owning fields require an explicit
  /// annotation before the value may be copied.
  enum class CopyKind { Implicit, Deep, Shallow, None };
  CopyKind copyKind = CopyKind::Implicit;

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
  /// File-level `@Unsafe;` / `@Safe;` directives.
  bool moduleUnsafe = false;
  bool moduleSafe = false;
};

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

[[nodiscard]] std::string describeType(const TypeExpr* type);
[[nodiscard]] std::string describeDeclKind(DeclKind kind);
[[nodiscard]] std::string describeExprKind(ExprKind kind);

void dumpAst(const ModuleAst& module, std::ostream& out);

}  // namespace coord
