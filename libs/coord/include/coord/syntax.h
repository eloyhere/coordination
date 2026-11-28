#pragma once

#include "coord/support.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace coord {

// Keep this list in sync with the lexer keyword table.
#define COORD_TOKEN_LIST(X)          \
  X(End, "end of file")              \
  X(Identifier, "identifier")        \
  X(DocComment, "doc comment")       \
  X(IntegerLiteral, "integer literal") \
  X(FractionLiteral, "fractional literal") \
  X(CharacterLiteral, "character literal") \
  X(StringLiteral, "string literal") \
  X(KwModule, "'module'")            \
  X(KwImport, "'import'")            \
  X(KwExport, "'export'")            \
  X(KwAs, "'as'")                    \
  X(KwClass, "'class'")              \
  X(KwInterface, "'interface'")      \
  X(KwImplements, "'implements'")    \
  X(KwExtends, "'extends'")          \
  X(KwAnnotation, "'annotation'")    \
  X(KwFunction, "'function'")        \
  X(KwVoid, "'void'")                \
  X(KwConst, "'const'")              \
  X(KwStatic, "'static'")            \
  X(KwVirtual, "'virtual'")          \
  X(KwOperator, "'operator'")        \
  X(KwWhere, "'where'")              \
  X(KwIf, "'if'")                    \
  X(KwElse, "'else'")                \
  X(KwLet, "'let'")                  \
  X(KwWhile, "'while'")              \
  X(KwFor, "'for'")                  \
  X(KwSwitch, "'switch'")            \
  X(KwIn, "'in'")                    \
  X(KwReturn, "'return'")            \
  X(KwBreak, "'break'")              \
  X(KwContinue, "'continue'")        \
  X(KwTrue, "'true'")                \
  X(KwFalse, "'false'")              \
  X(KwNull, "'null'")                \
  X(KwThis, "'this'")                \
  X(KwSuper, "'super'")              \
  X(KwIs, "'is'")                    \
  X(KwMatch, "'match'")              \
  X(KwExtern, "'extern'")            \
  X(KwUnsafe, "'unsafe'")            \
  X(KwNew, "'new'")                  \
  X(KwDelete, "'delete'")            \
  X(KwInteger, "'integer'")          \
  X(KwFix, "'fix'")                  \
  X(KwFloat, "'float'")              \
  X(KwRecord, "'record'")            \
  X(KwCharacter, "'character'")      \
  X(KwBoolean, "'boolean'")          \
  X(KwEnumerate, "'enumerate'")      \
  X(KwFinal, "'final'")              \
  X(KwOverride, "'override'")        \
  X(KwPublic, "'public'")            \
  X(KwPrivate, "'private'")          \
  X(KwProtected, "'protected'")      \
  X(LParen, "'('")                   \
  X(RParen, "')'")                   \
  X(LBrace, "'{'")                   \
  X(RBrace, "'}'")                   \
  X(LBracket, "'['")                 \
  X(RBracket, "']'")                 \
  X(Less, "'<'")                     \
  X(Greater, "'>'")                  \
  X(LessEqual, "'<='")               \
  X(GreaterEqual, "'>='")            \
  X(EqualEqual, "'=='")              \
  X(BangEqual, "'!='")               \
  X(Plus, "'+'")                     \
  X(PlusPlus, "'++'")                \
  X(MinusMinus, "'--'")              \
  X(Minus, "'-'")                    \
  X(Star, "'*'")                     \
  X(Slash, "'/'")                    \
  X(Percent, "'%'")                  \
  X(Amp, "'&'")                      \
  X(AmpAmp, "'&&'")                  \
  X(Pipe, "'|'")                     \
  X(PipePipe, "'||'")                \
  X(Caret, "'^'")                    \
  X(Tilde, "'~'")                    \
  X(Bang, "'!'")                     \
  X(Equal, "'='")                    \
  X(PlusEqual, "'+='")               \
  X(MinusEqual, "'-='")              \
  X(StarEqual, "'*='")               \
  X(SlashEqual, "'/='")              \
  X(PercentEqual, "'%='")            \
  X(AmpEqual, "'&='")                \
  X(PipeEqual, "'|='")               \
  X(CaretEqual, "'^='")              \
  X(Arrow, "'->'")                   \
  X(FatArrow, "'=>'")                \
  X(Colon, "':'")                    \
  X(ColonColon, "'::'")              \
  X(Semicolon, "';'")                \
  X(Comma, "','")                    \
  X(Dot, "'.'")                      \
  X(DotDot, "'..'")                  \
  X(DotDotEqual, "'..='")            \
  X(Question, "'?'")                 \
  X(At, "'@'")                       \
  X(Hash, "'#'")                     \
  X(Unknown, "unknown token")

enum class TokKind : std::uint16_t {
#define COORD_TOKEN_ENUM(name, text) name,
  COORD_TOKEN_LIST(COORD_TOKEN_ENUM)
#undef COORD_TOKEN_ENUM
};

[[nodiscard]] const char* tokenKindName(TokKind kind);
[[nodiscard]] bool isKeyword(TokKind kind);

struct Token {
  TokKind kind = TokKind::End;
  std::string_view text;
  Span span;
  std::int64_t intValue = 0;
  double fractionValue = 0.0;
  /// Cleaned source spelling of a fractional literal (no `_` separators).
  std::string fractionText;
  char32_t charValue = 0;
  std::string stringValue;
  std::string docText;
};

class Lexer {
public:
  Lexer(const SourceManager& sources, std::uint32_t fileId,
        DiagnosticEngine& diagnostics);

  std::vector<Token> lexAll();

private:
  [[nodiscard]] bool atEnd() const { return pos_ >= text_.size(); }
  [[nodiscard]] char peek(std::size_t lookahead = 0) const;
  char advance();
  bool match(char expected);

  void skipWhitespaceAndComments();
  Token makeToken(TokKind kind, std::uint32_t begin);
  Token lexIdentifierOrKeyword();
  Token lexNumber();
  Token lexCharacter();
  Token lexString();
  Token lexDocComment();
  Token lexPunctuation();

  [[nodiscard]] Span spanFrom(std::uint32_t begin) const;

  void error(Span span, std::string message);
  void errorAt(std::uint32_t begin, std::uint32_t end, std::string message);

  const SourceManager& sources_;
  std::uint32_t fileId_ = 0;
  DiagnosticEngine& diagnostics_;
  std::string_view text_;
  std::size_t pos_ = 0;
};

}  // namespace coord
