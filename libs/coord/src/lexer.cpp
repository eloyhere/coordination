#include "coord/syntax.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace coord {
namespace {

bool isIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentContinue(char c) {
  return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int utf8Length(unsigned char first) {
  if (first < 0x80) {
    return 1;
  }
  if ((first & 0xE0) == 0xC0) {
    return 2;
  }
  if ((first & 0xF0) == 0xE0) {
    return 3;
  }
  if ((first & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;
}

bool decodeUtf8(std::string_view text, std::size_t offset, char32_t& codePoint,
                std::size_t& length) {
  if (offset >= text.size()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(text[offset]);
  length = static_cast<std::size_t>(utf8Length(first));
  if (length == 0 || offset + length > text.size()) {
    return false;
  }
  codePoint = first;
  if (length == 1) {
    return true;
  }
  codePoint = first & (0xFF >> (length + 1));
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[offset + i]);
    if ((next & 0xC0) != 0x80) {
      return false;
    }
    codePoint = (codePoint << 6) | (next & 0x3F);
  }
  return true;
}

bool isUnicodeWhitespace(char32_t codePoint) {
  return codePoint == 0x00A0 || codePoint == 0x1680 ||
         (codePoint >= 0x2000 && codePoint <= 0x200A) ||
         codePoint == 0x2028 || codePoint == 0x2029 || codePoint == 0x202F ||
         codePoint == 0x205F || codePoint == 0x3000 || codePoint == 0xFEFF;
}

bool isControl(char32_t codePoint) {
  return codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F);
}

bool isIdentifierStartAt(std::string_view text, std::size_t offset) {
  if (offset >= text.size()) {
    return false;
  }
  const char c = text[offset];
  if (isIdentStart(c)) {
    return true;
  }
  if (static_cast<unsigned char>(c) < 0x80) {
    return false;
  }
  char32_t codePoint = 0;
  std::size_t length = 0;
  if (!decodeUtf8(text, offset, codePoint, length)) {
    return false;
  }
  return !isUnicodeWhitespace(codePoint) && !isControl(codePoint);
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

const std::unordered_map<std::string_view, TokKind>& keywordTable() {
  static const std::unordered_map<std::string_view, TokKind> table = {
      {"module", TokKind::KwModule},
      {"import", TokKind::KwImport},
      {"export", TokKind::KwExport},
      {"as", TokKind::KwAs},
      {"class", TokKind::KwClass},
      {"interface", TokKind::KwInterface},
      {"implements", TokKind::KwImplements},
      {"extends", TokKind::KwExtends},
      {"annotation", TokKind::KwAnnotation},
      {"function", TokKind::KwFunction},
      {"void", TokKind::KwVoid},
      {"const", TokKind::KwConst},
      {"static", TokKind::KwStatic},
      {"virtual", TokKind::KwVirtual},
      {"operator", TokKind::KwOperator},
      {"where", TokKind::KwWhere},
      {"if", TokKind::KwIf},
      {"else", TokKind::KwElse},
      {"let", TokKind::KwLet},
      {"while", TokKind::KwWhile},
      {"for", TokKind::KwFor},
      {"switch", TokKind::KwSwitch},
      {"in", TokKind::KwIn},
      {"return", TokKind::KwReturn},
      {"break", TokKind::KwBreak},
      {"continue", TokKind::KwContinue},
      {"true", TokKind::KwTrue},
      {"false", TokKind::KwFalse},
      {"null", TokKind::KwNull},
      {"this", TokKind::KwThis},
      {"super", TokKind::KwSuper},
      {"is", TokKind::KwIs},
      {"match", TokKind::KwMatch},
      {"extern", TokKind::KwExtern},
      {"unsafe", TokKind::KwUnsafe},
      {"new", TokKind::KwNew},
      {"delete", TokKind::KwDelete},
      {"integer", TokKind::KwInteger},
      {"fix", TokKind::KwFix},
      {"float", TokKind::KwFloat},
      {"record", TokKind::KwRecord},
      {"character", TokKind::KwCharacter},
      {"boolean", TokKind::KwBoolean},
      {"enumerate", TokKind::KwEnumerate},
      {"final", TokKind::KwFinal},
      {"override", TokKind::KwOverride},
      {"public", TokKind::KwPublic},
      {"private", TokKind::KwPrivate},
      {"protected", TokKind::KwProtected},
  };
  return table;
}

}  // namespace

const char* tokenKindName(TokKind kind) {
  switch (kind) {
#define COORD_TOKEN_NAME(name, text) \
  case TokKind::name:                \
    return text;
    COORD_TOKEN_LIST(COORD_TOKEN_NAME)
#undef COORD_TOKEN_NAME
  }
  return "unknown token";
}

bool isKeyword(TokKind kind) {
  const auto value = static_cast<std::uint16_t>(kind);
  return value >= static_cast<std::uint16_t>(TokKind::KwModule) &&
         value <= static_cast<std::uint16_t>(TokKind::KwProtected);
}

Lexer::Lexer(const SourceManager& sources, std::uint32_t fileId,
             DiagnosticEngine& diagnostics)
    : sources_(sources),
      fileId_(fileId),
      diagnostics_(diagnostics),
      text_(sources.file(fileId).text) {}

char Lexer::peek(std::size_t lookahead) const {
  const std::size_t index = pos_ + lookahead;
  return index < text_.size() ? text_[index] : '\0';
}

char Lexer::advance() {
  return pos_ < text_.size() ? text_[pos_++] : '\0';
}

bool Lexer::match(char expected) {
  if (peek() != expected) {
    return false;
  }
  ++pos_;
  return true;
}

Span Lexer::spanFrom(std::uint32_t begin) const {
  return Span{fileId_, begin, static_cast<std::uint32_t>(pos_)};
}

void Lexer::error(Span span, std::string message) {
  diagnostics_.error(span, "E1001", std::move(message));
}

void Lexer::errorAt(std::uint32_t begin, std::uint32_t end,
                    std::string message) {
  diagnostics_.error(Span{fileId_, begin, end}, "E1001", std::move(message));
}

void Lexer::skipWhitespaceAndComments() {
  for (;;) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v') {
      ++pos_;
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      if (peek(2) == '/') {
        break;
      }
      pos_ += 2;
      while (!atEnd() && peek() != '\n') {
        ++pos_;
      }
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      if (peek(2) == '*' && peek(3) != '/') {
        break;
      }
      const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
      pos_ += 2;
      int depth = 1;
      while (!atEnd() && depth > 0) {
        if (peek() == '/' && peek(1) == '*') {
          pos_ += 2;
          ++depth;
        } else if (peek() == '*' && peek(1) == '/') {
          pos_ += 2;
          --depth;
        } else {
          ++pos_;
        }
      }
      if (depth != 0) {
        errorAt(begin, static_cast<std::uint32_t>(pos_),
                "unterminated block comment");
      }
      continue;
    }
    break;
  }
}

Token Lexer::makeToken(TokKind kind, std::uint32_t begin) {
  Token token;
  token.kind = kind;
  token.span = spanFrom(begin);
  token.text = text_.substr(begin, pos_ - begin);
  return token;
}

Token Lexer::lexIdentifierOrKeyword() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (isIdentContinue(c)) {
      ++pos_;
      continue;
    }
    if (static_cast<unsigned char>(c) >= 0x80) {
      char32_t codePoint = 0;
      std::size_t length = 0;
      if (decodeUtf8(text_, pos_, codePoint, length) &&
          !isUnicodeWhitespace(codePoint) && !isControl(codePoint)) {
        pos_ += length;
        continue;
      }
    }
    break;
  }
  Token token = makeToken(TokKind::Identifier, begin);
  const auto& table = keywordTable();
  const auto it = table.find(token.text);
  if (it != table.end()) {
    token.kind = it->second;
  }
  return token;
}

Token Lexer::lexNumber() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  int base = 10;
  bool isFraction = false;

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    base = 16;
    pos_ += 2;
  } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
    base = 2;
    pos_ += 2;
  } else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
    base = 8;
    pos_ += 2;
  }

  std::string cleaned;
  auto consumeDigits = [&]() {
    while (true) {
      const char c = peek();
      if (c == '_') {
        ++pos_;
        continue;
      }
      if (base == 16 ? (hexValue(c) >= 0) : (c >= '0' && c < '0' + base)) {
        cleaned.push_back(c);
        ++pos_;
        continue;
      }
      break;
    }
  };

  consumeDigits();
  if (cleaned.empty()) {
    errorAt(begin, static_cast<std::uint32_t>(pos_),
            "expected digits after numeric base prefix");
  }

  if (base == 10) {
    // Fractional part: only when '.' is followed by a digit (so `1..2` works).
    if (peek() == '.' && isDigit(peek(1))) {
      isFraction = true;
      cleaned.push_back('.');
      ++pos_;
      consumeDigits();
    }
    if (peek() == 'e' || peek() == 'E') {
      isFraction = true;
      cleaned.push_back('e');
      ++pos_;
      if (peek() == '+' || peek() == '-') {
        cleaned.push_back(advance());
      }
      const std::size_t before = cleaned.size();
      consumeDigits();
      if (cleaned.size() == before) {
        errorAt(begin, static_cast<std::uint32_t>(pos_),
                "expected exponent digits");
      }
    }
  }

  if (isIdentStart(peek())) {
    const std::uint32_t suffixBegin = static_cast<std::uint32_t>(pos_);
    while (isIdentContinue(peek())) {
      ++pos_;
    }
    errorAt(suffixBegin, static_cast<std::uint32_t>(pos_),
            "numeric literal suffixes are not supported yet");
  }

  Token token = makeToken(isFraction ? TokKind::FractionLiteral
                                    : TokKind::IntegerLiteral,
                          begin);

  if (isFraction) {
    double value = 0.0;
    const auto result =
        std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (result.ec != std::errc{}) {
      errorAt(begin, static_cast<std::uint32_t>(pos_),
              "fractional literal is out of range");
    }
    token.fractionValue = value;
    token.fractionText = cleaned;
  } else {
    std::uint64_t value = 0;
    const auto result = std::from_chars(
        cleaned.data(), cleaned.data() + cleaned.size(), value, base);
    if (result.ec != std::errc{}) {
      errorAt(begin, static_cast<std::uint32_t>(pos_), "integer literal is out of range");
    }
    token.intValue = static_cast<std::int64_t>(value);
  }
  return token;
}

Token Lexer::lexCharacter() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  ++pos_;  // opening quote
  char32_t value = 0;
  bool ok = false;

  if (peek() == '\\') {
    ++pos_;
    const char escape = advance();
    switch (escape) {
      case 'n':
        value = U'\n';
        ok = true;
        break;
      case 't':
        value = U'\t';
        ok = true;
        break;
      case 'r':
        value = U'\r';
        ok = true;
        break;
      case '0':
        value = U'\0';
        ok = true;
        break;
      case '\\':
        value = U'\\';
        ok = true;
        break;
      case '\'':
        value = U'\'';
        ok = true;
        break;
      case '"':
        value = U'"';
        ok = true;
        break;
      case 'u': {
        if (!match('{')) {
          errorAt(begin, static_cast<std::uint32_t>(pos_),
                  "expected '{' after \\u escape");
          break;
        }
        char32_t codePoint = 0;
        int digits = 0;
        while (peek() != '}' && !atEnd() && digits < 8) {
          const int digit = hexValue(peek());
          if (digit < 0) {
            break;
          }
          codePoint = codePoint * 16 + static_cast<char32_t>(digit);
          ++pos_;
          ++digits;
        }
        if (!match('}') || digits == 0) {
          errorAt(begin, static_cast<std::uint32_t>(pos_),
                  "invalid \\u{...} escape in character literal");
          break;
        }
        value = codePoint;
        ok = true;
        break;
      }
      default:
        errorAt(begin, static_cast<std::uint32_t>(pos_),
                "unknown escape sequence in character literal");
        break;
    }
  } else if (!atEnd() && peek() != '\'' && peek() != '\n') {
    // Decode a single UTF-8 scalar.
    const auto first = static_cast<unsigned char>(advance());
    if (first < 0x80) {
      value = static_cast<char32_t>(first);
      ok = true;
    } else {
      int extra = 0;
      if ((first & 0xE0) == 0xC0) {
        extra = 1;
        value = first & 0x1F;
      } else if ((first & 0xF0) == 0xE0) {
        extra = 2;
        value = first & 0x0F;
      } else if ((first & 0xF8) == 0xF0) {
        extra = 3;
        value = first & 0x07;
      }
      ok = extra != 0;
      for (int i = 0; i < extra && ok; ++i) {
        const auto next = static_cast<unsigned char>(peek());
        if ((next & 0xC0) != 0x80) {
          ok = false;
          break;
        }
        value = (value << 6) | (next & 0x3F);
        ++pos_;
      }
    }
  }

  if (!match('\'')) {
    errorAt(begin, static_cast<std::uint32_t>(pos_),
            "unterminated character literal");
    // Recover: skip to the end of the line.
    while (!atEnd() && peek() != '\n') {
      ++pos_;
    }
  }

  Token token = makeToken(TokKind::CharacterLiteral, begin);
  token.charValue = ok ? value : U'\0';
  return token;
}

Token Lexer::lexDocComment() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  std::string content;
  if (peek(1) == '/') {
    pos_ += 3;
    while (!atEnd() && peek() != '\n') {
      content.push_back(peek());
      ++pos_;
    }
  } else {
    pos_ += 3;
    while (!atEnd() && !(peek() == '*' && peek(1) == '/')) {
      content.push_back(peek());
      ++pos_;
    }
    if (!atEnd()) {
      pos_ += 2;
    }
  }
  Token token = makeToken(TokKind::DocComment, begin);
  token.docText = content;
  return token;
}

Token Lexer::lexString() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  ++pos_;  // opening quote
  std::string value;
  bool terminated = false;

  while (!atEnd()) {
    const char c = peek();
    if (c == '"') {
      ++pos_;
      terminated = true;
      break;
    }
    if (c == '\n') {
      break;
    }
    if (c == '\\') {
      ++pos_;
      const char escape = advance();
      switch (escape) {
        case 'n':
          value.push_back('\n');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case '0':
          value.push_back('\0');
          break;
        case '\\':
          value.push_back('\\');
          break;
        case '"':
          value.push_back('"');
          break;
        case '\'':
          value.push_back('\'');
          break;
        default:
          errorAt(begin, static_cast<std::uint32_t>(pos_),
                  "unknown escape sequence in string literal");
          break;
      }
      continue;
    }
    value.push_back(c);
    ++pos_;
  }

  if (!terminated) {
    errorAt(begin, static_cast<std::uint32_t>(pos_),
            "unterminated string literal");
  }

  Token token = makeToken(TokKind::StringLiteral, begin);
  token.stringValue = std::move(value);
  return token;
}

Token Lexer::lexPunctuation() {
  const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
  const char c = advance();
  auto token = [&](TokKind kind) { return makeToken(kind, begin); };

  switch (c) {
    case '(':
      return token(TokKind::LParen);
    case ')':
      return token(TokKind::RParen);
    case '{':
      return token(TokKind::LBrace);
    case '}':
      return token(TokKind::RBrace);
    case '[':
      return token(TokKind::LBracket);
    case ']':
      return token(TokKind::RBracket);
    case ';':
      return token(TokKind::Semicolon);
    case ',':
      return token(TokKind::Comma);
    case '?':
      return token(TokKind::Question);
    case '@':
      return token(TokKind::At);
    case '#':
      return token(TokKind::Hash);
    case '~':
      return token(TokKind::Tilde);
    case '^':
      if (match('=')) {
        return token(TokKind::CaretEqual);
      }
      return token(TokKind::Caret);
    case '+':
      if (match('+')) {
        return token(TokKind::PlusPlus);
      }
      if (match('=')) {
        return token(TokKind::PlusEqual);
      }
      return token(TokKind::Plus);
    case '-':
      if (match('-')) {
        return token(TokKind::MinusMinus);
      }
      if (match('>')) {
        return token(TokKind::Arrow);
      }
      if (match('=')) {
        return token(TokKind::MinusEqual);
      }
      return token(TokKind::Minus);
    case '*':
      if (match('=')) {
        return token(TokKind::StarEqual);
      }
      return token(TokKind::Star);
    case '/':
      if (match('=')) {
        return token(TokKind::SlashEqual);
      }
      return token(TokKind::Slash);
    case '%':
      if (match('=')) {
        return token(TokKind::PercentEqual);
      }
      return token(TokKind::Percent);
    case '!':
      if (match('=')) {
        return token(TokKind::BangEqual);
      }
      return token(TokKind::Bang);
    case '=':
      if (match('=')) {
        return token(TokKind::EqualEqual);
      }
      if (match('>')) {
        return token(TokKind::FatArrow);
      }
      return token(TokKind::Equal);
    case '&':
      if (match('&')) {
        return token(TokKind::AmpAmp);
      }
      if (match('=')) {
        return token(TokKind::AmpEqual);
      }
      return token(TokKind::Amp);
    case '|':
      if (match('|')) {
        return token(TokKind::PipePipe);
      }
      if (match('=')) {
        return token(TokKind::PipeEqual);
      }
      return token(TokKind::Pipe);
    case '<':
      if (match('=')) {
        return token(TokKind::LessEqual);
      }
      return token(TokKind::Less);
    case '>':
      if (match('=')) {
        return token(TokKind::GreaterEqual);
      }
      return token(TokKind::Greater);
    case ':':
      if (match(':')) {
        return token(TokKind::ColonColon);
      }
      return token(TokKind::Colon);
    case '.':
      if (match('.')) {
        if (match('=')) {
          return token(TokKind::DotDotEqual);
        }
        return token(TokKind::DotDot);
      }
      return token(TokKind::Dot);
    default:
      break;
  }

  errorAt(begin, static_cast<std::uint32_t>(pos_),
          std::string("unexpected character '") + c + "'");
  return token(TokKind::Unknown);
}

std::vector<Token> Lexer::lexAll() {
  std::vector<Token> tokens;
  for (;;) {
    skipWhitespaceAndComments();
    const std::uint32_t begin = static_cast<std::uint32_t>(pos_);
    if (atEnd()) {
      Token end;
      end.kind = TokKind::End;
      end.span = Span{fileId_, begin, begin};
      tokens.push_back(end);
      break;
    }

    const char c = peek();
    if (c == '/' && peek(1) == '/' && peek(2) == '/') {
      tokens.push_back(lexDocComment());
    } else if (c == '/' && peek(1) == '*' && peek(2) == '*' &&
               peek(3) != '/') {
      tokens.push_back(lexDocComment());
    } else if (isIdentifierStartAt(text_, pos_)) {
      tokens.push_back(lexIdentifierOrKeyword());
    } else if (isDigit(c)) {
      tokens.push_back(lexNumber());
    } else if (c == '\'') {
      tokens.push_back(lexCharacter());
    } else if (c == '"') {
      tokens.push_back(lexString());
    } else {
      tokens.push_back(lexPunctuation());
    }
  }
  return tokens;
}

}  // namespace coord
