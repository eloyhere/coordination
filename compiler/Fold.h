#pragma once

// ============================================================================
// AST 常量折叠：在 Parser 生成的语法树上就地复用 ConstEvaluator。
//
// foldConstants(arena, moduleRoot) 的约定：
//   * 只处理 Module 根节点直接孩子中的顶层 const 声明与函数体；
//   * 第一遍按源码顺序登记顶层 `const 名字 = 纯常量表达式` 的值，供函数体与
//     后续常量声明中的标识符引用求值；
//   * 第二遍自上而下尝试折叠纯常量表达式，成功时新建 LiteralExpr 替换父节点的
//     children 指针（span 沿用被替换表达式，text 为十进制/true/false/带引号字符串）；
//   * 含调用、成员、下标、赋值、this 等节点的表达式整体不折叠，但会继续下钻
//     折叠其中的纯常量子表达式；
//   * 除零、溢出、预算超限等求值失败保留原子树，记录 E7001 诊断后继续处理其它表达式。
//
// 节点只增不删：被替换下来的旧子树仍归 AstArena 所有（仅把 parent 置空），
// 因此调用方可以继续用旧指针做调试或回滚。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "Ast.h"
#include "ConstEval.h"
#include "Diagnostic.h"
#include "Span.h"

namespace coordination::compiler
{
    // 一次常量折叠的统计与诊断。folded 是实际被替换的表达式个数。
    struct FoldResult
    {
        std::size_t folded = 0;
        DiagnosticBag diagnostics;

        bool ok() const noexcept
        {
            return !diagnostics.has_errors();
        }
    };

    namespace fold_detail
    {
        using ConstantTable = std::map<std::string, ConstValue>;

        // ConstEvaluator 支持的字面量原文：true/false、带引号字符串、数字开头的整数。
        // 浮点、字符、null 等不在支持范围内，按“非纯常量”跳过，不产生 E7001。
        inline bool is_supported_literal(std::string_view text) noexcept
        {
            if (text == "true" || text == "false")
            {
                return true;
            }
            if (text.empty())
            {
                return false;
            }
            if (text.front() == '"')
            {
                return true;
            }
            return text.front() >= '0' && text.front() <= '9';
        }

        inline bool is_expression_kind(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::LiteralExpr:
            case AstKind::IdentifierExpr:
            case AstKind::UnaryExpr:
            case AstKind::BinaryExpr:
            case AstKind::AssignExpr:
            case AstKind::CallExpr:
            case AstKind::MemberExpr:
            case AstKind::IndexExpr:
            case AstKind::ConditionalExpr:
            case AstKind::ThisExpr:
            case AstKind::SuperExpr:
            case AstKind::NewExpr:
            case AstKind::CastExpr:
            case AstKind::ParenExpr:
                return true;
            default:
                return false;
            }
        }

        inline bool is_statement_kind(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::BlockStmt:
            case AstKind::ExprStmt:
            case AstKind::IfStmt:
            case AstKind::WhileStmt:
            case AstKind::ForStmt:
            case AstKind::ReturnStmt:
            case AstKind::BreakStmt:
            case AstKind::ContinueStmt:
            case AstKind::LocalDeclStmt:
                return true;
            default:
                return false;
            }
        }

        inline bool is_supported_unary(std::string_view op) noexcept
        {
            return op == "+" || op == "-" || op == "!" || op == "~";
        }

        inline bool is_supported_binary(std::string_view op) noexcept
        {
            return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
                op == "<<" || op == ">>" || op == "&" || op == "|" || op == "^" ||
                op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
                op == ">=" || op == "&&" || op == "||";
        }

        // 是否为“纯常量”表达式：只由受支持的字面量、已登记常量引用、
        // 受支持的一元/二元/三目/括号节点组成；调用、成员、下标、赋值、
        // this、super、new、cast 等一律视为不纯，整体不折叠。
        inline bool is_pure_constant(const AstNode& node, const ConstantTable& constants)
        {
            switch (node.kind)
            {
            case AstKind::LiteralExpr:
                return is_supported_literal(node.text);
            case AstKind::IdentifierExpr:
            {
                const auto found = constants.find(node.text);
                return found != constants.end() && found->second.valid();
            }
            case AstKind::ParenExpr:
                return node.child_count() == 1u && node.child(0) != nullptr &&
                    is_pure_constant(*node.child(0), constants);
            case AstKind::UnaryExpr:
                return is_supported_unary(node.text) && node.child_count() == 1u &&
                    node.child(0) != nullptr && is_pure_constant(*node.child(0), constants);
            case AstKind::BinaryExpr:
                return is_supported_binary(node.text) && node.child_count() == 2u &&
                    node.child(0) != nullptr && node.child(1) != nullptr &&
                    is_pure_constant(*node.child(0), constants) &&
                    is_pure_constant(*node.child(1), constants);
            case AstKind::ConditionalExpr:
                return node.child_count() == 3u && node.child(0) != nullptr &&
                    node.child(1) != nullptr && node.child(2) != nullptr &&
                    is_pure_constant(*node.child(0), constants) &&
                    is_pure_constant(*node.child(1), constants) &&
                    is_pure_constant(*node.child(2), constants);
            default:
                return false;
            }
        }

        // 把求值结果编码成 LiteralExpr 的原文：整数十进制、布尔 true/false、
        // 字符串带双引号并转义。字符串中的控制字符按 ConstEvaluator 支持的转义形式输出。
        inline std::string encode_literal(const ConstValue& value)
        {
            switch (value.kind)
            {
            case ValueKind::integer:
                return std::to_string(value.integer);
            case ValueKind::boolean:
                return value.boolean ? "true" : "false";
            case ValueKind::text:
            {
                std::string out;
                out.reserve(value.text.size() + 2u);
                out.push_back('"');
                for (const char character : value.text)
                {
                    switch (character)
                    {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\0':
                        out += "\\0";
                        break;
                    default:
                        out.push_back(character);
                        break;
                    }
                }
                out.push_back('"');
                return out;
            }
            case ValueKind::invalid:
            default:
                return std::string();
            }
        }

        class Folder
        {
        public:
            explicit Folder(AstArena& arena) noexcept
                : arena_(arena)
            {
            }

            FoldResult run(AstNode& moduleRoot)
            {
                if (moduleRoot.kind != AstKind::Module)
                {
                    return result_;
                }

                collect_constants(moduleRoot);
                for (AstNode* declaration : moduleRoot.children)
                {
                    fold_top_level_declaration(declaration);
                }
                return result_;
            }

        private:
            static constexpr std::size_t no_index = static_cast<std::size_t>(-1);

            AstArena& arena_;
            FoldResult result_;
            ConstantTable constants_;

            // 取声明末尾的初始化表达式下标：ConstantDecl / LocalDeclStmt 的孩子顺序为
            // 标注* -> 类型? -> 初始化表达式?，从后往前找到的第一个表达式即为初始化器。
            static std::size_t initializer_index(const AstNode& declaration) noexcept
            {
                for (std::size_t index = declaration.child_count(); index > 0u; --index)
                {
                    const AstNode* child = declaration.child(index - 1u);
                    if (child != nullptr && is_expression_kind(child->kind))
                    {
                        return index - 1u;
                    }
                }
                return no_index;
            }

            void collect_constants(const AstNode& moduleRoot)
            {
                for (AstNode* child : moduleRoot.children)
                {
                    if (child != nullptr)
                    {
                        collect_constant(*child);
                    }
                }
            }

            // 只登记顶层 const（含 export 包裹）；求值失败或非纯常量的声明不登记，
            // 留给折叠阶段统一写 E7001 或按非纯常量跳过。
            void collect_constant(const AstNode& declaration)
            {
                if (declaration.kind == AstKind::ExportDecl)
                {
                    if (declaration.child_count() == 1u && declaration.child(0) != nullptr)
                    {
                        collect_constant(*declaration.child(0));
                    }
                    return;
                }
                if (declaration.kind != AstKind::ConstantDecl)
                {
                    return;
                }

                const std::size_t index = initializer_index(declaration);
                if (index == no_index)
                {
                    return;
                }
                const AstNode& initializer = *declaration.child(index);
                if (!is_pure_constant(initializer, constants_))
                {
                    return;
                }

                ConstEvaluator evaluator(constants_);
                const EvalResult evaluated = evaluator.evaluate(initializer);
                if (evaluated.ok)
                {
                    constants_[declaration.text] = evaluated.value;
                }
            }

            void fold_top_level_declaration(AstNode* declaration)
            {
                if (declaration == nullptr)
                {
                    return;
                }

                switch (declaration->kind)
                {
                case AstKind::ExportDecl:
                    if (declaration->child_count() == 1u && declaration->child(0) != nullptr)
                    {
                        fold_top_level_declaration(declaration->child(0));
                    }
                    break;
                case AstKind::ConstantDecl:
                    fold_declaration_initializer(*declaration);
                    break;
                case AstKind::FunctionDecl:
                    fold_function_body(*declaration);
                    break;
                default:
                    break;
                }
            }

            void fold_declaration_initializer(AstNode& declaration)
            {
                const std::size_t index = initializer_index(declaration);
                if (index != no_index)
                {
                    fold_slot(declaration, index);
                }
            }

            void fold_function_body(AstNode& function)
            {
                if (function.child_count() == 0u)
                {
                    return;
                }
                AstNode* body = function.child(function.child_count() - 1u);
                if (body != nullptr && body->kind == AstKind::BlockStmt)
                {
                    fold_statement(*body);
                }
            }

            void fold_statement(AstNode& statement)
            {
                switch (statement.kind)
                {
                case AstKind::BlockStmt:
                    for (AstNode* child : statement.children)
                    {
                        if (child != nullptr)
                        {
                            fold_statement(*child);
                        }
                    }
                    break;
                case AstKind::ExprStmt:
                    fold_slot(statement, 0u);
                    break;
                case AstKind::IfStmt:
                    fold_slot(statement, 0u);
                    fold_nested_statement(statement, 1u);
                    fold_nested_statement(statement, 2u);
                    break;
                case AstKind::WhileStmt:
                    fold_slot(statement, 0u);
                    fold_nested_statement(statement, 1u);
                    break;
                case AstKind::ForStmt:
                    fold_for_statement(statement);
                    break;
                case AstKind::ReturnStmt:
                    fold_slot(statement, 0u);
                    break;
                case AstKind::LocalDeclStmt:
                    fold_declaration_initializer(statement);
                    break;
                default:
                    break;
                }
            }

            // 分支/循环体既可能是 BlockStmt，也可能是表达式语句等单条语句。
            void fold_nested_statement(AstNode& parent, std::size_t index)
            {
                AstNode* child = parent.child(index);
                if (child == nullptr)
                {
                    return;
                }
                if (is_statement_kind(child->kind))
                {
                    fold_statement(*child);
                }
            }

            // ForStmt 的孩子顺序为 init? -> cond? -> step? -> body（body 恒为最后一个），
            // 其中 init 可能是 LocalDeclStmt，cond/step 是裸表达式。
            void fold_for_statement(AstNode& statement)
            {
                const std::size_t count = statement.child_count();
                if (count == 0u)
                {
                    return;
                }

                AstNode* body = statement.child(count - 1u);
                if (body != nullptr && is_statement_kind(body->kind))
                {
                    fold_statement(*body);
                }

                for (std::size_t index = 0u; index + 1u < count; ++index)
                {
                    AstNode* child = statement.child(index);
                    if (child == nullptr)
                    {
                        continue;
                    }
                    if (is_statement_kind(child->kind))
                    {
                        fold_statement(*child);
                    }
                    else if (is_expression_kind(child->kind))
                    {
                        fold_slot(statement, index);
                    }
                }
            }

            // 折叠 parent.children[index]：整体是纯常量时替换成 LiteralExpr；
            // 否则保持整体不动，继续下钻折叠其中的纯常量子表达式。
            void fold_slot(AstNode& parent, std::size_t index)
            {
                AstNode* expression = parent.child(index);
                if (expression == nullptr)
                {
                    return;
                }

                // 已是字面量，无需改写，也不必继续下钻。
                if (expression->kind == AstKind::LiteralExpr)
                {
                    return;
                }

                if (is_pure_constant(*expression, constants_))
                {
                    ConstEvaluator evaluator(constants_);
                    const EvalResult evaluated = evaluator.evaluate(*expression);
                    if (evaluated.ok)
                    {
                        AstNode* literal = arena_.make(AstKind::LiteralExpr, expression->span,
                            encode_literal(evaluated.value));
                        literal->parent = &parent;
                        parent.children[index] = literal;
                        expression->parent = nullptr;
                        ++result_.folded;
                        return;
                    }

                    // 除零、溢出、预算超限等：保留原子树，记录诊断，继续处理其它表达式。
                    result_.diagnostics.error("E7001",
                        "常量折叠失败: " + evaluated.error, expression->span);
                    return;
                }

                for (std::size_t childIndex = 0u; childIndex < expression->child_count(); ++childIndex)
                {
                    AstNode* child = expression->child(childIndex);
                    if (child == nullptr)
                    {
                        continue;
                    }
                    // 只下钻表达式与调用实参列表，避免误改类型节点里的编译期参数。
                    if (is_expression_kind(child->kind) || child->kind == AstKind::ArgumentList)
                    {
                        fold_slot(*expression, childIndex);
                    }
                }
            }
        };
    }

    // 就地折叠 moduleRoot 下的纯常量表达式，返回折叠次数与诊断。
    // arena 必须拥有 moduleRoot 及其全部节点，且在调用期间保持存活。
    inline FoldResult foldConstants(AstArena& arena, AstNode& moduleRoot)
    {
        fold_detail::Folder folder(arena);
        return folder.run(moduleRoot);
    }
}
