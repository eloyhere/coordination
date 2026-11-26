#pragma once

#include "coord/ast.h"
#include "coord/modules/descriptor.h"
#include "coord/support.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace coord::processor {

struct LineDirective {
  std::string file;
  std::int64_t line = 0;
};

/// R1 processor execution scope: bind a `description` argument to the target
/// declaration, evaluate the restricted processor body (local declarations,
/// description queries, `line(...)`), and report unsupported statements.
/// Generated declarations are deliberately not part of R1.
class Executor {
 public:
  Executor(const ModuleAst& module, DiagnosticEngine& diagnostics)
      : module_(module), diagnostics_(diagnostics) {
    descriptors_ = descriptor::Builder::build(module_);
  }

  void run(int maxRounds = 8) {
    std::vector<const Decl*> pending;
    for (const auto& decl : module_.decls) {
      if (!decl->isCore && !decl->isStandard) {
        pending.push_back(decl.get());
      }
    }

    std::set<const Decl*> processed;
    for (int round = 0; round < maxRounds; ++round) {
      std::vector<const Decl*> current;
      for (const Decl* decl : pending) {
        if (processed.insert(decl).second) {
          current.push_back(decl);
        }
      }
      if (current.empty()) {
        return;
      }

      const std::size_t before = generated_.size();
      for (const Decl* decl : current) {
        for (const AnnotationUse& use : decl->annotations) {
          const Decl* annotation = findAnnotation(use.name);
          if (annotation == nullptr) {
            continue;
          }
          std::string processorName = annotation->processorMethod;
          if (processorName.empty()) {
            processorName = findProcessorMethodName(*annotation);
          }
          if (processorName.empty()) {
            continue;
          }
          const Decl* method = findMethod(*annotation, processorName);
          if (method == nullptr || method->params.empty()) {
            continue;
          }
          executeProcessor(*method, *decl);
        }
      }

      if (generated_.size() == before) {
        return;
      }
      for (std::size_t i = before; i < generated_.size(); ++i) {
        pending.push_back(generated_[i].get());
      }
      if (round + 1 == maxRounds) {
        diagnostics_.error(
            generated_.back()->span, "E2068",
            "processor rounds exceeded the limit of " +
                std::to_string(maxRounds));
        return;
      }
    }
  }

  [[nodiscard]] int invocations() const { return invocations_; }
  [[nodiscard]] const std::vector<LineDirective>& lines() const {
    return lines_;
  }
  [[nodiscard]] std::vector<std::unique_ptr<Decl>> takeGenerated() {
    return std::move(generated_);
  }

 private:
  enum class ValueKind { Int, Bool, String, Description };
  struct Value {
    ValueKind kind = ValueKind::Int;
    std::int64_t integer = 0;
    bool boolean = false;
    std::string string;
    const Decl* description = nullptr;
  };

  struct Frame {
    std::map<std::string, Value> locals;
  };

  [[nodiscard]] const Decl* findAnnotation(const std::string& name) const {
    for (const auto& decl : module_.decls) {
      if (decl->kind == DeclKind::Annotation && decl->name == name) {
        return decl.get();
      }
    }
    return nullptr;
  }

  [[nodiscard]] static const Decl* findMethod(const Decl& owner,
                                              const std::string& name) {
    for (const auto& member : owner.members) {
      if (member->kind == DeclKind::Method && member->name == name) {
        return member.get();
      }
    }
    return nullptr;
  }

  [[nodiscard]] static std::string findProcessorMethodName(
      const Decl& owner) {
    for (const auto& member : owner.members) {
      if (member->kind != DeclKind::Method) {
        continue;
      }
      for (const AnnotationUse& use : member->annotations) {
        if (use.name == "Processor") {
          return member->name;
        }
      }
    }
    return {};
  }

  [[nodiscard]] static TypeExprPtr makeNamedType(const std::string& name,
                                                 Span span) {
    auto type = std::make_unique<TypeExpr>();
    type->kind = TypeExprKind::Named;
    type->name = name;
    type->span = span;
    return type;
  }

  void generateFunction(const std::string& name, Span span) {
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Function;
    decl->name = name;
    decl->span = span;
    decl->type = makeNamedType("void", span);
    auto body = std::make_unique<Stmt>();
    body->kind = StmtKind::Block;
    body->span = span;
    decl->body = std::move(body);
    generated_.push_back(std::move(decl));
  }

  void generateAnnotatedFunction(const std::string& name,
                                 const std::string& annotation, Span span) {
    generateFunction(name, span);
    AnnotationUse use;
    use.name = annotation;
    use.span = span;
    generated_.back()->annotations.push_back(std::move(use));
  }

  void generateReturningFunction(const std::string& name, std::int64_t value,
                                 Span span) {    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Function;
    decl->name = name;
    decl->span = span;
    decl->type = makeNamedType("integer", span);
    auto body = std::make_unique<Stmt>();
    body->kind = StmtKind::Block;
    body->span = span;
    auto returnStmt = std::make_unique<Stmt>();
    returnStmt->kind = StmtKind::Return;
    returnStmt->span = span;
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::IntLit;
    expr->intValue = value;
    expr->span = span;
    returnStmt->expr = std::move(expr);
    body->statements.push_back(std::move(returnStmt));
    decl->body = std::move(body);
    generated_.push_back(std::move(decl));
  }

  void generateConstant(const std::string& name, std::int64_t value,
                        Span span) {
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Const;
    decl->name = name;
    decl->span = span;
    decl->type = makeNamedType("integer", span);
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::IntLit;
    expr->intValue = value;
    expr->span = span;
    decl->init = std::move(expr);
    generated_.push_back(std::move(decl));
  }

  [[nodiscard]] static std::string kindName(DeclKind kind) {
    switch (kind) {
      case DeclKind::Class: return "class";
      case DeclKind::Interface: return "interface";
      case DeclKind::Annotation: return "annotation";
      case DeclKind::Enum: return "enumerate";
      case DeclKind::Function: return "function";
      case DeclKind::Method: return "method";
      case DeclKind::Field: return "field";
      case DeclKind::Constructor: return "constructor";
      case DeclKind::Destructor: return "destructor";
      case DeclKind::Const: return "const";
    }
    return "unknown";
  }

  [[nodiscard]] static int memberCount(const Decl& decl, DeclKind kind) {
    int count = 0;
    for (const auto& member : decl.members) {
      if (member->kind == kind) {
        ++count;
      }
    }
    return count;
  }

  void executeProcessor(const Decl& method, const Decl& target) {
    ++invocations_;
    Frame frame;
    if (!method.params.empty()) {
      Value value;
      value.kind = ValueKind::Description;
      value.description = &target;
      frame.locals[method.params[0].name] = value;
    }
    executeBlock(method.body.get(), frame);
  }

  void executeBlock(const Stmt* block, Frame& frame) {
    if (block == nullptr) {
      return;
    }
    if (block->kind == StmtKind::Block) {
      for (const auto& statement : block->statements) {
        if (!executeStmt(statement.get(), frame)) {
          return;
        }
      }
      return;
    }
    executeStmt(block, frame);
  }

  bool executeStmt(const Stmt* stmt, Frame& frame) {
    if (stmt == nullptr) {
      return true;
    }
    switch (stmt->kind) {
      case StmtKind::Block:
        executeBlock(stmt, frame);
        return true;
      case StmtKind::Var: {
        Value value;
        if (stmt->expr != nullptr) {
          value = evaluate(stmt->expr.get(), frame, stmt->span);
        }
        frame.locals[stmt->name] = value;
        return true;
      }
      case StmtKind::Expr:
        evaluate(stmt->expr.get(), frame, stmt->span);
        return true;
      case StmtKind::If: {
        Value condition = evaluate(stmt->condition.get(), frame, stmt->span);
        if (condition.kind != ValueKind::Bool) {
          diagnostics_.error(stmt->span, "E2067",
                             "processor if condition must be boolean");
          return true;
        }
        if (condition.boolean) {
          executeStmt(stmt->thenBranch.get(), frame);
        } else if (stmt->elseBranch != nullptr) {
          executeStmt(stmt->elseBranch.get(), frame);
        }
        return true;
      }
      case StmtKind::Return:
        return false;
      default:
        diagnostics_.error(
            stmt->span, "E2067",
            "statement is not supported in the R1 processor executor");
        return true;
    }
  }

  Value evaluate(const Expr* expr, Frame& frame, Span fallback) {
    if (expr == nullptr) {
      return {};
    }
    switch (expr->kind) {
      case ExprKind::IntLit:
        return makeInt(expr->intValue);
      case ExprKind::BoolLit:
        return makeBool(expr->boolValue);
      case ExprKind::StringLit:
        return makeString(expr->stringValue);
      case ExprKind::Name: {
        const auto found = frame.locals.find(expr->name);
        if (found != frame.locals.end()) {
          return found->second;
        }
        diagnostics_.error(expr->span, "E2067",
                           "unknown processor variable '" + expr->name + "'");
        return {};
      }
      case ExprKind::Call:
        return evaluateCall(expr, frame, fallback);
      default:
        diagnostics_.error(expr->span, "E2067",
                           "expression is not supported in the R1 processor "
                           "executor");
        return {};
    }
  }

  Value evaluateCall(const Expr* expr, Frame& frame, Span fallback) {
    if (expr->lhs == nullptr) {
      diagnostics_.error(fallback, "E2067", "missing processor callee");
      return {};
    }
    if (expr->lhs->kind == ExprKind::Name) {
      const std::string& name = expr->lhs->name;
      if (name == "addFunction") {
        if (expr->args.size() != 1) {
          diagnostics_.error(fallback, "E2067",
                             "addFunction() takes one name");
          return {};
        }
        Value generatedName = evaluate(expr->args[0].get(), frame, fallback);
        if (generatedName.kind != ValueKind::String) {
          diagnostics_.error(fallback, "E2067",
                             "addFunction() requires a string name");
          return {};
        }
        generateFunction(generatedName.string, fallback);
        return {};
      }
      if (name == "addAnnotatedFunction") {
        if (expr->args.size() != 2) {
          diagnostics_.error(fallback, "E2067",
                             "addAnnotatedFunction() takes a function name and "
                             "an annotation name");
          return {};
        }
        Value generatedName = evaluate(expr->args[0].get(), frame, fallback);
        Value annotationName = evaluate(expr->args[1].get(), frame, fallback);
        if (generatedName.kind != ValueKind::String ||
            annotationName.kind != ValueKind::String) {
          diagnostics_.error(
              fallback, "E2067",
              "addAnnotatedFunction() requires two string names");
          return {};
        }
        generateAnnotatedFunction(generatedName.string, annotationName.string,
                                  fallback);
        return {};
      }
      if (name == "addFunctionReturning") {
        if (expr->args.size() != 2) {
          diagnostics_.error(fallback, "E2067",
                             "addFunctionReturning() takes a name and an "
                             "integer");
          return {};
        }
        Value generatedName = evaluate(expr->args[0].get(), frame, fallback);
        Value value = evaluate(expr->args[1].get(), frame, fallback);
        if (generatedName.kind != ValueKind::String ||
            value.kind != ValueKind::Int) {
          diagnostics_.error(
              fallback, "E2067",
              "addFunctionReturning() requires (string name, integer value)");
          return {};
        }
        generateReturningFunction(generatedName.string, value.integer,
                                  fallback);
        return {};
      }
      if (name == "addConstant") {
        if (expr->args.size() != 2) {
          diagnostics_.error(fallback, "E2067",
                             "addConstant() takes a name and an integer");
          return {};
        }
        Value generatedName = evaluate(expr->args[0].get(), frame, fallback);
        Value value = evaluate(expr->args[1].get(), frame, fallback);
        if (generatedName.kind != ValueKind::String ||
            value.kind != ValueKind::Int) {
          diagnostics_.error(
              fallback, "E2067",
              "addConstant() requires (string name, integer value)");
          return {};
        }
        generateConstant(generatedName.string, value.integer, fallback);
        return {};
      }
      if (name != "line") {
        diagnostics_.error(fallback, "E2067",
                           "unsupported processor call '" + name + "'");
        return {};
      }
      if (expr->args.size() != 2) {
        diagnostics_.error(fallback, "E2067",
                           "line() requires a file and a line number");
        return {};
      }
      Value file = evaluate(expr->args[0].get(), frame, fallback);
      Value number = evaluate(expr->args[1].get(), frame, fallback);
      if (file.kind != ValueKind::String || number.kind != ValueKind::Int) {
        diagnostics_.error(fallback, "E2067",
                           "line() requires (string, integer)");
        return {};
      }
      lines_.push_back(LineDirective{file.string, number.integer});
      return {};
    }

    if (expr->lhs->kind != ExprKind::Member ||
        expr->lhs->lhs == nullptr ||
        expr->lhs->lhs->kind != ExprKind::Name) {
      diagnostics_.error(fallback, "E2067",
                         "only description.method() calls are supported in R1");
      return {};
    }

    const std::string& baseName = expr->lhs->lhs->name;
    const auto found = frame.locals.find(baseName);
    if (found == frame.locals.end() ||
        found->second.kind != ValueKind::Description ||
        found->second.description == nullptr) {
      diagnostics_.error(fallback, "E2067",
                         "'" + baseName + "' is not a description value");
      return {};
    }
    const Decl& target = *found->second.description;
    const std::string& method = expr->lhs->name;
    const descriptor::Declaration* info = nullptr;
    for (const auto& candidate : descriptors_) {
      if (candidate.origin == &target) {
        info = &candidate;
        break;
      }
    }
    if (method == "name") {
      return makeString(info != nullptr ? info->name : target.name);
    }
    if (method == "kind") {
      return makeString(info != nullptr ? info->kind : kindName(target.kind));
    }
    if (method == "fieldCount") {
      return makeInt(info != nullptr
                         ? static_cast<std::int64_t>(info->fields.size())
                         : memberCount(target, DeclKind::Field));
    }
    if (method == "methodCount") {
      return makeInt(info != nullptr
                         ? static_cast<std::int64_t>(info->methods.size())
                         : memberCount(target, DeclKind::Method));
    }
    if (method == "hasAnnotation") {
      if (expr->args.size() != 1) {
        diagnostics_.error(fallback, "E2067",
                           "hasAnnotation() takes one name");
        return makeBool(false);
      }
      Value requested = evaluate(expr->args[0].get(), frame, fallback);
      if (requested.kind != ValueKind::String) {
        diagnostics_.error(fallback, "E2067",
                           "hasAnnotation() requires a string");
        return makeBool(false);
      }
      if (info != nullptr) {
        for (const std::string& annotation : info->annotations) {
          if (annotation == requested.string) {
            return makeBool(true);
          }
        }
        return makeBool(false);
      }
      for (const AnnotationUse& use : target.annotations) {
        if (use.name == requested.string) {
          return makeBool(true);
        }
      }
      return makeBool(false);
    }
    diagnostics_.error(fallback, "E2067",
                       "unsupported description method '" + method + "'");
    return {};
  }

  [[nodiscard]] static Value makeInt(std::int64_t value) {
    Value result;
    result.kind = ValueKind::Int;
    result.integer = value;
    return result;
  }
  [[nodiscard]] static Value makeBool(bool value) {
    Value result;
    result.kind = ValueKind::Bool;
    result.boolean = value;
    return result;
  }
  [[nodiscard]] static Value makeString(std::string value) {
    Value result;
    result.kind = ValueKind::String;
    result.string = std::move(value);
    return result;
  }

  const ModuleAst& module_;
  DiagnosticEngine& diagnostics_;
  std::vector<descriptor::Declaration> descriptors_;
  int invocations_ = 0;
  std::vector<LineDirective> lines_;
  std::vector<std::unique_ptr<Decl>> generated_;
};

}  // namespace coord::processor
