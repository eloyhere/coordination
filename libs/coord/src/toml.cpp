#include "coord/modules/toml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace coord::toml {
namespace {

bool isBareKeyCharacter(char character) {
  const unsigned char value = static_cast<unsigned char>(character);
  return std::isalnum(value) != 0 || character == '_' || character == '-';
}

bool isDigit(char character) { return character >= '0' && character <= '9'; }

int hexValue(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

void appendUtf8(std::string& out, std::uint32_t code) {
  if (code <= 0x7F) {
    out.push_back(static_cast<char>(code));
  } else if (code <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::optional<Value> parseDocument() {
    root_ = Value::makeTable();
    while (true) {
      skipSpaceAndNewlines();
      if (eof()) {
        break;
      }
      if (peek() == '[') {
        if (!parseTableHeader()) {
          return std::nullopt;
        }
      } else {
        if (!parseKeyValue()) {
          return std::nullopt;
        }
      }
    }
    if (!error_.empty()) {
      return std::nullopt;
    }
    return std::move(root_);
  }

  [[nodiscard]] std::size_t errorLine() const {
    std::size_t line = 1;
    for (std::size_t i = 0; i < errorPosition_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++line;
      }
    }
    return line;
  }

  [[nodiscard]] std::size_t errorColumn() const {
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < errorPosition_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        lineStart = i + 1;
      }
    }
    return errorPosition_ - lineStart + 1;
  }

  [[nodiscard]] const std::string& error() const { return error_; }

 private:
  bool eof() const { return position_ >= text_.size(); }

  char peek(std::size_t offset = 0) const {
    return position_ + offset < text_.size() ? text_[position_ + offset]
                                             : '\0';
  }

  bool startsWith(std::string_view text) const {
    return text_.size() - position_ >= text.size() &&
           text_.substr(position_, text.size()) == text;
  }

  void advance(std::size_t count = 1) { position_ += count; }

  bool fail(const std::string& message) {
    if (error_.empty()) {
      error_ = message;
      errorPosition_ = position_;
    }
    return false;
  }

  void skipInlineSpace() {
    while (!eof() && (peek() == ' ' || peek() == '\t')) {
      advance();
    }
  }

  void skipComment() {
    if (peek() == '#') {
      while (!eof() && peek() != '\n' && peek() != '\r') {
        advance();
      }
    }
  }

  void skipSpaceAndNewlines() {
    for (;;) {
      skipInlineSpace();
      if (peek() == '#') {
        skipComment();
        continue;
      }
      if (peek() == '\r') {
        advance();
        if (peek() == '\n') {
          advance();
        }
        continue;
      }
      if (peek() == '\n') {
        advance();
        continue;
      }
      break;
    }
  }

  bool atLineEnd() const {
    return eof() || peek() == '\n' || peek() == '\r' || peek() == '#';
  }

  bool expectLineEnd() {
    skipInlineSpace();
    skipComment();
    if (eof()) {
      return true;
    }
    if (peek() == '\r') {
      advance();
      if (peek() == '\n') {
        advance();
      }
      return true;
    }
    if (peek() == '\n') {
      advance();
      return true;
    }
    return fail("unexpected characters after the value");
  }

  std::optional<std::string> parseBasicString() {
    if (peek() != '"') {
      return std::nullopt;
    }
    advance();
    std::string result;
    while (!eof()) {
      const char character = peek();
      if (character == '"') {
        advance();
        return result;
      }
      if (character == '\n' || character == '\r') {
        fail("newline in a single-line string");
        return std::nullopt;
      }
      if (character == '\\') {
        if (!parseEscape(result)) {
          return std::nullopt;
        }
        continue;
      }
      result.push_back(character);
      advance();
    }
    fail("unterminated string");
    return std::nullopt;
  }

  std::optional<std::string> parseLiteralString() {
    if (peek() != '\'') {
      return std::nullopt;
    }
    advance();
    std::string result;
    while (!eof()) {
      const char character = peek();
      if (character == '\'') {
        advance();
        return result;
      }
      if (character == '\n' || character == '\r') {
        fail("newline in a single-line literal string");
        return std::nullopt;
      }
      result.push_back(character);
      advance();
    }
    fail("unterminated literal string");
    return std::nullopt;
  }

  std::optional<std::string> parseMultilineBasicString() {
    advance(3);
    if (peek() == '\r') {
      advance();
      if (peek() == '\n') {
        advance();
      }
    } else if (peek() == '\n') {
      advance();
    }
    std::string result;
    while (!eof()) {
      if (startsWith("\"\"\"")) {
        advance(3);
        return result;
      }
      const char character = peek();
      if (character == '\\') {
        const std::size_t start = position_;
        advance();
        if (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r' ||
                       peek() == '\n')) {
          while (!eof() && (peek() == ' ' || peek() == '\t' ||
                            peek() == '\r' || peek() == '\n')) {
            advance();
          }
          continue;
        }
        position_ = start;
        if (!parseEscape(result)) {
          return std::nullopt;
        }
        continue;
      }
      if (character == '\r') {
        advance();
        if (peek() == '\n') {
          advance();
        }
        result.push_back('\n');
        continue;
      }
      result.push_back(character);
      advance();
    }
    fail("unterminated multi-line string");
    return std::nullopt;
  }

  std::optional<std::string> parseMultilineLiteralString() {
    advance(3);
    if (peek() == '\r') {
      advance();
      if (peek() == '\n') {
        advance();
      }
    } else if (peek() == '\n') {
      advance();
    }
    std::string result;
    while (!eof()) {
      if (startsWith("'''")) {
        advance(3);
        return result;
      }
      const char character = peek();
      if (character == '\r') {
        advance();
        if (peek() == '\n') {
          advance();
        }
        result.push_back('\n');
        continue;
      }
      result.push_back(character);
      advance();
    }
    fail("unterminated multi-line literal string");
    return std::nullopt;
  }

  bool parseEscape(std::string& out) {
    advance();  // backslash
    if (eof()) {
      return fail("unterminated escape sequence");
    }
    const char character = peek();
    advance();
    switch (character) {
      case 'b':
        out.push_back('\b');
        return true;
      case 't':
        out.push_back('\t');
        return true;
      case 'n':
        out.push_back('\n');
        return true;
      case 'f':
        out.push_back('\f');
        return true;
      case 'r':
        out.push_back('\r');
        return true;
      case '"':
        out.push_back('"');
        return true;
      case '\\':
        out.push_back('\\');
        return true;
      case 'u':
      case 'U': {
        const int digits = character == 'u' ? 4 : 8;
        std::uint32_t code = 0;
        for (int i = 0; i < digits; ++i) {
          const int value = hexValue(peek());
          if (value < 0) {
            return fail("invalid unicode escape");
          }
          code = code * 16 + static_cast<std::uint32_t>(value);
          advance();
        }
        if (code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF)) {
          return fail("invalid unicode scalar value in escape");
        }
        appendUtf8(out, code);
        return true;
      }
      default:
        return fail(std::string("unknown escape '\\") + character + "'");
    }
  }

  std::optional<std::string> parseKey() {
    if (eof()) {
      fail("expected a key");
      return std::nullopt;
    }
    if (peek() == '"') {
      return parseBasicString();
    }
    if (peek() == '\'') {
      return parseLiteralString();
    }
    const std::size_t begin = position_;
    while (!eof() && isBareKeyCharacter(peek())) {
      advance();
    }
    if (begin == position_) {
      fail("expected a key");
      return std::nullopt;
    }
    return std::string(text_.substr(begin, position_ - begin));
  }

  std::optional<std::vector<std::string>> parseKeyPath() {
    std::vector<std::string> path;
    std::optional<std::string> key = parseKey();
    if (!key.has_value()) {
      return std::nullopt;
    }
    path.push_back(std::move(*key));
    skipInlineSpace();
    while (peek() == '.') {
      advance();
      skipInlineSpace();
      key = parseKey();
      if (!key.has_value()) {
        return std::nullopt;
      }
      path.push_back(std::move(*key));
      skipInlineSpace();
    }
    return path;
  }

  Value* resolve(Value& root, const std::vector<std::string>& path,
                 bool create) {
    Value* current = &root;
    for (const std::string& segment : path) {
      if (!segment.empty() && segment.front() == '#') {
        if (!current->isArray()) {
          fail("key is not an array: '" + segment.substr(1) + "'");
          return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(
            std::strtoull(segment.c_str() + 1, nullptr, 10));
        if (index >= current->elements().size()) {
          fail("array index out of range");
          return nullptr;
        }
        current = &current->elements()[index];
        continue;
      }
      if (current->isArray()) {
        if (current->elements().empty() ||
            !current->elements().back().isTable()) {
          fail("cannot add a key to an array");
          return nullptr;
        }
        current = &current->elements().back();
      }
      if (!current->isTable()) {
        fail("key path crosses a non-table value");
        return nullptr;
      }
      Value* child = current->get(segment);
      if (child == nullptr) {
        if (!create) {
          fail("unknown table '" + segment + "'");
          return nullptr;
        }
        current->set(segment, Value::makeTable());
        child = current->get(segment);
      }
      current = child;
    }
    return current;
  }

  std::optional<Value> parseValue() {
    if (eof()) {
      fail("expected a value");
      return std::nullopt;
    }
    if (startsWith("\"\"\"")) {
      std::optional<std::string> text = parseMultilineBasicString();
      if (!text.has_value()) {
        return std::nullopt;
      }
      return Value::makeString(std::move(*text));
    }
    if (startsWith("'''")) {
      std::optional<std::string> text = parseMultilineLiteralString();
      if (!text.has_value()) {
        return std::nullopt;
      }
      return Value::makeString(std::move(*text));
    }
    switch (peek()) {
      case '"': {
        std::optional<std::string> text = parseBasicString();
        if (!text.has_value()) {
          return std::nullopt;
        }
        return Value::makeString(std::move(*text));
      }
      case '\'': {
        std::optional<std::string> text = parseLiteralString();
        if (!text.has_value()) {
          return std::nullopt;
        }
        return Value::makeString(std::move(*text));
      }
      case '[':
        return parseArray();
      case '{':
        return parseInlineTable();
      default:
        break;
    }
    if (startsWith("true") &&
        !isBareKeyCharacter(peek(4))) {
      advance(4);
      return Value::makeBoolean(true);
    }
    if (startsWith("false") &&
        !isBareKeyCharacter(peek(5))) {
      advance(5);
      return Value::makeBoolean(false);
    }
    return parseNumber();
  }

  std::optional<Value> parseArray() {
    advance();  // '['
    Value array = Value::makeArray();
    for (;;) {
      skipSpaceAndNewlines();
      if (eof()) {
        fail("unterminated array");
        return std::nullopt;
      }
      if (peek() == ']') {
        advance();
        return array;
      }
      std::optional<Value> element = parseValue();
      if (!element.has_value()) {
        return std::nullopt;
      }
      array.push(std::move(*element));
      skipSpaceAndNewlines();
      if (peek() == ',') {
        advance();
        continue;
      }
      if (peek() == ']') {
        advance();
        return array;
      }
      fail("expected ',' or ']' in array");
      return std::nullopt;
    }
  }

  std::optional<Value> parseInlineTable() {
    advance();  // '{'
    Value table = Value::makeTable();
    skipInlineSpace();
    if (peek() == '}') {
      advance();
      return table;
    }
    for (;;) {
      skipInlineSpace();
      if (peek() == '\n' || peek() == '\r') {
        fail("newline in an inline table");
        return std::nullopt;
      }
      std::optional<std::vector<std::string>> path = parseKeyPath();
      if (!path.has_value()) {
        return std::nullopt;
      }
      skipInlineSpace();
      if (peek() != '=') {
        fail("expected '=' in inline table");
        return std::nullopt;
      }
      advance();
      skipInlineSpace();
      std::optional<Value> value = parseValue();
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (!insertValue(table, *path, std::move(*value))) {
        return std::nullopt;
      }
      skipInlineSpace();
      if (peek() == ',') {
        advance();
        skipInlineSpace();
        if (peek() == '}') {
          advance();
          return table;
        }
        continue;
      }
      if (peek() == '}') {
        advance();
        return table;
      }
      fail("expected ',' or '}' in inline table");
      return std::nullopt;
    }
  }

  bool insertValue(Value& table, const std::vector<std::string>& path,
                   Value value) {
    std::vector<std::string> parentPath;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      parentPath.push_back(path[i]);
    }
    Value* parent = &table;
    for (const std::string& segment : parentPath) {
      if (!parent->isTable()) {
        return fail("key path crosses a non-table value");
      }
      Value* child = parent->get(segment);
      if (child == nullptr) {
        parent->set(segment, Value::makeTable());
        child = parent->get(segment);
      }
      parent = child;
    }
    if (!parent->isTable()) {
      return fail("key path crosses a non-table value");
    }
    if (parent->get(path.back()) != nullptr) {
      return fail("duplicate key '" + path.back() + "'");
    }
    parent->set(path.back(), std::move(value));
    return true;
  }

  std::optional<Value> parseNumber() {
    const std::size_t start = position_;
    bool negative = false;
    if (peek() == '+' || peek() == '-') {
      negative = peek() == '-';
      advance();
    }
    if (startsWith("inf") && !isBareKeyCharacter(peek(3))) {
      advance(3);
      const double value = std::numeric_limits<double>::infinity();
      return Value::makeFloat(negative ? -value : value);
    }
    if (startsWith("nan") && !isBareKeyCharacter(peek(3))) {
      advance(3);
      return Value::makeFloat(std::numeric_limits<double>::quiet_NaN());
    }
    if (peek() == '0' &&
        (peek(1) == 'x' || peek(1) == 'o' || peek(1) == 'b')) {
      const char marker = peek(1);
      const int base = marker == 'x' ? 16 : (marker == 'o' ? 8 : 2);
      advance(2);
      const std::size_t digitsBegin = position_;
      std::uint64_t value = 0;
      bool sawDigit = false;
      while (!eof()) {
        const char character = peek();
        if (character == '_') {
          advance();
          continue;
        }
        const int digit = hexValue(character);
        if (digit < 0 || digit >= base) {
          break;
        }
        sawDigit = true;
        if (value >
            (std::numeric_limits<std::uint64_t>::max() -
             static_cast<std::uint64_t>(digit)) /
                static_cast<std::uint64_t>(base)) {
          fail("integer literal is too large");
          return std::nullopt;
        }
        value = value * static_cast<std::uint64_t>(base) +
                static_cast<std::uint64_t>(digit);
        advance();
      }
      if (!sawDigit || position_ == digitsBegin) {
        fail("invalid integer literal");
        return std::nullopt;
      }
      if (isBareKeyCharacter(peek())) {
        fail("invalid character in integer literal");
        return std::nullopt;
      }
      if (value > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
        fail("integer literal is too large");
        return std::nullopt;
      }
      const std::int64_t signedValue = static_cast<std::int64_t>(value);
      return Value::makeInteger(negative ? -signedValue : signedValue);
    }

    bool isFloat = false;
    bool sawDigit = false;
    while (!eof() && (isDigit(peek()) || peek() == '_')) {
      if (isDigit(peek())) {
        sawDigit = true;
      }
      advance();
    }
    if (peek() == '.') {
      isFloat = true;
      advance();
      while (!eof() && (isDigit(peek()) || peek() == '_')) {
        if (isDigit(peek())) {
          sawDigit = true;
        }
        advance();
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      isFloat = true;
      advance();
      if (peek() == '+' || peek() == '-') {
        advance();
      }
      bool exponentDigit = false;
      while (!eof() && (isDigit(peek()) || peek() == '_')) {
        if (isDigit(peek())) {
          exponentDigit = true;
        }
        advance();
      }
      if (!exponentDigit) {
        fail("invalid exponent in number");
        return std::nullopt;
      }
    }
    if (!sawDigit) {
      position_ = start;
      fail("expected a value");
      return std::nullopt;
    }
    if (isBareKeyCharacter(peek())) {
      fail("invalid character in number");
      return std::nullopt;
    }
    std::string token(text_.substr(start, position_ - start));
    token.erase(std::remove(token.begin(), token.end(), '_'), token.end());
    if (isFloat) {
      char* end = nullptr;
      const double value = std::strtod(token.c_str(), &end);
      if (end == token.c_str() || *end != '\0') {
        fail("invalid float literal");
        return std::nullopt;
      }
      return Value::makeFloat(value);
    }
    char* end = nullptr;
    const long long value = std::strtoll(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0') {
      fail("invalid integer literal");
      return std::nullopt;
    }
    return Value::makeInteger(static_cast<std::int64_t>(value));
  }

  bool parseKeyValue() {
    std::optional<std::vector<std::string>> path = parseKeyPath();
    if (!path.has_value()) {
      return false;
    }
    skipInlineSpace();
    if (peek() != '=') {
      return fail("expected '=' after key '" + path->back() + "'");
    }
    advance();
    skipInlineSpace();
    std::optional<Value> value = parseValue();
    if (!value.has_value()) {
      return false;
    }
    std::vector<std::string> fullPath = currentPath_;
    for (std::size_t i = 0; i + 1 < path->size(); ++i) {
      fullPath.push_back((*path)[i]);
    }
    Value* table = resolve(root_, fullPath, true);
    if (table == nullptr) {
      return false;
    }
    if (!table->isTable()) {
      return fail("key path crosses a non-table value");
    }
    if (table->get(path->back()) != nullptr) {
      return fail("duplicate key '" + path->back() + "'");
    }
    table->set(path->back(), std::move(*value));
    return expectLineEnd();
  }

  bool parseTableHeader() {
    advance();  // '['
    const bool arrayOfTables = peek() == '[';
    if (arrayOfTables) {
      advance();
    }
    skipInlineSpace();
    std::optional<std::vector<std::string>> path = parseKeyPath();
    if (!path.has_value()) {
      return false;
    }
    skipInlineSpace();
    if (peek() != ']') {
      return fail("expected ']' in table header");
    }
    advance();
    if (arrayOfTables) {
      if (peek() != ']') {
        return fail("expected ']]' in array-of-tables header");
      }
      advance();
    }
    if (!expectLineEnd()) {
      return false;
    }
    if (arrayOfTables) {
      std::vector<std::string> parentPath(path->begin(), path->end() - 1);
      Value* parent = resolve(root_, parentPath, true);
      if (parent == nullptr) {
        return false;
      }
      if (parent->isArray()) {
        if (parent->elements().empty() ||
            !parent->elements().back().isTable()) {
          return fail("cannot extend an array-of-tables element");
        }
        parent = &parent->elements().back();
      }
      if (!parent->isTable()) {
        return fail("array-of-tables parent is not a table");
      }
      Value* array = parent->get(path->back());
      if (array == nullptr) {
        parent->set(path->back(), Value::makeArray());
        array = parent->get(path->back());
      }
      if (!array->isArray()) {
        return fail("'" + path->back() + "' is not an array of tables");
      }
      array->push(Value::makeTable());
      currentPath_ = *path;
      currentPath_.push_back(
          "#" + std::to_string(array->elements().size() - 1));
      return true;
    }
    Value* table = resolve(root_, *path, true);
    if (table == nullptr) {
      return false;
    }
    if (!table->isTable()) {
      return fail("table header does not name a table");
    }
    currentPath_ = *path;
    return true;
  }

  std::string_view text_;
  std::size_t position_ = 0;
  std::size_t errorPosition_ = 0;
  std::string error_;
  Value root_;
  std::vector<std::string> currentPath_;
};

std::string quoteKey(std::string_view key) {
  bool bare = !key.empty();
  for (const char character : key) {
    if (!isBareKeyCharacter(character)) {
      bare = false;
      break;
    }
  }
  if (bare) {
    return std::string(key);
  }
  std::string result = "\"";
  for (const char character : key) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  result.push_back('"');
  return result;
}

std::string formatPath(const std::vector<std::string>& path) {
  std::string result;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      result.push_back('.');
    }
    result += quoteKey(path[i]);
  }
  return result;
}

std::string quoteString(std::string_view text) {
  std::string result = "\"";
  for (const char character : text) {
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\r':
        result += "\\r";
        break;
      default:
        if (static_cast<unsigned char>(character) < 0x20) {
          std::ostringstream escape;
          escape << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(
                        static_cast<unsigned char>(character));
          result += escape.str();
        } else {
          result.push_back(character);
        }
        break;
    }
  }
  result.push_back('"');
  return result;
}

std::string formatFloat(double value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value < 0 ? "-inf" : "inf";
  }
  std::ostringstream out;
  out << std::setprecision(17) << value;
  std::string text = out.str();
  if (text.find('.') == std::string::npos && text.find('e') == std::string::npos &&
      text.find('E') == std::string::npos) {
    text += ".0";
  }
  return text;
}

bool isTableArray(const Value& value) {
  if (!value.isArray() || value.elements().empty()) {
    return false;
  }
  for (const Value& element : value.elements()) {
    if (!element.isTable()) {
      return false;
    }
  }
  return true;
}

std::string formatInline(const Value& value);

std::string formatInline(const Value& value) {
  switch (value.type()) {
    case Value::Type::String:
      return quoteString(value.asString());
    case Value::Type::Integer:
      return std::to_string(value.asInteger());
    case Value::Type::Float:
      return formatFloat(value.asFloat());
    case Value::Type::Boolean:
      return value.asBoolean() ? "true" : "false";
    case Value::Type::Array: {
      std::string result = "[";
      for (std::size_t i = 0; i < value.elements().size(); ++i) {
        if (i != 0) {
          result += ", ";
        }
        result += formatInline(value.elements()[i]);
      }
      result += "]";
      return result;
    }
    case Value::Type::Table:
      return "{}";
  }
  return "{}";
}

void writeTable(std::ostringstream& out, const Value& table,
                const std::vector<std::string>& path, bool root) {
  std::vector<std::pair<std::string, const Value*>> nested;
  for (std::size_t i = 0; i < table.keys().size(); ++i) {
    const std::string& key = table.keys()[i];
    const Value& value = table.values()[i];
    if (value.isTable() || isTableArray(value)) {
      nested.emplace_back(key, &value);
      continue;
    }
    if (!root) {
      // Nothing to do; the header was written by the caller.
    }
    out << quoteKey(key) << " = " << formatInline(value) << "\n";
  }
  for (const auto& [key, child] : nested) {
    std::vector<std::string> childPath = path;
    childPath.push_back(key);
    if (child->isTable()) {
      if (!out.str().empty()) {
        out << "\n";
      }
      out << "[" << formatPath(childPath) << "]\n";
      writeTable(out, *child, childPath, false);
    } else {
      for (const Value& element : child->elements()) {
        if (!out.str().empty()) {
          out << "\n";
        }
        out << "[[" << formatPath(childPath) << "]]\n";
        writeTable(out, element, childPath, false);
      }
    }
  }
}

}  // namespace

Value Value::makeTable() {
  Value value;
  value.type_ = Type::Table;
  return value;
}

Value Value::makeArray() {
  Value value;
  value.type_ = Type::Array;
  return value;
}

Value Value::makeString(std::string text) {
  Value value;
  value.type_ = Type::String;
  value.string_ = std::move(text);
  return value;
}

Value Value::makeInteger(std::int64_t integer) {
  Value value;
  value.type_ = Type::Integer;
  value.integer_ = integer;
  return value;
}

Value Value::makeFloat(double floating) {
  Value value;
  value.type_ = Type::Float;
  value.float_ = floating;
  return value;
}

Value Value::makeBoolean(bool boolean) {
  Value value;
  value.type_ = Type::Boolean;
  value.boolean_ = boolean;
  return value;
}

const Value* Value::get(std::string_view key) const {
  if (!isTable()) {
    return nullptr;
  }
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      return &values_[i];
    }
  }
  return nullptr;
}

Value* Value::get(std::string_view key) {
  if (!isTable()) {
    return nullptr;
  }
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      return &values_[i];
    }
  }
  return nullptr;
}

const Value* Value::find(std::string_view dotted) const {
  const Value* current = this;
  std::size_t begin = 0;
  while (begin <= dotted.size()) {
    const std::size_t end = dotted.find('.', begin);
    const std::string_view segment =
        dotted.substr(begin, end == std::string_view::npos
                                 ? std::string_view::npos
                                 : end - begin);
    current = current->get(segment);
    if (current == nullptr) {
      return nullptr;
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return current;
}

Value* Value::find(std::string_view dotted) {
  return const_cast<Value*>(
      static_cast<const Value*>(this)->find(dotted));
}

std::optional<std::string> Value::string(std::string_view key) const {
  const Value* child = get(key);
  if (child == nullptr || !child->isString()) {
    return std::nullopt;
  }
  return child->asString();
}

std::optional<std::int64_t> Value::integer(std::string_view key) const {
  const Value* child = get(key);
  if (child == nullptr || !child->isInteger()) {
    return std::nullopt;
  }
  return child->asInteger();
}

std::optional<double> Value::number(std::string_view key) const {
  const Value* child = get(key);
  if (child == nullptr) {
    return std::nullopt;
  }
  if (child->isFloat()) {
    return child->asFloat();
  }
  if (child->isInteger()) {
    return static_cast<double>(child->asInteger());
  }
  return std::nullopt;
}

std::optional<bool> Value::boolean(std::string_view key) const {
  const Value* child = get(key);
  if (child == nullptr || !child->isBoolean()) {
    return std::nullopt;
  }
  return child->asBoolean();
}

std::optional<std::vector<std::string>> Value::stringArray(
    std::string_view key) const {
  const Value* child = get(key);
  if (child == nullptr || !child->isArray()) {
    return std::nullopt;
  }
  std::vector<std::string> result;
  result.reserve(child->elements().size());
  for (const Value& element : child->elements()) {
    if (!element.isString()) {
      return std::nullopt;
    }
    result.push_back(element.asString());
  }
  return result;
}

void Value::set(std::string_view key, Value value) {
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      values_[i] = std::move(value);
      return;
    }
  }
  keys_.emplace_back(key);
  values_.push_back(std::move(value));
}

void Value::push(Value value) { array_.push_back(std::move(value)); }

std::string Value::typeName() const {
  switch (type_) {
    case Type::Table:
      return "table";
    case Type::Array:
      return "array";
    case Type::String:
      return "string";
    case Type::Integer:
      return "integer";
    case Type::Float:
      return "float";
    case Type::Boolean:
      return "boolean";
  }
  return "unknown";
}

ParseResult parse(std::string_view text) {
  ParseResult result;
  Parser parser(text);
  std::optional<Value> root = parser.parseDocument();
  if (!root.has_value()) {
    result.ok = false;
    result.error.line = parser.errorLine();
    result.error.column = parser.errorColumn();
    result.error.message = parser.error().empty() ? "invalid TOML"
                                                  : parser.error();
    return result;
  }
  result.ok = true;
  result.root = std::move(*root);
  return result;
}

std::optional<Value> parse(std::string_view text, std::string& error) {
  ParseResult result = parse(text);
  if (!result.ok) {
    error = "line " + std::to_string(result.error.line) + ", column " +
            std::to_string(result.error.column) + ": " + result.error.message;
    return std::nullopt;
  }
  return std::move(result.root);
}

std::string write(const Value& root) {
  std::ostringstream out;
  writeTable(out, root, {}, true);
  return out.str();
}

}  // namespace coord::toml
