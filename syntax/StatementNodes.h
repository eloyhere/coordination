#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SyntaxKind.h"
#include "SyntaxNode.h"
#include "SyntaxSpan.h"

namespace coordination::syntax
{
    // 语句类节点的公共基类。
    class StatementSyntax : public SyntaxNode
    {
    protected:
        explicit StatementSyntax(SyntaxKind kind, SyntaxSpan span = {}) noexcept
            : SyntaxNode(kind, span)
        {
        }
    };

    // 表达式语句：表达式后接分号，子节点 0 为表达式。
    class ExpressionStatement final : public StatementSyntax
    {
    public:
        explicit ExpressionStatement(std::unique_ptr<SyntaxNode> expression, SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::ExpressionStatement, span)
        {
            add_child(std::move(expression));
        }

        const SyntaxNode& expression() const noexcept
        {
            return child(0);
        }
    };

    // 块语句：{ ... }，子节点依次为块内语句。
    class BlockStatement final : public StatementSyntax
    {
    public:
        explicit BlockStatement(SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::BlockStatement, span)
        {
        }

        void add_statement(std::unique_ptr<SyntaxNode> statement_node)
        {
            add_child(std::move(statement_node));
        }

        std::size_t statement_count() const noexcept
        {
            return child_count();
        }

        const SyntaxNode& statement(std::size_t i) const noexcept
        {
            return child(i);
        }
    };

    // if 语句：子节点 0 条件、1 真分支，有 else 时 2 为假分支。
    class IfStatement final : public StatementSyntax
    {
    public:
        IfStatement(std::unique_ptr<SyntaxNode> condition,
                    std::unique_ptr<SyntaxNode> then_branch,
                    std::unique_ptr<SyntaxNode> else_branch = nullptr,
                    SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::IfStatement, span)
        {
            add_child(std::move(condition));
            add_child(std::move(then_branch));
            if (else_branch)
            {
                add_child(std::move(else_branch));
            }
        }

        const SyntaxNode& condition() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& then_branch() const noexcept
        {
            return child(1);
        }

        bool has_else() const noexcept
        {
            return child_count() > 2;
        }

        const SyntaxNode& else_branch() const noexcept
        {
            return child(2);
        }
    };

    // while 语句：子节点 0 条件、1 循环体。
    class WhileStatement final : public StatementSyntax
    {
    public:
        WhileStatement(std::unique_ptr<SyntaxNode> condition, std::unique_ptr<SyntaxNode> body, SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::WhileStatement, span)
        {
            add_child(std::move(condition));
            add_child(std::move(body));
        }

        const SyntaxNode& condition() const noexcept
        {
            return child(0);
        }

        const SyntaxNode& body() const noexcept
        {
            return child(1);
        }
    };

    // for 语句：初始化、条件、步进、循环体都可能缺省。
    // 缺省的部分不会生成子节点，因此访问器返回 nullptr，而不是用占位节点填空。
    class ForStatement final : public StatementSyntax
    {
    public:
        ForStatement(std::unique_ptr<SyntaxNode> initializer,
                     std::unique_ptr<SyntaxNode> condition,
                     std::unique_ptr<SyntaxNode> increment,
                     std::unique_ptr<SyntaxNode> body,
                     SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::ForStatement, span)
        {
            initializer_index_ = add_optional(std::move(initializer));
            condition_index_ = add_optional(std::move(condition));
            increment_index_ = add_optional(std::move(increment));
            body_index_ = add_optional(std::move(body));
        }

        const SyntaxNode* initializer() const noexcept
        {
            return at(initializer_index_);
        }

        const SyntaxNode* condition() const noexcept
        {
            return at(condition_index_);
        }

        const SyntaxNode* increment() const noexcept
        {
            return at(increment_index_);
        }

        const SyntaxNode* body() const noexcept
        {
            return at(body_index_);
        }

    private:
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        std::size_t add_optional(std::unique_ptr<SyntaxNode> node)
        {
            if (!node)
            {
                return npos;
            }
            const std::size_t index = child_count();
            add_child(std::move(node));
            return index;
        }

        const SyntaxNode* at(std::size_t index) const noexcept
        {
            return index == npos ? nullptr : children()[index].get();
        }

        std::size_t initializer_index_ = npos;
        std::size_t condition_index_ = npos;
        std::size_t increment_index_ = npos;
        std::size_t body_index_ = npos;
    };

    // return 语句：可选返回值，子节点 0 存在时即为返回值。
    class ReturnStatement final : public StatementSyntax
    {
    public:
        explicit ReturnStatement(std::unique_ptr<SyntaxNode> returned_value = nullptr, SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::ReturnStatement, span)
        {
            if (returned_value)
            {
                add_child(std::move(returned_value));
            }
        }

        bool has_expression() const noexcept
        {
            return child_count() > 0;
        }

        const SyntaxNode* expression() const noexcept
        {
            return has_expression() ? &child(0) : nullptr;
        }
    };

    class BreakStatement final : public StatementSyntax
    {
    public:
        explicit BreakStatement(SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::BreakStatement, span)
        {
        }
    };

    class ContinueStatement final : public StatementSyntax
    {
    public:
        explicit ContinueStatement(SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::ContinueStatement, span)
        {
        }
    };

    // 变量声明：声明类型 + 名字 + 可选初始化表达式（子节点 0）。
    class VariableDeclaration final : public StatementSyntax
    {
    public:
        VariableDeclaration(std::string name,
                            std::string declared_type,
                            std::unique_ptr<SyntaxNode> initializer = nullptr,
                            SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::VariableDeclaration, span)
            , name_(std::move(name))
            , declared_type_(std::move(declared_type))
            , has_initializer_(initializer != nullptr)
        {
            if (initializer)
            {
                add_child(std::move(initializer));
            }
        }

        std::string_view name() const noexcept
        {
            return name_;
        }

        std::string_view declared_type() const noexcept
        {
            return declared_type_;
        }

        bool has_initializer() const noexcept
        {
            return has_initializer_;
        }

        const SyntaxNode& initializer() const noexcept
        {
            return child(0);
        }

        std::string_view text() const noexcept override
        {
            return name_;
        }

    private:
        std::string name_;
        std::string declared_type_;
        bool has_initializer_;
    };

    // 函数声明：参数依次是子节点 0..n-1，函数体固定是最后一个子节点。
    class FunctionDeclaration final : public StatementSyntax
    {
    public:
        FunctionDeclaration(std::string name,
                            std::string return_type,
                            std::vector<std::unique_ptr<SyntaxNode>> parameters,
                            std::unique_ptr<SyntaxNode> body,
                            SyntaxSpan span = {})
            : StatementSyntax(SyntaxKind::FunctionDeclaration, span)
            , name_(std::move(name))
            , return_type_(std::move(return_type))
        {
            for (auto& parameter : parameters)
            {
                add_child(std::move(parameter));
            }
            add_child(std::move(body));
        }

        std::string_view name() const noexcept
        {
            return name_;
        }

        std::string_view return_type() const noexcept
        {
            return return_type_;
        }

        std::string_view text() const noexcept override
        {
            return name_;
        }

        std::size_t parameter_count() const noexcept
        {
            return child_count() - 1;
        }

        const SyntaxNode& parameter(std::size_t i) const noexcept
        {
            return child(i);
        }

        const SyntaxNode& body() const noexcept
        {
            return child(child_count() - 1);
        }

    private:
        std::string name_;
        std::string return_type_;
    };

    // 顶层节点：源文件的语句序列。
    class Program final : public SyntaxNode
    {
    public:
        Program() noexcept
            : SyntaxNode(SyntaxKind::Program)
        {
        }

        void add_statement(std::unique_ptr<SyntaxNode> statement_node)
        {
            add_child(std::move(statement_node));
        }

        std::size_t statement_count() const noexcept
        {
            return child_count();
        }

        const SyntaxNode& statement(std::size_t i) const noexcept
        {
            return child(i);
        }
    };
}
