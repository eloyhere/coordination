// Sema.h 测试：语义分析 MVP（声明收集、作用域与名称解析、重复/未定义诊断）。
// 覆盖：合法最小模块、内建类型、全局变量、前向引用、类型参数、
// 顶层重名 (E4001)、成员重名 (E4001)、局部重名 (E4001)、
// 未知字段/参数/局部变量类型 (E4002)、未知标识符 (E4003)、
// 循环外 break/continue (E4004)、void 函数带值 return (E4005)、
// 未声明标注 (E4006)、循环内 break/continue 不应误报。
// 全部通过时打印 "PASS: sema" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <string_view>

#include "compiler/Driver.h"
#include "compiler/Sema.h"

using namespace coordination::compiler;

namespace
{
    // 期望某诊断码出现的次数。
    struct CodeCount
    {
        std::string_view code;
        std::size_t count = 1;
    };

    void print_diagnostics(const DiagnosticBag& bag)
    {
        for (const Diagnostic& diagnostic : bag.diagnostics())
        {
            std::cout << "  " << diagnostic.code << " [" << diagnostic.span.begin << ", "
                      << diagnostic.span.end << "): " << diagnostic.message << '\n';
        }
    }

    bool check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cout << "FAIL: " << message << '\n';
            return false;
        }
        return true;
    }

    std::size_t count_code(const DiagnosticBag& bag, std::string_view code)
    {
        std::size_t count = 0;
        for (const Diagnostic& diagnostic : bag.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                ++count;
            }
        }
        return count;
    }

    // 同时断言诊断总条数与每个诊断码的出现次数；预期条数为 0 时还要求 ok() 为真。
    bool expect_diagnostics(const SemaResult& sema, std::initializer_list<CodeCount> expected,
        std::string_view label)
    {
        const DiagnosticBag& bag = sema.diagnostics;
        std::size_t total = 0;
        for (const CodeCount& item : expected)
        {
            total += item.count;
        }

        if (sema.ok() != (total == 0))
        {
            std::cout << "FAIL: " << label << ": ok() 与诊断数量不一致\n";
            print_diagnostics(bag);
            return false;
        }
        if (bag.diagnostics().size() != total)
        {
            std::cout << "FAIL: " << label << ": 期望 " << total << " 条诊断，实际 "
                      << bag.diagnostics().size() << " 条\n";
            print_diagnostics(bag);
            return false;
        }
        for (const CodeCount& item : expected)
        {
            const std::size_t actual = count_code(bag, item.code);
            if (actual != item.count)
            {
                std::cout << "FAIL: " << label << ": " << item.code << " 期望出现 " << item.count
                          << " 次，实际 " << actual << " 次\n";
                print_diagnostics(bag);
                return false;
            }
        }
        return true;
    }

    // 持有 FrontendResult 与 SemaResult，保证 SemaResult 里的 AST 指针在测试期间有效。
    class Analysis
    {
    public:
        Analysis(std::string_view display_name, std::string_view source)
            : frontend_(runFrontendFromText(display_name, source))
        {
            if (frontend_.ok())
            {
                sema_ = analyze(*frontend_.module);
            }
        }

        const FrontendResult& frontend() const noexcept
        {
            return frontend_;
        }

        const SemaResult& sema() const noexcept
        {
            return sema_;
        }

        bool parsed() const noexcept
        {
            return frontend_.ok();
        }

    private:
        FrontendResult frontend_;
        SemaResult sema_;
    };

    // 1) 合法最小模块：class + 字段 + 方法 + 构造器 + 局部变量。
    bool test_valid_module()
    {
        constexpr std::string_view kSource = R"(module demo;

class Point
{
    x: integer;
    y: integer;

    init(x: integer, y: integer)
    {
        this.x = x;
        this.y = y;
    }

    move(dx: integer, dy: integer): void
    {
        const nx = this.x + dx;
        mutable ny: integer = this.y + dy;
        this.x = nx;
        this.y = ny;
    }

    length(): integer
    {
        return this.x * this.x + this.y * this.y;
    }
}
)";

        Analysis analysis("valid.coord", kSource);
        if (!check(analysis.parsed(), "valid module: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        if (!check(analysis.sema().typeCount == 1, "valid module: 应有 1 个类型声明"))
        {
            return false;
        }
        if (!check(analysis.sema().symbolCount == 12, "valid module: 应收集到 12 个符号"))
        {
            return false;
        }

        const Symbol* point = analysis.sema().findTopLevel("Point");
        if (!check(point != nullptr && point->kind == SymbolKind::Class,
            "valid module: Point 应为顶层 Class 符号"))
        {
            return false;
        }
        if (!check(analysis.sema().findTopLevel("Nope") == nullptr,
            "valid module: 未声明名字不应被查到"))
        {
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "valid module");
    }

    // 2) 内建类型名：integer/character/timestamp/module/unknown/void/bool。
    bool test_builtin_types()
    {
        constexpr std::string_view kSource = R"(class Primitives
{
    a: integer;
    b: character;
    c: timestamp;
    d: module;
    e: unknown;
    f: bool;

    accept(value: module): void
    {
        const copy: integer = 0;
    }
}
)";

        Analysis analysis("builtin.coord", kSource);
        if (!check(analysis.parsed(), "builtin types: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "builtin types");
    }

    // 3) 模块级常量/变量可被函数体引用。
    bool test_top_level_variables()
    {
        constexpr std::string_view kSource = R"(const limit = 100;

mutable counter: integer = 0;

reset(): void
{
    counter = limit;
}
)";

        Analysis analysis("globals.coord", kSource);
        if (!check(analysis.parsed(), "top-level variables: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "top-level variables");
    }

    // 4) 前向引用：字段使用后面才声明的类型，方法调用后面才声明的方法。
    bool test_forward_reference()
    {
        constexpr std::string_view kSource = R"(class Widget
{
    item: Gadget;

    make(): void
    {
        this.build();
    }

    build(): void
    {
    }
}

class Gadget
{
}
)";

        Analysis analysis("forward.coord", kSource);
        if (!check(analysis.parsed(), "forward reference: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        if (!check(analysis.sema().typeCount == 2, "forward reference: 应有 2 个类型声明"))
        {
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "forward reference");
    }

    // 5) 类型参数 T 在字段与方法返回类型中可解析。
    bool test_type_parameter()
    {
        constexpr std::string_view kSource = R"(class Box<T>
{
    value: T;

    get(): T
    {
        return this.value;
    }
}
)";

        Analysis analysis("typeparam.coord", kSource);
        if (!check(analysis.parsed(), "type parameter: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "type parameter");
    }

    // 6) 顶层声明重名 -> E4001。
    bool test_top_level_duplicate()
    {
        constexpr std::string_view kSource = R"(class Payload
{
}

class Payload
{
}
)";

        Analysis analysis("dup_top.coord", kSource);
        if (!check(analysis.parsed(), "top-level duplicate: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4001", 1}}, "top-level duplicate");
    }

    // 7) 类成员重名 -> E4001。
    bool test_member_duplicate()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    value: integer;
    value: integer;
}
)";

        Analysis analysis("dup_member.coord", kSource);
        if (!check(analysis.parsed(), "member duplicate: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4001", 1}}, "member duplicate");
    }

    // 8) 同一 block 内局部变量重名 -> E4001。
    bool test_local_duplicate()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run(): void
    {
        const value = 1;
        const value = 2;
    }
}
)";

        Analysis analysis("dup_local.coord", kSource);
        if (!check(analysis.parsed(), "local duplicate: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4001", 1}}, "local duplicate");
    }

    // 9) 未知字段类型 -> E4002。
    bool test_unknown_field_type()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    value: MissingType;
}
)";

        Analysis analysis("unknown_type.coord", kSource);
        if (!check(analysis.parsed(), "unknown field type: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4002", 1}}, "unknown field type");
    }

    // 10) 参数类型、返回类型、局部变量类型中的未知类型名 -> 3 条 E4002。
    bool test_unknown_signature_types()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run(value: MissingParam): MissingReturn
    {
        const local: MissingLocal = value;
    }
}
)";

        Analysis analysis("unknown_types.coord", kSource);
        if (!check(analysis.parsed(), "unknown signature types: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4002", 3}}, "unknown signature types");
    }

    // 11) 表达式中引用未声明标识符 -> E4003。
    bool test_unknown_identifier()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    probe(): void
    {
        missing();
    }
}
)";

        Analysis analysis("unknown_ident.coord", kSource);
        if (!check(analysis.parsed(), "unknown identifier: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4003", 1}}, "unknown identifier");
    }

    // 12) this 出现在类型外（顶层函数）-> E4003。
    bool test_this_outside_type()
    {
        constexpr std::string_view kSource = R"(run(): void
{
    this.x = 1;
}
)";

        Analysis analysis("this_top.coord", kSource);
        if (!check(analysis.parsed(), "this outside type: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4003", 1}}, "this outside type");
    }

    // 13) 循环外 break -> E4004。
    bool test_break_outside_loop()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run(): void
    {
        break;
    }
}
)";

        Analysis analysis("break.coord", kSource);
        if (!check(analysis.parsed(), "break outside loop: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4004", 1}}, "break outside loop");
    }

    // 14) 循环外 continue -> E4004。
    bool test_continue_outside_loop()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run(): void
    {
        continue;
    }
}
)";

        Analysis analysis("continue.coord", kSource);
        if (!check(analysis.parsed(), "continue outside loop: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4004", 1}}, "continue outside loop");
    }

    // 15) 循环内 break/continue 不应误报。
    bool test_loop_jumps_are_valid()
    {
        constexpr std::string_view kSource = R"(class Looping
{
    run(): void
    {
        mutable index: integer = 0;
        while (index < 10)
        {
            index = index + 1;
            if (index == 5)
            {
                break;
            }
            continue;
        }
    }
}
)";

        Analysis analysis("loop.coord", kSource);
        if (!check(analysis.parsed(), "loop jumps: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "loop jumps");
    }

    // 16) 返回类型为 void 的函数带值 return -> E4005。
    bool test_void_return_value()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run(): void
    {
        return 1;
    }
}
)";

        Analysis analysis("void_return.coord", kSource);
        if (!check(analysis.parsed(), "void return value: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4005", 1}}, "void return value");
    }

    // 17) 返回类型缺失也按 void 处理 -> E4005。
    bool test_missing_return_type()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    run()
    {
        return 1;
    }
}
)";

        Analysis analysis("missing_return.coord", kSource);
        if (!check(analysis.parsed(), "missing return type: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4005", 1}}, "missing return type");
    }

    // 18) 已声明的标注可以正常使用。
    bool test_annotation_declared()
    {
        constexpr std::string_view kSource = R"(annotation serializable();

@serializable
class Good
{
}
)";

        Analysis analysis("annotation_ok.coord", kSource);
        if (!check(analysis.parsed(), "declared annotation: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        if (!check(analysis.sema().typeCount == 2, "declared annotation: 应有 2 个类型声明"))
        {
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "declared annotation");
    }

    // 19) 未声明的标注名 -> E4006。
    bool test_annotation_unknown()
    {
        constexpr std::string_view kSource = R"(@missing
class Broken
{
}
)";

        Analysis analysis("annotation_bad.coord", kSource);
        if (!check(analysis.parsed(), "unknown annotation: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4006", 1}}, "unknown annotation");
    }

    // 20) MVP 限制：import 声明整棵子树跳过，不产生 E4xxx。
    bool test_import_skipped()
    {
        constexpr std::string_view kSource = R"(import missing.package;

class Local
{
}
)";

        Analysis analysis("import.coord", kSource);
        if (!check(analysis.parsed(), "import skipped: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "import skipped");
    }

    // 21) export 包裹的声明仍按顶层符号收集，exported 标记正确。
    bool test_exported_declaration()
    {
        constexpr std::string_view kSource = R"(export class Visible
{
}
)";

        Analysis analysis("export.coord", kSource);
        if (!check(analysis.parsed(), "exported declaration: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        const Symbol* visible = analysis.sema().findTopLevel("Visible");
        if (!check(visible != nullptr && visible->kind == SymbolKind::Class && visible->exported,
            "exported declaration: Visible 应为带 exported 的顶层 Class 符号"))
        {
            return false;
        }
        return expect_diagnostics(analysis.sema(), {}, "exported declaration");
    }

    // 22) this 上不存在的成员 -> E4003。
    bool test_unknown_member()
    {
        constexpr std::string_view kSource = R"(class Broken
{
    probe(): void
    {
        this.missing();
    }
}
)";

        Analysis analysis("unknown_member.coord", kSource);
        if (!check(analysis.parsed(), "unknown member: 语法层应通过"))
        {
            print_diagnostics(analysis.frontend().diagnostics);
            return false;
        }
        return expect_diagnostics(analysis.sema(), {{"E4003", 1}}, "unknown member");
    }

    using TestFunction = bool (*)();

    constexpr TestFunction kTests[] = {
        &test_valid_module,
        &test_builtin_types,
        &test_top_level_variables,
        &test_forward_reference,
        &test_type_parameter,
        &test_top_level_duplicate,
        &test_member_duplicate,
        &test_local_duplicate,
        &test_unknown_field_type,
        &test_unknown_signature_types,
        &test_unknown_identifier,
        &test_this_outside_type,
        &test_break_outside_loop,
        &test_continue_outside_loop,
        &test_loop_jumps_are_valid,
        &test_void_return_value,
        &test_missing_return_type,
        &test_annotation_declared,
        &test_annotation_unknown,
        &test_import_skipped,
        &test_exported_declaration,
        &test_unknown_member,
    };
}

int main()
{
    for (const TestFunction test : kTests)
    {
        if (!test())
        {
            return 1;
        }
    }

    std::cout << "PASS: sema\n";
    return 0;
}
