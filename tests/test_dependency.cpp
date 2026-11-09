// Dependency.h 测试：模块依赖图/类型继承图的收集、环检测与增量缓存。
// 覆盖：无环 a->b->c、菱形 a->b,a->c,b->d,c->d、自环 a->a、二元环、三元环、
// 两个独立环、重边去重与字典序最小起点规范化；继承：单继承链、自继承、
// A:B/B:A、interface 多继承环、继承菱形；DependencyCache：update/has/
// fingerprint 稳定性（同样导出集合 fingerprint 相同、增删导出后变化）、
// dependencies/types 查询、未知模块返回空、非 Module 根节点不抛异常。
// 全部通过时打印 "PASS: dependency" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

    // 是否恰好 16 位小写十六进制。
    bool is_hex16(std::string_view text)
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

    void print_diagnostics(const FrontendResult& result, std::string_view label)
    {
        std::cout << "FAIL: " << label << " 前端报告错误\n";
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            std::cout << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        ++g_failures;
    }

    // 解析一段源码并把模块节点追加到 modules；语法错误记为失败。
    bool append_modules(std::string_view label, std::string_view source, std::vector<ModuleNode>& modules)
    {
        FrontendResult result = runFrontendFromText(label, source);
        if (result.module == nullptr || result.diagnostics.has_errors())
        {
            print_diagnostics(result, label);
            return false;
        }

        const std::vector<ModuleNode> collected = collectModules(*result.module);
        modules.insert(modules.end(), collected.begin(), collected.end());
        return true;
    }

    // 解析一段源码并把类型节点追加到 types；语法错误记为失败。
    bool append_types(std::string_view label, std::string_view source, std::vector<TypeNode>& types)
    {
        FrontendResult result = runFrontendFromText(label, source);
        if (result.module == nullptr || result.diagnostics.has_errors())
        {
            print_diagnostics(result, label);
            return false;
        }

        const std::vector<TypeNode> collected = collectTypes(*result.module);
        types.insert(types.end(), collected.begin(), collected.end());
        return true;
    }

    std::string cycle_text(const Cycle& cycle)
    {
        return describeCycle(cycle);
    }

    void print_cycles(const std::vector<Cycle>& cycles)
    {
        for (const Cycle& cycle : cycles)
        {
            std::cout << "  " << cycle_text(cycle) << '\n';
        }
    }

    // 断言环集合与 expected 完全一致：个数相同，且每个期望的环都出现。
    bool expect_cycles(const std::vector<Cycle>& cycles, std::initializer_list<std::string_view> expected,
        std::string_view label)
    {
        if (cycles.size() != expected.size())
        {
            std::cout << "FAIL: " << label << ": 期望 " << expected.size() << " 个环，实际 "
                      << cycles.size() << " 个\n";
            print_cycles(cycles);
            ++g_failures;
            return false;
        }

        bool ok = true;
        for (const std::string_view wanted : expected)
        {
            bool found = false;
            for (const Cycle& cycle : cycles)
            {
                const std::string text = cycle_text(cycle);
                if (std::string_view(text) == wanted)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                std::cout << "FAIL: " << label << ": 缺少环 " << wanted << '\n';
                print_cycles(cycles);
                ++g_failures;
                ok = false;
            }
        }
        return ok;
    }

    const TypeNode* find_type(const std::vector<TypeNode>& types, std::string_view name)
    {
        for (const TypeNode& type : types)
        {
            if (std::string_view(type.name) == name)
            {
                return &type;
            }
        }
        return nullptr;
    }

    bool has_base(const TypeNode& type, std::string_view base)
    {
        for (const std::string& edge : type.bases)
        {
            if (std::string_view(edge) == base)
            {
                return true;
            }
        }
        return false;
    }

    // ---------------- 循环 import ----------------

    void test_import_cycles()
    {
        // 无环链：a -> b -> c。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("chain_a", "module a;\nimport b;\n", modules);
            ok = append_modules("chain_b", "module b;\nimport c;\n", modules) && ok;
            ok = append_modules("chain_c", "module c;\nclass C { }\n", modules) && ok;
            if (ok)
            {
                check(modules.size() == 3, "无环链应收集到 3 个模块");
                check(modules[0].imports == std::vector<std::string>{"b"}, "a 的依赖应为 {b}");
                check(findImportCycles(modules).empty(), "无环链 a->b->c 不应报告环");
            }
        }

        // 菱形：a -> b、a -> c、b -> d、c -> d，不是环。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("diamond_a", "module a;\nimport b;\nimport c;\n", modules);
            ok = append_modules("diamond_b", "module b;\nimport d;\n", modules) && ok;
            ok = append_modules("diamond_c", "module c;\nimport d;\n", modules) && ok;
            ok = append_modules("diamond_d", "module d;\nclass D { }\n", modules) && ok;
            if (ok)
            {
                check(modules.size() == 4, "菱形应收集到 4 个模块");
                check(findImportCycles(modules).empty(), "菱形 a->b->d / a->c->d 不是环");
            }
        }

        // 自环：a -> a。
        {
            std::vector<ModuleNode> modules;
            if (append_modules("self_import", "module a;\nimport a;\n", modules))
            {
                expect_cycles(findImportCycles(modules), {"a -> a"}, "自环 import");
            }
        }

        // 二元环 + 重边：a 重复 import b（含别名），环只报一次。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("pair_a", "module a;\nimport b;\nimport b as alias;\n", modules);
            ok = append_modules("pair_b", "module b;\nimport a;\n", modules) && ok;
            if (ok)
            {
                check(modules[0].imports.size() == 1, "重复 import 应合并为一条依赖");
                expect_cycles(findImportCycles(modules), {"a -> b -> a"}, "二元环 + 重边");
            }
        }

        // 三元环：a -> b -> c -> a。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("triple_a", "module a;\nimport b;\n", modules);
            ok = append_modules("triple_b", "module b;\nimport c;\n", modules) && ok;
            ok = append_modules("triple_c", "module c;\nimport a;\n", modules) && ok;
            if (ok)
            {
                expect_cycles(findImportCycles(modules), {"a -> b -> c -> a"}, "三元环");
            }
        }

        // 两个独立环：a<->b 与 c<->d。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("two_a", "module a;\nimport b;\n", modules);
            ok = append_modules("two_b", "module b;\nimport a;\n", modules) && ok;
            ok = append_modules("two_c", "module c;\nimport d;\n", modules) && ok;
            ok = append_modules("two_d", "module d;\nimport c;\n", modules) && ok;
            if (ok)
            {
                expect_cycles(findImportCycles(modules), {"a -> b -> a", "c -> d -> c"}, "两个独立环");
            }
        }

        // 共享后缀的两个环（a->b->d->a 与 a->c->d->a）必须都被枚举，
        // 只靠"三色剪枝"会漏掉第二个。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("shared_a", "module a;\nimport b;\nimport c;\n", modules);
            ok = append_modules("shared_b", "module b;\nimport d;\n", modules) && ok;
            ok = append_modules("shared_c", "module c;\nimport d;\n", modules) && ok;
            ok = append_modules("shared_d", "module d;\nimport a;\n", modules) && ok;
            if (ok)
            {
                expect_cycles(findImportCycles(modules),
                    {"a -> b -> d -> a", "a -> c -> d -> a"}, "共享后缀的两个环");
            }
        }

        // 环规范化：输入顺序从 b 开始，报告仍从字典序最小的 a 起。
        {
            std::vector<ModuleNode> modules;
            bool ok = append_modules("norm_b", "module b;\nimport c;\n", modules);
            ok = append_modules("norm_c", "module c;\nimport a;\n", modules) && ok;
            ok = append_modules("norm_a", "module a;\nimport b;\n", modules) && ok;
            if (ok)
            {
                expect_cycles(findImportCycles(modules), {"a -> b -> c -> a"}, "环从字典序最小节点起");
            }
        }

        // 空输入：没有模块也没有环。
        {
            const std::vector<ModuleNode> modules;
            check(findImportCycles(modules).empty(), "空模块表不应有环");
        }
    }

    // ---------------- 循环继承 ----------------

    void test_inheritance_cycles()
    {
        // 单继承链：A : B : C，无环。
        {
            std::vector<TypeNode> types;
            if (append_types("inherit_chain", "class A : B { }\nclass B : C { }\nclass C { }\n", types))
            {
                check(types.size() == 3, "单继承链应收集 3 个类型");
                const TypeNode* type_a = find_type(types, "A");
                check(type_a != nullptr && type_a->bases.size() == 1 && has_base(*type_a, "B"),
                    "A 应有唯一父类型 B");
                check(findInheritanceCycles(types).empty(), "单继承链不应报告环");
            }
        }

        // 自继承：A : A。
        {
            std::vector<TypeNode> types;
            if (append_types("inherit_self", "class A : A { }\n", types))
            {
                expect_cycles(findInheritanceCycles(types), {"A -> A"}, "自继承");
            }
        }

        // 二元环：A : B、B : A。
        {
            std::vector<TypeNode> types;
            if (append_types("inherit_pair", "class A : B { }\nclass B : A { }\n", types))
            {
                expect_cycles(findInheritanceCycles(types), {"A -> B -> A"}, "二元继承环");
            }
        }

        // interface 多继承环：I1 -> I2 -> I1 与 I1 -> I3 -> I2 -> I1。
        {
            std::vector<TypeNode> types;
            if (append_types("inherit_interface",
                "interface I1 : I2, I3 { }\ninterface I2 : I1 { }\ninterface I3 : I2 { }\n", types))
            {
                const TypeNode* i1 = find_type(types, "I1");
                check(i1 != nullptr && i1->bases.size() == 2 && has_base(*i1, "I2") &&
                    has_base(*i1, "I3"), "interface I1 应有两个父接口");
                expect_cycles(findInheritanceCycles(types),
                    {"I1 -> I2 -> I1", "I1 -> I3 -> I2 -> I1"}, "interface 多继承环");
            }
        }

        // 继承菱形：A : B, C；B : D；C : D，不是环。
        {
            std::vector<TypeNode> types;
            if (append_types("inherit_diamond",
                "class A : B, C { }\nclass B : D { }\nclass C : D { }\nclass D { }\n", types))
            {
                check(findInheritanceCycles(types).empty(), "继承菱形不是环");
            }
        }

        // 空输入。
        {
            const std::vector<TypeNode> types;
            check(findInheritanceCycles(types).empty(), "空类型表不应有环");
        }
    }

    // ---------------- 增量缓存 ----------------

    void test_cache()
    {
        DependencyCache cache;
        check(cache.size() == 0, "空缓存 size 应为 0");
        check(!cache.has("a"), "空缓存 has 应为 false");

        FrontendResult a_source = runFrontendFromText("cache_a", "module a;\nimport b;\nexport class A { }\n");
        FrontendResult b_source = runFrontendFromText("cache_b", "module b;\nexport interface I { }\n");
        if (a_source.module == nullptr || a_source.diagnostics.has_errors())
        {
            print_diagnostics(a_source, "cache_a");
            return;
        }
        if (b_source.module == nullptr || b_source.diagnostics.has_errors())
        {
            print_diagnostics(b_source, "cache_b");
            return;
        }

        cache.update("a", *a_source.module);
        cache.update("b", *b_source.module);

        check(cache.size() == 2, "两次 update 后 size 应为 2");
        check(cache.has("a") && cache.has("b"), "has 应能查到已登记模块");
        check(!cache.has("zzz") && !cache.has(""), "has 对未知模块应返回 false");

        check(cache.fingerprint("a").size() == 16, "fingerprint 应为 16 位十六进制");
        check(is_hex16(cache.fingerprint("a")), "fingerprint 应只含小写十六进制字符");
        const std::vector<ModuleNode> collected = collectModules(*a_source.module);
        check(collected.size() == 1 && collected[0].fingerprint == cache.fingerprint("a"),
            "cache 与 collectModules 的 fingerprint 应一致");

        check(cache.dependencies("a") == std::vector<std::string>{"b"}, "a 的依赖应为 {b}");
        check(cache.dependencies("b").empty(), "b 没有依赖");
        check(cache.dependencies("zzz").empty(), "未知模块的依赖为空");

        const std::vector<TypeNode> types_a = cache.types("a");
        check(types_a.size() == 1 && types_a[0].name == "A" && types_a[0].bases.empty(),
            "a 的类型表应只有 A");
        const std::vector<TypeNode> types_b = cache.types("b");
        check(types_b.size() == 1 && types_b[0].name == "I", "b 的类型表应只有 I");
        check(cache.types("zzz").empty(), "未知模块的类型表为空");
        check(cache.fingerprint("zzz").empty(), "未知模块 fingerprint 为空串");

        // 导出集合相同（模块名、import 不同）=> fingerprint 相同。
        FrontendResult twin = runFrontendFromText("cache_twin", "module twin;\nexport class A { }\n");
        if (twin.ok())
        {
            cache.update("twin", *twin.module);
            check(cache.size() == 3, "新增模块后 size 应为 3");
            check(cache.fingerprint("twin") == cache.fingerprint("a"),
                "导出集合相同的模块 fingerprint 应相同");
        }
        else
        {
            print_diagnostics(twin, "cache_twin");
        }

        // 增加导出 => fingerprint 变化，条目数不变（update 整体替换）。
        const std::string before_more = cache.fingerprint("a");
        FrontendResult more = runFrontendFromText("cache_more",
            "module a;\nimport b;\nexport class A { }\nexport class B { }\n");
        if (more.ok())
        {
            cache.update("a", *more.module);
            check(cache.fingerprint("a") != before_more, "增加导出后 fingerprint 应变化");
            check(cache.size() == 3, "update 只替换旧记录，不应新增条目");
            check(cache.types("a").size() == 2, "更新后 a 的类型表应含 A、B");
        }
        else
        {
            print_diagnostics(more, "cache_more");
        }

        // 依赖被整体替换，删掉导出 B 后回到与 twin 相同的 fingerprint。
        FrontendResult moved = runFrontendFromText("cache_moved",
            "module a;\nimport c;\nexport class A { }\n");
        if (moved.ok())
        {
            cache.update("a", *moved.module);
            check(cache.dependencies("a") == std::vector<std::string>{"c"},
                "update 后依赖应整体替换为 {c}");
            check(cache.fingerprint("a") == cache.fingerprint("twin"),
                "删掉导出 B 后应与只导出 A 的模块 fingerprint 相同");
        }
        else
        {
            print_diagnostics(moved, "cache_moved");
        }

        // export 声明能被 fingerprint 感知：未导出 -> 导出，指纹变化。
        FrontendResult plain = runFrontendFromText("cache_plain", "module plain;\nclass P { }\n");
        FrontendResult exported = runFrontendFromText("cache_exported",
            "module plain;\nexport class P { }\n");
        if (plain.ok() && exported.ok())
        {
            cache.update("plain", *plain.module);
            const std::string plain_fingerprint = cache.fingerprint("plain");
            cache.update("plain", *exported.module);
            check(cache.fingerprint("plain") != plain_fingerprint,
                "把声明改成 export 后 fingerprint 应变化");
        }
        else
        {
            print_diagnostics(plain, "cache_plain");
            print_diagnostics(exported, "cache_exported");
        }

        // 无导出模块与空集合指纹一致。
        FrontendResult empty_source = runFrontendFromText("cache_empty", "module empty;\n");
        if (empty_source.ok())
        {
            cache.update("empty", *empty_source.module);
            check(is_hex16(cache.fingerprint("empty")), "无导出模块仍有 16 位 fingerprint");
            check(cache.fingerprint("empty") != cache.fingerprint("twin"),
                "无导出与有导出的 fingerprint 应不同");
        }
        else
        {
            print_diagnostics(empty_source, "cache_empty");
        }

        // 非 Module 根节点：不抛异常，记录为空导出/依赖/类型。
        AstArena arena;
        AstNode* error_node = arena.make(AstKind::Error, Span{});
        check(error_node != nullptr, "AstArena::make 应返回节点");
        if (error_node != nullptr)
        {
            cache.update("bad", *error_node);
            check(cache.has("bad"), "非 Module 根节点也应建立缓存条目");
            check(cache.dependencies("bad").empty() && cache.types("bad").empty(),
                "非 Module 根节点的依赖与类型应为空");
            check(cache.fingerprint("bad") == cache.fingerprint("empty"),
                "空导出集合与空记录的 fingerprint 应相同");
        }

        check(cache.size() == 6, "最终缓存条目数应为 6");
    }
}

int main()
{
    test_import_cycles();
    test_inheritance_cycles();
    test_cache();

    if (g_failures != 0)
    {
        std::cout << "FAIL: dependency (" << g_failures << " 处)\n";
        return 1;
    }

    std::cout << "PASS: dependency\n";
    return 0;
}
