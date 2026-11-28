#pragma once

#include "coord/ast.h"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace coord {

/// A compile-time value. Runtime values are not modelled here.
struct Value {
  enum class Kind { Void, Bool, Int, Fraction, Char, String };

  Kind kind = Kind::Void;
  bool boolValue = false;
  std::int64_t intValue = 0;
  double fractionValue = 0.0;
  char32_t charValue = 0;
  std::string stringValue;

  [[nodiscard]] static Value makeVoid();
  [[nodiscard]] static Value makeBool(bool value);
  [[nodiscard]] static Value makeInt(std::int64_t value);
  [[nodiscard]] static Value makeFraction(double value);
  [[nodiscard]] static Value makeChar(char32_t value);
  [[nodiscard]] static Value makeString(std::string value);

  [[nodiscard]] bool isNumeric() const {
    return kind == Kind::Int || kind == Kind::Fraction;
  }
  [[nodiscard]] double asDouble() const;
  [[nodiscard]] std::string toString() const;
};

/// Evaluates top-level `const` initializers at compile time. Any function may
/// be interpreted as long as it does not touch runtime-only state; there is no
/// `comptime` keyword in Coordination.
class Interpreter {
public:
  Interpreter(ModuleAst& module, DiagnosticEngine& diagnostics);

  void evaluate();
  /// Executes `@Retention(Compile)` blocks. Runtime-retained blocks are
  /// skipped here and emitted by the C++ backend instead.
  void executeRetainedBlocks();

  [[nodiscard]] const std::map<std::string, Value>& constValues() const {
    return constValues_;
  }
  [[nodiscard]] const std::string& compileTimeOutput() const {
    return output_;
  }
  [[nodiscard]] bool failed() const { return failed_; }

private:
  /// One lexical scope per `{ ... }` block, matching the semantic checker.
  struct Scope {
    std::map<std::string, Value> values;
  };

  struct ReturnSignal {
    Value value;
  };
  struct BreakSignal {};
  struct ContinueSignal {};

  std::optional<Value> evalExpr(Expr* expr);
  std::optional<Value> evalCall(Expr* expr);
  std::optional<Value> evalBinary(Expr* expr);
  std::optional<Value> evalUnary(Expr* expr);
  std::optional<Value> evalName(Expr* expr);

  void execStmt(Stmt* stmt);
  void execBlock(Stmt* stmt);
  void execRetainedScan(Stmt* stmt);

  std::optional<Value> callFunction(const Decl* decl,
                                    std::vector<Value> arguments);

  void pushScope();
  void popScope();
  void declare(const std::string& name, Value value);
  [[nodiscard]] Value* lookup(const std::string& name);

  [[nodiscard]] const Decl* findFunction(const std::string& name,
                                         std::size_t argc) const;

  bool consumeFuel(Span span);
  void error(Span span, std::string message);

  ModuleAst& module_;
  DiagnosticEngine& diagnostics_;
  std::vector<Scope> scopes_;
  std::map<std::string, Value> constValues_;
  std::string output_;
  std::int64_t fuel_ = 2'000'000;
  int callDepth_ = 0;
  bool failed_ = false;
  std::set<const Stmt*> retainedExecuted_;
};

}  // namespace coord
