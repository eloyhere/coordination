// Cif.h 测试：模块接口文件的序列化、解析、指纹与兼容性判断。
// 覆盖：完整往返（含 genericBodies）；序列化的键固定顺序与 parse 对空白/制表符/
// 空行/CRLF/BOM/键乱序的容忍；空值（空模块名、空编译器版本）往返；指纹对导出
// 顺序不敏感、增删导出变化、重复导出去重、只看名字，并与 Dependency 的模块指纹
// 交叉校验；坏输入（缺字段、坏指纹、未知格式版本、未知键、坏行、非法转义、重复键）；
// makeCifFromAst 从 AST 收集 class/interface/function 导出与 import（含重导出）、
// 以及非 Module 根节点；compatible 正反例。
// 全部通过时打印 "PASS: cif" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/Cif.h"
#include "compiler/Dependency.h"
#include "compiler/Driver.h"

using namespace coordination::compiler;

namespace
{
    int g_failures = 0;

    void fail(std::string_view message)
    {
        std::cout << "FAIL: " << message << '\n';
        ++g_failures;
    }

    bool check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            fail(message);
        }
        return condition;
    }

    bool has_text(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    // 是否恰好 16 位小写十六进制。
    bool is_lower_hex16(std::string_view text)
    {
        if (text.size() != 16)
        {
            return false;
        }
        for (const char ch : text)
        {
            const bool digit = ch >= '0' && ch <= '9';
            const bool lower = ch >= 'a' && ch <= 'f';
            if (!digit && !lower)
            {
                return false;
            }
        }
        return true;
    }

    // 把十六进制文本变成大写（用于构造"坏指纹"）。
    std::string to_upper_hex(std::string_view text)
    {
        std::string out(text);
        for (char& ch : out)
        {
            if (ch >= 'a' && ch <= 'f')
            {
                ch = static_cast<char>(ch - 'a' + 'A');
            }
        }
        return out;
    }

    bool same_exports(const std::vector<CifExport>& lhs, const std::vector<CifExport>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i].name != rhs[i].name || lhs[i].kind != rhs[i].kind)
            {
                return false;
            }
        }
        return true;
    }

    // 逐字段比较两个接口文档。
    bool same_module(const CifModule& lhs, const CifModule& rhs)
    {
        return lhs.formatVersion == rhs.formatVersion &&
            lhs.compilerVersion == rhs.compilerVersion &&
            lhs.name == rhs.name &&
            same_exports(lhs.exports, rhs.exports) &&
            lhs.imports == rhs.imports &&
            lhs.fingerprint == rhs.fingerprint &&
            lhs.genericBodies == rhs.genericBodies;
    }

    void expect_module_equal(std::string_view label, const CifModule& actual, const CifModule& expected)
    {
        if (same_module(actual, expected))
        {
            return;
        }
        std::cout << "FAIL: " << label << " 与期望不一致\n";
        std::cout << "--- 实际 ---\n" << serializeCif(actual);
        std::cout << "--- 期望 ---\n" << serializeCif(expected);
        ++g_failures;
    }

    // 期望解析失败：error 非空，且包含 needle（needle 为空则不检查内容）。
    void expect_error(std::string_view label, std::string_view text, std::string_view needle)
    {
        std::string error;
        const std::optional<CifModule> parsed = parseCif(text, &error);
        if (parsed.has_value())
        {
            std::cout << "FAIL: " << label << " 应解析失败，实际成功\n";
            ++g_failures;
            return;
        }
        if (error.empty())
        {
            std::cout << "FAIL: " << label << " 解析失败但没有写 error\n";
            ++g_failures;
            return;
        }
        if (!needle.empty() && !has_text(error, needle))
        {
            std::cout << "FAIL: " << label << " 的错误信息未包含 '" << needle << "': " << error << '\n';
            ++g_failures;
        }
    }

    void print_diagnostics(const FrontendResult& result, std::string_view label)
    {
        std::cout << "FAIL: " << label << " 前端报告错误\n";
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            std::cout << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        ++g_failures;
    }

    // 一个内容完整的样例接口文档。
    CifModule sample_module()
    {
        CifModule module;
        module.compilerVersion = "0.1.0";
        module.name = "math.vector";
        module.exports = {
            {"Vector", "class"},
            {"Drawable", "interface"},
            {"dot", "function"},
        };
        module.imports = {"math.scalar", "io.print"};
        module.fingerprint = computeFingerprint(module.exports);
        module.genericBodies = {"<T> { return T; }"};
        return module;
    }

    void test_roundtrip()
    {
        const CifModule original = sample_module();
        const std::string text = serializeCif(original);
        check(!text.empty() && text.back() == '\n', "序列化文本应以换行结尾");
        check(text.find('\r') == std::string::npos, "序列化文本不应包含 CR");

        std::string error = "stale";
        const std::optional<CifModule> parsed = parseCif(text, &error);
        if (!check(parsed.has_value(), "合法文本应解析成功"))
        {
            std::cout << "  error: " << error << '\n';
            return;
        }
        check(error.empty(), "解析成功时应清空 error");
        expect_module_equal("序列化往返", *parsed, original);
        check(serializeCif(*parsed) == text, "反序列化后再序列化应得到相同文本");

        // error 允许传 nullptr。
        check(parseCif(text, nullptr).has_value(), "error=nullptr 时应正常解析");
    }

    void test_key_order_and_whitespace()
    {
        const CifModule module = sample_module();
        const std::string text = serializeCif(module);

        // 单值键顺序固定：format-version -> compiler-version -> module -> fingerprint。
        const std::string header =
            "format-version 1\ncompiler-version 0.1.0\nmodule math.vector\nfingerprint " +
            module.fingerprint + "\n";
        check(text.rfind(header, 0) == 0,
            "头部键顺序应为 format-version/compiler-version/module/fingerprint");
        check(has_text(text, "export class Vector\nexport interface Drawable\nexport function dot\n"),
            "export 行应 kind 在前、名字在后，并保持导出顺序");
        check(has_text(text, "import math.scalar\nimport io.print\n"), "import 行应保持顺序");

        const std::size_t export_pos = text.find("export class Vector");
        const std::size_t import_pos = text.find("import math.scalar");
        const std::size_t body_pos = text.find("generic-body ");
        check(export_pos != std::string::npos && import_pos != std::string::npos &&
                body_pos != std::string::npos && export_pos < import_pos && import_pos < body_pos,
            "export/import/generic-body 三组应依次出现");

        // 空值只写键，不产生行尾空白；parse 能还原空模块名与空编译器版本。
        CifModule empty_values;
        empty_values.fingerprint = computeFingerprint(std::vector<CifExport>());
        const std::string empty_text = serializeCif(empty_values);
        check(empty_text.rfind("format-version 1\ncompiler-version\nmodule\nfingerprint ", 0) == 0,
            "空值应只写键，不应写行尾空白");
        const std::optional<CifModule> parsed_empty = parseCif(empty_text);
        if (check(parsed_empty.has_value(), "空模块名/空编译器版本应可解析"))
        {
            expect_module_equal("空值往返", *parsed_empty, empty_values);
        }

        // 容忍：BOM、行首尾空白、连续空白/制表符、空行、CRLF、无结尾换行、键乱序。
        CifModule expected;
        expected.compilerVersion = "0.1.0";
        expected.name = "math.vector";
        expected.exports = {
            {"Vector", "class"},
            {"Drawable", "interface"},
        };
        expected.imports = {"math.scalar"};
        expected.fingerprint = computeFingerprint(expected.exports);
        expected.genericBodies = {"<T> { return T; }"};

        const std::string messy =
            "\xEF\xBB\xBF  format-version\t1  \r\n"
            "\r\n"
            "fingerprint\t" + expected.fingerprint + "\r\n"
            "compiler-version   0.1.0\r\n"
            "module\t\tmath.vector\r\n"
            "\n"
            "export    class   Vector\r\n"
            "export\tinterface\tDrawable\n"
            "import  math.scalar  \r\n"
            " generic-body <T> { return T; } ";

        std::string error;
        const std::optional<CifModule> parsed = parseCif(messy, &error);
        if (check(parsed.has_value(), "BOM/空白/CRLF/空行/键乱序应被容忍"))
        {
            expect_module_equal("空白容忍", *parsed, expected);
        }
        else
        {
            std::cout << "  error: " << error << '\n';
        }
    }

    void test_fingerprint()
    {
        const std::vector<CifExport> forward = {{"Vector", "class"}, {"dot", "function"}};
        const std::vector<CifExport> reversed = {{"dot", "function"}, {"Vector", "class"}};
        const std::string base = computeFingerprint(forward);
        check(is_lower_hex16(base), "指纹应是 16 位小写十六进制");
        check(base == computeFingerprint(reversed), "指纹对导出顺序不敏感");

        // 增删导出都改变指纹。
        std::vector<CifExport> added = forward;
        added.push_back({"Drawable", "interface"});
        check(computeFingerprint(added) != base, "新增导出应改变指纹");

        const std::vector<CifExport> removed = {{"Vector", "class"}};
        check(computeFingerprint(removed) != base, "删除导出应改变指纹");

        // 重复导出去重：两份 Vector 与一份等价。
        const std::vector<CifExport> duplicated = {
            {"Vector", "class"},
            {"Vector", "class"},
            {"dot", "function"},
        };
        check(computeFingerprint(duplicated) == base, "重复导出去重后指纹不变");

        // 指纹只由名字决定：kind 不同不影响结果。
        const std::vector<CifExport> other_kind = {{"Vector", "interface"}, {"dot", "operator"}};
        check(computeFingerprint(other_kind) == base, "指纹只由导出名字决定");

        // 空集合依然产生 16 位指纹（FNV 偏移基）。
        check(is_lower_hex16(computeFingerprint(std::vector<CifExport>())),
            "空导出集合也应有 16 位指纹");

        // 与 Dependency.h 的模块指纹算法一致：同一导出集合得到相同指纹。
        FrontendResult result = runFrontendFromText("cif_fingerprint",
            "module math.vector;\n"
            "export class Vector { }\n"
            "export dot(): void;\n");
        if (result.ok())
        {
            const std::vector<ModuleNode> modules = collectModules(*result.module);
            if (check(modules.size() == 1, "应收集到一个模块节点"))
            {
                check(modules[0].fingerprint == base,
                    "CIF 指纹应与 Dependency 的模块指纹一致");
            }
        }
        else
        {
            print_diagnostics(result, "cif_fingerprint");
        }
    }

    void test_bad_input()
    {
        const CifModule module = sample_module();
        const std::string fingerprint = module.fingerprint;
        const std::string valid = serializeCif(module);

        // 缺必填字段。
        expect_error("缺少 format-version",
            "compiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint + "\n", "format-version");
        expect_error("缺少 compiler-version",
            "format-version 1\nmodule m\nfingerprint " + fingerprint + "\n", "compiler-version");
        expect_error("缺少 module",
            "format-version 1\ncompiler-version 0.1.0\nfingerprint " + fingerprint + "\n", "module");
        expect_error("缺少 fingerprint",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\n", "fingerprint");
        expect_error("空文本", "", "format-version");

        // 格式版本。
        expect_error("未知格式版本",
            "format-version 2\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint + "\n", "版本");
        expect_error("格式版本不是整数",
            "format-version v1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint + "\n",
            "format-version");

        // 坏指纹：长度不足 / 大写 / 非十六进制字符。
        expect_error("坏指纹（长度不足）",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint abc\n", "fingerprint");
        expect_error("坏指纹（大写）",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " +
                to_upper_hex(fingerprint) + "\n", "fingerprint");
        expect_error("坏指纹（非十六进制）",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint 000000000000000g\n",
            "fingerprint");

        // 未知键 / 坏行。
        expect_error("未知键", valid + "unknown-key value\n", "未知");
        expect_error("垃圾行", "this is not a cif\n", "未知");
        expect_error("export 字段不足",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint +
                "\nexport class\n", "export");
        expect_error("export 字段过多",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint +
                "\nexport class Point extra\n", "export");
        expect_error("export 行没有字段",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint +
                "\nexport\n", "export");
        expect_error("import 字段过多",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint +
                "\nimport a b\n", "import");
        expect_error("import 行没有字段",
            "format-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " + fingerprint +
                "\nimport\n", "import");

        // 非法转义：未知转义字母与行尾残缺的反斜杠。
        expect_error("非法转义",
            "format-version 1\ncompiler-version 0.1.0\nmodule a\\q\nfingerprint " + fingerprint + "\n",
            "转义");
        expect_error("残缺转义",
            "format-version 1\ncompiler-version 0.1.0\nmodule a\\\nfingerprint " + fingerprint + "\n",
            "转义");

        // 重复单值键。
        expect_error("重复 format-version",
            "format-version 1\nformat-version 1\ncompiler-version 0.1.0\nmodule m\nfingerprint " +
                fingerprint + "\n", "重复");
        expect_error("重复 fingerprint", valid + "fingerprint " + fingerprint + "\n", "重复");

        // error 指针可以为空：坏输入仍返回 nullopt，不崩溃。
        check(!parseCif("format-version 2\n", nullptr).has_value(),
            "error=nullptr 时坏输入仍应返回 nullopt");
    }

    void test_make_from_ast()
    {
        constexpr std::string_view source =
            "module math.vector;\n"
            "import math.scalar;\n"
            "import io.print as p;\n"
            "export import util;\n"
            "export class Vector { }\n"
            "export interface Drawable { }\n"
            "export dot(a: Vector, b: Vector): float;\n"
            "class Hidden { }\n";

        FrontendResult result = runFrontendFromText("cif_ast", source);
        if (!result.ok())
        {
            print_diagnostics(result, "cif_ast");
            return;
        }

        const CifModule module = makeCifFromAst(*result.module, "0.1.0");
        check(module.formatVersion == 1, "makeCifFromAst 的 formatVersion 应为 1");
        check(module.compilerVersion == "0.1.0", "compilerVersion 应原样保留");
        check(module.name == "math.vector", "模块名应取 Module 根节点 text");
        check(module.genericBodies.empty(), "此版本 genericBodies 应为空");
        check(module.exports.size() == 3, "应收集 3 个导出（未导出的 Hidden 不算）");

        if (module.exports.size() == 3)
        {
            check(module.exports[0].name == "Vector" && module.exports[0].kind == "class",
                "第一个导出应是 class Vector");
            check(module.exports[1].name == "Drawable" && module.exports[1].kind == "interface",
                "第二个导出应是 interface Drawable");
            check(module.exports[2].name == "dot" && module.exports[2].kind == "function",
                "第三个导出应是 function dot");
        }

        check(module.imports == std::vector<std::string>{"math.scalar", "io.print", "util"},
            "imports 应包含普通/别名/重导出 import，按源码顺序去重");
        check(module.fingerprint == computeFingerprint(module.exports),
            "makeCifFromAst 应写入导出集合指纹");
        check(is_lower_hex16(module.fingerprint), "makeCifFromAst 的指纹应是 16 位小写十六进制");

        // 与 DependencyCache / collectModules 交叉校验。
        const std::vector<ModuleNode> modules = collectModules(*result.module);
        if (check(modules.size() == 1, "应收集到一个模块节点"))
        {
            check(modules[0].fingerprint == module.fingerprint,
                "CIF 指纹应与 ModuleNode 指纹一致");
        }

        // AST 生成的文档应可序列化 / 解析。
        const std::optional<CifModule> parsed = parseCif(serializeCif(module));
        if (check(parsed.has_value(), "AST 生成的接口文档应可解析"))
        {
            expect_module_equal("AST 往返", *parsed, module);
        }

        // 源文件没有 `module x;` 声明时模块名为空，但文档仍然合法。
        FrontendResult unnamed = runFrontendFromText("cif_unnamed", "export class C { }\n");
        if (unnamed.ok())
        {
            const CifModule unnamed_module = makeCifFromAst(*unnamed.module, "0.1.0");
            check(unnamed_module.name.empty(), "未声明模块名时 name 应为空");
            check(unnamed_module.exports.size() == 1 && unnamed_module.exports[0].name == "C",
                "未声明模块名时仍应收集导出");
            const std::optional<CifModule> unnamed_parsed = parseCif(serializeCif(unnamed_module));
            if (check(unnamed_parsed.has_value(), "空模块名的接口文档应可解析"))
            {
                expect_module_equal("空模块名往返", *unnamed_parsed, unnamed_module);
            }
        }
        else
        {
            print_diagnostics(unnamed, "cif_unnamed");
        }

        // 非 Module 根节点：不抛异常，返回空接口文档。
        AstArena arena;
        AstNode* not_module = arena.make(AstKind::Error, Span{});
        const CifModule empty = makeCifFromAst(*not_module, "0.1.0");
        check(empty.name.empty() && empty.exports.empty() && empty.imports.empty(),
            "非 Module 根节点应得到空接口");
        check(empty.fingerprint.empty(), "非 Module 根节点的指纹应为空");
        check(empty.formatVersion == 1, "非 Module 根节点仍有默认 formatVersion");
        check(empty.compilerVersion == "0.1.0", "非 Module 根节点仍保留 compilerVersion");
    }

    void test_compatible()
    {
        const CifModule cached = sample_module();
        CifModule current = cached;
        check(compatible(cached, current), "完全相同应兼容");

        current = cached;
        current.exports.push_back({"Extra", "class"});
        current.fingerprint = computeFingerprint(current.exports);
        check(!compatible(cached, current), "导出集合变化后不应兼容");

        current = cached;
        current.compilerVersion = "0.2.0";
        check(!compatible(cached, current), "编译器版本不同不应兼容");

        current = cached;
        current.formatVersion = 2;
        check(!compatible(cached, current), "格式版本不同不应兼容");

        current = cached;
        current.formatVersion = 0;
        check(!compatible(cached, current), "未知格式版本不应兼容");

        // 模块名与 import 不参与兼容判断（由上层模块图负责）。
        current = cached;
        current.name = "other.module";
        check(compatible(cached, current), "模块名不参与兼容判断");

        current = cached;
        current.imports.push_back("extra.dep");
        check(compatible(cached, current), "import 变化不影响接口兼容判断");

        current = cached;
        current.genericBodies.push_back("body");
        check(compatible(cached, current), "预留泛型体不参与兼容判断");

        current = cached;
        current.fingerprint.clear();
        check(!compatible(cached, current), "空指纹不应兼容");
        check(!compatible(current, cached), "空指纹不应兼容（缓存侧）");
    }

    void test_generic_bodies()
    {
        CifModule module = sample_module();
        module.genericBodies = {
            "<T> { return T; }",
            "第一行\n第二行",
            "tab\there",
            "backslash\\path",
            "",
            "  前后空格  ",
        };

        const std::string text = serializeCif(module);
        std::size_t newline_count = 0;
        for (const char ch : text)
        {
            if (ch == '\n')
            {
                ++newline_count;
            }
        }
        const std::size_t expected_lines = 4 + module.exports.size() + module.imports.size() +
            module.genericBodies.size();
        check(newline_count == expected_lines, "每个字段（含多行泛型体）应各占一行");

        std::string error;
        const std::optional<CifModule> parsed = parseCif(text, &error);
        if (check(parsed.has_value(), "含 genericBodies 的文档应可解析"))
        {
            expect_module_equal("genericBodies 往返", *parsed, module);
        }
        else
        {
            std::cout << "  error: " << error << '\n';
        }
    }
}

int main()
{
    test_roundtrip();
    test_key_order_and_whitespace();
    test_fingerprint();
    test_bad_input();
    test_make_from_ast();
    test_compatible();
    test_generic_bodies();

    if (g_failures != 0)
    {
        std::cout << "FAIL: cif (" << g_failures << " 处)\n";
        return 1;
    }

    std::cout << "PASS: cif\n";
    return 0;
}
