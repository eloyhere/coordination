// SourceManager / DiagnosticPrinter 自包含测试：不依赖任何第三方测试框架。
// 覆盖行列表构造、1 基行列换算（首行、行尾、EOF、CRLF、空文件）、line_text、
// slice、越界行为，以及诊断格式化（错误/警告/note、错误码开关、源码行开关、
// notes、无效/越界 span、多诊断顺序）。全部通过时打印 PASS 并返回 0。

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/DiagnosticPrinter.h"
#include "compiler/Source.h"

namespace
{
    using coordination::compiler::Diagnostic;
    using coordination::compiler::DiagnosticBag;
    using coordination::compiler::format_diagnostic;
    using coordination::compiler::format_diagnostics;
    using coordination::compiler::LineColumn;
    using coordination::compiler::PrintOptions;
    using coordination::compiler::Severity;
    using coordination::compiler::SourceId;
    using coordination::compiler::SourceManager;
    using coordination::compiler::Span;

    constexpr SourceId kMissingSource = 999u;

    // 统一断言入口：失败时打印 "FAIL: 表达式 (行号)"。
    bool expect(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            std::cout << "FAIL: " << expression << " (line " << line << ")\n";
        }

        return condition;
    }

    // 把换行/制表符转义，便于在失败信息里看清差异。
    std::string escape(std::string_view text)
    {
        std::string result;
        for (char character : text)
        {
            switch (character)
            {
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += character;
                break;
            }
        }

        return result;
    }

    bool expect_equal(std::string_view actual, std::string_view expected, const char* expression, int line)
    {
        if (actual == expected)
        {
            return true;
        }

        std::cout << "FAIL: " << expression << " (line " << line << ")\n";
        std::cout << "  expected: [" << escape(expected) << "]\n";
        std::cout << "  actual:   [" << escape(actual) << "]\n";
        return false;
    }

    bool check_line_column(const SourceManager& sources, SourceId id, std::uint32_t offset,
        std::uint32_t line, std::uint32_t column, int source_line)
    {
        const LineColumn actual = sources.line_column(id, offset);
        if (actual.line == line && actual.column == column)
        {
            return true;
        }

        std::cout << "FAIL: line_column(" << offset << ") == " << line << ':' << column
            << ", actual " << actual.line << ':' << actual.column
            << " (line " << source_line << ")\n";
        return false;
    }

    template <typename Function>
    bool throws_out_of_range(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::out_of_range&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }

        return false;
    }
}

// 只能作为独立语句使用；失败时从当前测试函数返回 false。
#define CHECK(condition) \
    if (!expect((condition), #condition, __LINE__)) \
    { \
        return false; \
    }

#define CHECK_EQ(actual, expected) \
    if (!expect_equal((actual), (expected), #actual " == " #expected, __LINE__)) \
    { \
        return false; \
    }

// 依赖当前作用域里的 SourceManager 变量 sources。
#define CHECK_LC(id, offset, line, column) \
    if (!check_line_column(sources, (id), (offset), (line), (column), __LINE__)) \
    { \
        return false; \
    }

namespace
{
    bool test_add_file_and_contains()
    {
        SourceManager sources;
        CHECK(sources.file_count() == 0u);

        const SourceId first = sources.add_file("first.coord", "one");
        const SourceId second = sources.add_file("second.coord", "two\n");

        CHECK(first == 0u);
        CHECK(second == 1u);
        CHECK(sources.file_count() == 2u);
        CHECK(sources.contains(first));
        CHECK(sources.contains(second));
        CHECK(!sources.contains(2u));
        CHECK(sources.file(first).id == first);
        CHECK(sources.file(second).id == second);
        CHECK_EQ(sources.name(first), std::string_view("first.coord"));
        CHECK_EQ(sources.text(first), std::string_view("one"));
        CHECK_EQ(sources.text(second), std::string_view("two\n"));

        // add_file 复制文本，调用方后续修改原串不影响已保存内容。
        std::string owned = "mutable";
        const SourceId third = sources.add_file("third.coord", owned);
        owned[0] = 'X';
        CHECK_EQ(sources.text(third), std::string_view("mutable"));
        CHECK(sources.file_count() == 3u);

        return true;
    }

    bool test_line_column()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        // "alpha\n" 6 字节，"beta\r\n" 6 字节，"gamma\n" 6 字节，末尾换行后还有空行。
        const std::vector<std::uint32_t> starts = sources.file(source).line_starts;
        CHECK(starts.size() == 4u);
        CHECK(starts[0] == 0u);
        CHECK(starts[1] == 6u);
        CHECK(starts[2] == 12u);
        CHECK(starts[3] == 18u);

        CHECK_LC(source, 0u, 1u, 1u);    // 首行首列
        CHECK_LC(source, 4u, 1u, 5u);    // 首行最后一个字符
        CHECK_LC(source, 5u, 1u, 6u);    // 首行换行符
        CHECK_LC(source, 6u, 2u, 1u);    // 第二行首列
        CHECK_LC(source, 9u, 2u, 4u);    // 第二行最后字符 'a'
        CHECK_LC(source, 10u, 2u, 5u);   // CRLF 的 '\r'
        CHECK_LC(source, 11u, 2u, 6u);   // CRLF 的 '\n'
        CHECK_LC(source, 12u, 3u, 1u);   // 第三行首列
        CHECK_LC(source, 17u, 3u, 6u);   // 第三行换行符
        CHECK_LC(source, 18u, 4u, 1u);   // 文件末尾（末尾换行产生的空行）
        CHECK_LC(source, 100u, 4u, 1u);  // 越界 offset clamp 到文本末尾

        // 空文件仍有第 1 行。
        const SourceId empty = sources.add_file("empty.coord", "");
        CHECK(sources.file(empty).line_starts.size() == 1u);
        CHECK(sources.file(empty).line_starts[0] == 0u);
        CHECK_LC(empty, 0u, 1u, 1u);
        CHECK_LC(empty, 42u, 1u, 1u);

        // 没有末尾换行时，EOF 落在最后一行的行尾之后。
        const SourceId tail = sources.add_file("tail.coord", "x\ny");
        CHECK(sources.file(tail).line_starts.size() == 2u);
        CHECK_LC(tail, 3u, 2u, 2u);
        CHECK_LC(tail, 100u, 2u, 2u);

        // 列号按 UTF-8 字节计：'变' 占 3 字节，'量' 从第 4 列开始。
        const SourceId unicode = sources.add_file("unicode.coord", "变量\n");
        CHECK(sources.text(unicode).size() == 7u);
        CHECK_LC(unicode, 0u, 1u, 1u);
        CHECK_LC(unicode, 3u, 1u, 4u);

        return true;
    }

    bool test_line_text()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        CHECK_EQ(sources.line_text(source, 1u), std::string_view("alpha"));
        CHECK_EQ(sources.line_text(source, 2u), std::string_view("beta"));   // CRLF 的 \r 被去掉
        CHECK_EQ(sources.line_text(source, 3u), std::string_view("gamma"));
        CHECK_EQ(sources.line_text(source, 4u), std::string_view(""));       // 末尾换行后的空行
        CHECK(sources.line_text(source, 0u).empty());
        CHECK(sources.line_text(source, 5u).empty());
        CHECK(sources.line_text(source, 999u).empty());

        // 中间空行的行号仍然保留。
        const SourceId blanks = sources.add_file("blanks.coord", "a\n\nb\n");
        CHECK(sources.file(blanks).line_starts.size() == 4u);
        CHECK_EQ(sources.line_text(blanks, 1u), std::string_view("a"));
        CHECK_EQ(sources.line_text(blanks, 2u), std::string_view(""));
        CHECK_EQ(sources.line_text(blanks, 3u), std::string_view("b"));
        CHECK_EQ(sources.line_text(blanks, 4u), std::string_view(""));

        // 无末尾换行：最后一行不包含任何行尾字符。
        const SourceId tail = sources.add_file("tail.coord", "x\ny");
        CHECK_EQ(sources.line_text(tail, 2u), std::string_view("y"));

        // 空文件也有第 1 行，内容为空。
        const SourceId empty = sources.add_file("empty.coord", "");
        CHECK_EQ(sources.line_text(empty, 1u), std::string_view(""));
        CHECK(sources.line_text(empty, 2u).empty());

        return true;
    }

    bool test_slice()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        CHECK_EQ(sources.slice(Span{source, 0u, 5u}), std::string_view("alpha"));
        CHECK_EQ(sources.slice(Span{source, 6u, 10u}), std::string_view("beta"));
        CHECK_EQ(sources.slice(Span{source, 10u, 12u}), std::string_view("\r\n"));
        CHECK_EQ(sources.slice(Span{source, 6u, 18u}), std::string_view("beta\r\ngamma\n"));
        CHECK_EQ(sources.slice(Span{source, 0u, 18u}), sources.text(source));
        CHECK_EQ(sources.slice(Span{source, 12u, 12u}), std::string_view(""));
        CHECK_EQ(sources.slice(Span{source, 18u, 18u}), std::string_view(""));  // EOF 空区间合法
        CHECK_EQ(sources.slice(Span{source, 6u, 100u}), std::string_view("beta\r\ngamma\n"));  // end clamp
        CHECK_EQ(sources.slice(Span{source, 100u, 200u}), std::string_view(""));
        CHECK_EQ(sources.slice(Span{}), std::string_view(""));
        CHECK_EQ(sources.slice(Span{kMissingSource, 0u, 3u}), std::string_view(""));

        return true;
    }

    bool test_out_of_range()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("ok.coord", "ok");

        CHECK(sources.contains(source));
        CHECK(!sources.contains(kMissingSource));

        CHECK(throws_out_of_range([&sources]() { (void)sources.file(kMissingSource); }));
        CHECK(throws_out_of_range([&sources]() { (void)sources.text(kMissingSource); }));
        CHECK(throws_out_of_range([&sources]() { (void)sources.name(kMissingSource); }));
        CHECK(throws_out_of_range([&sources]() { (void)sources.line_column(kMissingSource, 0u); }));
        CHECK(throws_out_of_range([&sources]() { (void)sources.line_text(kMissingSource, 1u); }));

        CHECK(sources.file(source).id == source);
        CHECK(sources.line_text(source, 99u).empty());

        return true;
    }

    bool test_diagnostic_error_and_options()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        // 默认选项：错误 + 错误码 + 源码行 + 下划线。
        const Diagnostic error{Severity::error, "E0001", "unexpected token", Span{source, 6u, 10u}, {}};
        const std::string error_expected =
            "demo.coord:2:1: error[E0001]: unexpected token\n"
            "    beta\n"
            "    ^~~~\n";
        CHECK_EQ(format_diagnostic(error, sources), error_expected);

        // 关闭错误码：错误码字段不再出现。
        PrintOptions no_code;
        no_code.show_code = false;
        const Diagnostic plain{Severity::error, "E0001", "boom", Span{source, 0u, 5u}, {}};
        const std::string no_code_expected =
            "demo.coord:1:1: error: boom\n"
            "    alpha\n"
            "    ^~~~~\n";
        CHECK_EQ(format_diagnostic(plain, sources, no_code), no_code_expected);

        // 关闭源码行：只输出头部。
        PrintOptions no_source;
        no_source.show_source_line = false;
        const std::string no_source_expected = "demo.coord:1:1: error[E0001]: boom\n";
        CHECK_EQ(format_diagnostic(plain, sources, no_source), no_source_expected);

        // 两个开关都关闭。
        PrintOptions terse;
        terse.show_code = false;
        terse.show_source_line = false;
        CHECK_EQ(format_diagnostic(plain, sources, terse), std::string_view("demo.coord:1:1: error: boom\n"));

        // severity == note 也能单独格式化。
        const Diagnostic note{Severity::note, "N0001", "declared here", Span{source, 0u, 5u}, {}};
        const std::string note_expected =
            "demo.coord:1:1: note[N0001]: declared here\n"
            "    alpha\n"
            "    ^~~~~\n";
        CHECK_EQ(format_diagnostic(note, sources), note_expected);

        return true;
    }

    bool test_diagnostic_warning_notes()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        const Diagnostic warning{Severity::warning, "W0002", "unused variable", Span{source, 12u, 17u},
            {Span{source, 6u, 10u}, Span{source, 18u, 18u}}};
        const std::string expected =
            "demo.coord:3:1: warning[W0002]: unused variable\n"
            "    gamma\n"
            "    ^~~~~\n"
            "  note: demo.coord:2:1\n"
            "  note: demo.coord:4:1\n";
        CHECK_EQ(format_diagnostic(warning, sources), expected);

        // notes 不随 show_source_line 一起消失。
        PrintOptions no_source;
        no_source.show_source_line = false;
        const std::string notes_only_expected =
            "demo.coord:3:1: warning[W0002]: unused variable\n"
            "  note: demo.coord:2:1\n"
            "  note: demo.coord:4:1\n";
        CHECK_EQ(format_diagnostic(warning, sources, no_source), notes_only_expected);

        return true;
    }

    bool test_diagnostic_invalid_span()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("demo.coord", "alpha\nbeta\r\ngamma\n");

        // 无效 span：退化为 "<unknown>: error[...]: message"，不输出源码行。
        const Diagnostic invalid{Severity::error, "E0001", "broken span", Span{}, {}};
        CHECK_EQ(format_diagnostic(invalid, sources), std::string_view("<unknown>: error[E0001]: broken span\n"));

        // source 不存在：同样退化。
        const Diagnostic unknown{Severity::error, "E0001", "unknown source", Span{kMissingSource, 0u, 1u}, {}};
        CHECK_EQ(format_diagnostic(unknown, sources), std::string_view("<unknown>: error[E0001]: unknown source\n"));

        // begin 越界：只保留 path，不输出行列与源码行。
        const Diagnostic begin_out{Severity::error, "E0001", "begin out of range", Span{source, 100u, 105u}, {}};
        CHECK_EQ(format_diagnostic(begin_out, sources),
            std::string_view("demo.coord: error[E0001]: begin out of range\n"));

        // end 越界：同样退化为只保留 path。
        const Diagnostic end_out{Severity::error, "E0001", "end out of range", Span{source, 6u, 100u}, {}};
        CHECK_EQ(format_diagnostic(end_out, sources),
            std::string_view("demo.coord: error[E0001]: end out of range\n"));

        // EOF 处（offset == text.size()）是合法位置，正常输出行列与空源码行。
        const Diagnostic eof{Severity::error, "E0001", "unexpected end of file", Span{source, 18u, 18u}, {}};
        const std::string eof_expected =
            "demo.coord:4:1: error[E0001]: unexpected end of file\n"
            "    \n"
            "    ^\n";
        CHECK_EQ(format_diagnostic(eof, sources), eof_expected);

        // note span 无效时打印 "<unknown>"，但仍保留 notes 行。
        const Diagnostic with_bad_note{Severity::error, "E0001", "bad note", Span{source, 0u, 5u},
            {Span{}, Span{source, 12u, 17u}}};
        const std::string bad_note_expected =
            "demo.coord:1:1: error[E0001]: bad note\n"
            "    alpha\n"
            "    ^~~~~\n"
            "  note: <unknown>\n"
            "  note: demo.coord:3:1\n";
        CHECK_EQ(format_diagnostic(with_bad_note, sources), bad_note_expected);

        return true;
    }

    bool test_format_diagnostics_order()
    {
        SourceManager sources;
        const SourceId source = sources.add_file("order.coord", "one\ntwo\n");

        std::vector<Diagnostic> diagnostics;
        diagnostics.push_back(Diagnostic{Severity::warning, "W0002", "first", Span{source, 0u, 3u}, {}});
        diagnostics.push_back(Diagnostic{Severity::error, "E0001", "second", Span{source, 4u, 7u}, {}});

        const std::string expected =
            "order.coord:1:1: warning[W0002]: first\n"
            "    one\n"
            "    ^~~\n"
            "order.coord:2:1: error[E0001]: second\n"
            "    two\n"
            "    ^~~\n";
        CHECK_EQ(format_diagnostics(diagnostics, sources), expected);
        CHECK_EQ(format_diagnostics({}, sources), std::string_view(""));

        // 关闭源码行时仍严格按输入顺序拼接。
        PrintOptions headers_only;
        headers_only.show_source_line = false;
        const std::string headers_expected =
            "order.coord:1:1: warning[W0002]: first\n"
            "order.coord:2:1: error[E0001]: second\n";
        CHECK_EQ(format_diagnostics(diagnostics, sources, headers_only), headers_expected);

        // 与 DiagnosticBag 收集的结果拼接一致。
        DiagnosticBag bag;
        bag.warning("W0002", "first", Span{source, 0u, 3u});
        bag.error("E0001", "second", Span{source, 4u, 7u});
        CHECK(bag.warning_count() == 1u);
        CHECK(bag.error_count() == 1u);
        CHECK_EQ(format_diagnostics(bag.diagnostics(), sources, headers_only), headers_expected);

        return true;
    }
}

int main()
{
    if (!test_add_file_and_contains()
        || !test_line_column()
        || !test_line_text()
        || !test_slice()
        || !test_out_of_range()
        || !test_diagnostic_error_and_options()
        || !test_diagnostic_warning_notes()
        || !test_diagnostic_invalid_span()
        || !test_format_diagnostics_order())
    {
        return 1;
    }

    std::cout << "PASS: source_manager\n";
    return 0;
}
