// Driver.h 端到端测试：源文本/源文件 -> Lexer -> Parser -> AST + 诊断。
// 覆盖：最小 class（字段 + 方法）、语法错误、空输入、文件读取往返、
// 不存在的文件产生 E3001，以及移动 FrontendResult 后 module 指针仍然有效。
// 全部通过时打印 "PASS: driver" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "compiler/Driver.h"

using namespace coordination::compiler;

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cout << "FAIL: " << message << '\n';
            return false;
        }
        return true;
    }

    bool contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    const Diagnostic* find_diagnostic(const FrontendResult& result, std::string_view code)
    {
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    // 当前目录下的临时源文件，析构时删除；不依赖 %TEMP% 等平台路径。
    struct TemporarySource
    {
        std::string path;

        bool write(std::string_view content) const
        {
            std::ofstream stream(path, std::ios::binary);
            if (!stream.is_open())
            {
                return false;
            }
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.close();
            return !stream.fail();
        }

        ~TemporarySource()
        {
            if (!path.empty())
            {
                std::remove(path.c_str());
            }
        }
    };

    // 1) 最小 class：字段 + 方法，应当无诊断且 AST 含 ClassDecl。
    bool test_minimal_class()
    {
        constexpr std::string_view kSource =
            "module demo;\n"
            "class Point\n"
            "{\n"
            "    x: int = 0;\n"
            "    y: int = 0;\n"
            "\n"
            "    move(dx: int, dy: int): void\n"
            "    {\n"
            "        this.x = this.x + dx;\n"
            "        this.y = this.y + dy;\n"
            "    }\n"
            "}\n";

        const FrontendResult result = runFrontendFromText("point.coord", kSource);

        if (!check(result.ok(), "minimal class: ok() should be true"))
        {
            return false;
        }
        if (!check(result.source != kInvalidSource, "minimal class: source id should be valid"))
        {
            return false;
        }
        if (!check(std::string_view(result.sourceText) == kSource, "minimal class: sourceText should round-trip"))
        {
            return false;
        }
        if (!check(result.module != nullptr && result.module->kind == AstKind::Module,
            "minimal class: root should be Module"))
        {
            return false;
        }
        if (!check(result.module->first_child(AstKind::ClassDecl) != nullptr,
            "minimal class: Module should have a ClassDecl child"))
        {
            return false;
        }

        const std::string ast = dumpAst(result);
        if (!check(contains(ast, "ClassDecl"), "minimal class: dumpAst should contain ClassDecl"))
        {
            return false;
        }
        if (!check(contains(ast, "FieldDecl") && contains(ast, "MethodDecl"),
            "minimal class: dumpAst should contain field and method"))
        {
            return false;
        }
        if (!check(renderDiagnostics(result).empty(), "minimal class: renderDiagnostics should be empty"))
        {
            return false;
        }
        return true;
    }

    // 2) 语法错误（字段缺少 ';'）应当报错并输出带文件名的诊断。
    bool test_syntax_error()
    {
        constexpr std::string_view kSource =
            "class Broken\n"
            "{\n"
            "    value: int\n"
            "}\n";

        const FrontendResult result = runFrontendFromText("broken.coord", kSource);

        if (!check(!result.ok(), "syntax error: ok() should be false"))
        {
            return false;
        }
        if (!check(result.diagnostics.has_errors(), "syntax error: diagnostics should contain errors"))
        {
            return false;
        }
        if (!check(result.module != nullptr, "syntax error: parser should still return a Module"))
        {
            return false;
        }
        if (!check(find_diagnostic(result, "E2001") != nullptr, "syntax error: missing ';' should be E2001"))
        {
            return false;
        }

        const std::string rendered = renderDiagnostics(result);
        if (!check(!rendered.empty(), "syntax error: renderDiagnostics should not be empty"))
        {
            return false;
        }
        if (!check(contains(rendered, "broken.coord"), "syntax error: rendered diagnostics should mention file name"))
        {
            return false;
        }
        return true;
    }

    // 3) 空输入：仍返回 Module，且没有任何诊断。
    bool test_empty_input()
    {
        const FrontendResult result = runFrontendFromText("empty.coord", "");

        if (!check(result.ok(), "empty input: ok() should be true"))
        {
            return false;
        }
        if (!check(result.module != nullptr && result.module->kind == AstKind::Module,
            "empty input: root should be Module"))
        {
            return false;
        }
        if (!check(result.module->child_count() == 0, "empty input: Module should have no children"))
        {
            return false;
        }
        if (!check(!result.diagnostics.has_errors(), "empty input: diagnostics should have no errors"))
        {
            return false;
        }
        if (!check(result.diagnostics.diagnostics().empty(), "empty input: diagnostics should be empty"))
        {
            return false;
        }
        if (!check(dumpAst(result) == "(Module)", "empty input: dumpAst should be (Module)"))
        {
            return false;
        }
        if (!check(result.sourceText.empty(), "empty input: sourceText should be empty"))
        {
            return false;
        }
        return true;
    }

    // 4) runFrontend 读取临时文件应当成功；不存在的路径应当产生 E3001。
    bool test_file_round_trip()
    {
        TemporarySource temporary{"test_driver_tmp_input.coord"};
        constexpr std::string_view kSource =
            "module tmp;\n"
            "class Tiny\n"
            "{\n"
            "    value: int = 42;\n"
            "}\n";

        if (!check(temporary.write(kSource), "file: should write the temporary source file"))
        {
            return false;
        }

        const FrontendResult result = runFrontend(temporary.path);
        if (!check(result.ok(), "file: ok() should be true"))
        {
            return false;
        }
        if (!check(std::string_view(result.sourceText) == kSource, "file: sourceText should match file content"))
        {
            return false;
        }
        if (!check(contains(dumpAst(result), "ClassDecl"), "file: dumpAst should contain ClassDecl"))
        {
            return false;
        }

        const std::string missing = "test_driver_missing_input.coord";
        std::remove(missing.c_str()); // 确保路径确实不存在
        const FrontendResult failure = runFrontend(missing);

        if (!check(!failure.ok(), "file: missing file should not be ok()"))
        {
            return false;
        }
        if (!check(failure.source == kInvalidSource, "file: missing file should keep an invalid SourceId"))
        {
            return false;
        }
        if (!check(failure.module == nullptr && failure.arena == nullptr,
            "file: missing file should not produce an AST"))
        {
            return false;
        }

        const Diagnostic* diagnostic = find_diagnostic(failure, "E3001");
        if (!check(diagnostic != nullptr, "file: missing file should report E3001"))
        {
            return false;
        }
        if (!check(diagnostic->severity == Severity::error, "file: E3001 should be an error"))
        {
            return false;
        }
        if (!check(!diagnostic->span.valid(), "file: E3001 span should be invalid"))
        {
            return false;
        }
        if (!check(contains(renderDiagnostics(failure), missing),
            "file: E3001 diagnostics should mention the path"))
        {
            return false;
        }
        return true;
    }

    // 5) 移动构造后 arena 随 unique_ptr 转移，module 指向的节点地址不变。
    bool test_move_keeps_module()
    {
        FrontendResult original = runFrontendFromText("move.coord",
            "class Moved\n"
            "{\n"
            "    value: int = 1;\n"
            "}\n");

        if (!check(original.ok(), "move: original result should parse"))
        {
            return false;
        }

        AstNode* const module = original.module;
        const std::size_t node_count = original.arena->node_count();

        FrontendResult moved(std::move(original));
        if (!check(moved.arena != nullptr, "move: arena should be transferred"))
        {
            return false;
        }
        if (!check(moved.module == module, "move: module pointer should stay stable"))
        {
            return false;
        }
        if (!check(moved.module->kind == AstKind::Module, "move: moved module should still be Module"))
        {
            return false;
        }
        if (!check(moved.arena->node_count() == node_count, "move: arena node count should be preserved"))
        {
            return false;
        }
        if (!check(moved.ok(), "move: moved result should still be ok()"))
        {
            return false;
        }
        if (!check(contains(dumpAst(moved), "ClassDecl"), "move: dumpAst after move should contain ClassDecl"))
        {
            return false;
        }
        if (!check(original.arena == nullptr, "move: source arena should be empty after move"))
        {
            return false;
        }
        return true;
    }
}

int main()
{
    const bool passed = test_minimal_class() &&
                        test_syntax_error() &&
                        test_empty_input() &&
                        test_file_round_trip() &&
                        test_move_keeps_module();
    if (!passed)
    {
        return 1;
    }

    std::cout << "PASS: driver\n";
    return 0;
}
