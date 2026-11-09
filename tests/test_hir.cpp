// Hir.h 自包含测试：不依赖第三方测试框架。
// 覆盖：Driver 解析真实源码后的 AST -> HIR 下降、函数/参数/局部变量槽位、
// 整数与布尔类型推断、integer(32) 统一为 64 位、文本字面量与文本比较、
// unknown 类型与前向调用、类内方法（无 this）、嵌套块展开、
// 以及 if/赋值/数组参数/顶层常量等不支持节点产生 E6001。
// 全部通过时打印 "PASS: hir" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

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

    std::size_t count_code(const HirModule& module, std::string_view code)
    {
        std::size_t count = 0;
        for (const Diagnostic& diagnostic : module.diagnostics.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                ++count;
            }
        }
        return count;
    }

    void print_hir_diagnostics(const HirModule& module)
    {
        for (const Diagnostic& diagnostic : module.diagnostics.diagnostics())
        {
            std::cout << "  " << diagnostic.code << ": " << diagnostic.message << "\n";
        }
    }

    // 用真实前端解析源码，再下降为 HIR。HIR 不保留 AST 指针，函数返回后仍可使用。
    bool lower_source(const char* display_name, std::string_view source, HirModule& module)
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

    // 1) 顶层函数、参数槽位、局部声明槽位与整数算术的类型推断。
    bool test_function_and_integer_types()
    {
        constexpr std::string_view kSource = R"(add(a: integer(64), b: integer(32)): integer(64)
{
    const sum: integer(64) = a + b;
    return sum * 2 - 1;
}
)";

        HirModule module;
        if (!lower_source("hir_integer.coord", kSource, module))
        {
            return false;
        }
        if (!module.ok())
        {
            print_hir_diagnostics(module);
        }
        CHECK(module.ok());
        CHECK(module.functions.size() == 1u);

        const HirFunction& function = module.functions[0];
        CHECK(function.name == "add");
        CHECK(function.parameters.size() == 2u);
        CHECK(function.parameters[0].first == "a");
        CHECK(function.parameters[0].second == HirType::integer64);
        CHECK(function.parameters[1].first == "b");
        CHECK(function.parameters[1].second == HirType::integer64); // integer(32) 统一为 integer64
        CHECK(function.returnType == HirType::integer64);
        CHECK(function.body.size() == 2u);
        CHECK(function.body[0].kind == HirStmtKind::localDecl);
        CHECK(function.body[0].name == "sum");
        CHECK(function.body[0].type == HirType::integer64);
        CHECK(function.body[1].kind == HirStmtKind::returnStmt);

        const std::string dump = dumpHir(module);
        CHECK(contains(dump,
            "(function add (param a integer64) (param b integer64) (result integer64)"));
        CHECK(contains(dump,
            "    (localDecl sum integer64 (binary add integer64 (localRef 0 a integer64) "
            "(localRef 1 b integer64)))\n"));
        CHECK(contains(dump,
            "(return (binary sub integer64 (binary mul integer64 (localRef 2 sum integer64) "
            "(integer 2 integer64)) (integer 1 integer64)))"));
        return true;
    }

    // 2) 布尔运算、比较、文本字面量与文本相等的类型推断，以及无返回值 return。
    bool test_boolean_and_text_inference()
    {
        constexpr std::string_view kSource = R"(check(flag: bool): bool
{
    const ok: bool = flag && !false;
    return (1 < 2) == ok;
}
say(): void
{
    const greeting = "hi\n";
    const same: bool = greeting == "hi\n";
    return;
}
)";

        HirModule module;
        if (!lower_source("hir_bool_text.coord", kSource, module))
        {
            return false;
        }
        if (!module.ok())
        {
            print_hir_diagnostics(module);
        }
        CHECK(module.ok());
        CHECK(module.functions.size() == 2u);

        const HirFunction& check_function = module.functions[0];
        CHECK(check_function.name == "check");
        CHECK(check_function.returnType == HirType::boolean);
        CHECK(check_function.body.size() == 2u);
        CHECK(check_function.body[0].type == HirType::boolean);
        CHECK(check_function.body[1].value.type == HirType::boolean);

        const HirFunction& say_function = module.functions[1];
        CHECK(say_function.name == "say");
        CHECK(say_function.returnType == HirType::voidType);
        CHECK(say_function.body.size() == 3u);
        CHECK(say_function.body[0].kind == HirStmtKind::localDecl);
        CHECK(say_function.body[0].name == "greeting");
        CHECK(say_function.body[0].type == HirType::text);
        CHECK(say_function.body[1].type == HirType::boolean);
        CHECK(say_function.body[2].kind == HirStmtKind::returnStmt);
        CHECK(say_function.body[2].value.type == HirType::voidType);

        const std::string dump = dumpHir(module);
        CHECK(contains(dump,
            "(localDecl ok boolean (binary logicalAnd boolean (localRef 0 flag boolean) "
            "(unary logicalNot boolean (boolean false boolean))))"));
        CHECK(contains(dump,
            "(return (binary eq boolean (binary lt boolean (integer 1 integer64) "
            "(integer 2 integer64)) (localRef 1 ok boolean)))"));
        CHECK(contains(dump, "(function say (result void)"));
        CHECK(contains(dump, "(localDecl greeting text (text \"hi\\n\" text))"));
        CHECK(contains(dump,
            "(localDecl same boolean (binary eq boolean (localRef 0 greeting text) "
            "(text \"hi\\n\" text)))"));
        CHECK(contains(dump, "    (return)\n"));
        return true;
    }

    // 3) unknown 类型、未识别命名类型降级、函数调用与前向调用返回类型。
    bool test_unknown_types_and_calls()
    {
        constexpr std::string_view kSource = R"(unknownValue(value: unknown): unknown
{
    const result: unknown = value;
    return result;
}
mystery(): Widget
{
    return 1;
}
use(): integer
{
    return mystery() + unknownValue(1);
}
)";

        HirModule module;
        if (!lower_source("hir_unknown.coord", kSource, module))
        {
            return false;
        }
        if (!module.ok())
        {
            print_hir_diagnostics(module);
        }
        CHECK(module.ok());
        CHECK(module.functions.size() == 3u);

        const HirFunction& unknown_function = module.functions[0];
        CHECK(unknown_function.parameters.size() == 1u);
        CHECK(unknown_function.parameters[0].second == HirType::unknown);
        CHECK(unknown_function.returnType == HirType::unknown);
        CHECK(unknown_function.body[0].type == HirType::unknown);

        const HirFunction& mystery_function = module.functions[1];
        CHECK(mystery_function.name == "mystery");
        CHECK(mystery_function.returnType == HirType::unknown); // Widget 降级为 unknown
        CHECK(mystery_function.body[0].value.type == HirType::integer64);

        const HirFunction& use_function = module.functions[2];
        CHECK(use_function.name == "use");
        CHECK(use_function.returnType == HirType::integer64);
        CHECK(use_function.body.size() == 1u);
        CHECK(use_function.body[0].value.type == HirType::unknown);

        const std::string dump = dumpHir(module);
        CHECK(contains(dump,
            "(function unknownValue (param value unknown) (result unknown)"));
        CHECK(contains(dump, "(localDecl result unknown (localRef 0 value unknown))"));
        CHECK(contains(dump,
            "(return (binary add unknown (call mystery unknown) "
            "(call unknownValue unknown (integer 1 integer64))))"));
        return true;
    }

    // 4) if 语句与赋值表达式不在最小子集内：各报一条 E6001 并跳过。
    bool test_unsupported_nodes()
    {
        constexpr std::string_view kSource = R"(choose(a: integer): integer
{
    if (a > 0)
    {
        return a;
    }
    return 0;
}
assign(): void
{
    const x = 1;
    x = 2;
    return;
}
)";

        HirModule module;
        if (!lower_source("hir_unsupported.coord", kSource, module))
        {
            return false;
        }
        if (count_code(module, "E6001") != 2u)
        {
            print_hir_diagnostics(module);
        }
        CHECK(!module.ok());
        CHECK(count_code(module, "E6001") == 2u);
        CHECK(module.functions.size() == 2u);

        const HirFunction& choose_function = module.functions[0];
        CHECK(choose_function.name == "choose");
        CHECK(choose_function.body.size() == 1u); // if 整条跳过，只剩 return 0
        CHECK(choose_function.body[0].kind == HirStmtKind::returnStmt);
        CHECK(choose_function.body[0].value.kind == HirExprKind::integerLiteral);

        const std::string dump = dumpHir(module);
        CHECK(!contains(dump, "(localRef 0 a integer64)")); // if 内部的 return a 未被下降
        CHECK(contains(dump, "(return (integer 0 integer64))"));
        CHECK(contains(dump, "(localDecl x integer64 (integer 1 integer64))"));
        CHECK(!contains(dump, "(exprStmt")); // 赋值语句整条跳过
        return true;
    }

    // 5) 类内无 this 的方法、字段静默忽略、嵌套块递归展开与槽位连续编号。
    bool test_class_method_and_nested_block()
    {
        constexpr std::string_view kSource = R"(class Counter
{
    seed: integer;

    add(a: integer, b: integer): integer
    {
        {
            const base = 10;
            return a + b + base;
        }
    }
}
)";

        HirModule module;
        if (!lower_source("hir_class.coord", kSource, module))
        {
            return false;
        }
        if (!module.ok())
        {
            print_hir_diagnostics(module);
        }
        CHECK(module.ok()); // 字段不进入 HIR，不产生诊断
        CHECK(module.functions.size() == 1u);

        const HirFunction& function = module.functions[0];
        CHECK(function.name == "add");
        CHECK(function.parameters.size() == 2u);
        CHECK(function.body.size() == 2u);

        const std::string dump = dumpHir(module);
        CHECK(contains(dump,
            "(function add (param a integer64) (param b integer64) (result integer64)"));
        CHECK(contains(dump, "(localDecl base integer64 (integer 10 integer64))"));
        CHECK(contains(dump,
            "(return (binary add integer64 (binary add integer64 (localRef 0 a integer64) "
            "(localRef 1 b integer64)) (localRef 2 base integer64)))"));
        return true;
    }

    // 6) 顶层常量与数组参数类型不支持：各报一条 E6001；坏函数整体跳过，好函数保留。
    bool test_unsupported_signature_and_top_level()
    {
        constexpr std::string_view kSource = R"(const GLOBAL: integer = 1;

bad(values: [integer; 2]): void
{
    return;
}

fine(): void
{
    return;
}
)";

        HirModule module;
        if (!lower_source("hir_signature.coord", kSource, module))
        {
            return false;
        }
        if (count_code(module, "E6001") != 2u)
        {
            print_hir_diagnostics(module);
        }
        CHECK(!module.ok());
        CHECK(count_code(module, "E6001") == 2u);
        CHECK(module.functions.size() == 1u);
        CHECK(module.functions[0].name == "fine");

        const std::string dump = dumpHir(module);
        CHECK(contains(dump, "(function fine (result void)"));
        CHECK(!contains(dump, "(function bad"));
        return true;
    }
}

int main()
{
    bool passed = true;
    if (!test_function_and_integer_types())
    {
        passed = false;
    }
    if (!test_boolean_and_text_inference())
    {
        passed = false;
    }
    if (!test_unknown_types_and_calls())
    {
        passed = false;
    }
    if (!test_unsupported_nodes())
    {
        passed = false;
    }
    if (!test_class_method_and_nested_block())
    {
        passed = false;
    }
    if (!test_unsupported_signature_and_top_level())
    {
        passed = false;
    }

    if (!passed)
    {
        std::cout << "FAIL: hir\n";
        return 1;
    }
    std::cout << "PASS: hir\n";
    return 0;
}
