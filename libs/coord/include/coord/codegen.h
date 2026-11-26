#pragma once

#include "coord/ast.h"
#include "coord/interp.h"
#include "coord/modules/descriptor.h"

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace coord {

/// Lowers the checked AST to a single C++20 translation unit.
///
/// RAII mapping: the emitter never writes member destruction calls. C++
/// destroys members in reverse declaration order after the destructor body and
/// then destroys the base subobject, which is exactly Coordination's rule.
/// `~this.member;` statements are erased and kept as comments.
class CppEmitter {
public:
  CppEmitter(const ModuleAst& module,
             const std::map<std::string, Value>& constantValues,
             std::ostream& out);

  void emit();
  void setTestMode(bool enabled) { testMode_ = enabled; }
  void setTestFilter(std::string filter) { testFilter_ = std::move(filter); }

private:
  void emitPrelude();
  void emitClass(const Decl& decl);
  void emitEnum(const Decl& decl);
  void emitInterface(const Decl& decl);
  void emitFunction(const Decl& decl);
  void emitConst(const Decl& decl);
  void emitMainWrapper();
  void emitTestRunner();
  void emitRetentionTables();
  void emitAnnotationTraps(const Decl& decl);
  void emitRuntimeReflectionTables();

  void emitParams(const std::vector<Param>& params);
  void emitReturnType(const Decl& decl);
  void emitStorageType(const TypeExpr* type, bool isUnsigned, int length);
  void emitAnnotations(const std::vector<AnnotationUse>& annotations);
  void emitInheritedAnnotations(const Decl& decl);
  void emitType(const TypeExpr* type);
  void emitStmt(const Stmt* stmt);
  void emitBlock(const Stmt* block);
  void emitExpr(const Expr* expr);
  void emitInitializer(const Expr* expr, int radix);
  void emitShareEdgeRecord(const Expr* assign);

  [[nodiscard]] std::string typeToString(const TypeExpr* type) const;
  [[nodiscard]] std::string sanitize(const std::string& name) const;
  [[nodiscard]] std::string literal(const Value& value) const;
  [[nodiscard]] const Value* constant(const std::string& name) const;
  [[nodiscard]] bool isResultCall(const Expr* expr) const;
  [[nodiscard]] bool isErrorCall(const Expr* expr) const;
  [[nodiscard]] const Decl* proxyOwner(const Expr* base) const;
  [[nodiscard]] const Decl* proxyTrap(const Decl* owner,
                                      const std::string& annotation) const;
  [[nodiscard]] const Decl* proxyInstanceTrap(
      const Decl* owner, const std::string& annotation) const;
  [[nodiscard]] bool proxyRuntime(const Decl* owner) const;
  [[nodiscard]] const Decl* fieldOf(const Decl* owner,
                                    const std::string& name) const;
  [[nodiscard]] const Param* paramOf(const Decl* callable,
                                     const std::string& name) const;
  [[nodiscard]] const Decl* callableOf(const Expr* callee) const;
  [[nodiscard]] std::string trapCallName(const Decl* owner,
                                         const Decl& trap) const;
  [[nodiscard]] bool isProxyField(const Decl* owner,
                                  const std::string& name) const;

  void indent();
  void line(const std::string& text);

  const ModuleAst& module_;
  const std::map<std::string, Value>& constantValues_;
  std::ostream& out_;
  int depth_ = 0;

  std::set<std::string> typeParams_;
  std::set<std::string> fields_;
  const Decl* currentClassDecl_ = nullptr;
  const Decl* currentCallable_ = nullptr;
  int refTemps_ = 0;
  std::map<std::string, int> smartLocals_;
  std::vector<descriptor::Declaration> descriptors_;
  std::string currentBase_;
  std::string currentReturnType_;
  bool currentResult_ = false;
  bool testMode_ = false;
  std::string testFilter_;
  int tempCounter_ = 0;
  int literalRadix_ = 0;
  std::vector<std::set<std::string>> locals_;
};

}  // namespace coord
