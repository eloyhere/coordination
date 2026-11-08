#include "../compiler/Lexer.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// 失败即打印原因并退出；全部通过时只输出一行 PASS。
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (false)

using namespace coordination::compiler;

namespace
{
    constexpr SourceId kSource = 7;

    struct LexResult
    {
        std::vector<Token> tokens;
        DiagnosticBag diagnostics;
    };

    LexResult lex(std::string_view text)
    {
        LexResult result;
        Lexer lexer(kSource, text, result.diagnostics);
        result.tokens = lexer.tokenize();
        return result;
    }

    void dump(std::string_view source, const LexResult& result)
    {
        std::cout << "  source: `" << source << "` (" << source.size() << " bytes)" << std::endl;
        for (std::size_t i = 0; i < result.tokens.size(); ++i)
        {
            const Token& token = result.tokens[i];
            std::cout << "  [" << i << "] " << token_kind_name(token.kind) << " span=[" << token.span.begin << ","
                      << token.span.end << ") text=`" << token.text << "`" << std::endl;
        }
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            std::cout << "  diag " << diagnostic.code << " span=[" << diagnostic.span.begin << ","
                      << diagnostic.span.end << ") " << diagnostic.message << std::endl;
        }
    }

    // 期望的 token：种类 + span + 原文。
    struct ExpectedToken
    {
        TokenKind kind;
        std::uint32_t begin;
        std::uint32_t end;
        std::string_view text;
    };

    // 逐 token 校验种类、span、原文、SourceId，并要求没有任何诊断。
    bool check_tokens(std::string_view source, std::initializer_list<ExpectedToken> expected)
    {
        const LexResult result = lex(source);
        bool ok = result.tokens.size() == expected.size() && result.diagnostics.diagnostics().empty();
        if (ok)
        {
            std::size_t index = 0;
            for (const ExpectedToken& item : expected)
            {
                const Token& token = result.tokens[index];
                if (token.kind != item.kind || token.span.begin != item.begin || token.span.end != item.end ||
                    token.span.source != kSource || std::string_view(token.text) != item.text)
                {
                    ok = false;
                    break;
                }
                ++index;
            }
        }
        if (!ok)
        {
            std::cout << "FAIL: token 序列、span 或原文与预期不符" << std::endl;
            dump(source, result);
        }
        return ok;
    }

    // 只比较种类序列（错误恢复等无需 span 的场景）。
    bool check_kinds(std::string_view source, std::initializer_list<TokenKind> expected)
    {
        const LexResult result = lex(source);
        bool ok = result.tokens.size() == expected.size();
        if (ok)
        {
            std::size_t index = 0;
            for (TokenKind kind : expected)
            {
                if (result.tokens[index].kind != kind)
                {
                    ok = false;
                    break;
                }
                ++index;
            }
        }
        if (!ok)
        {
            std::cout << "FAIL: token 种类序列与预期不符" << std::endl;
            dump(source, result);
            std::cout << "  expected:";
            for (TokenKind kind : expected)
            {
                std::cout << ' ' << token_kind_name(kind);
            }
            std::cout << std::endl;
        }
        return ok;
    }

    // 单个 token + 零宽 EndOfFile；span 覆盖整个源，且没有诊断。
    bool check_single(std::string_view text, TokenKind kind)
    {
        const LexResult result = lex(text);
        const bool ok = result.tokens.size() == 2u && result.tokens[0].kind == kind &&
                        result.tokens[0].span.source == kSource && result.tokens[0].span.begin == 0u &&
                        result.tokens[0].span.end == text.size() &&
                        std::string_view(result.tokens[0].text) == text &&
                        result.tokens[1].kind == TokenKind::EndOfFile &&
                        result.tokens[1].span.begin == text.size() &&
                        result.tokens[1].span.end == text.size() && result.diagnostics.error_count() == 0u;
        if (!ok)
        {
            std::cout << "FAIL: 期望单个 " << token_kind_name(kind) << " token（span 覆盖全源、无诊断）" << std::endl;
            dump(text, result);
        }
        return ok;
    }

    // 整个源只产生一个 Error token，外加一个错误码。
    bool check_single_error(std::string_view text, std::string_view code)
    {
        const LexResult result = lex(text);
        const bool has_expected_code = result.diagnostics.error_count() == 1u &&
                                       !result.diagnostics.diagnostics().empty() &&
                                       std::string_view(result.diagnostics.diagnostics()[0].code) == code;
        const bool ok = result.tokens.size() == 2u && result.tokens[0].kind == TokenKind::Error &&
                        result.tokens[0].span.begin == 0u && result.tokens[0].span.end == text.size() &&
                        std::string_view(result.tokens[0].text) == text &&
                        result.tokens[1].kind == TokenKind::EndOfFile &&
                        result.tokens[1].span.begin == text.size() &&
                        result.tokens[1].span.end == text.size() && has_expected_code;
        if (!ok)
        {
            std::cout << "FAIL: 期望单个 Error token 且错误码为 " << code << std::endl;
            dump(text, result);
        }
        return ok;
    }

    // 整段被丢弃（例如未闭合块注释）：只剩 EndOfFile，外加一个错误码。
    bool check_eof_only_with_error(std::string_view text, std::string_view code)
    {
        const LexResult result = lex(text);
        const bool has_expected_code = result.diagnostics.error_count() == 1u &&
                                       !result.diagnostics.diagnostics().empty() &&
                                       std::string_view(result.diagnostics.diagnostics()[0].code) == code;
        const bool ok = result.tokens.size() == 1u && result.tokens[0].kind == TokenKind::EndOfFile &&
                        result.tokens[0].span.begin == text.size() &&
                        result.tokens[0].span.end == text.size() && has_expected_code;
        if (!ok)
        {
            std::cout << "FAIL: 期望只剩 EndOfFile 且错误码为 " << code << std::endl;
            dump(text, result);
        }
        return ok;
    }

    // 结果中至少出现一次指定错误码。
    bool has_error_code(std::string_view text, const LexResult& result, std::string_view code)
    {
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                return true;
            }
        }
        std::cout << "FAIL: 缺少错误码 " << code << std::endl;
        dump(text, result);
        return false;
    }
}

int main()
{
    // ---- 空输入：只有零宽 EndOfFile ----
    {
        const LexResult result = lex("");
        CHECK(result.tokens.size() == 1u);
        CHECK(result.tokens[0].kind == TokenKind::EndOfFile);
        CHECK(result.tokens[0].span.source == kSource);
        CHECK(result.tokens[0].span.begin == 0u);
        CHECK(result.tokens[0].span.end == 0u);
        CHECK(result.tokens[0].text.empty());
        CHECK(result.diagnostics.diagnostics().empty());
    }

    // ---- 纯空白与注释：同样只有 EndOfFile，位置在源末尾 ----
    {
        const std::string_view source = "  \t\r\n // 行注释\n /* 块注释 */ \n";
        const LexResult result = lex(source);
        CHECK(result.tokens.size() == 1u);
        CHECK(result.tokens[0].kind == TokenKind::EndOfFile);
        CHECK(result.tokens[0].span.begin == source.size());
        CHECK(result.tokens[0].span.end == source.size());
        CHECK(result.diagnostics.error_count() == 0u);
    }

    // ---- 全部 40 个关键字 ----
    {
        const struct
        {
            std::string_view text;
            TokenKind kind;
        } keywords[] = {
            {"export", TokenKind::KwExport},
            {"import", TokenKind::KwImport},
            {"module", TokenKind::KwModule},
            {"interface", TokenKind::KwInterface},
            {"class", TokenKind::KwClass},
            {"concept", TokenKind::KwConcept},
            {"annotation", TokenKind::KwAnnotation},
            {"type", TokenKind::KwType},
            {"distinct", TokenKind::KwDistinct},
            {"override", TokenKind::KwOverride},
            {"hide", TokenKind::KwHide},
            {"open", TokenKind::KwOpen},
            {"final", TokenKind::KwFinal},
            {"new", TokenKind::KwNew},
            {"for", TokenKind::KwFor},
            {"const", TokenKind::KwConst},
            {"init", TokenKind::KwInit},
            {"deinit", TokenKind::KwDeinit},
            {"return", TokenKind::KwReturn},
            {"if", TokenKind::KwIf},
            {"else", TokenKind::KwElse},
            {"while", TokenKind::KwWhile},
            {"for", TokenKind::KwFor},
            {"break", TokenKind::KwBreak},
            {"continue", TokenKind::KwContinue},
            {"comptime", TokenKind::KwComptime},
            {"operator", TokenKind::KwOperator},
            {"out", TokenKind::KwOut},
            {"in", TokenKind::KwIn},
            {"void", TokenKind::KwVoid},
            {"this", TokenKind::KwThis},
            {"super", TokenKind::KwSuper},
            {"true", TokenKind::KwTrue},
            {"false", TokenKind::KwFalse},
            {"null", TokenKind::KwNull},
            {"dyn", TokenKind::KwDyn},
            {"as", TokenKind::KwAs},
            {"is", TokenKind::KwIs},
            {"new", TokenKind::KwNew},
            {"repeatable", TokenKind::KwRepeatable},
        };
        CHECK(std::size(keywords) == 40u);
        for (const auto& keyword : keywords)
        {
            CHECK(check_single(keyword.text, keyword.kind));
            CHECK(token_kind_name(keyword.kind) != "Unknown");
            CHECK(!token_kind_name(keyword.kind).empty());
            const bool expect_keyword = keyword.kind != TokenKind::KwTrue &&
                                        keyword.kind != TokenKind::KwFalse &&
                                        keyword.kind != TokenKind::KwNull;
            const LexResult result = lex(keyword.text);
            CHECK(!result.tokens.empty());
            CHECK(result.tokens[0].is_keyword() == expect_keyword);
        }
    }

    // ---- 标识符：下划线开头、含数字、大小写敏感 ----
    {
        const std::string_view identifiers[] = {
            "foo", "Foo", "Bar9", "x1", "Canvas", "f64", "Let", "IF",
            "letter", "iffy", "classy", "newton", "returnValue", "x9Y",
        };
        for (std::string_view text : identifiers)
        {
            CHECK(check_single(text, TokenKind::Identifier));
            const LexResult result = lex(text);
            CHECK(!result.tokens.empty());
            CHECK(!result.tokens[0].is_keyword());
            CHECK(!result.tokens[0].is_literal());
        }
        // 下划线不是合法标识符字符：_ / _bar / a_b_c 都必须报错
        {
            const LexResult result = lex("_");
            CHECK(result.diagnostics.has_errors());
            CHECK(!result.tokens.empty());
            CHECK(result.tokens[0].kind == TokenKind::Error);
        }
        CHECK(check_single("new", TokenKind::KwNew));
        CHECK(check_single("Let", TokenKind::Identifier));
        CHECK(check_single("true", TokenKind::KwTrue));
        CHECK(check_single("True", TokenKind::Identifier));
    }

    // ---- 标注写作 @Name(args)：名称首字母大写时不是关键字 ----
    {
        CHECK(check_tokens("@Override draw;",
                           {{TokenKind::At, 0u, 1u, "@"},
                            {TokenKind::Identifier, 1u, 9u, "Override"},
                            {TokenKind::Identifier, 10u, 14u, "draw"},
                            {TokenKind::Semicolon, 14u, 15u, ";"},
                            {TokenKind::EndOfFile, 15u, 15u, ""}}));
        CHECK(check_kinds("@override", {TokenKind::At, TokenKind::KwOverride, TokenKind::EndOfFile}));
        CHECK(check_kinds("@Repeatable(a, b)", {TokenKind::At, TokenKind::Identifier, TokenKind::LParen,
                                                TokenKind::Identifier, TokenKind::Comma, TokenKind::Identifier,
                                                TokenKind::RParen, TokenKind::EndOfFile}));
    }

    // ---- 全部标点与运算符（单独出现时 span 覆盖整个源）----
    {
        const struct
        {
            std::string_view text;
            TokenKind kind;
        } symbols[] = {
            {"{", TokenKind::LBrace},
            {"}", TokenKind::RBrace},
            {"(", TokenKind::LParen},
            {")", TokenKind::RParen},
            {"[", TokenKind::LBracket},
            {"]", TokenKind::RBracket},
            {";", TokenKind::Semicolon},
            {",", TokenKind::Comma},
            {".", TokenKind::Dot},
            {":", TokenKind::Colon},
            {"@", TokenKind::At},
            {"?", TokenKind::Question},
            {"+", TokenKind::Plus},
            {"-", TokenKind::Minus},
            {"*", TokenKind::Star},
            {"/", TokenKind::Slash},
            {"%", TokenKind::Percent},
            {"=", TokenKind::Assign},
            {"==", TokenKind::Eq},
            {"!=", TokenKind::Ne},
            {"<", TokenKind::Lt},
            {"<=", TokenKind::Le},
            {">", TokenKind::Gt},
            {">=", TokenKind::Ge},
            {"&&", TokenKind::AmpAmp},
            {"||", TokenKind::PipePipe},
            {"!", TokenKind::Bang},
            {"~", TokenKind::Tilde},
            {"&", TokenKind::Amp},
            {"|", TokenKind::Pipe},
            {"+=", TokenKind::PlusAssign},
            {"-=", TokenKind::MinusAssign},
            {"*=", TokenKind::StarAssign},
            {"/=", TokenKind::SlashAssign},
            {"%=", TokenKind::PercentAssign},
        };
        CHECK(std::size(symbols) == 35u);
        for (const auto& symbol : symbols)
        {
            CHECK(check_single(symbol.text, symbol.kind));
        }
        // 运算符 token 都不是关键字/字面量。
        const LexResult result = lex("+");
        CHECK(!result.tokens[0].is_keyword());
        CHECK(!result.tokens[0].is_literal());
    }

    // ---- 最长匹配：双字符运算符优先于单字符 ----
    {
        CHECK(check_kinds("== = != ! <= < >= > && & || | += + -= - *= * /= / %= %",
                          {TokenKind::Eq, TokenKind::Assign, TokenKind::Ne, TokenKind::Bang, TokenKind::Le,
                           TokenKind::Lt, TokenKind::Ge, TokenKind::Gt, TokenKind::AmpAmp, TokenKind::Amp,
                           TokenKind::PipePipe, TokenKind::Pipe, TokenKind::PlusAssign, TokenKind::Plus,
                           TokenKind::MinusAssign, TokenKind::Minus, TokenKind::StarAssign, TokenKind::Star,
                           TokenKind::SlashAssign, TokenKind::Slash, TokenKind::PercentAssign, TokenKind::Percent,
                           TokenKind::EndOfFile}));
        // 中间有空白时不会被合并。
        CHECK(check_kinds("= =", {TokenKind::Assign, TokenKind::Assign, TokenKind::EndOfFile}));
        CHECK(check_kinds("a.b", {TokenKind::Identifier, TokenKind::Dot, TokenKind::Identifier,
                                  TokenKind::EndOfFile}));
    }

    // ---- 整数：十进制 / 十六进制 / 二进制 / 下划线分隔 ----
    {
        const std::string_view integers[] = {
            "0", "7", "42", "007", "1_000", "1_2_3", "12345678901234567890",
            "0xFF", "0X1a2B", "0x0", "0x1F_2E", "0xFF_FF", "0B0",
            "0b1010", "0B11", "0b0", "0b1_010", "0B1111_0000",
        };
        for (std::string_view text : integers)
        {
            CHECK(check_single(text, TokenKind::IntegerLiteral));
            const LexResult result = lex(text);
            CHECK(!result.tokens.empty());
            CHECK(result.tokens[0].is_literal());
            CHECK(!result.tokens[0].is_keyword());
        }
    }

    // ---- 浮点：有小数点或指数；`1.` 明确当作浮点 ----
    {
        const std::string_view floats[] = {
            "1.0", "0.5", "1.", "3.1415926", "1e10", "1E10", "1.5e-3", "2.5E+2", "6e0", "1_0.2_5", "1.5e1_0",
        };
        for (std::string_view text : floats)
        {
            CHECK(check_single(text, TokenKind::FloatLiteral));
        }
        // `1.` 只有两个 token（不会切成 1 和 .）。
        CHECK(check_tokens("1.", {{TokenKind::FloatLiteral, 0u, 2u, "1."},
                                  {TokenKind::EndOfFile, 2u, 2u, ""}}));
        // 小数点前没有数字时：`.5` 是 Dot + 整数 5（不支持前导小数点浮点）。
        CHECK(check_tokens(".5", {{TokenKind::Dot, 0u, 1u, "."},
                                  {TokenKind::IntegerLiteral, 1u, 2u, "5"},
                                  {TokenKind::EndOfFile, 2u, 2u, ""}}));
    }

    // ---- 字符串：原文切片、支持 \" \\ \n \t \r \0 ----
    {
        const std::string_view strings[] = {
            R"("hello")",
            R"("")",
            R"("a\"b")",
            R"("a\\b")",
            R"("a\nb")",
            R"("a\tb")",
            R"("a\rb")",
            R"("\0")",
            R"("\'")",
            R"("// not a comment")",
            R"("/* not a comment */")",
            R"("mixed \"quotes\" and \\ backslashes")",
            R"("semi; colon: comma,")",
        };
        for (std::string_view text : strings)
        {
            CHECK(check_single(text, TokenKind::StringLiteral));
        }
    }

    // ---- 字符字面量：恰好一个字符或一个转义 ----
    {
        const std::string_view chars[] = {
            "'a'", "'Z'", "'0'", "'_'", "' '", "'\\n'", "'\\t'", "'\\r'", "'\\0'", "'\\\\'", "'\\''",
        };
        for (std::string_view text : chars)
        {
            CHECK(check_single(text, TokenKind::CharLiteral));
        }
    }

    // ---- 行注释与块注释 ----
    {
        CHECK(check_tokens("new x; // trailing",
                           {{TokenKind::KwNew, 0u, 3u, "new"},
                            {TokenKind::Identifier, 4u, 5u, "x"},
                            {TokenKind::Semicolon, 5u, 6u, ";"},
                            {TokenKind::EndOfFile, 18u, 18u, ""}}));
        CHECK(check_tokens("/* block */ x",
                           {{TokenKind::Identifier, 12u, 13u, "x"},
                            {TokenKind::EndOfFile, 13u, 13u, ""}}));
        CHECK(check_tokens("/**/x",
                           {{TokenKind::Identifier, 4u, 5u, "x"},
                            {TokenKind::EndOfFile, 5u, 5u, ""}}));
        // 块注释不支持嵌套：遇到第一个 */ 就结束，后面的内容照常切分。
        CHECK(check_tokens("/* /* */ x",
                           {{TokenKind::Identifier, 9u, 10u, "x"},
                            {TokenKind::EndOfFile, 10u, 10u, ""}}));
        // 行注释吃到行尾（CR/LF 都算），后面的 token 不受影响。
        CHECK(check_tokens("a // c\r\nb",
                           {{TokenKind::Identifier, 0u, 1u, "a"},
                            {TokenKind::Identifier, 8u, 9u, "b"},
                            {TokenKind::EndOfFile, 9u, 9u, ""}}));
        // 行注释到文件末尾（没有换行）。
        CHECK(check_tokens("x // no newline at eof",
                           {{TokenKind::Identifier, 0u, 1u, "x"},
                            {TokenKind::EndOfFile, 22u, 22u, ""}}));
        CHECK(check_kinds("//", {TokenKind::EndOfFile}));
        CHECK(check_kinds("/**/", {TokenKind::EndOfFile}));
        CHECK(check_kinds("/* a */ /* b */ x", {TokenKind::Identifier, TokenKind::EndOfFile}));
    }

    // ---- span 精确值 ----
    {
        CHECK(check_tokens("new value = 42;",
                           {{TokenKind::KwNew, 0u, 3u, "new"},
                            {TokenKind::Identifier, 4u, 9u, "value"},
                            {TokenKind::Assign, 10u, 11u, "="},
                            {TokenKind::IntegerLiteral, 12u, 14u, "42"},
                            {TokenKind::Semicolon, 14u, 15u, ";"},
                            {TokenKind::EndOfFile, 15u, 15u, ""}}));
        // 语言里的方法声明不带 fn 关键字。
        CHECK(check_tokens("draw(canvas: &Canvas): void;",
                           {{TokenKind::Identifier, 0u, 4u, "draw"},
                            {TokenKind::LParen, 4u, 5u, "("},
                            {TokenKind::Identifier, 5u, 11u, "canvas"},
                            {TokenKind::Colon, 11u, 12u, ":"},
                            {TokenKind::Amp, 13u, 14u, "&"},
                            {TokenKind::Identifier, 14u, 20u, "Canvas"},
                            {TokenKind::RParen, 20u, 21u, ")"},
                            {TokenKind::Colon, 21u, 22u, ":"},
                            {TokenKind::KwVoid, 23u, 27u, "void"},
                            {TokenKind::Semicolon, 27u, 28u, ";"},
                            {TokenKind::EndOfFile, 28u, 28u, ""}}));
        CHECK(check_tokens("new x: Int? = null;",
                           {{TokenKind::KwNew, 0u, 3u, "new"},
                            {TokenKind::Identifier, 4u, 5u, "x"},
                            {TokenKind::Colon, 5u, 6u, ":"},
                            {TokenKind::Identifier, 7u, 10u, "Int"},
                            {TokenKind::Question, 10u, 11u, "?"},
                            {TokenKind::Assign, 12u, 13u, "="},
                            {TokenKind::KwNull, 14u, 18u, "null"},
                            {TokenKind::Semicolon, 18u, 19u, ";"},
                            {TokenKind::EndOfFile, 19u, 19u, ""}}));
    }

    // ---- 错误案例：未闭合字符串，后续 token 不丢 ----
    {
        const std::string_view source = "\"abc";
        const LexResult result = lex(source);
        CHECK(result.tokens.size() == 2u);
        CHECK(result.tokens[0].kind == TokenKind::Error);
        CHECK(result.tokens[0].span.begin == 0u);
        CHECK(result.tokens[0].span.end == 4u);
        CHECK(result.tokens[0].text == "\"abc");
        CHECK(result.tokens[1].kind == TokenKind::EndOfFile);
        CHECK(result.tokens[1].span.begin == 4u);
        CHECK(result.tokens[1].span.end == 4u);
        CHECK(result.diagnostics.error_count() == 1u);
        CHECK(has_error_code(source, result, "E1001"));

        // 字符串不跨行：遇到换行先报未闭合，换行之后的标识符照常产出。
        const std::string_view multiline = "\"ab\ncd";
        const LexResult second = lex(multiline);
        CHECK(second.tokens.size() == 3u);
        CHECK(second.tokens[0].kind == TokenKind::Error);
        CHECK(second.tokens[0].span.begin == 0u);
        CHECK(second.tokens[0].span.end == 3u);
        CHECK(second.tokens[1].kind == TokenKind::Identifier);
        CHECK(second.tokens[1].span.begin == 4u);
        CHECK(second.tokens[1].span.end == 6u);
        CHECK(second.tokens[2].kind == TokenKind::EndOfFile);
        CHECK(second.diagnostics.error_count() == 1u);
        CHECK(has_error_code(multiline, second, "E1001"));
    }

    // ---- 错误案例：非法字符（含非 ASCII），保证前进且不丢后续 token ----
    {
        const std::string_view source = "new $x = 1;";
        const LexResult result = lex(source);
        CHECK(result.tokens.size() == 7u);
        CHECK(result.tokens[0].kind == TokenKind::KwNew);
        CHECK(result.tokens[1].kind == TokenKind::Error);
        CHECK(result.tokens[1].span.begin == 4u);
        CHECK(result.tokens[1].span.end == 5u);
        CHECK(result.tokens[1].text == "$");
        CHECK(result.tokens[2].kind == TokenKind::Identifier);
        CHECK(result.tokens[2].text == "x");
        CHECK(result.tokens[3].kind == TokenKind::Assign);
        CHECK(result.tokens[4].kind == TokenKind::IntegerLiteral);
        CHECK(result.tokens[5].kind == TokenKind::Semicolon);
        CHECK(result.tokens[6].kind == TokenKind::EndOfFile);
        CHECK(result.diagnostics.error_count() == 1u);
        CHECK(has_error_code(source, result, "E1003"));

        // 连续两个非法字符各自报错，游标必须前进。
        CHECK(check_kinds("$#", {TokenKind::Error, TokenKind::Error, TokenKind::EndOfFile}));
        CHECK(check_single_error("$", "E1003"));
        CHECK(check_single_error("#", "E1003"));
        CHECK(check_single_error("\\", "E1003"));

        // 非 ASCII（UTF-8 多字节）逐码点报错：`变量` 两个汉字、两个 Error token。
        const std::string_view unicode = "new 变量 = 1;";
        const LexResult cjk = lex(unicode);
        CHECK(cjk.tokens.size() == 7u);
        CHECK(cjk.tokens[0].kind == TokenKind::KwNew);
        CHECK(cjk.tokens[1].kind == TokenKind::Error);
        CHECK(cjk.tokens[1].span.begin == 4u);
        CHECK(cjk.tokens[1].span.end == 7u);
        CHECK(cjk.tokens[2].kind == TokenKind::Error);
        CHECK(cjk.tokens[2].span.begin == 7u);
        CHECK(cjk.tokens[2].span.end == 10u);
        CHECK(cjk.tokens[3].kind == TokenKind::Assign);
        CHECK(cjk.tokens[4].kind == TokenKind::IntegerLiteral);
        CHECK(cjk.tokens[5].kind == TokenKind::Semicolon);
        CHECK(cjk.tokens[6].kind == TokenKind::EndOfFile);
        CHECK(cjk.diagnostics.error_count() == 2u);
        CHECK(has_error_code(unicode, cjk, "E1003"));
    }

    // ---- 错误案例：非法数字字面量 ----
    {
        CHECK(check_single_error("0x", "E1004"));
        CHECK(check_single_error("0X", "E1004"));
        CHECK(check_single_error("0b", "E1004"));
        CHECK(check_single_error("0b2", "E1004"));
        CHECK(check_single_error("1e", "E1004"));
        CHECK(check_single_error("1e+", "E1004"));
        CHECK(check_single_error("12ab", "E1004"));
        CHECK(check_single_error("0x1g", "E1004"));
        CHECK(check_single_error("1.5f", "E1004"));
    }

    // ---- 错误案例：字符字面量 ----
    {
        CHECK(check_single_error("''", "E1006"));
        CHECK(check_single_error("'ab'", "E1006"));
        CHECK(check_single_error("'a", "E1005"));
    }

    // ---- 错误案例：未闭合块注释整段丢弃，但仍以 EndOfFile 收尾 ----
    {
        CHECK(check_eof_only_with_error("/* unclosed", "E1002"));
        CHECK(check_eof_only_with_error("/*", "E1002"));

        const std::string_view source = "new x; /* unclosed";
        const LexResult result = lex(source);
        CHECK(result.tokens.size() == 4u);
        CHECK(result.tokens[0].kind == TokenKind::KwNew);
        CHECK(result.tokens[1].kind == TokenKind::Identifier);
        CHECK(result.tokens[2].kind == TokenKind::Semicolon);
        CHECK(result.tokens[3].kind == TokenKind::EndOfFile);
        CHECK(result.tokens[3].span.begin == source.size());
        CHECK(result.tokens[3].span.end == source.size());
        CHECK(result.diagnostics.error_count() == 1u);
        CHECK(has_error_code(source, result, "E1002"));
    }

    // ---- 错误案例：未知转义序列（报错但保留字面量 token）----
    {
        const std::string_view source = R"("a\qb")";
        const LexResult result = lex(source);
        CHECK(result.tokens.size() == 2u);
        CHECK(result.tokens[0].kind == TokenKind::StringLiteral);
        CHECK(std::string_view(result.tokens[0].text) == source);
        CHECK(result.diagnostics.error_count() == 1u);
        CHECK(has_error_code(source, result, "E1007"));

        CHECK(check_single_error("'a", "E1005"));
        const std::string_view char_source = "'\\q'";
        const LexResult char_result = lex(char_source);
        CHECK(char_result.tokens.size() == 2u);
        CHECK(char_result.tokens[0].kind == TokenKind::CharLiteral);
        CHECK(char_result.diagnostics.error_count() == 1u);
        CHECK(has_error_code(char_source, char_result, "E1007"));
    }

    // ---- Token 辅助接口 ----
    {
        Token token;
        token.kind = TokenKind::IntegerLiteral;
        CHECK(token.is_literal());
        CHECK(!token.is_keyword());
        token.kind = TokenKind::FloatLiteral;
        CHECK(token.is_literal());
        token.kind = TokenKind::StringLiteral;
        CHECK(token.is_literal());
        token.kind = TokenKind::CharLiteral;
        CHECK(token.is_literal());
        token.kind = TokenKind::KwNew;
        CHECK(token.is_keyword());
        CHECK(!token.is_literal());
        token.kind = TokenKind::KwTrue;
        CHECK(!token.is_keyword());
        CHECK(token.is_literal());
        token.kind = TokenKind::KwFalse;
        CHECK(!token.is_keyword());
        CHECK(token.is_literal());
        token.kind = TokenKind::KwNull;
        CHECK(!token.is_keyword());
        CHECK(token.is_literal());
        token.kind = TokenKind::Identifier;
        CHECK(!token.is_keyword());
        CHECK(!token.is_literal());
        token.kind = TokenKind::Semicolon;
        CHECK(!token.is_keyword());
        CHECK(!token.is_literal());
        token.kind = TokenKind::EndOfFile;
        CHECK(!token.is_keyword());
        CHECK(!token.is_literal());

        CHECK(token_kind_name(TokenKind::Identifier) == "Identifier");
        CHECK(token_kind_name(TokenKind::EndOfFile) == "EndOfFile");
        CHECK(token_kind_name(TokenKind::KwRepeatable) == "KwRepeatable");
        CHECK(token_kind_name(TokenKind::PercentAssign) == "PercentAssign");
        CHECK(token_kind_name(TokenKind::Error) == "Error");
        CHECK(token_kind_name(TokenKind::Amp) == "Amp");
    }

    // ---- 综合片段：模块、类、字段、标注、方法声明 ----
    {
        const std::string_view source =
            "module geometry;\n"
            "\n"
            "export class Circle : Shape\n"
            "{\n"
            "    new radius: f64 = 1.5;\n"
            "\n"
            "    @Override\n"
            "    draw(canvas: &Canvas): void;\n"
            "}\n";
        const LexResult result = lex(source);
        CHECK(result.diagnostics.error_count() == 0u);
        CHECK(result.tokens.size() == 30u);
        CHECK(result.tokens[0].kind == TokenKind::KwModule);
        CHECK(result.tokens[1].kind == TokenKind::Identifier);
        CHECK(result.tokens[2].kind == TokenKind::Semicolon);
        CHECK(result.tokens[3].kind == TokenKind::KwExport);
        CHECK(result.tokens[4].kind == TokenKind::KwClass);
        CHECK(result.tokens[5].text == "Circle");
        CHECK(result.tokens[8].kind == TokenKind::LBrace);
        CHECK(result.tokens[14].kind == TokenKind::FloatLiteral);
        CHECK(result.tokens[16].kind == TokenKind::At);
        CHECK(result.tokens[17].kind == TokenKind::Identifier);
        CHECK(result.tokens[17].text == "Override");
        CHECK(result.tokens[22].kind == TokenKind::Amp);
        CHECK(result.tokens[26].kind == TokenKind::KwVoid);
        CHECK(result.tokens[28].kind == TokenKind::RBrace);
        CHECK(result.tokens[29].kind == TokenKind::EndOfFile);
        CHECK(result.tokens[29].span.begin == source.size());
        CHECK(result.tokens[29].span.end == source.size());

        // 所有 token：同源、span 合法、按源序不重叠、原文与 span 切片一致。
        std::uint32_t previous_end = 0;
        for (const Token& token : result.tokens)
        {
            CHECK(token.span.source == kSource);
            CHECK(token.span.begin <= token.span.end);
            CHECK(token.span.end <= source.size());
            CHECK(token.span.begin >= previous_end);
            previous_end = token.span.end;
            if (token.kind != TokenKind::EndOfFile)
            {
                CHECK(std::string_view(token.text) == source.substr(token.span.begin, token.span.length()));
            }
        }
    }

    // ---- 同一个 Lexer 可以重复 tokenize（每次都从头开始）----
    {
        DiagnosticBag diagnostics;
        Lexer lexer(kSource, "new x = 1;", diagnostics);
        const std::vector<Token> first = lexer.tokenize();
        const std::vector<Token> second = lexer.tokenize();
        CHECK(first.size() == 6u);
        CHECK(second.size() == 6u);
        CHECK(first.back().kind == TokenKind::EndOfFile);
        CHECK(second.back().kind == TokenKind::EndOfFile);
        CHECK(first.back().span.begin == 10u);
        CHECK(diagnostics.error_count() == 0u);
    }

    std::cout << "PASS: lexer" << std::endl;
    return 0;
}
