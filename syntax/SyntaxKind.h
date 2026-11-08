#pragma once

#include <string_view>

namespace coordination::syntax
{
    // 语法单元的种类。
    //
    // 枚举顺序有意义：is_token / is_expression / is_statement 用区间判断，
    // 新增种类时请保持同类相邻，并同步更新 kind_name 与区间判断。
    enum class SyntaxKind
    {
        // ---- 词法单元 ----
        EndOfFileToken,
        IdentifierToken,
        NumericToken,
        StringToken,
        OperatorToken,
        PunctuationToken,

        // ---- 表达式 ----
        LiteralExpression,
        IdentifierExpression,
        UnaryExpression,
        BinaryExpression,
        AssignmentExpression,
        CallExpression,
        MemberAccessExpression,
        IndexExpression,
        ConditionalExpression,

        // ---- 语句 ----
        ExpressionStatement,
        BlockStatement,
        IfStatement,
        WhileStatement,
        ForStatement,
        ReturnStatement,
        BreakStatement,
        ContinueStatement,

        // ---- 声明与顶层 ----
        VariableDeclaration,
        FunctionDeclaration,
        Program,
    };

    constexpr bool is_token(SyntaxKind kind) noexcept
    {
        return kind >= SyntaxKind::EndOfFileToken && kind <= SyntaxKind::PunctuationToken;
    }

    constexpr bool is_expression(SyntaxKind kind) noexcept
    {
        return kind >= SyntaxKind::LiteralExpression && kind <= SyntaxKind::ConditionalExpression;
    }

    constexpr bool is_statement(SyntaxKind kind) noexcept
    {
        return kind >= SyntaxKind::ExpressionStatement && kind <= SyntaxKind::Program;
    }

    constexpr std::string_view kind_name(SyntaxKind kind) noexcept
    {
        switch (kind)
        {
        case SyntaxKind::EndOfFileToken: return "EndOfFileToken";
        case SyntaxKind::IdentifierToken: return "IdentifierToken";
        case SyntaxKind::NumericToken: return "NumericToken";
        case SyntaxKind::StringToken: return "StringToken";
        case SyntaxKind::OperatorToken: return "OperatorToken";
        case SyntaxKind::PunctuationToken: return "PunctuationToken";
        case SyntaxKind::LiteralExpression: return "LiteralExpression";
        case SyntaxKind::IdentifierExpression: return "IdentifierExpression";
        case SyntaxKind::UnaryExpression: return "UnaryExpression";
        case SyntaxKind::BinaryExpression: return "BinaryExpression";
        case SyntaxKind::AssignmentExpression: return "AssignmentExpression";
        case SyntaxKind::CallExpression: return "CallExpression";
        case SyntaxKind::MemberAccessExpression: return "MemberAccessExpression";
        case SyntaxKind::IndexExpression: return "IndexExpression";
        case SyntaxKind::ConditionalExpression: return "ConditionalExpression";
        case SyntaxKind::ExpressionStatement: return "ExpressionStatement";
        case SyntaxKind::BlockStatement: return "BlockStatement";
        case SyntaxKind::IfStatement: return "IfStatement";
        case SyntaxKind::WhileStatement: return "WhileStatement";
        case SyntaxKind::ForStatement: return "ForStatement";
        case SyntaxKind::ReturnStatement: return "ReturnStatement";
        case SyntaxKind::BreakStatement: return "BreakStatement";
        case SyntaxKind::ContinueStatement: return "ContinueStatement";
        case SyntaxKind::VariableDeclaration: return "VariableDeclaration";
        case SyntaxKind::FunctionDeclaration: return "FunctionDeclaration";
        case SyntaxKind::Program: return "Program";
        }
        return "Unknown";
    }
}
