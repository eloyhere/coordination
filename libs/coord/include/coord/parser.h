#pragma once

#include "coord/ast.h"
#include "coord/syntax.h"

#include <memory>
#include <vector>

namespace coord {

class Parser {
public:
  Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics);

  std::unique_ptr<ModuleAst> parseModule(std::uint32_t fileId);

private:
  [[nodiscard]] const Token& peek(std::size_t offset = 0) const;
  [[nodiscard]] const Token& current() const { return peek(0); }
  [[nodiscard]] bool check(TokKind kind) const { return current().kind == kind; }
  [[nodiscard]] bool atEnd() const { return check(TokKind::End); }
  bool match(TokKind kind);
  const Token& advance();

  void error(const Token& token, std::string message);
  void errorAt(Span span, std::string message);
  bool expect(TokKind kind, const char* what);
  void synchronizeTopLevel();
  void synchronizeStatement();
  void takeDocInto(Decl& decl);

  std::vector<AnnotationUse> parseAnnotations();
  ImportDecl parseImport();
  std::vector<std::string> parsePath();

  DeclPtr parseTopLevelDecl(std::vector<AnnotationUse> annotations,
                            bool exported);
  DeclPtr parseClass(std::vector<AnnotationUse> annotations, bool exported,
                     bool isInterface);
  DeclPtr parseEnum(std::vector<AnnotationUse> annotations, bool exported);
  DeclPtr parseClassMember(const std::string& className);
  DeclPtr parseAnnotation(std::vector<AnnotationUse> annotations, bool exported);
  DeclPtr parseFunction(std::vector<AnnotationUse> annotations, bool exported);
  DeclPtr parseConst(std::vector<AnnotationUse> annotations, bool exported);

  std::vector<GenericParam> parseGenericParams();
  std::vector<Param> parseParams();
  Param parseParam();

  TypeExprPtr parseType(bool allowRef);
  TypeExprPtr parseUnionType(bool allowRef);
  TypeExprPtr parseIntersectType(bool allowRef);
  TypeExprPtr parsePostfixType();
  TypeExprPtr parsePrimaryType();

  StmtPtr parseBlock();
  StmtPtr parseStatement();
  StmtPtr parseIf();
  StmtPtr parseFor();
  StmtPtr parseWhile();
  StmtPtr parseSwitch(bool isMatch = false);
  StmtPtr parseDestructure();
  StmtPtr parseReturn();
  StmtPtr parseDestroy();
  StmtPtr parseVarDecl();
  StmtPtr parseExprStmt();
  [[nodiscard]] bool looksLikeVarDecl() const;
  [[nodiscard]] bool looksLikeDestructure() const;
  [[nodiscard]] bool looksLikeConstructureArgs() const;
  bool parseConstructureInitializer(Stmt& stmt);

  ExprPtr parseExpression();
  ExprPtr parseAssignment();
  ExprPtr parseBinary(int minPrecedence);
  ExprPtr parseUnary();
  ExprPtr parsePostfix();
  ExprPtr parsePrimary();
  ExprPtr parseObjectLiteral();
  [[nodiscard]] bool looksLikeLambda() const;
  ExprPtr parseLambda();
  std::vector<ExprPtr> parseArguments();

  static int binaryPrecedence(TokKind kind);
  static bool isAssignmentOperator(TokKind kind);

  std::vector<Token> tokens_;
  DiagnosticEngine& diagnostics_;
  std::size_t pos_ = 0;
  std::string pendingDoc_;
};

}  // namespace coord
