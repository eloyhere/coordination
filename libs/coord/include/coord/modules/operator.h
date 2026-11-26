#pragma once

#include <string_view>
#include <vector>

namespace coord {

struct OperatorInfo {
  std::string_view symbol;
  int precedence = 0;
  bool rightAssociative = false;
  bool overloadable = true;
};

/// Single source of truth for operator precedence and overloading policy.
/// Parser precedence and the `operator` declaration checks should both consult
/// this table.
inline const std::vector<OperatorInfo>& operatorTable() {
  static const std::vector<OperatorInfo> table = {
      {"=", 0, true, false},    {"+=", 0, true, false},
      {"-=", 0, true, false},   {"*=", 0, true, false},
      {"/=", 0, true, false},   {"%=", 0, true, false},
      {"&=", 0, true, false},   {"|=", 0, true, false},
      {"^=", 0, true, false},   {"||", 1, false, true},
      {"&&", 2, false, true},   {"|", 3, false, true},
      {"^", 4, false, true},    {"&", 5, false, true},
      {"==", 6, false, true},   {"!=", 6, false, true},
      {"<", 7, false, true},    {"<=", 7, false, true},
      {">", 7, false, true},    {">=", 7, false, true},
      {"+", 8, false, true},    {"-", 8, false, true},
      {"*", 9, false, true},    {"/", 9, false, true},
      {"%", 9, false, true},
  };
  return table;
}

inline const OperatorInfo* findOperator(std::string_view symbol) {
  for (const OperatorInfo& info : operatorTable()) {
    if (info.symbol == symbol) {
      return &info;
    }
  }
  return nullptr;
}

}  // namespace coord
