#pragma once

// A small, dependency-free TOML reader/writer covering the subset used by
// Coordination projects:
//
//   * comments, blank lines, bare and quoted keys, dotted keys;
//   * basic/literal strings, multi-line strings, escapes;
//   * integers (decimal/hex/octal/binary), floats, booleans;
//   * arrays (including multi-line and trailing commas);
//   * inline tables, `[table]` and `[[array.of.tables]]`.
//
// Dates and times are not used by the toolchain and are rejected with a clear
// error. The writer emits canonical TOML from a `Value` tree; it is used for
// generated configuration and lock files.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coord::toml {

class Value {
 public:
  enum class Type { Table, Array, String, Integer, Float, Boolean };

  Value() = default;

  [[nodiscard]] static Value makeTable();
  [[nodiscard]] static Value makeArray();
  [[nodiscard]] static Value makeString(std::string text);
  [[nodiscard]] static Value makeInteger(std::int64_t value);
  [[nodiscard]] static Value makeFloat(double value);
  [[nodiscard]] static Value makeBoolean(bool value);

  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool isTable() const { return type_ == Type::Table; }
  [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
  [[nodiscard]] bool isString() const { return type_ == Type::String; }
  [[nodiscard]] bool isInteger() const { return type_ == Type::Integer; }
  [[nodiscard]] bool isFloat() const { return type_ == Type::Float; }
  [[nodiscard]] bool isBoolean() const { return type_ == Type::Boolean; }

  [[nodiscard]] const std::string& asString() const { return string_; }
  [[nodiscard]] std::int64_t asInteger() const { return integer_; }
  [[nodiscard]] double asFloat() const { return float_; }
  [[nodiscard]] bool asBoolean() const { return boolean_; }

  [[nodiscard]] const std::vector<Value>& elements() const { return array_; }
  [[nodiscard]] std::vector<Value>& elements() { return array_; }
  [[nodiscard]] const std::vector<std::string>& keys() const { return keys_; }
  [[nodiscard]] const std::vector<Value>& values() const { return values_; }

  /// Direct child lookup; returns nullptr when missing or when this is not a
  /// table.
  [[nodiscard]] const Value* get(std::string_view key) const;
  [[nodiscard]] Value* get(std::string_view key);
  /// Dotted lookup: `find("tools.format")`.
  [[nodiscard]] const Value* find(std::string_view dotted) const;
  [[nodiscard]] Value* find(std::string_view dotted);

  [[nodiscard]] std::optional<std::string> string(std::string_view key) const;
  [[nodiscard]] std::optional<std::int64_t> integer(std::string_view key) const;
  [[nodiscard]] std::optional<double> number(std::string_view key) const;
  [[nodiscard]] std::optional<bool> boolean(std::string_view key) const;
  [[nodiscard]] std::optional<std::vector<std::string>> stringArray(
      std::string_view key) const;

  /// Insert or replace a key. Only valid for tables.
  void set(std::string_view key, Value value);
  /// Append an element. Only valid for arrays.
  void push(Value value);

  [[nodiscard]] std::string typeName() const;

 private:
  Type type_ = Type::Table;
  std::string string_;
  std::int64_t integer_ = 0;
  double float_ = 0.0;
  bool boolean_ = false;
  std::vector<Value> array_;
  std::vector<std::string> keys_;
  std::vector<Value> values_;
};

struct ParseError {
  std::size_t line = 0;
  std::size_t column = 0;
  std::string message;
};

struct ParseResult {
  bool ok = false;
  Value root;
  ParseError error;
};

/// Parses a complete document. On failure `error.line`/`column` are 1-based.
[[nodiscard]] ParseResult parse(std::string_view text);

/// Serialises a value tree as canonical TOML (tables become sections).
[[nodiscard]] std::string write(const Value& root);

/// Convenience wrapper returning the error message.
[[nodiscard]] std::optional<Value> parse(std::string_view text,
                                         std::string& error);

}  // namespace coord::toml
