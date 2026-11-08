#pragma once

#include <string>
#include <string_view>

#include "Span.h"

namespace coordination::compiler
{
    // 词法单元种类。关键字统一用 Kw 前缀，便于 Token::is_keyword() 做区间判断；
    // true / false / null 虽是 Kw 开头，但语义上是字面量，另行排除。
    enum class TokenKind
    {
        EndOfFile,
        Identifier,
        IntegerLiteral,
        FloatLiteral,
        StringLiteral,
        CharLiteral,
        Error,

        // ---- 关键字（共 40 个，保持连续区间）----
        KwExport,
        KwImport,
        KwModule,
        KwInterface,
        KwClass,
        KwConcept,
        KwAnnotation,
        KwType,
        KwDistinct,
        KwOverride,
        KwHide,
        KwOpen,
        KwFinal,
        KwConst,
        KwInit,
        KwDeinit,
        KwReturn,
        KwIf,
        KwElse,
        KwWhile,
        KwFor,
        KwBreak,
        KwContinue,
        KwComptime,
        KwOperator,
        KwOut,
        KwIn,
        KwVoid,
        KwThis,
        KwSuper,
        KwTrue,
        KwFalse,
        KwNull,
        KwDyn,
        KwAs,
        KwIs,
        KwNew,
        KwRepeatable,

        // ---- 标点 ----
        LBrace,
        RBrace,
        LParen,
        RParen,
        LBracket,
        RBracket,
        Semicolon,
        Comma,
        Dot,
        Colon,
        At,
        Question,

        // ---- 运算符 ----
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Assign,
        Eq,
        Ne,
        Lt,
        Le,
        Gt,
        Ge,
        AmpAmp,
        PipePipe,
        Bang,
        Tilde,
        Amp,   // 单个 &：引用类型（&Canvas）
        Pipe,  // 单个 |：联合类型
        PlusAssign,
        MinusAssign,
        StarAssign,
        SlashAssign,
        PercentAssign,
    };

    // 单个词法单元。text 保存原文切片（关键字、标点、错误片段都保留），便于诊断展示。
    struct Token
    {
        TokenKind kind = TokenKind::EndOfFile;
        Span span{};
        std::string text;

        // 真正的关键字：Kw* 且排除 true / false / null 三个字面量。
        constexpr bool is_keyword() const noexcept
        {
            if (kind < TokenKind::KwExport || kind > TokenKind::KwRepeatable)
            {
                return false;
            }
            return kind != TokenKind::KwTrue && kind != TokenKind::KwFalse && kind != TokenKind::KwNull;
        }

        // 可参与常量求值的字面量种类。true / false / null 虽属 Kw*，但语义上是字面量，
        // 因此与 is_keyword() 互补：每个 Kw* 要么是关键字，要么是这三个字面量之一。
        constexpr bool is_literal() const noexcept
        {
            return kind == TokenKind::IntegerLiteral || kind == TokenKind::FloatLiteral ||
                   kind == TokenKind::StringLiteral || kind == TokenKind::CharLiteral ||
                   kind == TokenKind::KwTrue || kind == TokenKind::KwFalse || kind == TokenKind::KwNull;
        }
    };

    // 调试/测试用的稳定名字；不随枚举顺序变化。
    constexpr std::string_view token_kind_name(TokenKind kind) noexcept
    {
        switch (kind)
        {
        case TokenKind::EndOfFile: return "EndOfFile";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::IntegerLiteral: return "IntegerLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::CharLiteral: return "CharLiteral";
        case TokenKind::Error: return "Error";
        case TokenKind::KwExport: return "KwExport";
        case TokenKind::KwImport: return "KwImport";
        case TokenKind::KwModule: return "KwModule";
        case TokenKind::KwInterface: return "KwInterface";
        case TokenKind::KwClass: return "KwClass";
        case TokenKind::KwConcept: return "KwConcept";
        case TokenKind::KwAnnotation: return "KwAnnotation";
        case TokenKind::KwType: return "KwType";
        case TokenKind::KwDistinct: return "KwDistinct";
        case TokenKind::KwOverride: return "KwOverride";
        case TokenKind::KwHide: return "KwHide";
        case TokenKind::KwOpen: return "KwOpen";
        case TokenKind::KwFinal: return "KwFinal";
        case TokenKind::KwConst: return "KwConst";
        case TokenKind::KwInit: return "KwInit";
        case TokenKind::KwDeinit: return "KwDeinit";
        case TokenKind::KwReturn: return "KwReturn";
        case TokenKind::KwIf: return "KwIf";
        case TokenKind::KwElse: return "KwElse";
        case TokenKind::KwWhile: return "KwWhile";
        case TokenKind::KwFor: return "KwFor";
        case TokenKind::KwBreak: return "KwBreak";
        case TokenKind::KwContinue: return "KwContinue";
        case TokenKind::KwComptime: return "KwComptime";
        case TokenKind::KwOperator: return "KwOperator";
        case TokenKind::KwOut: return "KwOut";
        case TokenKind::KwIn: return "KwIn";
        case TokenKind::KwVoid: return "KwVoid";
        case TokenKind::KwThis: return "KwThis";
        case TokenKind::KwSuper: return "KwSuper";
        case TokenKind::KwTrue: return "KwTrue";
        case TokenKind::KwFalse: return "KwFalse";
        case TokenKind::KwNull: return "KwNull";
        case TokenKind::KwDyn: return "KwDyn";
        case TokenKind::KwAs: return "KwAs";
        case TokenKind::KwIs: return "KwIs";
        case TokenKind::KwNew: return "KwNew";
        case TokenKind::KwRepeatable: return "KwRepeatable";
        case TokenKind::LBrace: return "LBrace";
        case TokenKind::RBrace: return "RBrace";
        case TokenKind::LParen: return "LParen";
        case TokenKind::RParen: return "RParen";
        case TokenKind::LBracket: return "LBracket";
        case TokenKind::RBracket: return "RBracket";
        case TokenKind::Semicolon: return "Semicolon";
        case TokenKind::Comma: return "Comma";
        case TokenKind::Dot: return "Dot";
        case TokenKind::Colon: return "Colon";
        case TokenKind::At: return "At";
        case TokenKind::Question: return "Question";
        case TokenKind::Plus: return "Plus";
        case TokenKind::Minus: return "Minus";
        case TokenKind::Star: return "Star";
        case TokenKind::Slash: return "Slash";
        case TokenKind::Percent: return "Percent";
        case TokenKind::Assign: return "Assign";
        case TokenKind::Eq: return "Eq";
        case TokenKind::Ne: return "Ne";
        case TokenKind::Lt: return "Lt";
        case TokenKind::Le: return "Le";
        case TokenKind::Gt: return "Gt";
        case TokenKind::Ge: return "Ge";
        case TokenKind::AmpAmp: return "AmpAmp";
        case TokenKind::PipePipe: return "PipePipe";
        case TokenKind::Bang: return "Bang";
        case TokenKind::Tilde: return "Tilde";
        case TokenKind::Amp: return "Amp";
        case TokenKind::Pipe: return "Pipe";
        case TokenKind::PlusAssign: return "PlusAssign";
        case TokenKind::MinusAssign: return "MinusAssign";
        case TokenKind::StarAssign: return "StarAssign";
        case TokenKind::SlashAssign: return "SlashAssign";
        case TokenKind::PercentAssign: return "PercentAssign";
        }
        return "Unknown";
    }
}
