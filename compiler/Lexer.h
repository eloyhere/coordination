#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostic.h"
#include "Span.h"
#include "Token.h"

namespace coordination::compiler
{
    // 词法分析器：把源文本切成 Token 序列，错误写入 DiagnosticBag。
    //
    // 设计约束：
    //   * 纯头文件、无异常：任何输入（含乱码、未闭合字面量）都以 EndOfFile 收尾；
    //   * 每个循环分支都必须让游标前进，避免死循环；
    //   * Token.text 一律保存原文切片，不做解码（解码留给常量求值阶段）。
    //
    // 错误码（E1xxx 段，供工具链稳定引用）：
    //   E1001 未闭合字符串  E1002 未闭合块注释  E1003 非法字符
    //   E1004 非法数字字面量  E1005 未闭合字符字面量
    //   E1006 非法字符字面量（空/多字符）  E1007 未知转义序列
    class Lexer
    {
    public:
        Lexer(SourceId source, std::string_view text, DiagnosticBag& diagnostics)
            : source_(source),
              text_(text),
              diagnostics_(diagnostics)
        {
        }

        // 切分整个源文本；末尾恒有一个零宽 EndOfFile（位于 text.size()）。
        // 可重复调用，每次从头开始（诊断会再次追加，调用方自行去重）。
        std::vector<Token> tokenize()
        {
            index_ = 0;
            std::vector<Token> tokens;
            for (;;)
            {
                skip_trivia();
                if (at_end())
                {
                    tokens.push_back(make_token(TokenKind::EndOfFile, index_, index_));
                    return tokens;
                }
                tokens.push_back(scan_token());
            }
        }

    private:
        // ---------------- 游标 ----------------

        bool at_end() const noexcept
        {
            return index_ >= text_.size();
        }

        // 越界返回 '\0'，由调用方保证循环前进。
        char peek(std::size_t offset = 0) const noexcept
        {
            const std::size_t position = index_ + offset;
            return position < text_.size() ? text_[position] : '\0';
        }

        // 若下一个字符是 expected 则吃掉并返回 true。
        bool match(char expected) noexcept
        {
            if (peek() != expected)
            {
                return false;
            }
            ++index_;
            return true;
        }

        // ---------------- 字符分类 ----------------

        static bool is_digit(char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        static bool is_hex_digit(char c) noexcept
        {
            return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        static bool is_binary_digit(char c) noexcept
        {
            return c == '0' || c == '1';
        }

        static bool is_identifier_start(char c) noexcept
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        static bool is_identifier_continue(char c) noexcept
        {
            return is_identifier_start(c) || is_digit(c);
        }

        static bool is_space(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        // ---------------- Token 构造 ----------------

        Span make_span(std::size_t begin, std::size_t end) const noexcept
        {
            return Span::from_bounds(source_, static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(end));
        }

        Token make_token(TokenKind kind, std::size_t begin, std::size_t end) const
        {
            Token token;
            token.kind = kind;
            token.span = make_span(begin, end);
            token.text.assign(text_.substr(begin, end - begin));
            return token;
        }

        void report(std::string code, std::string message, std::size_t begin, std::size_t end)
        {
            diagnostics_.error(std::move(code), std::move(message), make_span(begin, end));
        }

        // ---------------- 空白与注释 ----------------

        // 跳过空白、// 行注释、/* */ 块注释（不支持嵌套，遇到第一个 */ 即结束）。
        void skip_trivia()
        {
            while (!at_end())
            {
                const char c = peek();
                if (is_space(c))
                {
                    ++index_;
                    continue;
                }
                if (c == '/' && peek(1) == '/')
                {
                    index_ += 2;
                    while (!at_end() && peek() != '\n' && peek() != '\r')
                    {
                        ++index_;
                    }
                    continue;
                }
                if (c == '/' && peek(1) == '*')
                {
                    skip_block_comment();
                    continue;
                }
                break;
            }
        }

        void skip_block_comment()
        {
            const std::size_t begin = index_;
            index_ += 2;
            while (!at_end())
            {
                if (peek() == '*' && peek(1) == '/')
                {
                    index_ += 2;
                    return;
                }
                ++index_;
            }
            // 到达文件末尾仍未闭合：整段注释丢弃，只报一次错。
            report("E1002", "未闭合的块注释", begin, index_);
        }

        // ---------------- 主分派 ----------------

        Token scan_token()
        {
            const char c = peek();
            if (is_identifier_start(c))
            {
                return scan_identifier();
            }
            if (is_digit(c))
            {
                return scan_number();
            }
            switch (c)
            {
            case '"': return scan_string();
            case '\'': return scan_char();
            default: break;
            }
            if (is_punctuation(c))
            {
                return scan_punctuation();
            }
            return scan_illegal_character();
        }

        static bool is_punctuation(char c) noexcept
        {
            // 与 scan_punctuation 的 switch 保持一致。
            constexpr std::string_view kPunctuation = "{}()[];,.:@?+-*/%=<>!~&|";
            return kPunctuation.find(c) != std::string_view::npos;
        }

        // ---------------- 标识符与关键字 ----------------

        Token scan_identifier()
        {
            const std::size_t begin = index_;
            while (!at_end() && is_identifier_continue(peek()))
            {
                ++index_;
            }
            const std::string_view spelling = text_.substr(begin, index_ - begin);
            return make_token(keyword_kind(spelling), begin, index_);
        }

        // 大小写敏感：let 是关键字，Let 只是标识符；标注名 @Override 也不是关键字。
        static TokenKind keyword_kind(std::string_view spelling) noexcept
        {
            if (spelling == "export") return TokenKind::KwExport;
            if (spelling == "import") return TokenKind::KwImport;
            if (spelling == "module") return TokenKind::KwModule;
            if (spelling == "interface") return TokenKind::KwInterface;
            if (spelling == "class") return TokenKind::KwClass;
            if (spelling == "concept") return TokenKind::KwConcept;
            if (spelling == "annotation") return TokenKind::KwAnnotation;
            if (spelling == "type") return TokenKind::KwType;
            if (spelling == "distinct") return TokenKind::KwDistinct;
            if (spelling == "override") return TokenKind::KwOverride;
            if (spelling == "hide") return TokenKind::KwHide;
            if (spelling == "open") return TokenKind::KwOpen;
            if (spelling == "final") return TokenKind::KwFinal;
                    if (spelling == "const") return TokenKind::KwConst;
            if (spelling == "init") return TokenKind::KwInit;
            if (spelling == "deinit") return TokenKind::KwDeinit;
            if (spelling == "return") return TokenKind::KwReturn;
            if (spelling == "if") return TokenKind::KwIf;
            if (spelling == "else") return TokenKind::KwElse;
            if (spelling == "while") return TokenKind::KwWhile;
            if (spelling == "for") return TokenKind::KwFor;
            if (spelling == "break") return TokenKind::KwBreak;
            if (spelling == "continue") return TokenKind::KwContinue;
            if (spelling == "comptime") return TokenKind::KwComptime;
            if (spelling == "operator") return TokenKind::KwOperator;
            if (spelling == "out") return TokenKind::KwOut;
            if (spelling == "in") return TokenKind::KwIn;
            if (spelling == "void") return TokenKind::KwVoid;
            if (spelling == "this") return TokenKind::KwThis;
            if (spelling == "super") return TokenKind::KwSuper;
            if (spelling == "true") return TokenKind::KwTrue;
            if (spelling == "false") return TokenKind::KwFalse;
            if (spelling == "null") return TokenKind::KwNull;
            if (spelling == "dyn") return TokenKind::KwDyn;
            if (spelling == "as") return TokenKind::KwAs;
            if (spelling == "is") return TokenKind::KwIs;
            if (spelling == "new") return TokenKind::KwNew;
            if (spelling == "repeatable") return TokenKind::KwRepeatable;
            return TokenKind::Identifier;
        }

        // ---------------- 数字 ----------------

        void scan_decimal_digits() noexcept
        {
            while (!at_end() && (is_digit(peek()) || peek() == '_'))
            {
                ++index_;
            }
        }

        // 十进制 / 0x 十六进制 / 0b 二进制整数，以及带小数点或指数的浮点数。
        // 规则细节：
        //   * '_' 可作数字分隔符，不参与计数；
        //   * 有小数点或指数即为浮点，`1.` 也是浮点（小数点后允许零位）；
        //   * 十六进制/二进制不支持小数点，`0x1.5` 会被切成 0x1 . 5；
        //   * 数字后紧跟标识符字符（12ab、0xFG、1.5f）视为非法数字，整段吞掉报一次错。
        Token scan_number()
        {
            const std::size_t begin = index_;
            bool is_float = false;
            bool valid = true;

            if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X'))
            {
                index_ += 2;
                std::size_t digits = 0;
                while (!at_end() && (is_hex_digit(peek()) || peek() == '_'))
                {
                    if (peek() != '_')
                    {
                        ++digits;
                    }
                    ++index_;
                }
                valid = digits != 0;
            }
            else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B'))
            {
                index_ += 2;
                std::size_t digits = 0;
                while (!at_end() && (is_binary_digit(peek()) || peek() == '_'))
                {
                    if (peek() != '_')
                    {
                        ++digits;
                    }
                    ++index_;
                }
                valid = digits != 0;
            }
            else
            {
                scan_decimal_digits();
                if (peek() == '.')
                {
                    is_float = true;
                    ++index_;
                    scan_decimal_digits();
                }
                if (peek() == 'e' || peek() == 'E')
                {
                    is_float = true;
                    ++index_;
                    if (peek() == '+' || peek() == '-')
                    {
                        ++index_;
                    }
                    const std::size_t exponent_begin = index_;
                    scan_decimal_digits();
                    // 指数部分必须至少有一位数字。
                    valid = exponent_begin != index_;
                }
            }

            if (is_identifier_continue(peek()))
            {
                // 非法后缀：吞掉剩余标识符字符，只报一次错，避免逐字符报错。
                valid = false;
                while (!at_end() && is_identifier_continue(peek()))
                {
                    ++index_;
                }
            }

            if (!valid)
            {
                report("E1004", "非法的数字字面量", begin, index_);
                return make_token(TokenKind::Error, begin, index_);
            }
            return make_token(is_float ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral, begin, index_);
        }

        // ---------------- 字符串与字符 ----------------

        Token scan_string()
        {
            const std::size_t begin = index_;
            ++index_; // 起始双引号
            for (;;)
            {
                if (at_end() || peek() == '\n' || peek() == '\r')
                {
                    // 字符串不跨行：遇到换行或文件尾即判未闭合，错误 token 不含换行。
                    report("E1001", "未闭合的字符串字面量", begin, index_);
                    return make_token(TokenKind::Error, begin, index_);
                }
                const char c = peek();
                if (c == '"')
                {
                    ++index_;
                    return make_token(TokenKind::StringLiteral, begin, index_);
                }
                if (c == '\\')
                {
                    scan_escape();
                    continue;
                }
                ++index_;
            }
        }

        Token scan_char()
        {
            const std::size_t begin = index_;
            ++index_; // 起始单引号
            if (at_end() || peek() == '\n' || peek() == '\r')
            {
                report("E1005", "未闭合的字符字面量", begin, index_);
                return make_token(TokenKind::Error, begin, index_);
            }
            if (peek() == '\'')
            {
                ++index_;
                report("E1006", "字符字面量不能为空", begin, index_);
                return make_token(TokenKind::Error, begin, index_);
            }
            // 恰好一个字符，或一个转义序列。
            if (peek() == '\\')
            {
                scan_escape();
            }
            else
            {
                ++index_;
            }
            if (peek() == '\'')
            {
                ++index_;
                return make_token(TokenKind::CharLiteral, begin, index_);
            }
            // 多字符（'ab'）或缺少收尾引号：吞到引号/行尾后报一次错。
            while (!at_end() && peek() != '\'' && peek() != '\n' && peek() != '\r')
            {
                ++index_;
            }
            if (peek() == '\'')
            {
                ++index_;
                report("E1006", "字符字面量只能包含一个字符", begin, index_);
                return make_token(TokenKind::Error, begin, index_);
            }
            report("E1005", "未闭合的字符字面量", begin, index_);
            return make_token(TokenKind::Error, begin, index_);
        }

        // 支持 \" \\ \n \t \r \0 以及字符字面量需要的 \'。
        // 未知转义报 E1007，但仍然前进，字面量 token 本身保留给上层继续解析。
        void scan_escape()
        {
            const std::size_t begin = index_;
            ++index_; // 反斜杠
            if (at_end())
            {
                return; // 由外层按"未闭合"处理
            }
            const char c = peek();
            if (c == '"' || c == '\'' || c == '\\' || c == 'n' || c == 't' || c == 'r' || c == '0')
            {
                ++index_;
                return;
            }
            ++index_;
            report("E1007", "未知的转义序列", begin, index_);
        }

        // ---------------- 标点与运算符 ----------------

        // 双字符运算符的最长匹配辅助：命中 second 就产出 doubled，否则产出 single。
        // 注意：不能写成 make_token(match(second) ? ... : ..., begin, index_)——函数实参求值
        // 顺序未指定，index_ 可能先于 match 读取，导致 span 少一个字节。
        Token scan_optional_double(std::size_t begin, char second, TokenKind single, TokenKind doubled)
        {
            if (match(second))
            {
                return make_token(doubled, begin, index_);
            }
            return make_token(single, begin, index_);
        }

        // 最长匹配：双字符运算符优先于单字符。
        Token scan_punctuation()
        {
            const std::size_t begin = index_;
            const char c = peek();
            ++index_;
            switch (c)
            {
            case '{': return make_token(TokenKind::LBrace, begin, index_);
            case '}': return make_token(TokenKind::RBrace, begin, index_);
            case '(': return make_token(TokenKind::LParen, begin, index_);
            case ')': return make_token(TokenKind::RParen, begin, index_);
            case '[': return make_token(TokenKind::LBracket, begin, index_);
            case ']': return make_token(TokenKind::RBracket, begin, index_);
            case ';': return make_token(TokenKind::Semicolon, begin, index_);
            case ',': return make_token(TokenKind::Comma, begin, index_);
            case '.': return make_token(TokenKind::Dot, begin, index_);
            case ':': return make_token(TokenKind::Colon, begin, index_);
            case '@': return make_token(TokenKind::At, begin, index_);
            case '?': return make_token(TokenKind::Question, begin, index_);
            case '~': return make_token(TokenKind::Tilde, begin, index_);
            case '+': return scan_optional_double(begin, '=', TokenKind::Plus, TokenKind::PlusAssign);
            case '-': return scan_optional_double(begin, '=', TokenKind::Minus, TokenKind::MinusAssign);
            case '*': return scan_optional_double(begin, '=', TokenKind::Star, TokenKind::StarAssign);
            case '/': return scan_optional_double(begin, '=', TokenKind::Slash, TokenKind::SlashAssign);
            case '%': return scan_optional_double(begin, '=', TokenKind::Percent, TokenKind::PercentAssign);
            case '=': return scan_optional_double(begin, '=', TokenKind::Assign, TokenKind::Eq);
            case '!': return scan_optional_double(begin, '=', TokenKind::Bang, TokenKind::Ne);
            case '<': return scan_optional_double(begin, '=', TokenKind::Lt, TokenKind::Le);
            case '>': return scan_optional_double(begin, '=', TokenKind::Gt, TokenKind::Ge);
            case '&': return scan_optional_double(begin, '&', TokenKind::Amp, TokenKind::AmpAmp);
            case '|': return scan_optional_double(begin, '|', TokenKind::Pipe, TokenKind::PipePipe);
            default:
                break;
            }
            // is_punctuation 已过滤，理论上不可达；保底也要前进并给出错误 token。
            report("E1003", "非法的标点符号", begin, index_);
            return make_token(TokenKind::Error, begin, index_);
        }

        // ---------------- 非法字符 ----------------

        Token scan_illegal_character()
        {
            const std::size_t begin = index_;
            const unsigned char byte = static_cast<unsigned char>(peek());
            ++index_;
            if (byte >= 0x80)
            {
                // 非 ASCII：把整个 UTF-8 码点算作一个非法字符，避免逐字节刷屏。
                while (!at_end() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80)
                {
                    ++index_;
                }
            }
            report("E1003", "源码中出现非法字符", begin, index_);
            return make_token(TokenKind::Error, begin, index_);
        }

        SourceId source_;
        std::string_view text_;
        DiagnosticBag& diagnostics_;
        std::size_t index_ = 0;
    };
}
