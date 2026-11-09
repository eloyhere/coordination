// BackendC.h 自包含测试：不依赖第三方测试框架，也不调用外部 C 编译器。
// 覆盖：函数签名与类型映射、调用与函数原型、二元/一元表达式括号、
// 文本字面量转义与文本比较、非法 C 标识符转义、emitEntryPoint 正反例、
// E8001/E8002 诊断、emitComments 开关以及生成文本的花括号平衡。
// 全部通过时打印 "PASS: backend_c" 并返回 0，否则打印 "FAIL: ..." 并返回 1。
//
// 大多数用例走真实前端：Driver 解析源码 -> lowerToHir -> emitC；
// 未知操作码/未知 HIR 类型无法由 HIR 下降产生，因此用手工构造的 HirModule 覆盖。

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#include "compiler/BackendC.h"
#include "compiler/Driver.h"
#include "compiler/Hir.h"

using namespace coordination::compiler;

// 失败的断言立即从当前测试函数返回 false；std::cout 由 main 统一输出结果。
#define CHECK(condition) \
    if (!(condition)) \
    { \
        std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")\n"; \
        return false; \
    }

namespace
{
    bool contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    std::size_t count_occurrences(std::string_view text, std::string_view needle)
    {
        if (needle.empty())
        {
            return 0;
        }
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = text.find(needle, position)) != std::string_view::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    std::size_t count_code(const DiagnosticBag& diagnostics, std::string_view code)
    {
        std::size_t count = 0;
        for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        {
            if (diagnostic.code == code)
            {
                ++count;
            }
        }
        return count;
    }

    void print_backend_diagnostics(const CBackendResult& result)
    {
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            std::cout << "  " << diagnostic.code << ": " << diagnostic.message << "\n";
        }
    }

    // 生成文本的花括号必须配平；字符串字面量和注释里的花括号在用例中刻意避开。
    bool braces_balanced(std::string_view text)
    {
        std::size_t depth = 0;
        for (const char character : text)
        {
            if (character == '{')
            {
                ++depth;
            }
            else if (character == '}')
            {
                if (depth == 0)
                {
                    return false;
                }
                --depth;
            }
        }
        return depth == 0;
    }

    // 用真实前端解析源码，再下降为 HIR；HIR 不保留 AST 指针，函数返回后仍可使用。
    bool lower_source(std::string_view display_name, std::string_view source, HirModule& module)
    {
        const FrontendResult frontend = runFrontendFromText(display_name, source);
        if (!frontend.ok())
        {
            std::cout << "FAIL: 测试源码解析失败: " << display_name << "\n";
            std::cout << renderDiagnostics(frontend);
            return false;
        }
        module = lowerToHir(*frontend.module);
        return true;
    }

    // 1) 函数签名、类型映射、局部声明、无返回值 return 与原型顺序。
    bool test_signatures_and_type_mapping()
    {
        constexpr std::string_view kSource = R"(add(a: integer(64), b: integer(32)): integer(64)
{
    const sum: integer(64) = a + b;
    return sum;
}
negate(flag: bool): bool
{
    return !flag;
}
greeting(): void
{
    const message = "hi";
    return;
}
nothing(): void
{
    return;
}
)";

        HirModule module;
        if (!lower_source("backend_signature.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        CHECK(result.ok());
        CHECK(result.diagnostics.diagnostics().empty());

        // integer64/boolean/text/void 的映射，以及形参名沿用 HIR 名字。
        CHECK(contains(result.source, "int64_t add(int64_t a, int64_t b)\n{\n"));
        CHECK(contains(result.source, "    int64_t sum = (a + b);\n"));
        CHECK(contains(result.source, "    return sum;\n"));
        CHECK(contains(result.source, "bool negate(bool flag)\n{\n"));
        CHECK(contains(result.source, "    return (!flag);\n"));
        // text 来自文本字面量的类型推断：局部变量映射为 const char*。
        CHECK(contains(result.source, "    const char* message = \"hi\";\n"));
        CHECK(contains(result.source, "void nothing(void)\n{\n"));
        CHECK(contains(result.source, "    return;\n"));
        CHECK(contains(result.source,
            "#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>"));

        // 原型在定义之前，且定义按 HIR 顺序输出。
        CHECK(contains(result.source, "int64_t add(int64_t a, int64_t b);\n"));
        CHECK(result.source.find("int64_t add(int64_t a, int64_t b);\n") <
            result.source.find("int64_t add(int64_t a, int64_t b)\n{"));

        const std::size_t add_position = result.source.find("int64_t add(int64_t a, int64_t b)\n{");
        const std::size_t negate_position = result.source.find("bool negate(bool flag)\n{");
        const std::size_t greeting_position = result.source.find("void greeting(void)\n{");
        const std::size_t nothing_position = result.source.find("void nothing(void)\n{");
        CHECK(add_position != std::string::npos);
        CHECK(negate_position != std::string::npos);
        CHECK(greeting_position != std::string::npos);
        CHECK(nothing_position != std::string::npos);
        CHECK(add_position < negate_position);
        CHECK(negate_position < greeting_position);
        CHECK(greeting_position < nothing_position);

        CHECK(braces_balanced(result.source));
        return true;
    }

    // 2) 二元/一元表达式的括号：优先级由语法树决定，输出一律加括号。
    bool test_expression_parentheses()
    {
        constexpr std::string_view kSource = R"(calc(a: integer, b: integer, c: bool): integer
{
    const x = a + b * 2;
    const y = -x + 1;
    const z = !c || (a < b) && c;
    return y / 2 % 3 + ~y;
}
)";

        HirModule module;
        if (!lower_source("backend_expression.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        CHECK(result.ok());

        CHECK(contains(result.source, "int64_t x = (a + (b * 2LL));\n"));
        CHECK(contains(result.source, "int64_t y = ((-x) + 1LL);\n"));
        CHECK(contains(result.source, "bool z = ((!c) || ((a < b) && c));\n"));
        CHECK(contains(result.source, "return (((y / 2LL) % 3LL) + (~y));\n"));
        CHECK(contains(result.source, "&&"));
        CHECK(contains(result.source, "||"));
        CHECK(braces_balanced(result.source));
        return true;
    }

    // 3) 调用生成 name(args)，并有前置原型支持“先调用、后定义”。
    bool test_calls_and_forward_declaration()
    {
        constexpr std::string_view kSource = R"(use(): integer
{
    return helper(2);
}
run(): void
{
    helper(1);
    return;
}
helper(value: integer): integer
{
    return value;
}
)";

        HirModule module;
        if (!lower_source("backend_call.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        CHECK(result.ok());

        CHECK(contains(result.source, "return helper(2LL);\n"));
        CHECK(contains(result.source, "    helper(1LL);\n"));
        CHECK(contains(result.source, "int64_t helper(int64_t value)\n{\n    return value;\n}\n"));
        CHECK(contains(result.source, "int64_t helper(int64_t value);\n"));
        CHECK(result.source.find("int64_t helper(int64_t value);\n") <
            result.source.find("int64_t use(void)\n{"));
        CHECK(braces_balanced(result.source));
        return true;
    }

    // 4) 文本字面量转义（引号/反斜杠/换行/制表/NUL）与文本比较（strcmp）。
    //    注意：HIR 的命名类型映射不识别 text 注解（会降级为 unknown），
    //    因此文本类型一律来自字面量推断；text 形参/返回值稍后用直接构造的 HIR 覆盖。
    bool test_text_literals_and_comparison()
    {
        constexpr std::string_view kSource = R"(message(): void
{
    const value = "quote:\" backslash:\\ newline:\n tab:\t nul:\0 end";
    return;
}
same(): bool
{
    const left = "hi";
    const right = "hi";
    return left == right;
}
less(): bool
{
    const left = "a";
    const right = "b";
    return left < right;
}
)";

        HirModule module;
        if (!lower_source("backend_text.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        CHECK(result.ok());

        CHECK(contains(result.source,
            "const char* value = \"quote:\\\" backslash:\\\\ newline:\\n tab:\\t nul:\\000 end\";"));
        CHECK(contains(result.source, "const char* left = \"hi\";"));
        CHECK(contains(result.source, "return (strcmp(left, right) == 0);\n"));
        CHECK(contains(result.source, "return (strcmp(left, right) < 0);\n"));
        CHECK(braces_balanced(result.source));

        // text 形参/返回值的映射：const char* echo(const char* value)。
        HirModule textSignature;
        HirFunction echo;
        echo.name = "echo";
        echo.returnType = HirType::text;
        echo.parameters.emplace_back("value", HirType::text);
        HirStmt returnValue;
        returnValue.kind = HirStmtKind::returnStmt;
        returnValue.value.kind = HirExprKind::localRef;
        returnValue.value.type = HirType::text;
        returnValue.value.refIndex = 0;
        echo.body.push_back(returnValue);
        textSignature.functions.push_back(echo);

        const CBackendResult echoResult = emitC(textSignature);
        CHECK(echoResult.ok());
        CHECK(contains(echoResult.source,
            "const char* echo(const char* value)\n{\n    return value;\n}\n"));
        CHECK(braces_balanced(echoResult.source));
        return true;
    }

    // 5) 非法 C 标识符（C 关键字）转义为 _esc_ + 十六进制，调用点同步改名。
    bool test_escaped_identifiers()
    {
        constexpr std::string_view kSource = R"(int(int: integer, struct: integer): integer
{
    const signed = int + 1;
    return signed;
}
caller(): integer
{
    return int(2, 3);
}
)";

        HirModule module;
        if (!lower_source("backend_escape.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        CHECK(result.ok());

        // int -> 69 6e 74，struct -> 73 74 72 75 63 74，signed -> 73 69 67 6e 65 64。
        CHECK(contains(result.source,
            "int64_t _esc_696e74(int64_t _esc_696e74, int64_t _esc_737472756374)\n{\n"));
        CHECK(contains(result.source, "int64_t _esc_7369676e6564 = (_esc_696e74 + 1LL);\n"));
        CHECK(contains(result.source, "return _esc_7369676e6564;\n"));
        CHECK(contains(result.source, "return _esc_696e74(2LL, 3LL);\n"));
        CHECK(contains(result.source, "名称改写"));
        CHECK(contains(result.source, "'int' -> '_esc_696e74'"));
        CHECK(braces_balanced(result.source));
        return true;
    }

    // 6) emitEntryPoint：integer64/void 的 main 生成入口；不合格 main 报 E8002。
    bool test_entry_point()
    {
        {
            constexpr std::string_view kSource = R"(main(): integer
{
    return 7;
}
)";
            HirModule module;
            if (!lower_source("backend_main_int.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            CBackendOptions options;
            options.emitEntryPoint = true;
            const CBackendResult result = emitC(module, options);
            if (!result.ok())
            {
                print_backend_diagnostics(result);
            }
            CHECK(result.ok());
            CHECK(contains(result.source, "int64_t coord_main(void)\n{\n    return 7LL;\n}\n"));
            CHECK(contains(result.source,
                "int main(void)\n{\n    return (int)coord_main();\n}\n"));
            CHECK(count_occurrences(result.source, "int main(void)") == 1u);
            CHECK(contains(result.source, "名称改写"));
            CHECK(braces_balanced(result.source));
        }

        {
            constexpr std::string_view kSource = R"(main(): void
{
    return;
}
)";
            HirModule module;
            if (!lower_source("backend_main_void.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            CBackendOptions options;
            options.emitEntryPoint = true;
            const CBackendResult result = emitC(module, options);
            CHECK(result.ok());
            CHECK(contains(result.source, "void coord_main(void)\n{\n    return;\n}\n"));
            CHECK(contains(result.source,
                "int main(void)\n{\n    coord_main();\n    return 0;\n}\n"));
            CHECK(braces_balanced(result.source));
        }

        {
            // 带参数的 main 不是合法入口：报 E8002，且不改名、不生成入口。
            constexpr std::string_view kSource = R"(main(value: integer): integer
{
    return value;
}
)";
            HirModule module;
            if (!lower_source("backend_main_param.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            CBackendOptions options;
            options.emitEntryPoint = true;
            const CBackendResult result = emitC(module, options);
            CHECK(!result.ok());
            CHECK(count_code(result.diagnostics, "E8002") == 1u);
            CHECK(!contains(result.source, "int main(void)"));
            CHECK(contains(result.source, "int64_t main(int64_t value)\n{\n    return value;\n}\n"));
            CHECK(braces_balanced(result.source));
        }

        {
            // 返回 boolean 的 main 同样不合格。
            constexpr std::string_view kSource = R"(main(): bool
{
    return true;
}
)";
            HirModule module;
            if (!lower_source("backend_main_bool.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            CBackendOptions options;
            options.emitEntryPoint = true;
            const CBackendResult result = emitC(module, options);
            CHECK(!result.ok());
            CHECK(count_code(result.diagnostics, "E8002") == 1u);
            CHECK(!contains(result.source, "int main(void)"));
        }

        {
            // 没有 main：报 E8002。
            constexpr std::string_view kSource = R"(run(): void
{
    return;
}
)";
            HirModule module;
            if (!lower_source("backend_main_missing.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            CBackendOptions options;
            options.emitEntryPoint = true;
            const CBackendResult result = emitC(module, options);
            CHECK(!result.ok());
            CHECK(count_code(result.diagnostics, "E8002") == 1u);
            CHECK(!contains(result.source, "int main(void)"));
            CHECK(contains(result.source, "void run(void)\n{\n    return;\n}\n"));
        }

        {
            // 默认不生成入口：HIR main 原样输出为 C main。
            constexpr std::string_view kSource = R"(main(): integer
{
    return 7;
}
)";
            HirModule module;
            if (!lower_source("backend_main_default.coord", kSource, module))
            {
                return false;
            }
            CHECK(module.ok());

            const CBackendResult result = emitC(module);
            CHECK(result.ok());
            CHECK(!contains(result.source, "int main(void)"));
            CHECK(contains(result.source, "int64_t main(void)\n{\n    return 7LL;\n}\n"));
        }
        return true;
    }

    // 7) unknown 类型只报 E8001 警告，不阻止生成。
    bool test_unknown_type_warning()
    {
        constexpr std::string_view kSource = R"(unknownValue(value: unknown): unknown
{
    const result: unknown = value;
    return result;
}
)";

        HirModule module;
        if (!lower_source("backend_unknown.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        const CBackendResult result = emitC(module);
        if (!result.ok())
        {
            print_backend_diagnostics(result);
        }
        // 返回类型、形参、局部变量各一条 E8001。
        CHECK(result.ok());
        CHECK(result.diagnostics.error_count() == 0u);
        CHECK(result.diagnostics.warning_count() == 3u);
        CHECK(count_code(result.diagnostics, "E8001") == 3u);
        CHECK(contains(result.source, "int64_t unknownValue(int64_t value)\n{\n"));
        CHECK(contains(result.source, "    int64_t result = value;\n"));
        CHECK(braces_balanced(result.source));
        return true;
    }

    // 8) 未知 HIR 操作码/类型报 E8002 并跳过节点；void 形参跳过整个函数。
    bool test_unknown_nodes_and_void_parameter()
    {
        HirModule module;

        HirStmt returnOne;
        returnOne.kind = HirStmtKind::returnStmt;
        returnOne.value.kind = HirExprKind::integerLiteral;
        returnOne.value.type = HirType::integer64;
        returnOne.value.integer = 1;

        HirFunction good;
        good.name = "good";
        good.returnType = HirType::integer64;
        good.body.push_back(returnOne);
        module.functions.push_back(good);

        // 未知表达式类别：跳过该 return，后面的合法 return 仍然生成。
        HirFunction broken;
        broken.name = "broken";
        broken.returnType = HirType::integer64;
        HirStmt unsupportedReturn;
        unsupportedReturn.kind = HirStmtKind::returnStmt;
        unsupportedReturn.value.kind = static_cast<HirExprKind>(99);
        unsupportedReturn.value.type = HirType::integer64;
        broken.body.push_back(unsupportedReturn);
        // 未知语句类别：同样跳过。
        HirStmt unsupportedStatement;
        unsupportedStatement.kind = static_cast<HirStmtKind>(88);
        broken.body.push_back(unsupportedStatement);
        broken.body.push_back(returnOne);
        module.functions.push_back(broken);

        // 未知 HIR 类型作为形参：签名无法生成，跳过整个函数。
        HirFunction badSignature;
        badSignature.name = "badSignature";
        badSignature.returnType = HirType::integer64;
        badSignature.parameters.emplace_back("value", static_cast<HirType>(77));
        module.functions.push_back(badSignature);

        const CBackendResult result = emitC(module);
        CHECK(!result.ok());
        CHECK(count_code(result.diagnostics, "E8002") == 3u);
        CHECK(contains(result.source, "int64_t good(void)\n{\n    return 1LL;\n}\n"));
        CHECK(contains(result.source, "int64_t broken(void)\n{\n    return 1LL;\n}\n"));
        CHECK(!contains(result.source, "badSignature"));
        CHECK(braces_balanced(result.source));

        // 源码能走到的 void 形参：HIR 接受，但 C 后端必须报 E8002 并跳过整个函数；
        // 同一模块里的正常函数继续生成。
        constexpr std::string_view kSource = R"(bad(x: void): integer
{
    return 1;
}
fine(): integer
{
    return 2;
}
)";
        HirModule voidModule;
        if (!lower_source("backend_void_param.coord", kSource, voidModule))
        {
            return false;
        }
        CHECK(voidModule.ok());

        const CBackendResult voidResult = emitC(voidModule);
        CHECK(!voidResult.ok());
        CHECK(count_code(voidResult.diagnostics, "E8002") == 1u);
        CHECK(!contains(voidResult.source, "bad("));
        CHECK(contains(voidResult.source, "int64_t fine(void)\n{\n    return 2LL;\n}\n"));
        CHECK(braces_balanced(voidResult.source));
        return true;
    }

    // 9) emitComments=false 时完全不输出注释，其余生成规则不变。
    bool test_comments_toggle()
    {
        constexpr std::string_view kSource = R"(add(a: integer, b: integer): integer
{
    return a + b;
}
)";

        HirModule module;
        if (!lower_source("backend_comments.coord", kSource, module))
        {
            return false;
        }
        CHECK(module.ok());

        CBackendOptions options;
        options.emitComments = false;
        const CBackendResult result = emitC(module, options);
        CHECK(result.ok());
        CHECK(!contains(result.source, "/*"));
        CHECK(!contains(result.source, "*/"));
        CHECK(!contains(result.source, "名称转义"));
        CHECK(contains(result.source, "#include <stdint.h>"));
        CHECK(contains(result.source,
            "int64_t add(int64_t a, int64_t b)\n{\n    return (a + b);\n}\n"));
        CHECK(braces_balanced(result.source));
        return true;
    }
}

int main()
{
    bool passed = true;
    if (!test_signatures_and_type_mapping())
    {
        passed = false;
    }
    if (!test_expression_parentheses())
    {
        passed = false;
    }
    if (!test_calls_and_forward_declaration())
    {
        passed = false;
    }
    if (!test_text_literals_and_comparison())
    {
        passed = false;
    }
    if (!test_escaped_identifiers())
    {
        passed = false;
    }
    if (!test_entry_point())
    {
        passed = false;
    }
    if (!test_unknown_type_warning())
    {
        passed = false;
    }
    if (!test_unknown_nodes_and_void_parameter())
    {
        passed = false;
    }
    if (!test_comments_toggle())
    {
        passed = false;
    }

    if (!passed)
    {
        std::cout << "FAIL: backend_c\n";
        return 1;
    }
    std::cout << "PASS: backend_c\n";
    return 0;
}
