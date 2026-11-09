#pragma once

// ============================================================================
// 编译期常量表达式求值器（comptime MVP）
//
// 输入是 Parser 生成的表达式子树；输出带类型的值或首个错误。支持：
//   * 整数字面量：十进制、0x/0X 十六进制、0b/0B 二进制，允许 '_' 分隔；
//   * 布尔与字符串字面量、括号、标识符（从构造时传入的常量表查找）；
//   * 一元 + - ! ~；
//   * 二元 + - * / % << >> & | ^ == != < <= > >= && ||；
//   * 三目 ?:、文本 + 拼接、&&/|| 短路、三目只求值被选中的分支。
//
// 规则与边界：
//   * 整数使用 int64，所有算术、除法与左移都做溢出检查；
//   * 除零、移位次数越界、类型不匹配都返回 ok=false 与中文错误；
//   * 每一步访问都会消耗步数，每次递归都会消耗深度；超限即报预算错误；
//   * 错误只保留第一个，不抛异常；EvalResult::steps 记录实际消耗的步数。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "Ast.h"

namespace coordination::compiler
{
    enum class ValueKind { integer, boolean, text, invalid };

    struct ConstValue
    {
        ValueKind kind = ValueKind::invalid;
        std::int64_t integer = 0;
        bool boolean = false;
        std::string text;

        bool valid() const noexcept
        {
            return kind != ValueKind::invalid;
        }
    };

    struct EvalLimits
    {
        std::size_t steps = 100000;
        std::size_t depth = 256;
    };

    struct EvalResult
    {
        ConstValue value;
        bool ok = false;
        std::string error;
        std::size_t steps = 0;
    };

    class ConstEvaluator
    {
    public:
        explicit ConstEvaluator(std::map<std::string, ConstValue> constants = {}, EvalLimits limits = {})
            : constants_(std::move(constants)),
              limits_(limits)
        {
        }

        EvalResult evaluate(const AstNode& expression);

    private:
        std::map<std::string, ConstValue> constants_;
        EvalLimits limits_;

        static std::string_view kind_text(ValueKind kind) noexcept
        {
            switch (kind)
            {
            case ValueKind::integer:
                return "整数";
            case ValueKind::boolean:
                return "布尔";
            case ValueKind::text:
                return "文本";
            case ValueKind::invalid:
            default:
                return "无效值";
            }
        }

        static ConstValue make_integer(std::int64_t value)
        {
            ConstValue result;
            result.kind = ValueKind::integer;
            result.integer = value;
            return result;
        }

        static ConstValue make_boolean(bool value)
        {
            ConstValue result;
            result.kind = ValueKind::boolean;
            result.boolean = value;
            return result;
        }

        static ConstValue make_text(std::string value)
        {
            ConstValue result;
            result.kind = ValueKind::text;
            result.text = std::move(value);
            return result;
        }

        static int digit_value(char character, int base) noexcept
        {
            int value = -1;
            if (character >= '0' && character <= '9')
            {
                value = character - '0';
            }
            else if (character >= 'a' && character <= 'f')
            {
                value = character - 'a' + 10;
            }
            else if (character >= 'A' && character <= 'F')
            {
                value = character - 'A' + 10;
            }
            if (value < 0 || value >= base)
            {
                return -1;
            }
            return value;
        }

        // 解析非负整数字面量；超出 int64 正数上限时报溢出。
        static bool parse_integer_literal(std::string_view text, std::int64_t& out, std::string& error)
        {
            int base = 10;
            std::size_t index = 0;
            if (text.size() >= 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                base = 16;
                index = 2u;
            }
            else if (text.size() >= 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
            {
                base = 2;
                index = 2u;
            }

            const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            const std::uint64_t base_value = static_cast<std::uint64_t>(base);
            std::uint64_t value = 0;
            std::size_t digits = 0;
            for (; index < text.size(); ++index)
            {
                if (text[index] == '_')
                {
                    continue;
                }

                const int digit = digit_value(text[index], base);
                if (digit < 0)
                {
                    error = "非法整数字面量: ";
                    error.append(text.data(), text.size());
                    return false;
                }

                ++digits;
                const std::uint64_t digit_value_unsigned = static_cast<std::uint64_t>(digit);
                if (value > (limit - digit_value_unsigned) / base_value)
                {
                    error = "整数溢出";
                    return false;
                }
                value = value * base_value + digit_value_unsigned;
            }

            if (digits == 0u)
            {
                error = "非法整数字面量: ";
                error.append(text.data(), text.size());
                return false;
            }

            out = static_cast<std::int64_t>(value);
            return true;
        }

        // 把带引号的字符串字面量解码成实际文本；支持 \" \\ \n \t \r \0 \'。
        static bool decode_string_literal(std::string_view text, std::string& out, std::string& error)
        {
            if (text.size() < 2u || text.front() != '"' || text.back() != '"')
            {
                error = "未闭合或格式非法的字符串字面量";
                return false;
            }

            std::string decoded;
            decoded.reserve(text.size() - 2u);
            for (std::size_t index = 1u; index + 1u < text.size(); ++index)
            {
                const char current = text[index];
                if (current == '"')
                {
                    error = "字符串字面量中出现未转义的双引号";
                    return false;
                }
                if (current != '\\')
                {
                    decoded.push_back(current);
                    continue;
                }

                if (index + 2u >= text.size())
                {
                    error = "字符串字面量以未完成的转义序列结尾";
                    return false;
                }

                const char escaped = text[++index];
                switch (escaped)
                {
                case '"':
                    decoded.push_back('"');
                    break;
                case '\'':
                    decoded.push_back('\'');
                    break;
                case '\\':
                    decoded.push_back('\\');
                    break;
                case 'n':
                    decoded.push_back('\n');
                    break;
                case 't':
                    decoded.push_back('\t');
                    break;
                case 'r':
                    decoded.push_back('\r');
                    break;
                case '0':
                    decoded.push_back('\0');
                    break;
                default:
                    error = "未知的字符串转义: \\";
                    error.push_back(escaped);
                    return false;
                }
            }

            out = std::move(decoded);
            return true;
        }

        static bool checked_add(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) noexcept
        {
            if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
                (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs))
            {
                return false;
            }
            out = lhs + rhs;
            return true;
        }

        static bool checked_sub(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) noexcept
        {
            if ((rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs) ||
                (rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs))
            {
                return false;
            }
            out = lhs - rhs;
            return true;
        }

        // 取绝对值到无符号域，确保 INT64_MIN 也能安全参与乘法检查。
        static std::uint64_t magnitude(std::int64_t value) noexcept
        {
            if (value >= 0)
            {
                return static_cast<std::uint64_t>(value);
            }
            return static_cast<std::uint64_t>(-(value + 1)) + 1u;
        }

        static bool checked_mul(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) noexcept
        {
            if (lhs == 0 || rhs == 0)
            {
                out = 0;
                return true;
            }

            const bool negative = (lhs < 0) != (rhs < 0);
            const std::uint64_t lhs_magnitude = magnitude(lhs);
            const std::uint64_t rhs_magnitude = magnitude(rhs);
            const std::uint64_t positive_limit =
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            const std::uint64_t negative_limit = positive_limit + 1u;
            const std::uint64_t limit = negative ? negative_limit : positive_limit;
            if (lhs_magnitude > limit / rhs_magnitude)
            {
                return false;
            }

            const std::uint64_t product = lhs_magnitude * rhs_magnitude;
            if (!negative)
            {
                out = static_cast<std::int64_t>(product);
                return true;
            }
            if (product == negative_limit)
            {
                out = std::numeric_limits<std::int64_t>::min();
                return true;
            }
            out = -static_cast<std::int64_t>(product);
            return true;
        }

        static constexpr std::int64_t integer_bits = 64;

        struct EvaluationState
        {
            const ConstEvaluator& evaluator;
            std::size_t steps = 0;
            bool failed = false;
            std::string error;

            bool set_error(std::string message)
            {
                if (!failed)
                {
                    failed = true;
                    error = std::move(message);
                }
                return false;
            }

            bool charge_budget(std::size_t depth)
            {
                if (depth > evaluator.limits_.depth)
                {
                    return set_error("常量表达式超出求值预算（深度上限）");
                }
                if (steps >= evaluator.limits_.steps)
                {
                    return set_error("常量表达式超出求值预算（步数上限）");
                }
                ++steps;
                return true;
            }

            bool require_kind(const ConstValue& value, ValueKind expected, std::string_view context)
            {
                if (value.kind == expected)
                {
                    return true;
                }
                return set_error("类型不匹配：" + std::string(context) + "需要" +
                    std::string(kind_text(expected)) + "，实际为" + std::string(kind_text(value.kind)));
            }

            bool binary_type_error(std::string_view op, const ConstValue& left, const ConstValue& right)
            {
                return set_error("类型不匹配：运算符 '" + std::string(op) + "' 不支持 " +
                    std::string(kind_text(left.kind)) + " 与 " + std::string(kind_text(right.kind)) +
                    " 操作数");
            }

            bool require_integer_pair(std::string_view op, const ConstValue& left, const ConstValue& right)
            {
                if (left.kind == ValueKind::integer && right.kind == ValueKind::integer)
                {
                    return true;
                }
                return binary_type_error(op, left, right);
            }

            bool evaluate_child(const AstNode* child, std::size_t depth, ConstValue& out)
            {
                if (child == nullptr)
                {
                    return set_error("常量表达式缺少操作数");
                }
                return evaluate_node(*child, depth, out);
            }

            bool evaluate_literal(std::string_view text, ConstValue& out)
            {
                if (text == "true")
                {
                    out = ConstEvaluator::make_boolean(true);
                    return true;
                }
                if (text == "false")
                {
                    out = ConstEvaluator::make_boolean(false);
                    return true;
                }
                if (!text.empty() && text.front() == '"')
                {
                    std::string decoded;
                    std::string reason;
                    if (!ConstEvaluator::decode_string_literal(text, decoded, reason))
                    {
                        return set_error(std::move(reason));
                    }
                    out = ConstEvaluator::make_text(std::move(decoded));
                    return true;
                }
                if (!text.empty() && text.front() >= '0' && text.front() <= '9')
                {
                    std::int64_t value = 0;
                    std::string reason;
                    if (!ConstEvaluator::parse_integer_literal(text, value, reason))
                    {
                        return set_error(std::move(reason));
                    }
                    out = ConstEvaluator::make_integer(value);
                    return true;
                }
                return set_error("不支持的常量字面量: " + std::string(text));
            }

            bool evaluate_identifier(std::string_view text, ConstValue& out)
            {
                const std::string name(text);
                const auto found = evaluator.constants_.find(name);
                if (found == evaluator.constants_.end())
                {
                    return set_error("未知常量: " + name);
                }
                if (!found->second.valid())
                {
                    return set_error("常量 '" + name + "' 不是有效的编译期常量");
                }
                out = found->second;
                return true;
            }

            bool evaluate_paren(const AstNode& node, std::size_t depth, ConstValue& out)
            {
                if (node.child_count() != 1u)
                {
                    return set_error("括号表达式缺少内部表达式");
                }
                return evaluate_child(node.child(0), depth + 1u, out);
            }

            bool evaluate_unary(const AstNode& node, std::size_t depth, ConstValue& out)
            {
                if (node.child_count() != 1u)
                {
                    return set_error("一元表达式需要恰好一个操作数");
                }

                ConstValue operand;
                if (!evaluate_child(node.child(0), depth + 1u, operand))
                {
                    return false;
                }

                const std::string& op = node.text;
                if (op == "+")
                {
                    if (!require_kind(operand, ValueKind::integer, "一元 '+' 的操作数"))
                    {
                        return false;
                    }
                    out = operand;
                    return true;
                }
                if (op == "-")
                {
                    if (!require_kind(operand, ValueKind::integer, "一元 '-' 的操作数"))
                    {
                        return false;
                    }
                    if (operand.integer == std::numeric_limits<std::int64_t>::min())
                    {
                        return set_error("整数溢出（一元取负）");
                    }
                    out = ConstEvaluator::make_integer(-operand.integer);
                    return true;
                }
                if (op == "!")
                {
                    if (!require_kind(operand, ValueKind::boolean, "一元 '!' 的操作数"))
                    {
                        return false;
                    }
                    out = ConstEvaluator::make_boolean(!operand.boolean);
                    return true;
                }
                if (op == "~")
                {
                    if (!require_kind(operand, ValueKind::integer, "一元 '~' 的操作数"))
                    {
                        return false;
                    }
                    out = ConstEvaluator::make_integer(~operand.integer);
                    return true;
                }
                return set_error("不支持的一元运算符: '" + op + "'");
            }

            bool evaluate_logical(const AstNode& node, std::size_t depth, bool is_and, ConstValue& out)
            {
                ConstValue left;
                if (!evaluate_child(node.child(0), depth + 1u, left))
                {
                    return false;
                }
                if (!require_kind(left, ValueKind::boolean, "逻辑运算符的操作数"))
                {
                    return false;
                }

                if (is_and && !left.boolean)
                {
                    out = ConstEvaluator::make_boolean(false);
                    return true;
                }
                if (!is_and && left.boolean)
                {
                    out = ConstEvaluator::make_boolean(true);
                    return true;
                }

                ConstValue right;
                if (!evaluate_child(node.child(1), depth + 1u, right))
                {
                    return false;
                }
                if (!require_kind(right, ValueKind::boolean, "逻辑运算符的操作数"))
                {
                    return false;
                }
                out = ConstEvaluator::make_boolean(right.boolean);
                return true;
            }

            bool evaluate_shift(std::string_view op, const ConstValue& left, const ConstValue& right,
                ConstValue& out)
            {
                if (right.integer < 0 || right.integer >= ConstEvaluator::integer_bits)
                {
                    return set_error("移位越界：移位次数必须在 0 到 63 之间");
                }

                const unsigned int amount = static_cast<unsigned int>(right.integer);
                if (op == ">>")
                {
                    // 负数采用算术右移，避免依赖实现定义的行为。
                    if (left.integer < 0)
                    {
                        out = ConstEvaluator::make_integer(~(~left.integer >> amount));
                    }
                    else
                    {
                        out = ConstEvaluator::make_integer(left.integer >> amount);
                    }
                    return true;
                }

                // 左移按乘以 2 的幂逐次检查，-1 << 63 恰好等于 INT64_MIN 时仍然合法。
                std::int64_t shifted = left.integer;
                for (std::int64_t iteration = 0; iteration < right.integer; ++iteration)
                {
                    std::int64_t next = 0;
                    if (!ConstEvaluator::checked_add(shifted, shifted, next))
                    {
                        return set_error("整数移位溢出");
                    }
                    shifted = next;
                }
                out = ConstEvaluator::make_integer(shifted);
                return true;
            }

            bool evaluate_binary(const AstNode& node, std::size_t depth, ConstValue& out)
            {
                if (node.child_count() != 2u)
                {
                    return set_error("二元表达式需要恰好两个操作数");
                }

                const std::string& op = node.text;
                if (op == "&&" || op == "||")
                {
                    return evaluate_logical(node, depth, op == "&&", out);
                }

                ConstValue left;
                ConstValue right;
                if (!evaluate_child(node.child(0), depth + 1u, left) ||
                    !evaluate_child(node.child(1), depth + 1u, right))
                {
                    return false;
                }

                if (op == "+")
                {
                    if (left.kind == ValueKind::text && right.kind == ValueKind::text)
                    {
                        out = ConstEvaluator::make_text(left.text + right.text);
                        return true;
                    }
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    std::int64_t value = 0;
                    if (!ConstEvaluator::checked_add(left.integer, right.integer, value))
                    {
                        return set_error("整数加法溢出");
                    }
                    out = ConstEvaluator::make_integer(value);
                    return true;
                }

                if (op == "-")
                {
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    std::int64_t value = 0;
                    if (!ConstEvaluator::checked_sub(left.integer, right.integer, value))
                    {
                        return set_error("整数减法溢出");
                    }
                    out = ConstEvaluator::make_integer(value);
                    return true;
                }

                if (op == "*")
                {
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    std::int64_t value = 0;
                    if (!ConstEvaluator::checked_mul(left.integer, right.integer, value))
                    {
                        return set_error("整数乘法溢出");
                    }
                    out = ConstEvaluator::make_integer(value);
                    return true;
                }

                if (op == "/" || op == "%")
                {
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    if (right.integer == 0)
                    {
                        return set_error("整数除零");
                    }
                    if (left.integer == std::numeric_limits<std::int64_t>::min() && right.integer == -1)
                    {
                        return set_error(op == "/" ? "整数除法溢出" : "整数取模溢出");
                    }
                    out = ConstEvaluator::make_integer(op == "/" ? left.integer / right.integer
                                                                 : left.integer % right.integer);
                    return true;
                }

                if (op == "<<" || op == ">>")
                {
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    return evaluate_shift(op, left, right, out);
                }

                if (op == "&" || op == "|" || op == "^")
                {
                    if (!require_integer_pair(op, left, right))
                    {
                        return false;
                    }
                    std::int64_t value = 0;
                    if (op == "&")
                    {
                        value = left.integer & right.integer;
                    }
                    else if (op == "|")
                    {
                        value = left.integer | right.integer;
                    }
                    else
                    {
                        value = left.integer ^ right.integer;
                    }
                    out = ConstEvaluator::make_integer(value);
                    return true;
                }

                if (op == "==" || op == "!=")
                {
                    if (left.kind != right.kind)
                    {
                        return binary_type_error(op, left, right);
                    }
                    bool equal = false;
                    switch (left.kind)
                    {
                    case ValueKind::integer:
                        equal = left.integer == right.integer;
                        break;
                    case ValueKind::boolean:
                        equal = left.boolean == right.boolean;
                        break;
                    case ValueKind::text:
                        equal = left.text == right.text;
                        break;
                    case ValueKind::invalid:
                    default:
                        return binary_type_error(op, left, right);
                    }
                    out = ConstEvaluator::make_boolean(op == "==" ? equal : !equal);
                    return true;
                }

                if (op == "<" || op == "<=" || op == ">" || op == ">=")
                {
                    bool comparison = false;
                    if (left.kind == ValueKind::integer && right.kind == ValueKind::integer)
                    {
                        if (op == "<")
                        {
                            comparison = left.integer < right.integer;
                        }
                        else if (op == "<=")
                        {
                            comparison = left.integer <= right.integer;
                        }
                        else if (op == ">")
                        {
                            comparison = left.integer > right.integer;
                        }
                        else
                        {
                            comparison = left.integer >= right.integer;
                        }
                    }
                    else if (left.kind == ValueKind::text && right.kind == ValueKind::text)
                    {
                        if (op == "<")
                        {
                            comparison = left.text < right.text;
                        }
                        else if (op == "<=")
                        {
                            comparison = left.text <= right.text;
                        }
                        else if (op == ">")
                        {
                            comparison = left.text > right.text;
                        }
                        else
                        {
                            comparison = left.text >= right.text;
                        }
                    }
                    else
                    {
                        return binary_type_error(op, left, right);
                    }
                    out = ConstEvaluator::make_boolean(comparison);
                    return true;
                }

                return set_error("不支持的二元运算符: '" + op + "'");
            }

            bool evaluate_conditional(const AstNode& node, std::size_t depth, ConstValue& out)
            {
                if (node.child_count() != 3u)
                {
                    return set_error("三目表达式需要条件、真值分支与假值分支");
                }

                ConstValue condition;
                if (!evaluate_child(node.child(0), depth + 1u, condition))
                {
                    return false;
                }
                if (!require_kind(condition, ValueKind::boolean, "三目运算符的条件"))
                {
                    return false;
                }

                const std::size_t branch = condition.boolean ? 1u : 2u;
                return evaluate_child(node.child(branch), depth + 1u, out);
            }

            bool evaluate_node(const AstNode& node, std::size_t depth, ConstValue& out)
            {
                if (!charge_budget(depth))
                {
                    return false;
                }

                switch (node.kind)
                {
                case AstKind::LiteralExpr:
                    return evaluate_literal(node.text, out);
                case AstKind::IdentifierExpr:
                    return evaluate_identifier(node.text, out);
                case AstKind::ParenExpr:
                    return evaluate_paren(node, depth, out);
                case AstKind::UnaryExpr:
                    return evaluate_unary(node, depth, out);
                case AstKind::BinaryExpr:
                    return evaluate_binary(node, depth, out);
                case AstKind::ConditionalExpr:
                    return evaluate_conditional(node, depth, out);
                case AstKind::Error:
                    return set_error("常量表达式包含语法错误节点");
                default:
                    return set_error("不支持的常量表达式节点: " + std::string(ast_kind_name(node.kind)));
                }
            }
        };
    };

    inline EvalResult ConstEvaluator::evaluate(const AstNode& expression)
    {
        EvalResult result;
        EvaluationState state{*this};
        ConstValue value;
        if (state.evaluate_node(expression, 1u, value))
        {
            result.ok = true;
            result.value = std::move(value);
        }
        result.error = std::move(state.error);
        result.steps = state.steps;
        return result;
    }
}
