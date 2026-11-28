#include "coord/parser.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

namespace coord {
namespace {

std::string lowerAscii(std::string text) {
  for (char& character : text) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

int annotationRadix(const std::vector<AnnotationUse>& annotations,
                    bool& found) {
  found = false;
  for (const AnnotationUse& use : annotations) {
    if (use.name != "Radix" || use.args.empty() || use.args[0] == nullptr ||
        use.args[0]->kind != ExprKind::IntLit) {
      continue;
    }
    found = true;
    return static_cast<int>(use.args[0]->intValue);
  }
  return 0;
}

/// One numeric type-annotation argument (`@Length`, `@Radix`, `@Power`).
struct NumericAnnotation {
  bool present = false;
  bool valid = false;
  int value = 0;
  Span span;
};

NumericAnnotation findNumericAnnotation(
    const std::vector<AnnotationUse>& annotations, std::string_view name) {
  NumericAnnotation result;
  for (const AnnotationUse& use : annotations) {
    if (use.name != name) {
      continue;
    }
    result.present = true;
    result.span = use.span;
    const Expr* literal =
        use.args.size() == 1 ? use.args[0].get() : nullptr;
    bool negative = false;
    while (literal != nullptr && literal->kind == ExprKind::Unary &&
           (literal->op == "+" || literal->op == "-")) {
      if (literal->op == "-") {
        negative = !negative;
      }
      literal = literal->lhs.get();
    }
    if (literal != nullptr && literal->kind == ExprKind::IntLit) {
      const std::int64_t magnitude = literal->intValue;
      const std::int64_t value = negative ? -magnitude : magnitude;
      if (value >= -2147483647LL && value <= 2147483647LL) {
        result.valid = true;
        result.value = static_cast<int>(value);
      }
    }
    break;
  }
  return result;
}

/// Binds leading @Length/@Radix/@Power annotations to the type expression that
/// follows them, so numeric metadata lives on the type rather than only on the
/// enclosing declaration.
void bindNumericAnnotations(const std::vector<AnnotationUse>& annotations,
                            TypeExpr* type) {
  if (type == nullptr) {
    return;
  }
  const NumericAnnotation length = findNumericAnnotation(annotations, "Length");
  if (length.present) {
    type->hasNumericLength = true;
    type->numericLength =
        length.valid ? length.value : std::numeric_limits<int>::min();
    type->numericLengthSpan = length.span;
  }
  const NumericAnnotation radix = findNumericAnnotation(annotations, "Radix");
  if (radix.present) {
    type->hasNumericRadix = true;
    type->numericRadix =
        radix.valid ? radix.value : std::numeric_limits<int>::min();
    type->numericRadixSpan = radix.span;
  }
  const NumericAnnotation power = findNumericAnnotation(annotations, "Power");
  if (power.present) {
    type->hasNumericPower = true;
    type->numericPower =
        power.valid ? power.value : std::numeric_limits<int>::min();
    type->numericPowerSpan = power.span;
  }
}

Span join(Span begin, Span end) {
  return Span{begin.file, begin.begin, end.end};
}

bool isTypeKeyword(TokKind kind) {
  switch (kind) {
    case TokKind::KwVoid:
    case TokKind::KwInteger:
    case TokKind::KwFix:
    case TokKind::KwFloat:
    case TokKind::KwRecord:
    case TokKind::KwCharacter:
    case TokKind::KwBoolean:
    case TokKind::KwFunction:
      return true;
    default:
      return false;
  }
}

Access accessFromToken(TokKind kind) {
  switch (kind) {
    case TokKind::KwPublic:
      return Access::Public;
    case TokKind::KwPrivate:
      return Access::Private;
    case TokKind::KwProtected:
      return Access::Protected;
    default:
      return Access::Default;
  }
}

std::string keywordTypeName(TokKind kind) {
  switch (kind) {
    case TokKind::KwVoid:
      return "void";
    case TokKind::KwInteger:
      return "integer";
    case TokKind::KwFix:
      return "fix";
    case TokKind::KwFloat:
      return "float";
    case TokKind::KwRecord:
      return "record";
    case TokKind::KwCharacter:
      return "character";
    case TokKind::KwBoolean:
      return "boolean";
    case TokKind::KwFunction:
      return "function";
    default:
      return {};
  }
}

std::string operatorText(TokKind kind) {
  switch (kind) {
    case TokKind::Plus:
      return "+";
    case TokKind::Minus:
      return "-";
    case TokKind::Star:
      return "*";
    case TokKind::Slash:
      return "/";
    case TokKind::Percent:
      return "%";
    case TokKind::Amp:
      return "&";
    case TokKind::Pipe:
      return "|";
    case TokKind::Caret:
      return "^";
    case TokKind::Tilde:
      return "~";
    case TokKind::Bang:
      return "!";
    case TokKind::EqualEqual:
      return "==";
    case TokKind::BangEqual:
      return "!=";
    case TokKind::Less:
      return "<";
    case TokKind::Greater:
      return ">";
    case TokKind::LessEqual:
      return "<=";
    case TokKind::GreaterEqual:
      return ">=";
    case TokKind::AmpAmp:
      return "&&";
    case TokKind::PipePipe:
      return "||";
    case TokKind::Equal:
      return "=";
    case TokKind::PlusEqual:
      return "+=";
    case TokKind::MinusEqual:
      return "-=";
    case TokKind::StarEqual:
      return "*=";
    case TokKind::SlashEqual:
      return "/=";
    case TokKind::PercentEqual:
      return "%=";
    case TokKind::AmpEqual:
      return "&=";
    case TokKind::PipeEqual:
      return "|=";
    case TokKind::CaretEqual:
      return "^=";
    default:
      return {};
  }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

const Token& Parser::peek(std::size_t offset) const {
  const std::size_t index = std::min(pos_ + offset, tokens_.size() - 1);
  return tokens_[index];
}

bool Parser::match(TokKind kind) {
  if (!check(kind)) {
    return false;
  }
  ++pos_;
  return true;
}

const Token& Parser::advance() {
  const Token& token = current();
  if (!atEnd()) {
    ++pos_;
  }
  return token;
}

void Parser::error(const Token& token, std::string message) {
  diagnostics_.error(token.span, "E1101", std::move(message));
}

void Parser::errorAt(Span span, std::string message) {
  diagnostics_.error(span, "E1101", std::move(message));
}

bool Parser::expect(TokKind kind, const char* what) {
  if (match(kind)) {
    return true;
  }
  error(current(), std::string("expected ") + what + ", found " +
                       tokenKindName(current().kind));
  return false;
}

void Parser::synchronizeTopLevel() {
  while (!atEnd()) {
    if (match(TokKind::Semicolon)) {
      return;
    }
    if (check(TokKind::RBrace)) {
      advance();
      return;
    }
    if (check(TokKind::KwClass) || check(TokKind::KwInterface) ||
        check(TokKind::KwEnumerate) || check(TokKind::KwAnnotation) ||
        check(TokKind::KwConst) || check(TokKind::KwExport) ||
        check(TokKind::At)) {
      return;
    }
    advance();
  }
}

void Parser::synchronizeStatement() {
  while (!atEnd()) {
    if (match(TokKind::Semicolon)) {
      return;
    }
    if (check(TokKind::RBrace)) {
      return;
    }
    advance();
  }
}

std::vector<std::string> Parser::parsePath() {
  std::vector<std::string> path;
  if (!check(TokKind::Identifier)) {
    error(current(), "expected identifier in path");
    return path;
  }
  path.push_back(std::string(advance().text));
  while (match(TokKind::Dot)) {
    if (!check(TokKind::Identifier)) {
      error(current(), "expected identifier after '.'");
      break;
    }
    path.push_back(std::string(advance().text));
  }
  return path;
}

void Parser::takeDocInto(Decl& decl) {
  if (!pendingDoc_.empty()) {
    decl.doc = pendingDoc_;
    pendingDoc_.clear();
  }
}

std::vector<AnnotationUse> Parser::parseAnnotations() {
  std::vector<AnnotationUse> annotations;
  while (check(TokKind::DocComment)) {
    std::string text = advance().docText;
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      continue;
    }
    std::size_t end = text.find_last_not_of(" \t\r\n");
    pendingDoc_ += text.substr(begin, end - begin + 1);
    pendingDoc_ += '\n';
  }
  while (match(TokKind::At)) {
    AnnotationUse use;
    use.span = current().span;
    if (!check(TokKind::Identifier)) {
      error(current(), "expected annotation name after '@'");
      break;
    }
    use.name = std::string(advance().text);
    if (match(TokKind::Less)) {
      if (!check(TokKind::Greater)) {
        do {
          use.typeArgs.push_back(parseType(false));
        } while (match(TokKind::Comma));
      }
      expect(TokKind::Greater, "'>'");
    }
    if (match(TokKind::LParen)) {
      if (!check(TokKind::RParen)) {
        do {
          if (use.name == "Target") {
            const TokKind kind = current().kind;
            const bool targetKeyword =
                kind == TokKind::KwClass || kind == TokKind::KwInterface ||
                kind == TokKind::KwAnnotation ||
                kind == TokKind::KwFunction || kind == TokKind::KwEnumerate;
            if (check(TokKind::Identifier) || targetKeyword) {
              auto arg = std::make_unique<Expr>();
              arg->span = current().span;
              arg->kind = ExprKind::Name;
              arg->name = std::string(current().text);
              advance();
              use.args.push_back(std::move(arg));
            } else {
              error(current(), "expected @Target name");
            }
          } else {
            use.args.push_back(parseExpression());
          }
        } while (match(TokKind::Comma));
      }
      expect(TokKind::RParen, "')'");
    }
    annotations.push_back(std::move(use));
  }
  return annotations;
}

ImportDecl Parser::parseImport() {
  ImportDecl import;
  import.span = current().span;
  expect(TokKind::KwImport, "'import'");
  if (check(TokKind::LBrace)) {
    advance();
    if (!check(TokKind::RBrace)) {
      do {
        if (check(TokKind::Identifier)) {
          import.items.push_back(std::string(advance().text));
        } else {
          error(current(), "expected imported name");
          break;
        }
      } while (match(TokKind::Comma));
    }
    expect(TokKind::RBrace, "'}'");
    if (!check(TokKind::Identifier) || current().text != "from") {
      error(current(), "expected 'from' after import list");
    } else {
      advance();
    }
    import.path = parsePath();
    expect(TokKind::Semicolon, "';'");
    return import;
  }
  import.path = parsePath();
  if (match(TokKind::KwAs)) {
    if (check(TokKind::Identifier)) {
      import.alias = std::string(advance().text);
      import.hasAlias = true;
    } else {
      error(current(), "expected alias after 'as'");
    }
  }
  expect(TokKind::Semicolon, "';'");
  return import;
}

std::unique_ptr<ModuleAst> Parser::parseModule(std::uint32_t fileId) {
  auto module = std::make_unique<ModuleAst>();
  module->fileId = fileId;

  if (match(TokKind::KwModule)) {
    module->modulePath = parsePath();
    expect(TokKind::Semicolon, "';'");
  }

  while (check(TokKind::KwImport)) {
    module->imports.push_back(parseImport());
  }

  while (!atEnd()) {
    if (check(TokKind::At) && peek(1).kind == TokKind::Identifier &&
        (peek(1).text == "Unsafe" || peek(1).text == "Safe")) {
      const bool unsafeDirective = peek(1).text == "Unsafe";
      const std::size_t start = pos_;
      parseAnnotations();
      if (match(TokKind::Semicolon)) {
        module->moduleUnsafe = module->moduleUnsafe || unsafeDirective;
        module->moduleSafe = module->moduleSafe || !unsafeDirective;
        continue;
      }
      pos_ = start;
    }
    std::vector<AnnotationUse> annotations = parseAnnotations();
    std::string topDoc = std::move(pendingDoc_);
    pendingDoc_.clear();
    bool exported = false;
    if (match(TokKind::KwExport)) {
      if (check(TokKind::LBrace)) {
        advance();
        if (!check(TokKind::RBrace)) {
          do {
            if (check(TokKind::Identifier)) {
              module->exportedNames.push_back(
                  std::string(advance().text));
            } else {
              error(current(), "expected exported name");
              break;
            }
          } while (match(TokKind::Comma));
        }
        expect(TokKind::RBrace, "'}'");
        expect(TokKind::Semicolon, "';'");
        continue;
      }
      exported = true;
    }
    DeclPtr decl = parseTopLevelDecl(std::move(annotations), exported);
    if (decl != nullptr) {
      if (!topDoc.empty()) {
        decl->doc = std::move(topDoc);
      }
      module->decls.push_back(std::move(decl));
    } else {
      const std::size_t before = pos_;
      synchronizeTopLevel();
      if (pos_ == before && !atEnd() && !check(TokKind::RBrace)) {
        advance();
      }
    }
  }
  for (const std::string& name : module->exportedNames) {
    for (const auto& decl : module->decls) {
      if (decl->name == name) {
        decl->exported = true;
      }
    }
  }
  return module;
}

std::vector<GenericParam> Parser::parseGenericParams() {
  std::vector<GenericParam> params;
  if (!match(TokKind::Less)) {
    return params;
  }
  do {
    GenericParam param;
    param.span = current().span;
    if (!check(TokKind::Identifier)) {
      error(current(), "expected generic parameter name");
      break;
    }
    param.name = std::string(advance().text);
    if (match(TokKind::KwExtends)) {
      param.bound = parseType(false);
    }
    params.push_back(std::move(param));
  } while (match(TokKind::Comma));
  expect(TokKind::Greater, "'>'");
  return params;
}

Param Parser::parseParam() {
  Param param;
  param.annotations = parseAnnotations();
  param.span = current().span;
  bool hasRadix = false;
  const int declaredRadix = annotationRadix(param.annotations, hasRadix);
  param.radix = hasRadix ? declaredRadix : 0;
  for (const AnnotationUse& use : param.annotations) {
    if (use.name == "Unique") {
      param.isUnique = true;
    } else if (use.name == "Share") {
      param.isShare = true;
    } else if (use.name == "Weak") {
      param.isWeak = true;
    } else if (use.name == "Optional") {
      param.isOptional = true;
      if (!use.args.empty() && use.args[0] != nullptr) {
        Expr* argument = use.args[0].get();
        if (argument->kind == ExprKind::Name) {
          const std::string policy = lowerAscii(argument->name);
          if (policy == "skip") {
            param.proxyPolicy = ProxyPolicy::Skip;
          } else if (policy == "abort") {
            param.proxyPolicy = ProxyPolicy::Abort;
          } else if (policy == "error") {
            param.proxyPolicy = ProxyPolicy::Error;
          }
        } else if (argument->kind == ExprKind::Call &&
                   argument->lhs != nullptr &&
                   argument->lhs->kind == ExprKind::Name &&
                   lowerAscii(argument->lhs->name) == "default" &&
                   argument->args.size() == 1) {
          param.proxyPolicy = ProxyPolicy::Default;
          param.optionalDefault = argument->args[0].get();
        }
      }
    }
  }
  param.type = parseType(true);
  bindNumericAnnotations(param.annotations, param.type.get());
  if (param.type != nullptr && param.type->hasNumericPower) {
    param.hasPower = true;
    param.power = param.type->numericPower;
  }
  if (check(TokKind::Identifier)) {
    param.name = std::string(advance().text);
  } else {
    error(current(), "expected parameter name");
  }
  if (match(TokKind::Equal)) {
    param.defaultValue = parseExpression();
  }
  return param;
}

std::vector<Param> Parser::parseParams() {
  std::vector<Param> params;
  if (!expect(TokKind::LParen, "'('")) {
    return params;
  }
  if (!check(TokKind::RParen)) {
    do {
      params.push_back(parseParam());
    } while (match(TokKind::Comma));
  }
  expect(TokKind::RParen, "')'");
  return params;
}

TypeExprPtr Parser::parsePrimaryType() {
  auto make = [](TypeExprKind kind, Span span) {
    auto node = std::make_unique<TypeExpr>();
    node->kind = kind;
    node->span = span;
    return node;
  };

  if (isTypeKeyword(current().kind)) {
    const Token& token = advance();
    auto node = make(TypeExprKind::Named, token.span);
    node->name = keywordTypeName(token.kind);
    if (match(TokKind::Less)) {
      if (!check(TokKind::Greater)) {
        do {
          node->args.push_back(parseType(true));
        } while (match(TokKind::Comma));
      }
      expect(TokKind::Greater, "'>'");
    }
    return node;
  }

  if (check(TokKind::Identifier)) {
    const Token& token = advance();
    auto node = make(TypeExprKind::Named, token.span);
    node->name = std::string(token.text);
    while (match(TokKind::Dot)) {
      if (!check(TokKind::Identifier)) {
        error(current(), "expected name after '.'");
        break;
      }
      node->name += "." + std::string(advance().text);
    }
    if (match(TokKind::Less)) {
      if (!check(TokKind::Greater)) {
        do {
          node->args.push_back(parseType(true));
        } while (match(TokKind::Comma));
      }
      expect(TokKind::Greater, "'>'");
    }
    return node;
  }

  if (check(TokKind::LParen)) {
    const Token& open = advance();
    std::vector<TypeExprPtr> parts;
    if (!check(TokKind::RParen)) {
      do {
        parts.push_back(parseType(true));
      } while (match(TokKind::Comma));
    }
    expect(TokKind::RParen, "')'");
    if (match(TokKind::Arrow)) {
      auto node = make(TypeExprKind::Function, open.span);
      node->args = std::move(parts);
      node->returnType = parseType(false);
      return node;
    }
    if (parts.size() == 1) {
      return std::move(parts.front());
    }
    auto node = make(TypeExprKind::Function, open.span);
    node->args = std::move(parts);
    node->returnType = make(TypeExprKind::Named, open.span);
    node->returnType->name = "void";
    return node;
  }

  error(current(), "expected a type");
  auto node = make(TypeExprKind::Named, current().span);
  node->name = "<error>";
  return node;
}

TypeExprPtr Parser::parsePostfixType() {
  TypeExprPtr type = parsePrimaryType();
  while (true) {
    if (match(TokKind::LBracket)) {
      auto array = std::make_unique<TypeExpr>();
      array->kind = TypeExprKind::Array;
      array->span = type->span;
      array->inner = std::move(type);
      array->arraySize = parseExpression();
      expect(TokKind::RBracket, "']'");
      type = std::move(array);
      continue;
    }
    if (match(TokKind::Star)) {
      auto pointer = std::make_unique<TypeExpr>();
      pointer->kind = TypeExprKind::Pointer;
      pointer->span = type->span;
      pointer->inner = std::move(type);
      type = std::move(pointer);
      continue;
    }
    break;
  }
  return type;
}

TypeExprPtr Parser::parseIntersectType(bool allowRef) {
  TypeExprPtr first = parsePostfixType();
  if (allowRef || !check(TokKind::Amp)) {
    return first;
  }
  auto node = std::make_unique<TypeExpr>();
  node->kind = TypeExprKind::Intersect;
  node->span = first->span;
  node->args.push_back(std::move(first));
  while (match(TokKind::Amp)) {
    node->args.push_back(parsePostfixType());
  }
  return node;
}

TypeExprPtr Parser::parseUnionType(bool allowRef) {
  TypeExprPtr first = parseIntersectType(allowRef);
  if (!check(TokKind::Pipe)) {
    return first;
  }
  auto node = std::make_unique<TypeExpr>();
  node->kind = TypeExprKind::Union;
  node->span = first->span;
  node->args.push_back(std::move(first));
  while (match(TokKind::Pipe)) {
    node->args.push_back(parseIntersectType(allowRef));
  }
  return node;
}

TypeExprPtr Parser::parseType(bool allowRef) {
  if (match(TokKind::KwConst)) {
    TypeExprPtr base = parsePrimaryType();
    if (allowRef && match(TokKind::Amp)) {
      auto ref = std::make_unique<TypeExpr>();
      ref->kind = TypeExprKind::Ref;
      ref->constRef = true;
      ref->inner = std::move(base);
      return ref;
    }
    TypeExprPtr type = std::make_unique<TypeExpr>();
    type->kind = TypeExprKind::Const;
    type->inner = std::move(base);
    while (true) {
      if (match(TokKind::Star)) {
        auto pointer = std::make_unique<TypeExpr>();
        pointer->kind = TypeExprKind::Pointer;
        pointer->span = type->span;
        pointer->inner = std::move(type);
        type = std::move(pointer);
        continue;
      }
      if (match(TokKind::LBracket)) {
        auto array = std::make_unique<TypeExpr>();
        array->kind = TypeExprKind::Array;
        array->span = type->span;
        array->inner = std::move(type);
        array->arraySize = parseExpression();
        expect(TokKind::RBracket, "']'");
        type = std::move(array);
        continue;
      }
      break;
    }
    return type;
  }

  TypeExprPtr type = parseUnionType(allowRef);
  if (allowRef && match(TokKind::Amp)) {
    auto ref = std::make_unique<TypeExpr>();
    ref->kind = TypeExprKind::Ref;
    ref->inner = std::move(type);
    return ref;
  }
  return type;
}

DeclPtr Parser::parseTopLevelDecl(std::vector<AnnotationUse> annotations,
                                  bool exported) {
  DeclPtr decl;
  const bool isFinal = match(TokKind::KwFinal);
  if (check(TokKind::KwClass)) {
    decl = parseClass(std::move(annotations), exported, false);
    if (decl != nullptr) {
      decl->isFinal = isFinal || decl->isFinal;
    }
  } else if (check(TokKind::KwEnumerate)) {
    if (isFinal) {
      error(current(), "'final' can only modify a class");
    }
    decl = parseEnum(std::move(annotations), exported);
  } else if (check(TokKind::KwInterface)) {
    decl = parseClass(std::move(annotations), exported, true);
  } else if (check(TokKind::KwAnnotation)) {
    decl = parseAnnotation(std::move(annotations), exported);
  } else if (check(TokKind::KwConst)) {
    decl = parseConst(std::move(annotations), exported);
  } else {
    decl = parseFunction(std::move(annotations), exported);
  }
  return decl;
}

DeclPtr Parser::parseClass(std::vector<AnnotationUse> annotations, bool exported,
                           bool isInterface) {
  auto decl = std::make_unique<Decl>();
  decl->kind = isInterface ? DeclKind::Interface : DeclKind::Class;
  decl->exported = exported;
  decl->annotations = std::move(annotations);
  decl->span = current().span;
  advance();  // class / interface

  if (!check(TokKind::Identifier)) {
    error(current(), "expected class name");
    return nullptr;
  }
  decl->name = std::string(advance().text);
  if (check(TokKind::Less)) {
    decl->generics = parseGenericParams();
  }
  if (match(TokKind::KwFinal)) {
    decl->isFinal = true;
  }

  if (match(TokKind::KwExtends)) {
    decl->base = parseType(true);
  }
  if (match(TokKind::KwImplements)) {
    do {
      decl->interfaces.push_back(parseType(true));
    } while (match(TokKind::Comma));
  }

  if (!expect(TokKind::LBrace, "'{'")) {
    return nullptr;
  }
  Access currentAccess =
      isInterface ? Access::Public : Access::Private;
  while (!atEnd() && !check(TokKind::RBrace)) {
    if ((check(TokKind::KwPublic) || check(TokKind::KwPrivate) ||
         check(TokKind::KwProtected)) &&
        peek(1).kind == TokKind::Colon) {
      currentAccess = accessFromToken(advance().kind);
      advance();  // ':'
      continue;
    }
    DeclPtr member = parseClassMember(decl->name);
    if (member != nullptr) {
      takeDocInto(*member);
      member->access = currentAccess;
      decl->members.push_back(std::move(member));
    } else {
      const std::size_t before = pos_;
      synchronizeTopLevel();
      if (pos_ == before && !atEnd() && !check(TokKind::RBrace)) {
        advance();
      }
    }
  }
  expect(TokKind::RBrace, "'}'");
  match(TokKind::Semicolon);
  return decl;
}

DeclPtr Parser::parseEnum(std::vector<AnnotationUse> annotations,
                          bool exported) {
  auto decl = std::make_unique<Decl>();
  decl->kind = DeclKind::Enum;
  decl->exported = exported;
  decl->annotations = std::move(annotations);
  decl->span = current().span;
  advance();  // enumerate

  if (!check(TokKind::Identifier)) {
    error(current(), "expected enum name");
    return nullptr;
  }
  decl->name = std::string(advance().text);

  if (match(TokKind::KwImplements)) {
    do {
      decl->interfaces.push_back(parseType(true));
    } while (match(TokKind::Comma));
  }

  if (!expect(TokKind::LBrace, "'{'")) {
    return nullptr;
  }

  while (!atEnd() && !check(TokKind::RBrace)) {
    EnumVariant variant;
    variant.span = current().span;
    variant.annotations = parseAnnotations();
    if (!check(TokKind::Identifier)) {
      error(current(), "expected enum variant name");
      break;
    }
    variant.name = std::string(advance().text);
    if (match(TokKind::Equal)) {
      variant.value = parseExpression();
    }
    decl->variants.push_back(std::move(variant));
    if (match(TokKind::Comma)) {
      continue;
    }
    break;
  }

  if (match(TokKind::Semicolon)) {
    while (!atEnd() && !check(TokKind::RBrace)) {
      DeclPtr member = parseClassMember(decl->name);
      if (member != nullptr) {
        takeDocInto(*member);
        member->access = Access::Public;
        decl->members.push_back(std::move(member));
      } else {
        synchronizeTopLevel();
      }
    }
  }

  expect(TokKind::RBrace, "'}'");
  match(TokKind::Semicolon);
  return decl;
}

DeclPtr Parser::parseClassMember(const std::string& className) {
  std::vector<AnnotationUse> annotations = parseAnnotations();
  bool isVirtual = false;
  bool isOverride = false;
  bool isFinal = false;
  bool isStatic = false;
  for (;;) {
    if (match(TokKind::KwVirtual)) {
      isVirtual = true;
    } else if (match(TokKind::KwOverride)) {
      isOverride = true;
    } else if (match(TokKind::KwFinal)) {
      isFinal = true;
    } else if (match(TokKind::KwStatic)) {
      isStatic = true;
    } else {
      break;
    }
  }

  if (check(TokKind::Tilde)) {
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Destructor;
    decl->span = current().span;
    decl->annotations = std::move(annotations);
    advance();
    if (!check(TokKind::Identifier)) {
      error(current(), "expected destructor name");
      return nullptr;
    }
    decl->name = std::string(advance().text);
    expect(TokKind::LParen, "'('");
    expect(TokKind::RParen, "')'");
    if (check(TokKind::LBrace)) {
      decl->body = parseBlock();
    } else {
      expect(TokKind::Semicolon, "';'");
    }
    return decl;
  }

  if (check(TokKind::Identifier) && std::string(peek().text) == className &&
      peek(1).kind == TokKind::LParen) {
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Constructor;
    decl->span = current().span;
    decl->annotations = std::move(annotations);
    decl->name = std::string(advance().text);
    decl->params = parseParams();
    if (match(TokKind::Colon)) {
      decl->hasInitializerList = true;
      do {
        Initializer initializer;
        initializer.span = current().span;
        if (match(TokKind::KwSuper)) {
          initializer.kind = Initializer::Kind::Super;
        } else if (check(TokKind::Identifier)) {
          initializer.kind = Initializer::Kind::Field;
          initializer.name = std::string(advance().text);
        } else {
          error(current(), "expected 'super' or field name in initializer list");
          break;
        }
        initializer.args = parseArguments();
        decl->initializers.push_back(std::move(initializer));
      } while (match(TokKind::Comma));
    }
    if (check(TokKind::LBrace)) {
      decl->body = parseBlock();
    } else {
      expect(TokKind::Semicolon, "';'");
    }
    return decl;
  }

  TypeExprPtr type = parseType(true);
  bindNumericAnnotations(annotations, type.get());
  if (!check(TokKind::Identifier)) {
    error(current(), "expected member name");
    if (!atEnd() && !check(TokKind::RBrace)) {
      advance();
    }
    return nullptr;
  }
  std::string name(advance().text);

  if (check(TokKind::Less)) {
    // Generic method.
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Method;
    decl->span = type->span;
    decl->annotations = std::move(annotations);
    decl->isVirtual = isVirtual;
    decl->isOverride = isOverride;
    decl->isFinal = isFinal;
    decl->isStatic = isStatic;
    decl->type = std::move(type);
    decl->name = std::move(name);
    decl->generics = parseGenericParams();
    decl->params = parseParams();
    while (match(TokKind::KwConst)) {
      decl->isConstMethod = true;
    }
    if (check(TokKind::LBrace)) {
      decl->body = parseBlock();
    } else {
      expect(TokKind::Semicolon, "';'");
    }
    return decl;
  }

  if (check(TokKind::LParen)) {
    auto decl = std::make_unique<Decl>();
    decl->kind = DeclKind::Method;
    decl->span = type->span;
    decl->annotations = std::move(annotations);
    decl->isVirtual = isVirtual;
    decl->isOverride = isOverride;
    decl->isFinal = isFinal;
    decl->isStatic = isStatic;
    decl->type = std::move(type);
    decl->name = std::move(name);
    decl->params = parseParams();
    while (match(TokKind::KwConst)) {
      decl->isConstMethod = true;
    }
    if (check(TokKind::LBrace)) {
      decl->body = parseBlock();
    } else {
      expect(TokKind::Semicolon, "';'");
    }
    return decl;
  }

  auto decl = std::make_unique<Decl>();
  decl->kind = DeclKind::Field;
  decl->span = type->span;
  bool hasRadix = false;
  const int declaredRadix = annotationRadix(annotations, hasRadix);
  decl->radix = hasRadix ? declaredRadix : 0;
  decl->annotations = std::move(annotations);
  decl->type = std::move(type);
  decl->name = std::move(name);
  decl->isFinal = isFinal;
  if (match(TokKind::Equal)) {
    decl->init = parseExpression();
  }
  expect(TokKind::Semicolon, "';'");
  return decl;
}

DeclPtr Parser::parseAnnotation(std::vector<AnnotationUse> annotations,
                                bool exported) {
  auto decl = std::make_unique<Decl>();
  decl->kind = DeclKind::Annotation;
  decl->exported = exported;
  decl->annotations = std::move(annotations);
  decl->span = current().span;
  advance();  // annotation

  if (!check(TokKind::Identifier)) {
    error(current(), "expected annotation name");
    return nullptr;
  }
  decl->name = std::string(advance().text);

  if (check(TokKind::LParen)) {
    decl->params = parseParams();
  } else if (check(TokKind::Less)) {
    decl->generics = parseGenericParams();
    if (check(TokKind::LParen)) {
      decl->params = parseParams();
    }
  }

  if (match(TokKind::KwImplements)) {
    do {
      decl->interfaces.push_back(parseType(true));
    } while (match(TokKind::Comma));
  }

  if (match(TokKind::LBrace)) {
    while (!atEnd() && !check(TokKind::RBrace)) {
      DeclPtr member = parseClassMember(decl->name);
      if (member != nullptr) {
        takeDocInto(*member);
        decl->members.push_back(std::move(member));
      } else {
        synchronizeTopLevel();
      }
    }
    expect(TokKind::RBrace, "'}'");
    match(TokKind::Semicolon);
  } else {
    expect(TokKind::Semicolon, "';'");
  }
  return decl;
}

DeclPtr Parser::parseFunction(std::vector<AnnotationUse> annotations,
                              bool exported) {
  auto decl = std::make_unique<Decl>();
  decl->kind = DeclKind::Function;
  decl->exported = exported;
  decl->annotations = std::move(annotations);
  decl->span = current().span;

  decl->type = parseType(true);
  bindNumericAnnotations(decl->annotations, decl->type.get());

  if (match(TokKind::KwOperator)) {
    if (check(TokKind::Identifier)) {
      // Named operator, e.g. operator add.
      decl->name = "operator" + std::string(advance().text);
    } else {
      const Token& token = advance();
      const std::string text = operatorText(token.kind);
      if (text.empty()) {
        error(token, "expected an overloadable operator symbol");
      }
      decl->name = "operator" + text;
    }
  } else if (check(TokKind::Identifier)) {
    decl->name = std::string(advance().text);
  } else {
    error(current(), "expected function name");
    return nullptr;
  }

  if (check(TokKind::Less)) {
    decl->generics = parseGenericParams();
  }
  decl->params = parseParams();

  if (check(TokKind::KwWhere)) {
    // Constraints are parsed but not yet enforced by the bootstrap checker.
    advance();
    do {
      parseType(false);
    } while (match(TokKind::Comma));
  }

  if (check(TokKind::LBrace)) {
    decl->body = parseBlock();
  } else {
    expect(TokKind::Semicolon, "';'");
  }
  return decl;
}

DeclPtr Parser::parseConst(std::vector<AnnotationUse> annotations,
                           bool exported) {
  auto decl = std::make_unique<Decl>();
  decl->kind = DeclKind::Const;
  decl->exported = exported;
  bool hasRadix = false;
  decl->radix = annotationRadix(annotations, hasRadix);
  decl->annotations = std::move(annotations);
  decl->span = current().span;
  advance();  // const

  // `const X = ...` has an inferred type; `const int X = ...` does not.
  if (check(TokKind::Identifier) && peek(1).kind == TokKind::Equal) {
    decl->name = std::string(advance().text);
  } else {
    decl->type = parseType(true);
    bindNumericAnnotations(decl->annotations, decl->type.get());
    if (!check(TokKind::Identifier)) {
      error(current(), "expected constant name");
      return nullptr;
    }
    decl->name = std::string(advance().text);
  }

  expect(TokKind::Equal, "'='");
  decl->init = parseExpression();
  expect(TokKind::Semicolon, "';'");
  return decl;
}

StmtPtr Parser::parseBlock() {
  auto block = std::make_unique<Stmt>();
  block->kind = StmtKind::Block;
  block->span = current().span;
  if (!expect(TokKind::LBrace, "'{'")) {
    return block;
  }
  while (!atEnd() && !check(TokKind::RBrace)) {
    StmtPtr stmt = parseStatement();
    if (stmt != nullptr) {
      block->statements.push_back(std::move(stmt));
    } else {
      synchronizeStatement();
    }
  }
  expect(TokKind::RBrace, "'}'");
  return block;
}

bool Parser::looksLikeVarDecl() const {
  if (check(TokKind::At) || check(TokKind::KwConst)) {
    return true;
  }
  if (check(TokKind::LParen)) {
    int depth = 0;
    for (std::size_t i = 0;; ++i) {
      const TokKind kind = peek(i).kind;
      if (kind == TokKind::End) {
        return false;
      }
      if (kind == TokKind::LParen) {
        ++depth;
      } else if (kind == TokKind::RParen) {
        --depth;
        if (depth == 0) {
          return peek(i + 1).kind == TokKind::Arrow;
        }
      }
    }
  }
  if (isTypeKeyword(current().kind) || check(TokKind::Star)) {
    return true;
  }
  if (!check(TokKind::Identifier)) {
    return false;
  }
  if (peek(1).kind == TokKind::Identifier) {
    return true;
  }
  if (peek(1).kind == TokKind::Less) {
    // Scan forward to the matching '>' and require an identifier after it.
    int depth = 0;
    for (std::size_t i = 1; i < 64; ++i) {
      const TokKind kind = peek(i).kind;
      if (kind == TokKind::End) {
        return false;
      }
      if (kind == TokKind::Less) {
        ++depth;
      } else if (kind == TokKind::Greater) {
        --depth;
        if (depth == 0) {
          return peek(i + 1).kind == TokKind::Identifier;
        }
      }
    }
  }
  return false;
}

bool Parser::looksLikeConstructureArgs() const {
  if (!check(TokKind::LBrace)) {
    return false;
  }
  const Token& first = peek(1);
  if (isTypeKeyword(first.kind)) {
    return true;
  }
  if (first.kind == TokKind::IntegerLiteral ||
      first.kind == TokKind::StringLiteral ||
      first.kind == TokKind::KwTrue || first.kind == TokKind::KwFalse ||
      first.kind == TokKind::KwNull) {
    return true;
  }
  if (first.kind != TokKind::Identifier) {
    return false;
  }
  const Token& second = peek(2);
  // `{x: 1}` is an object literal; untyped `{x, y}`, `{x = 1}` and typed
  // `{Point p}` entries are @Constructure arguments.
  return second.kind != TokKind::Colon;
}

bool Parser::parseConstructureInitializer(Stmt& stmt) {
  if (!expect(TokKind::LBrace, "'{'")) {
    return false;
  }
  stmt.constructureInit = true;
  if (!check(TokKind::RBrace)) {
    do {
      ConstructureArgument argument;
      argument.span = current().span;
      const bool typed =
          isTypeKeyword(current().kind) ||
          (check(TokKind::Identifier) &&
           (peek(1).kind == TokKind::Identifier ||
            peek(1).kind == TokKind::Star ||
            peek(1).kind == TokKind::Less));
      if (typed) {
        argument.type = parseType(true);
      }
      if (check(TokKind::Identifier) &&
          (typed || peek(1).kind == TokKind::Equal)) {
        argument.name = std::string(advance().text);
      }
      if (match(TokKind::Equal)) {
        argument.value = parseExpression();
      } else if (argument.type != nullptr && !argument.name.empty()) {
        auto value = std::make_unique<Expr>();
        value->kind = ExprKind::Name;
        value->name = argument.name;
        value->span = argument.span;
        argument.value = std::move(value);
      } else {
        argument.value = parseExpression();
      }
      stmt.constructureArgs.push_back(std::move(argument));
    } while (match(TokKind::Comma));
  }
  return expect(TokKind::RBrace, "'}'");
}

StmtPtr Parser::parseVarDecl() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Var;
  stmt->span = current().span;

  std::vector<AnnotationUse> annotations = parseAnnotations();
  bool hasRadix = false;
  stmt->radix = annotationRadix(annotations, hasRadix);
  for (AnnotationUse& use : annotations) {
    if (use.name == "Optional") {
      stmt->isOptional = true;
      if (!use.args.empty()) {
        Expr* argument = use.args[0].get();
        if (argument != nullptr && argument->kind == ExprKind::Name) {
          const std::string policy = lowerAscii(argument->name);
          if (policy == "error") {
            stmt->proxyPolicy = ProxyPolicy::Error;
          } else if (policy == "skip") {
            stmt->proxyPolicy = ProxyPolicy::Skip;
          } else if (policy == "abort") {
            stmt->proxyPolicy = ProxyPolicy::Abort;
          } else {
            errorAt(argument->span,
                  "unknown @Optional policy '" + argument->name +
                      "' (expected Error, Skip, Abort or Default(expr))");
          }
        } else if (argument != nullptr && argument->kind == ExprKind::Call &&
                   argument->lhs != nullptr &&
                   argument->lhs->kind == ExprKind::Name &&
                   lowerAscii(argument->lhs->name) == "default" &&
                   argument->args.size() == 1) {
          stmt->proxyPolicy = ProxyPolicy::Default;
          stmt->optionalDefault = std::move(argument->args[0]);
        } else {
          errorAt(use.span,
                "@Optional expects Error, Skip, Abort or Default(expr)");
        }
        if (use.args.size() > 1) {
          errorAt(use.span, "@Optional accepts at most one policy argument");
        }
      }
    } else if (use.name == "Unique") {
      stmt->isUnique = true;
    } else if (use.name == "Share") {
      stmt->isShare = true;
    } else if (use.name == "Weak") {
      stmt->isWeak = true;
    } else if (use.name == "Unsigned") {
      stmt->isUnsigned = true;
    } else if (use.name == "Signed") {
      stmt->isSigned = true;
    } else if (use.name == "Length" && !use.args.empty() &&
               use.args[0] != nullptr &&
               use.args[0]->kind == ExprKind::IntLit) {
      stmt->length = static_cast<int>(use.args[0]->intValue);
    }
  }

  if (check(TokKind::KwConst) && peek(1).kind == TokKind::Identifier &&
      peek(2).kind == TokKind::Equal) {
    advance();
    stmt->isConst = true;
    stmt->name = std::string(advance().text);
  } else {
    stmt->type = parseType(true);
    bindNumericAnnotations(annotations, stmt->type.get());
    if (stmt->type != nullptr) {
      if (stmt->type->hasNumericLength && stmt->type->numericLength > 0) {
        stmt->length = stmt->type->numericLength;
      }
      if (stmt->type->hasNumericRadix && stmt->type->numericRadix > 0) {
        stmt->radix = stmt->type->numericRadix;
      }
      if (stmt->type->hasNumericPower) {
        stmt->hasPower = true;
        stmt->power = stmt->type->numericPower;
      }
    }
    if (!check(TokKind::Identifier)) {
      error(current(), "expected variable name");
      return nullptr;
    }
    stmt->name = std::string(advance().text);
  }

  if (match(TokKind::Equal)) {
    if (looksLikeConstructureArgs()) {
      if (!parseConstructureInitializer(*stmt)) {
        return nullptr;
      }
    } else {
      stmt->expr = parseExpression();
    }
  } else if (check(TokKind::LBrace)) {
    stmt->braceInit = true;
    advance();  // {
    if (!check(TokKind::RBrace)) {
      do {
        stmt->initArgs.push_back(parseExpression());
      } while (match(TokKind::Comma));
    }
    expect(TokKind::RBrace, "'}'");
  } else if (check(TokKind::LParen)) {
    stmt->directInit = true;
    auto call = std::make_unique<Expr>();
    call->kind = ExprKind::Call;
    call->span = stmt->span;
    auto callee = std::make_unique<Expr>();
    callee->kind = ExprKind::Name;
    callee->name = describeType(stmt->type.get());
    callee->span = stmt->type ? stmt->type->span : stmt->span;
    call->lhs = std::move(callee);
    call->args = parseArguments();
    stmt->expr = std::move(call);
  }

  expect(TokKind::Semicolon, "';'");
  return stmt;
}

StmtPtr Parser::parseIf() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::If;
  stmt->span = current().span;
  advance();  // if
  if (match(TokKind::KwLet)) {
    error(current(), "if let has been removed; use isPresent()/isOk() and get()/unwrap()");
    if (check(TokKind::Identifier)) {
      advance();
    }
    expect(TokKind::Equal, "'='");
    parseExpression();
    auto truth = std::make_unique<Expr>();
    truth->kind = ExprKind::BoolLit;
    truth->boolValue = true;
    truth->span = stmt->span;
    stmt->condition = std::move(truth);
  } else {
    expect(TokKind::LParen, "'('");
    stmt->condition = parseExpression();
    expect(TokKind::RParen, "')'");
  }
  stmt->thenBranch = parseStatement();
  if (match(TokKind::KwElse)) {
    stmt->elseBranch = parseStatement();
  }
  return stmt;
}

StmtPtr Parser::parseFor() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::ForIn;
  stmt->span = current().span;
  advance();  // for
  if (!check(TokKind::Identifier)) {
    error(current(), "expected an iteration variable after 'for'");
    return stmt;
  }
  stmt->name = std::string(advance().text);
  expect(TokKind::KwIn, "'in'");
  stmt->expr = parseExpression();
  stmt->thenBranch = parseStatement();
  return stmt;
}

StmtPtr Parser::parseWhile() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::While;
  stmt->span = current().span;
  advance();  // while
  expect(TokKind::LParen, "'('");
  stmt->condition = parseExpression();
  expect(TokKind::RParen, "')'");
  stmt->thenBranch = parseStatement();
  return stmt;
}

StmtPtr Parser::parseSwitch(bool isMatch) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Switch;
  stmt->isMatch = isMatch;
  stmt->span = current().span;
  advance();  // switch/match
  expect(TokKind::LParen, "'('");
  stmt->condition = parseExpression();
  expect(TokKind::RParen, "')'");
  if (!expect(TokKind::LBrace, "'{'")) {
    return stmt;
  }
  bool sawDefault = false;
  while (!atEnd() && !check(TokKind::RBrace)) {
    bool isDefault = false;
    if (check(TokKind::Identifier) && current().text == "case") {
      advance();
    } else if (check(TokKind::Identifier) && current().text == "default") {
      if (isMatch) {
        error(current(), "match does not allow a default branch");
      }
      advance();
      isDefault = true;
      if (sawDefault) {
        error(current(), "duplicate default branch");
      }
      sawDefault = true;
    } else {
      error(current(), "expected 'case' or 'default' in switch");
      break;
    }
    stmt->switchValues.push_back(isDefault ? nullptr : parseExpression());
    expect(TokKind::Colon, "':'");
    if (isDefault) {
      if (!check(TokKind::RBrace)) {
        // default must be last; still parse its body for recovery
      }
    }
    StmtPtr body = parseStatement();
    if (body == nullptr) {
      error(current(), "expected switch branch body");
      break;
    }
    stmt->switchBodies.push_back(std::move(body));
    if (isDefault) {
      if (!check(TokKind::RBrace)) {
        error(current(), "default branch must be last in switch");
        break;
      }
    }
  }
  expect(TokKind::RBrace, "'}'");
  return stmt;
}

StmtPtr Parser::parseMatchBody() {
  if (check(TokKind::LBrace)) {
    return parseBlock();
  }
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Expr;
  stmt->span = current().span;
  stmt->expr = parseExpression();
  return stmt;
}

PatternPtr Parser::parsePatternPrimary() {
  auto pattern = std::make_unique<Pattern>();
  pattern->span = current().span;
  if (check(TokKind::Identifier) && current().text == "_") {
    advance();
    pattern->kind = PatternKind::Wildcard;
    return pattern;
  }
  const auto literal = [&]() -> ExprPtr {
    auto expr = std::make_unique<Expr>();
    expr->span = current().span;
    if (match(TokKind::Minus)) {
      if (!check(TokKind::IntegerLiteral)) {
        error(current(), "expected an integer after '-' in a range");
        return expr;
      }
      expr->kind = ExprKind::IntLit;
      expr->intValue = -advance().intValue;
      return expr;
    }
    if (check(TokKind::IntegerLiteral)) {
      expr->kind = ExprKind::IntLit;
      expr->intValue = advance().intValue;
    } else if (check(TokKind::CharacterLiteral)) {
      expr->kind = ExprKind::CharLit;
      expr->charValue = advance().charValue;
    } else if (check(TokKind::StringLiteral)) {
      expr->kind = ExprKind::StringLit;
      expr->stringValue = advance().stringValue;
    } else if (match(TokKind::KwTrue)) {
      expr->kind = ExprKind::BoolLit;
      expr->boolValue = true;
    } else if (match(TokKind::KwFalse)) {
      expr->kind = ExprKind::BoolLit;
      expr->boolValue = false;
    }
    return expr;
  };
  if (check(TokKind::IntegerLiteral) || check(TokKind::CharacterLiteral) ||
      check(TokKind::StringLiteral) || check(TokKind::KwTrue) ||
      check(TokKind::KwFalse) ||
      (check(TokKind::Minus) &&
       peek(1).kind == TokKind::IntegerLiteral)) {
    pattern->kind = PatternKind::Literal;
    pattern->value = literal();
  } else if (check(TokKind::Identifier)) {
    std::string name = std::string(advance().text);
    while (match(TokKind::Dot)) {
      if (!check(TokKind::Identifier)) {
        error(current(), "expected a name after '.' in a match pattern");
        break;
      }
      name += ".";
      name += std::string(advance().text);
    }
    pattern->kind = PatternKind::Path;
    pattern->name = std::move(name);
  } else {
    error(current(), "expected a match pattern");
    advance();
    pattern->kind = PatternKind::Wildcard;
    return pattern;
  }
  if (pattern->kind == PatternKind::Literal &&
      (check(TokKind::DotDot) || check(TokKind::DotDotEqual))) {
    auto range = std::make_unique<Pattern>();
    range->kind = PatternKind::Range;
    range->span = pattern->span;
    range->inclusive = check(TokKind::DotDotEqual);
    advance();
    PatternPtr upper = parsePatternPrimary();
    if (upper->kind != PatternKind::Literal) {
      error(current(), "range patterns require literal bounds");
    }
    range->left = std::move(pattern);
    range->right = std::move(upper);
    return range;
  }
  return pattern;
}

PatternPtr Parser::parsePattern() {
  PatternPtr pattern = parsePatternPrimary();
  while (check(TokKind::Pipe)) {
    advance();
    PatternPtr right = parsePatternPrimary();
    auto combined = std::make_unique<Pattern>();
    combined->kind = PatternKind::Or;
    combined->span = pattern->span;
    combined->left = std::move(pattern);
    combined->right = std::move(right);
    pattern = std::move(combined);
  }
  if (check(TokKind::KwIf)) {
    advance();
    auto guarded = std::make_unique<Pattern>();
    guarded->kind = PatternKind::Guard;
    guarded->span = pattern->span;
    guarded->left = std::move(pattern);
    guarded->guard = parseExpression();
    pattern = std::move(guarded);
  }
  return pattern;
}

StmtPtr Parser::parseMatch() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Switch;
  stmt->isMatch = true;
  stmt->span = current().span;
  advance();  // match
  expect(TokKind::LParen, "'('");
  stmt->condition = parseExpression();
  expect(TokKind::RParen, "')'");
  if (!expect(TokKind::LBrace, "'{'")) {
    return stmt;
  }
  while (!atEnd() && !check(TokKind::RBrace)) {
    PatternPtr pattern;
    if (check(TokKind::Identifier) && current().text == "case") {
      advance();
      pattern = parsePattern();
      expect(TokKind::Colon, "':' after a match pattern");
    } else if (check(TokKind::Identifier) && current().text == "default") {
      advance();
      pattern = std::make_unique<Pattern>();
      pattern->kind = PatternKind::Wildcard;
      pattern->span = current().span;
      expect(TokKind::Colon, "':' after 'default'");
    } else {
      pattern = parsePattern();
      expect(TokKind::FatArrow, "'=>' after a match pattern");
    }
    StmtPtr body = parseMatchBody();
    if (body == nullptr) {
      error(current(), "expected a match branch body");
      break;
    }
    stmt->matchPatterns.push_back(std::move(pattern));
    stmt->switchBodies.push_back(std::move(body));
    match(TokKind::Comma);
  }
  expect(TokKind::RBrace, "'}'");
  return stmt;
}

bool Parser::looksLikeDestructure() const {
  if (!check(TokKind::LBrace)) {
    return false;
  }
  int depth = 0;
  for (std::size_t i = 0; i < 128; ++i) {
    const TokKind kind = peek(i).kind;
    if (kind == TokKind::End) {
      return false;
    }
    if (kind == TokKind::LBrace) {
      ++depth;
    } else if (kind == TokKind::RBrace) {
      --depth;
      if (depth == 0) {
        return peek(i + 1).kind == TokKind::Equal;
      }
    } else if (kind == TokKind::Semicolon) {
      return false;
    }
  }
  return false;
}

StmtPtr Parser::parseDestructure() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Destructure;
  stmt->span = current().span;
  expect(TokKind::LBrace, "'{'");
  while (!atEnd() && !check(TokKind::RBrace)) {
    DestructureBinding binding;
    binding.span = current().span;
    binding.type = parseType(true);
    if (!check(TokKind::Identifier)) {
      error(current(), "expected destructured field name");
      break;
    }
    binding.name = std::string(advance().text);
    stmt->destructureBindings.push_back(std::move(binding));
    if (!match(TokKind::Comma)) {
      break;
    }
  }
  expect(TokKind::RBrace, "'}'");
  expect(TokKind::Equal, "'='");
  stmt->expr = parseExpression();
  expect(TokKind::Semicolon, "';'");
  return stmt;
}

StmtPtr Parser::parseReturn() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Return;
  stmt->span = current().span;
  advance();  // return
  if (!check(TokKind::Semicolon)) {
    stmt->expr = parseExpression();
  }
  expect(TokKind::Semicolon, "';'");
  return stmt;
}

StmtPtr Parser::parseDestroy() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Destroy;
  stmt->span = current().span;
  advance();  // ~
  stmt->expr = parsePostfix();
  expect(TokKind::Semicolon, "';'");
  return stmt;
}

StmtPtr Parser::parseExprStmt() {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Expr;
  stmt->span = current().span;
  stmt->expr = parseExpression();
  expect(TokKind::Semicolon, "';'");
  return stmt;
}

StmtPtr Parser::parseLockBlock() {
  auto spec = std::make_unique<Stmt>();
  spec->kind = StmtKind::Block;
  spec->span = current().span;
  spec->hasLock = true;
  advance();  // @
  advance();  // Lock
  expect(TokKind::LParen, "'(' after @Lock");
  const bool hasMode = check(TokKind::Identifier) &&
                       current().text == "Exclusive" &&
                       peek(1).kind == TokKind::Comma;
  if (hasMode) {
    advance();  // Exclusive
    spec->lockMode = Stmt::LockMode::Exclusive;
    expect(TokKind::Comma, "',' after the lock mode");
    spec->lockMutex = parseExpression();
    if (match(TokKind::Comma)) {
      expect(TokKind::LBracket, "'[' before @Lock captures");
      while (!atEnd() && !check(TokKind::RBracket)) {
        Stmt::LockCaptureSpec capture;
        capture.span = current().span;
        bool isConst = false;
        if (match(TokKind::KwConst)) {
          isConst = true;
        }
        const bool isRef = match(TokKind::Amp);
        if (isConst && !isRef) {
          error(current(), "'const' requires '&' in a @Lock capture");
        }
        if (!check(TokKind::Identifier)) {
          error(current(), "expected a capture name in @Lock");
          break;
        }
        capture.name = std::string(advance().text);
        capture.kind = isConst
                           ? Stmt::LockCaptureKind::ConstRef
                           : (isRef ? Stmt::LockCaptureKind::MutRef
                                    : Stmt::LockCaptureKind::Value);
        spec->lockCaptures.push_back(std::move(capture));
        if (!match(TokKind::Comma)) {
          break;
        }
      }
      expect(TokKind::RBracket, "']' after @Lock captures");
    }
  } else {
    // Legacy `@Lock(mutex)` form; Sema reports the E2094 migration error.
    spec->lockMutex = parseExpression();
  }
  expect(TokKind::RParen, "')' after @Lock arguments");
  if (!check(TokKind::LBrace)) {
    error(current(), "@Lock must be followed by a block");
    return spec;
  }
  StmtPtr block = parseBlock();
  block->hasLock = true;
  block->lockMode = spec->lockMode;
  block->lockMutex = std::move(spec->lockMutex);
  block->lockCaptures = std::move(spec->lockCaptures);
  block->span = spec->span;
  return block;
}

StmtPtr Parser::parseStatement() {
  if (check(TokKind::At) && peek(1).kind == TokKind::Identifier &&
      (peek(1).text == "Unsafe" || peek(1).text == "Safe") &&
      peek(2).kind == TokKind::LBrace) {
    const bool unsafeScope = peek(1).text == "Unsafe";
    advance();
    advance();
    StmtPtr block = parseBlock();
    if (block != nullptr) {
      block->unsafeBlock = unsafeScope;
      block->safeBlock = !unsafeScope;
    }
    return block;
  }
  if (check(TokKind::At) && peek(1).kind == TokKind::Identifier &&
      peek(1).text == "Retention") {
    std::vector<AnnotationUse> annotations = parseAnnotations();
    if (check(TokKind::LBrace)) {
      StmtPtr block = parseBlock();
      block->annotations = std::move(annotations);
      return block;
    }
    error(current(), "@Retention must be followed by a block");
    return nullptr;
  }
  if (check(TokKind::At) && peek(1).kind == TokKind::Identifier &&
      peek(1).text == "Lock") {
    return parseLockBlock();
  }
  if (check(TokKind::LBrace)) {
    if (looksLikeDestructure()) {
      return parseDestructure();
    }
    return parseBlock();
  }
  if (check(TokKind::KwIf)) {
    return parseIf();
  }
  if (check(TokKind::KwFor)) {
    return parseFor();
  }
  if (check(TokKind::KwSwitch)) {
    return parseSwitch();
  }
  if (check(TokKind::KwMatch)) {
    return parseMatch();
  }
  if (check(TokKind::KwWhile)) {
    return parseWhile();
  }
  if (check(TokKind::KwReturn)) {
    return parseReturn();
  }
  if (match(TokKind::KwBreak)) {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Break;
    stmt->span = current().span;
    expect(TokKind::Semicolon, "';'");
    return stmt;
  }
  if (match(TokKind::KwContinue)) {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Continue;
    stmt->span = current().span;
    expect(TokKind::Semicolon, "';'");
    return stmt;
  }
  if (check(TokKind::Tilde)) {
    return parseDestroy();
  }
  if (looksLikeVarDecl()) {
    return parseVarDecl();
  }
  return parseExprStmt();
}

std::vector<ExprPtr> Parser::parseArguments() {
  std::vector<ExprPtr> args;
  if (!expect(TokKind::LParen, "'('")) {
    return args;
  }
  if (!check(TokKind::RParen)) {
    do {
      args.push_back(parseExpression());
    } while (match(TokKind::Comma));
  }
  expect(TokKind::RParen, "')'");
  return args;
}

int Parser::binaryPrecedence(TokKind kind) {
  switch (kind) {
    case TokKind::PipePipe:
      return 1;
    case TokKind::AmpAmp:
      return 2;
    case TokKind::Pipe:
      return 3;
    case TokKind::Caret:
      return 4;
    case TokKind::Amp:
      return 5;
    case TokKind::EqualEqual:
    case TokKind::BangEqual:
      return 6;
    case TokKind::Less:
    case TokKind::LessEqual:
    case TokKind::Greater:
    case TokKind::GreaterEqual:
      return 7;
    case TokKind::Plus:
    case TokKind::Minus:
      return 9;
    case TokKind::Star:
    case TokKind::Slash:
    case TokKind::Percent:
      return 10;
    default:
      return 0;
  }
}

bool Parser::isAssignmentOperator(TokKind kind) {
  switch (kind) {
    case TokKind::Equal:
    case TokKind::PlusEqual:
    case TokKind::MinusEqual:
    case TokKind::StarEqual:
    case TokKind::SlashEqual:
    case TokKind::PercentEqual:
    case TokKind::AmpEqual:
    case TokKind::PipeEqual:
    case TokKind::CaretEqual:
      return true;
    default:
      return false;
  }
}

ExprPtr Parser::parseExpression() {
  if (check(TokKind::KwIf)) {
    return parseIfExpression();
  }
  return parseAssignment();
}

ExprPtr Parser::parseIfExpression() {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::IfExpr;
  expr->span = current().span;
  expr->ifExpr = std::make_shared<IfExprInfo>();
  advance();  // if
  expect(TokKind::LParen, "'('");
  expr->ifExpr->condition = parseExpression();
  expect(TokKind::RParen, "')'");
  if (check(TokKind::LBrace)) {
    error(current(),
          "if-expression branches must be expressions in v0; use a normal "
          "'if' statement for blocks");
  }
  expr->ifExpr->thenValue = parseExpression();
  if (!expect(TokKind::KwElse, "'else' in an if expression")) {
    return expr;
  }
  if (check(TokKind::KwIf)) {
    expr->ifExpr->elseValue = parseIfExpression();
  } else {
    expr->ifExpr->elseValue = parseExpression();
  }
  return expr;
}

ExprPtr Parser::parseAssignment() {
  ExprPtr lhs = parseBinary(1);
  if (isAssignmentOperator(current().kind)) {
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Assign;
    expr->span = lhs->span;
    expr->op = operatorText(current().kind);
    advance();
    expr->lhs = std::move(lhs);
    expr->rhs = parseAssignment();
    return expr;
  }
  return lhs;
}

ExprPtr Parser::parseBinary(int minPrecedence) {
  ExprPtr lhs = parseUnary();
  while (true) {
    const bool shiftLeft =
        check(TokKind::Less) && peek(1).kind == TokKind::Less;
    const bool shiftRight =
        check(TokKind::Greater) && peek(1).kind == TokKind::Greater;
    const int precedence =
        (shiftLeft || shiftRight) ? 8 : binaryPrecedence(current().kind);
    if (precedence == 0 || precedence < minPrecedence) {
      break;
    }
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Binary;
    expr->op = shiftLeft ? "<<"
                         : (shiftRight ? ">>" : operatorText(current().kind));
    expr->span = lhs->span;
    advance();
    if (shiftLeft || shiftRight) {
      advance();
    }
    expr->lhs = std::move(lhs);
    expr->rhs = parseBinary(precedence + 1);
    lhs = std::move(expr);
  }
  return lhs;
}

ExprPtr Parser::parseUnary() {
  if (check(TokKind::PlusPlus) || check(TokKind::MinusMinus)) {
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Inc;
    expr->op = "pre";
    expr->intValue = check(TokKind::PlusPlus) ? 1 : -1;
    expr->span = current().span;
    advance();
    expr->lhs = parseUnary();
    return expr;
  }
  if (check(TokKind::Bang) || check(TokKind::Minus) || check(TokKind::Plus) ||
      check(TokKind::Tilde) || check(TokKind::Star) || check(TokKind::Amp)) {
    auto expr = std::make_unique<Expr>();
    expr->kind = ExprKind::Unary;
    expr->op = operatorText(current().kind);
    expr->span = current().span;
    advance();
    expr->lhs = parseUnary();
    return expr;
  }
  return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
  ExprPtr expr = parsePrimary();
  while (!atEnd()) {
    if (check(TokKind::LParen)) {
      auto call = std::make_unique<Expr>();
      call->kind = ExprKind::Call;
      call->span = expr->span;
      call->lhs = std::move(expr);
      call->args = parseArguments();
      expr = std::move(call);
      continue;
    }
    if (match(TokKind::Dot)) {
      auto member = std::make_unique<Expr>();
      member->kind = ExprKind::Member;
      member->span = expr->span;
      if (!check(TokKind::Identifier)) {
        error(current(), "expected member name after '.'");
        break;
      }
      member->name = std::string(advance().text);
      member->lhs = std::move(expr);
      expr = std::move(member);
      continue;
    }
    if (match(TokKind::ColonColon)) {
      auto ref = std::make_unique<Expr>();
      ref->kind = ExprKind::MethodRef;
      ref->span = expr->span;
      ref->lhs = std::move(expr);
      if (!check(TokKind::Identifier)) {
        error(current(), "expected a method name after '::'");
        break;
      }
      ref->name = std::string(advance().text);
      expr = std::move(ref);
      continue;
    }
    if (match(TokKind::LBracket)) {
      auto index = std::make_unique<Expr>();
      index->kind = ExprKind::Index;
      index->span = expr->span;
      index->lhs = std::move(expr);
      index->rhs = parseExpression();
      expect(TokKind::RBracket, "']'");
      expr = std::move(index);
      continue;
    }
    if (check(TokKind::PlusPlus) || check(TokKind::MinusMinus)) {
      const bool increment = check(TokKind::PlusPlus);
      advance();
      auto inc = std::make_unique<Expr>();
      inc->kind = ExprKind::Inc;
      inc->span = expr->span;
      inc->lhs = std::move(expr);
      inc->op = "post";
      inc->intValue = increment ? 1 : -1;
      expr = std::move(inc);
      continue;
    }
    break;
  }
  return expr;
}

ExprPtr Parser::parseObjectLiteral() {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::ObjectLit;
  expr->span = current().span;
  expect(TokKind::LBrace, "'{'");
  while (!atEnd() && !check(TokKind::RBrace)) {
    ObjectField field;
    field.span = current().span;
    if (!check(TokKind::Identifier)) {
      error(current(), "expected object field name");
      break;
    }
    field.name = std::string(advance().text);
    if (match(TokKind::Colon)) {
      field.value = parseExpression();
    } else {
      auto value = std::make_unique<Expr>();
      value->kind = ExprKind::Name;
      value->name = field.name;
      value->span = field.span;
      field.value = std::move(value);
    }
    expr->fields.push_back(std::move(field));
    if (!match(TokKind::Comma)) {
      break;
    }
  }
  expect(TokKind::RBrace, "'}'");
  return expr;
}

ExprPtr Parser::parsePrimary() {
  auto expr = std::make_unique<Expr>();
  expr->span = current().span;

  if (check(TokKind::LBrace)) {
    return parseObjectLiteral();
  }
  if (check(TokKind::LBracket) && looksLikeLambda()) {
    return parseLambda();
  }
  if (match(TokKind::LBracket)) {
    expr->kind = ExprKind::ArrayLit;
    if (!check(TokKind::RBracket)) {
      do {
        expr->args.push_back(parseExpression());
      } while (match(TokKind::Comma));
    }
    expect(TokKind::RBracket, "']'");
    return expr;
  }
  if (check(TokKind::IntegerLiteral)) {
    expr->kind = ExprKind::IntLit;
    expr->intValue = advance().intValue;
    return expr;
  }
  if (check(TokKind::FractionLiteral)) {
    const Token& token = advance();
    expr->kind = ExprKind::FractionLit;
    expr->fractionValue = token.fractionValue;
    expr->fractionText = token.fractionText;
    return expr;
  }
  if (check(TokKind::CharacterLiteral)) {
    expr->kind = ExprKind::CharLit;
    expr->charValue = advance().charValue;
    return expr;
  }
  if (check(TokKind::StringLiteral)) {
    expr->kind = ExprKind::StringLit;
    expr->stringValue = advance().stringValue;
    return expr;
  }
  if (match(TokKind::KwTrue)) {
    expr->kind = ExprKind::BoolLit;
    expr->boolValue = true;
    return expr;
  }
  if (match(TokKind::KwFalse)) {
    expr->kind = ExprKind::BoolLit;
    expr->boolValue = false;
    return expr;
  }
  if (match(TokKind::KwNull)) {
    expr->kind = ExprKind::NullLit;
    return expr;
  }
  if (match(TokKind::KwThis)) {
    expr->kind = ExprKind::This;
    return expr;
  }
  if (match(TokKind::KwSuper)) {
    expr->kind = ExprKind::Super;
    return expr;
  }
  if (check(TokKind::Identifier)) {
    expr->kind = ExprKind::Name;
    expr->name = std::string(advance().text);
    return expr;
  }
  if (looksLikeLambda()) {
    return parseLambda();
  }
  if (match(TokKind::LParen)) {
    ExprPtr inner = parseExpression();
    expect(TokKind::RParen, "')'");
    return inner;
  }

  error(current(), std::string("expected an expression, found ") +
                       tokenKindName(current().kind));
  advance();
  expr->kind = ExprKind::IntLit;
  expr->intValue = 0;
  return expr;
}

bool Parser::looksLikeLambda() const {
  std::size_t start = 0;
  if (check(TokKind::LBracket)) {
    int brackets = 0;
    std::size_t i = 0;
    for (;; ++i) {
      const TokKind kind = peek(i).kind;
      if (kind == TokKind::End) {
        return false;
      }
      if (kind == TokKind::LBracket) {
        ++brackets;
      } else if (kind == TokKind::RBracket) {
        --brackets;
        if (brackets == 0) {
          break;
        }
      }
    }
    start = i + 1;
    if (peek(start).kind != TokKind::LParen) {
      return false;
    }
  } else if (!check(TokKind::LParen)) {
    return false;
  }
  int depth = 0;
  for (std::size_t i = start;; ++i) {
    const TokKind kind = peek(i).kind;
    if (kind == TokKind::End) {
      return false;
    }
    if (kind == TokKind::LParen) {
      ++depth;
    } else if (kind == TokKind::RParen) {
      --depth;
      if (depth == 0) {
        return peek(i + 1).kind == TokKind::Colon;
      }
    }
  }
}

ExprPtr Parser::parseLambda() {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Lambda;
  expr->span = current().span;
  expr->lambda = std::make_shared<LambdaInfo>();
  if (match(TokKind::LBracket)) {
    std::string captures = "[";
    bool first = true;
    while (!check(TokKind::RBracket) && !atEnd()) {
      if (!first) {
        captures += ", ";
      }
      first = false;
      Capture capture;
      capture.span = current().span;
      if (match(TokKind::Equal)) {
        capture.kind = Capture::Kind::ByValue;
        captures += "=";
      } else if (match(TokKind::Amp)) {
        capture.kind = Capture::Kind::ByRef;
        if (check(TokKind::Identifier)) {
          capture.name = advance().text;
          captures += "&" + capture.name;
        } else {
          captures += "&";
        }
      } else if (match(TokKind::KwThis)) {
        capture.kind = Capture::Kind::This;
        captures += "this";
      } else if (check(TokKind::Identifier)) {
        capture.kind = Capture::Kind::ByValue;
        capture.name = advance().text;
        captures += capture.name;
      } else {
        error(current(), "expected a lambda capture");
        advance();
        continue;
      }
      expr->lambda->captures.push_back(capture);
      if (!match(TokKind::Comma)) {
        break;
      }
    }
    expect(TokKind::RBracket, "']' after lambda captures");
    captures += "]";
    expr->lambda->captureList = captures;
  }
  expr->lambda->params = parseParams();
  expect(TokKind::Colon, "':' before a lambda return type");
  expr->lambda->returnType = parseType(true);
  expect(TokKind::Arrow, "'->' before a lambda body");
  if (check(TokKind::LBrace)) {
    expr->lambda->body = parseBlock();
  } else {
    expr->lambda->bodyExpr = parseExpression();
  }
  return expr;
}

}  // namespace coord
