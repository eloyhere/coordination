#include "coord/interp.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace coord {

Value Value::makeVoid() { return Value{}; }

Value Value::makeBool(bool value) {
  Value v;
  v.kind = Kind::Bool;
  v.boolValue = value;
  return v;
}

Value Value::makeInt(std::int64_t value) {
  Value v;
  v.kind = Kind::Int;
  v.intValue = value;
  return v;
}

Value Value::makeFraction(double value) {
  Value v;
  v.kind = Kind::Fraction;
  v.fractionValue = value;
  return v;
}

Value Value::makeChar(char32_t value) {
  Value v;
  v.kind = Kind::Char;
  v.charValue = value;
  return v;
}

Value Value::makeString(std::string value) {
  Value v;
  v.kind = Kind::String;
  v.stringValue = std::move(value);
  return v;
}

double Value::asDouble() const {
  return kind == Kind::Int ? static_cast<double>(intValue) : fractionValue;
}

std::string Value::toString() const {
  switch (kind) {
    case Kind::Void:
      return "void";
    case Kind::Bool:
      return boolValue ? "true" : "false";
    case Kind::Int:
      return std::to_string(intValue);
    case Kind::Fraction: {
      std::ostringstream stream;
      stream << fractionValue;
      return stream.str();
    }
    case Kind::Char: {
      std::string result;
      const char32_t value = charValue;
      if (value < 0x80) {
        result.push_back(static_cast<char>(value));
      } else if (value < 0x800) {
        result.push_back(static_cast<char>(0xC0 | (value >> 6)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
      } else if (value < 0x10000) {
        result.push_back(static_cast<char>(0xE0 | (value >> 12)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
      } else {
        result.push_back(static_cast<char>(0xF0 | (value >> 18)));
        result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
      }
      return result;
    }
    case Kind::String:
      return stringValue;
  }
  return {};
}

Interpreter::Interpreter(ModuleAst& module, DiagnosticEngine& diagnostics)
    : module_(module), diagnostics_(diagnostics) {}

void Interpreter::pushScope() { scopes_.emplace_back(); }

void Interpreter::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Interpreter::declare(const std::string& name, Value value) {
  if (scopes_.empty()) {
    pushScope();
  }
  scopes_.back().values[name] = std::move(value);
}

Value* Interpreter::lookup(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    const auto found = it->values.find(name);
    if (found != it->values.end()) {
      return &found->second;
    }
  }
  return nullptr;
}

void Interpreter::error(Span span, std::string message) {
  diagnostics_.error(span, "E3001", std::move(message));
  failed_ = true;
}

bool Interpreter::consumeFuel(Span span) {
  if (fuel_ <= 0) {
    error(span, "compile-time evaluation exceeded its fuel budget");
    return false;
  }
  --fuel_;
  return true;
}

const Decl* Interpreter::findFunction(const std::string& name,
                                      std::size_t argc) const {
  for (const auto& decl : module_.decls) {
    if (decl->kind == DeclKind::Function && decl->name == name &&
        decl->params.size() == argc && decl->body != nullptr) {
      return decl.get();
    }
  }
  return nullptr;
}

void Interpreter::executeRetainedBlocks() {
  for (const auto& decl : module_.decls) {
    execRetainedScan(decl->body.get());
  }
}

void Interpreter::execRetainedScan(Stmt* stmt) {
  if (stmt == nullptr) {
    return;
  }
  if (stmt->kind == StmtKind::Block) {
    if (stmt->blockRetention == BlockRetention::Compile) {
      if (retainedExecuted_.insert(stmt).second) {
        try {
          execBlock(stmt);
        } catch (...) {
          // Compile-time blocks are evaluated for their side effects; a
          // `return`/`break` has no enclosing function/loop here.
        }
      }
      return;
    }
    for (const auto& child : stmt->statements) {
      execRetainedScan(child.get());
    }
    return;
  }
  if (stmt->kind == StmtKind::If) {
    execRetainedScan(stmt->thenBranch.get());
    execRetainedScan(stmt->elseBranch.get());
    return;
  }
  if (stmt->kind == StmtKind::While || stmt->kind == StmtKind::ForIn) {
    execRetainedScan(stmt->thenBranch.get());
    return;
  }
  if (stmt->kind == StmtKind::Switch) {
    for (const auto& body : stmt->switchBodies) {
      execRetainedScan(body.get());
    }
  }
}

void Interpreter::evaluate() {
  for (const auto& decl : module_.decls) {
    if (decl->kind != DeclKind::Const || decl->init == nullptr) {
      continue;
    }
    const std::optional<Value> value = evalExpr(decl->init.get());
    if (value.has_value()) {
      constValues_[decl->name] = *value;
    }
  }
}

std::optional<Value> Interpreter::evalName(Expr* expr) {
  if (Value* local = lookup(expr->name)) {
    return *local;
  }
  const auto constant = constValues_.find(expr->name);
  if (constant != constValues_.end()) {
    return constant->second;
  }
  return std::nullopt;
}

std::optional<Value> Interpreter::evalUnary(Expr* expr) {
  if (expr->lhs == nullptr) {
    return std::nullopt;
  }
  const std::optional<Value> operand = evalExpr(expr->lhs.get());
  if (!operand.has_value()) {
    return std::nullopt;
  }
  if (expr->op == "!") {
    if (operand->kind != Value::Kind::Bool) {
      return std::nullopt;
    }
    return Value::makeBool(!operand->boolValue);
  }
  if (!operand->isNumeric()) {
    return std::nullopt;
  }
  if (expr->op == "-") {
    return operand->kind == Value::Kind::Int
               ? Value::makeInt(-operand->intValue)
               : Value::makeFraction(-operand->fractionValue);
  }
  if (expr->op == "+") {
    return operand;
  }
  if (expr->op == "~") {
    if (operand->kind != Value::Kind::Int) {
      return std::nullopt;
    }
    return Value::makeInt(~operand->intValue);
  }
  return std::nullopt;
}

std::optional<Value> Interpreter::evalBinary(Expr* expr) {
  const std::optional<Value> lhs = evalExpr(expr->lhs.get());
  if (!lhs.has_value()) {
    return std::nullopt;
  }
  // Short-circuit logical operators.
  if (expr->op == "&&" || expr->op == "||") {
    if (lhs->kind != Value::Kind::Bool) {
      return std::nullopt;
    }
    if (expr->op == "&&" && !lhs->boolValue) {
      return Value::makeBool(false);
    }
    if (expr->op == "||" && lhs->boolValue) {
      return Value::makeBool(true);
    }
    const std::optional<Value> rhs = evalExpr(expr->rhs.get());
    if (!rhs.has_value() || rhs->kind != Value::Kind::Bool) {
      return std::nullopt;
    }
    return rhs;
  }

  const std::optional<Value> rhs = evalExpr(expr->rhs.get());
  if (!rhs.has_value()) {
    return std::nullopt;
  }

  if (expr->op == "==" || expr->op == "!=") {
    bool equal = false;
    if (lhs->kind == Value::Kind::String && rhs->kind == Value::Kind::String) {
      equal = lhs->stringValue == rhs->stringValue;
    } else if (lhs->kind == Value::Kind::Bool &&
               rhs->kind == Value::Kind::Bool) {
      equal = lhs->boolValue == rhs->boolValue;
    } else if (lhs->isNumeric() && rhs->isNumeric()) {
      equal = lhs->kind == Value::Kind::Int && rhs->kind == Value::Kind::Int
                  ? lhs->intValue == rhs->intValue
                  : lhs->asDouble() == rhs->asDouble();
    } else {
      return std::nullopt;
    }
    return Value::makeBool(expr->op == "==" ? equal : !equal);
  }

  if (expr->op == "+" && lhs->kind == Value::Kind::String &&
      rhs->kind == Value::Kind::String) {
    return Value::makeString(lhs->stringValue + rhs->stringValue);
  }

  if (!lhs->isNumeric() || !rhs->isNumeric()) {
    return std::nullopt;
  }

  const bool useInt =
      lhs->kind == Value::Kind::Int && rhs->kind == Value::Kind::Int;
  const double a = lhs->asDouble();
  const double b = rhs->asDouble();

  if (expr->op == "<") {
    return Value::makeBool(useInt ? lhs->intValue < rhs->intValue : a < b);
  }
  if (expr->op == "<=") {
    return Value::makeBool(useInt ? lhs->intValue <= rhs->intValue : a <= b);
  }
  if (expr->op == ">") {
    return Value::makeBool(useInt ? lhs->intValue > rhs->intValue : a > b);
  }
  if (expr->op == ">=") {
    return Value::makeBool(useInt ? lhs->intValue >= rhs->intValue : a >= b);
  }

  if (useInt) {
    const std::int64_t x = lhs->intValue;
    const std::int64_t y = rhs->intValue;
    if (expr->op == "+") {
      return Value::makeInt(x + y);
    }
    if (expr->op == "-") {
      return Value::makeInt(x - y);
    }
    if (expr->op == "*") {
      return Value::makeInt(x * y);
    }
    if (expr->op == "/") {
      if (y == 0) {
        error(expr->span, "division by zero in constant expression");
        return std::nullopt;
      }
      return Value::makeInt(x / y);
    }
    if (expr->op == "<<") {
      return Value::makeInt(x << y);
    }
    if (expr->op == ">>") {
      return Value::makeInt(x >> y);
    }
    if (expr->op == "%") {
      if (y == 0) {
        error(expr->span, "division by zero in constant expression");
        return std::nullopt;
      }
      return Value::makeInt(x % y);
    }
    if (expr->op == "&") {
      return Value::makeInt(x & y);
    }
    if (expr->op == "|") {
      return Value::makeInt(x | y);
    }
    if (expr->op == "^") {
      return Value::makeInt(x ^ y);
    }
  }

  if (expr->op == "+") {
    return Value::makeFraction(a + b);
  }
  if (expr->op == "-") {
    return Value::makeFraction(a - b);
  }
  if (expr->op == "*") {
    return Value::makeFraction(a * b);
  }
  if (expr->op == "/") {
    if (b == 0.0) {
      error(expr->span, "division by zero in constant expression");
      return std::nullopt;
    }
    return Value::makeFraction(a / b);
  }
  return std::nullopt;
}

std::optional<Value> Interpreter::evalCall(Expr* expr) {
  Expr* callee = expr->lhs.get();
  if (callee == nullptr || callee->kind != ExprKind::Name) {
    return std::nullopt;
  }
  if (callee->name == "print") {
    for (const auto& arg : expr->args) {
      const std::optional<Value> value = evalExpr(arg.get());
      if (!value.has_value()) {
        return std::nullopt;
      }
      output_ += value->toString();
      output_ += '\n';
    }
    return Value::makeVoid();
  }

  const Decl* function = findFunction(callee->name, expr->args.size());
  if (function == nullptr || !function->generics.empty()) {
    return std::nullopt;
  }

  std::vector<Value> arguments;
  arguments.reserve(expr->args.size());
  for (const auto& arg : expr->args) {
    const std::optional<Value> value = evalExpr(arg.get());
    if (!value.has_value()) {
      return std::nullopt;
    }
    arguments.push_back(*value);
  }
  return callFunction(function, std::move(arguments));
}

std::optional<Value> Interpreter::callFunction(const Decl* decl,
                                               std::vector<Value> arguments) {
  if (decl == nullptr || decl->body == nullptr) {
    return std::nullopt;
  }
  if (++callDepth_ > 512) {
    error(decl->span, "compile-time recursion limit exceeded");
    --callDepth_;
    return std::nullopt;
  }
  if (!consumeFuel(decl->span)) {
    --callDepth_;
    return std::nullopt;
  }

  pushScope();
  for (std::size_t i = 0; i < decl->params.size(); ++i) {
    declare(decl->params[i].name, arguments[i]);
  }

  std::optional<Value> result = Value::makeVoid();
  try {
    execStmt(decl->body.get());
  } catch (const ReturnSignal& signal) {
    result = signal.value;
  } catch (const BreakSignal&) {
    error(decl->span, "'break' outside of a loop during compile-time evaluation");
    result = std::nullopt;
  } catch (const ContinueSignal&) {
    error(decl->span,
          "'continue' outside of a loop during compile-time evaluation");
    result = std::nullopt;
  }

  popScope();
  --callDepth_;
  return result;
}

std::optional<Value> Interpreter::evalExpr(Expr* expr) {
  if (expr == nullptr) {
    return std::nullopt;
  }
  if (!consumeFuel(expr->span)) {
    return std::nullopt;
  }
  switch (expr->kind) {
    case ExprKind::IntLit:
      return Value::makeInt(expr->intValue);
    case ExprKind::FractionLit:
      return Value::makeFraction(expr->fractionValue);
    case ExprKind::CharLit:
      return Value::makeChar(expr->charValue);
    case ExprKind::BoolLit:
      return Value::makeBool(expr->boolValue);
    case ExprKind::StringLit:
      return Value::makeString(expr->stringValue);
    case ExprKind::Name:
      return evalName(expr);
    case ExprKind::Unary:
      return evalUnary(expr);
    case ExprKind::Binary:
      return evalBinary(expr);
    case ExprKind::Call:
      return evalCall(expr);
    default:
      return std::nullopt;
  }
}

void Interpreter::execBlock(Stmt* block) {
  pushScope();
  try {
    for (const auto& stmt : block->statements) {
      execStmt(stmt.get());
    }
  } catch (...) {
    // `return` / `break` / `continue` unwind through the block; the scope must
    // still be popped or recursive compile-time calls observe stale bindings.
    popScope();
    throw;
  }
  popScope();
}

void Interpreter::execStmt(Stmt* stmt) {
  if (stmt == nullptr) {
    return;
  }
  if (!consumeFuel(stmt->span)) {
    return;
  }
  switch (stmt->kind) {
    case StmtKind::Block:
      if (stmt->blockRetention == BlockRetention::Compile) {
        if (retainedExecuted_.insert(stmt).second) {
          try {
            execBlock(stmt);
          } catch (...) {
          }
        }
        return;
      }
      if (stmt->blockRetention == BlockRetention::Runtime) {
        return;
      }
      execBlock(stmt);
      return;
    case StmtKind::Var: {
      Value value;
      if (stmt->expr != nullptr) {
        const std::optional<Value> evaluated = evalExpr(stmt->expr.get());
        if (!evaluated.has_value()) {
          throw ReturnSignal{Value::makeVoid()};
        }
        value = *evaluated;
      }
      declare(stmt->name, value);
      return;
    }
    case StmtKind::Expr:
      (void)evalExpr(stmt->expr.get());
      return;
    case StmtKind::If: {
      const std::optional<Value> condition = evalExpr(stmt->condition.get());
      if (!condition.has_value()) {
        return;
      }
      const bool truth = condition->kind == Value::Kind::Bool
                             ? condition->boolValue
                             : (condition->isNumeric() &&
                                (condition->kind == Value::Kind::Int
                                     ? condition->intValue != 0
                                     : condition->fractionValue != 0.0));
      if (truth) {
        execStmt(stmt->thenBranch.get());
      } else {
        execStmt(stmt->elseBranch.get());
      }
      return;
    }
    case StmtKind::While: {
      while (true) {
        const std::optional<Value> condition = evalExpr(stmt->condition.get());
        if (!condition.has_value()) {
          return;
        }
        const bool truth = condition->kind == Value::Kind::Bool &&
                           condition->boolValue;
        if (!truth) {
          break;
        }
        try {
          execStmt(stmt->thenBranch.get());
        } catch (const BreakSignal&) {
          break;
        } catch (const ContinueSignal&) {
          continue;
        }
      }
      return;
    }
    case StmtKind::Return: {
      Value value;
      if (stmt->expr != nullptr) {
        const std::optional<Value> evaluated = evalExpr(stmt->expr.get());
        if (!evaluated.has_value()) {
          throw ReturnSignal{Value::makeVoid()};
        }
        value = *evaluated;
      }
      throw ReturnSignal{value};
    }
    case StmtKind::Break:
      throw BreakSignal{};
    case StmtKind::Continue:
      throw ContinueSignal{};
    case StmtKind::Destroy:
      return;
  }
}

}  // namespace coord
