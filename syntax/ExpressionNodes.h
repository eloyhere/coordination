#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "SyntaxKind.h"
#include "SyntaxNode.h"
#include "SyntaxSpan.h"

namespace coordination::syntax
{
    // 表达式类节点的公共基类，便于用 child_as<ExpressionSyntax>() 做整体判断。
    class ExpressionSyntax : public SyntaxNode
    {
    protected:
        explicit ExpressionSyntax(SyntaxKind kind, SyntaxSpan span = {}) noexcept
            : SyntaxNode(kind, span)
        {
        }
    };

    enum class LiteralKind
    {
        number,
        string,
        boolean,
        null_value,
    };

    // 字面量：1、3.14、"text"、true、false、null。
    class LiteralExpression final : public ExpressionSyntax
    {
    public:
        LiteralExpression(LiteralKind literal_kind, std::string literal_text, SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::LiteralExpression, span)
            , literal_kind_(literal_kind)
            , text_(std::move(literal_text))
        {
        }

        LiteralKind literal_kind() const noexcept
        {
            return literal_kind_;
        }

        std::string_view text() const noexcept override
        {
            return text_;
        }

    private:
        LiteralKind literal_kind_;
        std::string text_;
    };

    // 标识符引用。
    class IdentifierExpression final : public ExpressionSyntax
    {
    public:
        explicit IdentifierExpression(std::string name, SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::IdentifierExpression, span)
            , name_(std::move(name))
        {
        }

        std::string_view name() const noexcept
        {
            return name_;
        }

        std::string_view text() const noexcept override
        {
            return name_;
        }

    private:
        std::string name_;
    };

    enum class UnaryOperator
    {
        plus,
        negate,
        logical_not,
        bitwise_not,
    };

    constexpr std::string_view to_string(UnaryOperator op) noexcept
    {
        switch (op)
        {
        case UnaryOperator::plus: return "+";
        case UnaryOperator::negate: return "-";
        case UnaryOperator::logical_not: return "!";
        case UnaryOperator::bitwise_not: return "~";
        }
        return "?";
    }

    // 一元表达式：子节点 0 为操作数。
    class UnaryExpression final : public ExpressionSyntax
    {
    public:
        UnaryExpression(UnaryOperator op, std::unique_ptr<SyntaxNode> operand, SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::UnaryExpression, span)
            , op_(op)
        {
            add_child(std::move(operand));
        }

        UnaryOperator op() const noexcept
        {
            return op_;
        }

        const SyntaxNode& operand() const noexcept
        {
            return child(0);
        }

        std::string_view text() const noexcept override
        {
            return to_string(op_);
        }

    private:
        UnaryOperator op_;
    };

    enum class BinaryOperator
    {
        add,
        subtract,
        multiply,
        divide,
        modulo,
        equal,
        not_equal,
        less,
        less_equal,
        greater,
        greater_equal,
        logical_and,
        logical_or,
    };

    constexpr std::string_view to_string(BinaryOperator op) noexcept
    {
        switch (op)
        {
        case BinaryOperator::add: return "+";
        case BinaryOperator::subtract: return "-";
        case BinaryOperator::multiply: return "*";
        case BinaryOperator::divide: return "/";
        case BinaryOperator::modulo: return "%";
        case BinaryOperator::equal: return "==";
        case BinaryOperator::not_equal: return "!=";
        case BinaryOperator::less: return "<";
        case BinaryOperator::less_equal: return "<=";
        case BinaryOperator::greater: return ">";
        case BinaryOperator::greater_equal: return ">=";
        case BinaryOperator::logical_and: return "&&";
        case BinaryOperator::logical_or: return "||";
        }
        return "?";
    }

    // 二元表达式：子节点 0 为左操作数，1 为右操作数。
    class BinaryExpression final : public ExpressionSyntax
    {
    public:
        BinaryExpression(BinaryOperator op,
                         std::unique_ptr<SyntaxNode> left_operand,
                         std::unique_ptr<SyntaxNode> right_operand,
                         SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::BinaryExpression, span)
            , op_(op)
        {
            add_child(std::move(left_operand));
            add_child(std::move(right_operand));
        }

        BinaryOperator op() const noexcept
        {
            return op_;
        }

        const SyntaxNode& left() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& right() const noexcept
        {
            return child(1);
        }

        std::string_view text() const noexcept override
        {
            return to_string(op_);
        }

    private:
        BinaryOperator op_;
    };

    // 赋值表达式：子节点 0 为目标，1 为值。
    class AssignmentExpression final : public ExpressionSyntax
    {
    public:
        AssignmentExpression(std::unique_ptr<SyntaxNode> target,
                             std::unique_ptr<SyntaxNode> assigned_value,
                             SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::AssignmentExpression, span)
        {
            add_child(std::move(target));
            add_child(std::move(assigned_value));
        }

        const SyntaxNode& target() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& assigned_value() const noexcept
        {
            return child(1);
        }

        std::string_view text() const noexcept override
        {
            return "=";
        }
    };

    // 调用表达式：子节点 0 为被调用者，其余为实参。
    class CallExpression final : public ExpressionSyntax
    {
    public:
        explicit CallExpression(std::unique_ptr<SyntaxNode> callee, SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::CallExpression, span)
        {
            add_child(std::move(callee));
        }

        const SyntaxNode& callee() const noexcept
        {
            return child(0);
        }

        std::size_t argument_count() const noexcept
        {
            return child_count() - 1;
        }

        const SyntaxNode& argument(std::size_t i) const noexcept
        {
            return child(i + 1);
        }

        void add_argument(std::unique_ptr<SyntaxNode> argument_node)
        {
            add_child(std::move(argument_node));
        }
    };

    // 成员访问：obj.name，子节点 0 为 obj。
    class MemberAccessExpression final : public ExpressionSyntax
    {
    public:
        MemberAccessExpression(std::unique_ptr<SyntaxNode> target, std::string member_name, SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::MemberAccessExpression, span)
            , member_(std::move(member_name))
        {
            add_child(std::move(target));
        }

        const SyntaxNode& target() const noexcept
        {
            return child(0);
        }

        std::string_view member() const noexcept
        {
            return member_;
        }

        std::string_view text() const noexcept override
        {
            return member_;
        }

    private:
        std::string member_;
    };

    // 下标访问：obj[index]，子节点 0 为 obj，1 为下标。
    class IndexExpression final : public ExpressionSyntax
    {
    public:
        IndexExpression(std::unique_ptr<SyntaxNode> target,
                        std::unique_ptr<SyntaxNode> subscript,
                        SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::IndexExpression, span)
        {
            add_child(std::move(target));
            add_child(std::move(subscript));
        }

        const SyntaxNode& target() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& subscript() const noexcept
        {
            return child(1);
        }
    };

    // 三目表达式：cond ? when_true : when_false。
    class ConditionalExpression final : public ExpressionSyntax
    {
    public:
        ConditionalExpression(std::unique_ptr<SyntaxNode> condition,
                              std::unique_ptr<SyntaxNode> when_true,
                              std::unique_ptr<SyntaxNode> when_false,
                              SyntaxSpan span = {})
            : ExpressionSyntax(SyntaxKind::ConditionalExpression, span)
        {
            add_child(std::move(condition));
            add_child(std::move(when_true));
            add_child(std::move(when_false));
        }

        const SyntaxNode& condition() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& when_true() const noexcept
        {
            return child(1);
        }

        const SyntaxNode& when_false() const noexcept
        {
            return child(2);
        }
    };
}
