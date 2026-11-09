// ModuleLoader.h 测试：多文件模块加载、import 解析与诊断。
// 覆盖：两模块依赖、传递依赖、按路径去重、缺失 import（E5003）、
// 重复模块名（E5004）、A->B->A 与自 import 循环（E5001）、入口不存在、
// 相对/绝对入口路径、缺省模块名、解析失败仍记录且 root 为空、
// 以及 a.b / a/b.cn 两种 import 路径形式与越界路径拒绝。
// 用临时目录写入 .cn 文件，结束时递归删除；不依赖仓库内固定路径。
// 全部通过时打印 "PASS: module_loader" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "compiler/ModuleLoader.h"

using namespace coordination::compiler;

namespace
{
    namespace fs = std::filesystem;

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

    bool contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    bool ends_with(std::string_view text, std::string_view suffix)
    {
        return text.size() >= suffix.size() &&
            text.substr(text.size() - suffix.size()) == suffix;
    }

    // 每次运行唯一的临时目录；析构时递归删除，写入失败不抛异常。
    class TempDirectory
    {
    public:
        TempDirectory()
        {
            std::error_code ec;
            fs::path base = fs::temp_directory_path(ec);
            if (ec || base.empty())
            {
                base = fs::current_path(ec);
            }

            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100 && path_.empty(); ++attempt)
            {
                const fs::path candidate = base /
                    ("coordination_module_loader_" + std::to_string(stamp) + "_" +
                        std::to_string(attempt));
                ec.clear();
                if (fs::create_directory(candidate, ec))
                {
                    path_ = candidate;
                }
            }
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        ~TempDirectory()
        {
            if (!path_.empty())
            {
                std::error_code ec;
                fs::remove_all(path_, ec);
            }
        }

        bool valid() const noexcept
        {
            return !path_.empty();
        }

        const fs::path& path() const noexcept
        {
            return path_;
        }

        // 根目录的 generic 绝对路径，直接交给 ModuleLoader::ModuleLoader。
        std::string text() const
        {
            return path_.generic_string();
        }

        // 写入相对路径 relative 的源文件，必要时创建父目录。
        bool write(std::string_view relative, std::string_view content) const
        {
            if (path_.empty())
            {
                return false;
            }

            const fs::path target = path_ / fs::path(std::string(relative));
            if (!target.parent_path().empty())
            {
                std::error_code ec;
                fs::create_directories(target.parent_path(), ec);
            }

            std::ofstream stream(target, std::ios::binary);
            if (!stream.is_open())
            {
                return false;
            }
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.close();
            return !stream.fail();
        }

    private:
        fs::path path_;
    };

    const Diagnostic* find_diagnostic(const PackageLoadResult& result, std::string_view code)
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

    std::size_t count_diagnostics(const PackageLoadResult& result, std::string_view code)
    {
        std::size_t count = 0;
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                ++count;
            }
        }
        return count;
    }

    void print_diagnostics(const PackageLoadResult& result, std::string_view label)
    {
        std::cout << "FAIL: " << label << " 产生错误\n";
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            std::cout << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        ++g_failures;
    }

    const ModuleFile* find_module(const PackageLoadResult& result, std::string_view name)
    {
        for (const ModuleFile& module : result.modules)
        {
            if (module.name == name)
            {
                return &module;
            }
        }
        return nullptr;
    }

    std::size_t count_modules(const PackageLoadResult& result, std::string_view name)
    {
        std::size_t count = 0;
        for (const ModuleFile& module : result.modules)
        {
            if (module.name == name)
            {
                ++count;
            }
        }
        return count;
    }

    // 1) 两个模块：入口 app import base，模块名取 module 声明，import 解析为模块名。
    void test_two_modules()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "两模块: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("base.cn",
            "module base;\n"
            "\n"
            "export class Base\n"
            "{\n"
            "    value: int = 0;\n"
            "}\n") &&
            dir.write("main.cn",
                "module app;\n"
                "\n"
                "import base;\n"
                "\n"
                "class App\n"
                "{\n"
                "    base: Base;\n"
                "}\n");
        if (!check(wrote, "两模块: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("main.cn");
        if (!result.ok())
        {
            print_diagnostics(result, "两模块");
            return;
        }

        check(result.root == dir.text(), "两模块: result.root 应为加载器根目录");
        check(loader.root() == dir.text(), "两模块: root() 应返回构造参数");
        check(result.modules.size() == 2, "两模块: 应加载 2 个文件");
        check(result.importCycles.empty(), "两模块: 不应有循环 import");
        check(count_diagnostics(result, "E5003") == 0, "两模块: 不应报 E5003");
        check(count_diagnostics(result, "E5004") == 0, "两模块: 不应报 E5004");

        const ModuleFile* app = find_module(result, "app");
        const ModuleFile* base = find_module(result, "base");
        check(app != nullptr, "两模块: 缺少模块 app");
        check(base != nullptr, "两模块: 缺少模块 base");
        if (app != nullptr)
        {
            check(app->root != nullptr && app->root->kind == AstKind::Module,
                "两模块: app 应有 Module 根节点");
            check(app->arena != nullptr, "两模块: app 应持有 arena");
            check(app->imports.size() == 1 && app->imports[0] == "base",
                "两模块: app.imports 应为 {base}");
            check(ends_with(app->path, "main.cn"), "两模块: app.path 应指向 main.cn");
        }
        if (base != nullptr)
        {
            check(base->imports.empty(), "两模块: base 不应有 import");
            check(ends_with(base->path, "base.cn"), "两模块: base.path 应指向 base.cn");
        }
        check(!result.modules.empty() && result.modules[0].name == "app",
            "两模块: 入口模块应排在加载顺序最前");
    }

    // 2) 传递依赖：a -> b -> c，三个文件都加载且 import 链完整。
    void test_transitive()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "传递依赖: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("c.cn",
            "module c;\n"
            "export class C\n"
            "{\n"
            "}\n") &&
            dir.write("b.cn",
                "module b;\n"
                "import c;\n"
                "export class B\n"
                "{\n"
                "    c: C;\n"
                "}\n") &&
            dir.write("a.cn",
                "module a;\n"
                "import b;\n"
                "export class A\n"
                "{\n"
                "    b: B;\n"
                "}\n");
        if (!check(wrote, "传递依赖: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("a.cn");
        if (!result.ok())
        {
            print_diagnostics(result, "传递依赖");
            return;
        }

        check(result.modules.size() == 3, "传递依赖: 应加载 3 个文件");
        if (result.modules.size() == 3)
        {
            check(result.modules[0].name == "a" && result.modules[1].name == "b" &&
                result.modules[2].name == "c", "传递依赖: 加载顺序应为 a,b,c");
        }

        const ModuleFile* a = find_module(result, "a");
        const ModuleFile* b = find_module(result, "b");
        const ModuleFile* c = find_module(result, "c");
        check(a != nullptr && b != nullptr && c != nullptr, "传递依赖: 缺少模块");
        if (a != nullptr)
        {
            check(a->imports.size() == 1 && a->imports[0] == "b",
                "传递依赖: a.imports 应为 {b}");
        }
        if (b != nullptr)
        {
            check(b->imports.size() == 1 && b->imports[0] == "c",
                "传递依赖: b.imports 应为 {c}");
        }
        if (c != nullptr)
        {
            check(c->imports.empty(), "传递依赖: c 不应有 import");
        }
    }

    // 3) 缺失 import：报 E5003，入口仍记录，缺失依赖保留在 imports 中。
    void test_missing_import()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "缺失 import: 创建临时目录失败"))
        {
            return;
        }

        if (!check(dir.write("main.cn",
            "module app;\n"
            "import missing.dep;\n"), "缺失 import: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("main.cn");
        check(!result.ok(), "缺失 import: ok() 应为 false");
        check(result.modules.size() == 1, "缺失 import: 只应记录入口文件");
        check(count_diagnostics(result, "E5003") == 1, "缺失 import: 应报一次 E5003");
        check(result.importCycles.empty(), "缺失 import: 不应有循环");

        const ModuleFile* app = find_module(result, "app");
        if (check(app != nullptr, "缺失 import: 缺少入口模块"))
        {
            check(app->imports.size() == 1, "缺失 import: imports 应保留缺失依赖");
            if (!app->imports.empty())
            {
                check(app->imports[0] == "missing.dep",
                    "缺失 import: 无法解析的依赖应保留 import 原文");
            }
        }
    }

    // 4) 重复模块名：两个文件都声明 module dup，报 E5004。
    void test_duplicate_module_name()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "重复模块名: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("a.cn",
            "module dup;\n"
            "import b;\n") &&
            dir.write("b.cn",
                "module dup;\n");
        if (!check(wrote, "重复模块名: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("a.cn");
        check(!result.ok(), "重复模块名: ok() 应为 false");
        check(result.modules.size() == 2, "重复模块名: 两个文件都应记录");
        check(count_diagnostics(result, "E5004") == 1, "重复模块名: 应报一次 E5004");
        check(result.importCycles.empty(), "重复模块名: 重名时跳过环检测");
    }

    // 5) A -> B -> A 循环：importCycles 记录环并报 E5001。
    void test_import_cycle()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "循环 import: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("a.cn",
            "module a;\n"
            "import b;\n") &&
            dir.write("b.cn",
                "module b;\n"
                "import a;\n");
        if (!check(wrote, "循环 import: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("a.cn");
        check(!result.ok(), "循环 import: ok() 应为 false");
        check(result.modules.size() == 2, "循环 import: 两个文件都应加载一次");
        check(result.importCycles.size() == 1, "循环 import: 应记录一个环");
        if (result.importCycles.size() == 1)
        {
            const Cycle& cycle = result.importCycles[0];
            check(cycle.path.size() == 3, "循环 import: 环路径应为三元");
            if (cycle.path.size() == 3)
            {
                check(cycle.path[0] == "a" && cycle.path[1] == "b" &&
                    cycle.path[2] == "a", "循环 import: 环路径应为 a -> b -> a");
            }
            check(describeCycle(cycle) == "a -> b -> a",
                "循环 import: describeCycle 文本应为 a -> b -> a");
        }
        check(count_diagnostics(result, "E5001") == 1, "循环 import: 应报一次 E5001");
    }

    // 6) 自 import：s -> s 也应报 E5001。
    void test_self_import_cycle()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "自 import: 创建临时目录失败"))
        {
            return;
        }

        if (!check(dir.write("s.cn",
            "module s;\n"
            "import s;\n"), "自 import: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("s.cn");
        check(!result.ok(), "自 import: ok() 应为 false");
        check(result.modules.size() == 1, "自 import: 只应加载一次");
        check(result.importCycles.size() == 1, "自 import: 应记录一个环");
        if (result.importCycles.size() == 1)
        {
            check(result.importCycles[0].path == std::vector<std::string>({"s", "s"}),
                "自 import: 环路径应为 {s, s}");
        }
        check(count_diagnostics(result, "E5001") == 1, "自 import: 应报一次 E5001");
    }

    // 7) 入口不存在：只报 E5003，modules 为空。
    void test_missing_entry()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "入口缺失: 创建临时目录失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("does_not_exist.cn");
        check(!result.ok(), "入口缺失: ok() 应为 false");
        check(result.modules.empty(), "入口缺失: modules 应为空");
        check(count_diagnostics(result, "E5003") == 1, "入口缺失: 应报一次 E5003");
        check(result.root == dir.text(), "入口缺失: result.root 应保留根目录");

        const PackageLoadResult absoluteResult =
            loader.load((dir.path() / "does_not_exist.cn").generic_string());
        check(!absoluteResult.ok(), "入口缺失(绝对路径): ok() 应为 false");
        check(absoluteResult.modules.empty(), "入口缺失(绝对路径): modules 应为空");
        check(count_diagnostics(absoluteResult, "E5003") == 1,
            "入口缺失(绝对路径): 应报一次 E5003");
    }

    // 8) 相对/绝对入口路径：同一文件得到一致的路径与模块名。
    void test_relative_and_absolute()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "相对/绝对路径: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("entry.cn",
            "module entry;\n"
            "import pkg.util;\n"
            "class Entry\n"
            "{\n"
            "}\n") &&
            dir.write("pkg/util.cn",
                "module pkg.util;\n"
                "export class Util\n"
                "{\n"
                "}\n");
        if (!check(wrote, "相对/绝对路径: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult relativeResult = loader.load("entry.cn");
        const PackageLoadResult absoluteResult =
            loader.load((dir.path() / "entry.cn").generic_string());

        if (!relativeResult.ok() || !absoluteResult.ok())
        {
            print_diagnostics(relativeResult, "相对路径");
            print_diagnostics(absoluteResult, "绝对路径");
            return;
        }

        check(relativeResult.modules.size() == 2, "相对路径: 应加载 2 个文件");
        check(absoluteResult.modules.size() == 2, "绝对路径: 应加载 2 个文件");
        if (relativeResult.modules.size() == 2 && absoluteResult.modules.size() == 2)
        {
            check(relativeResult.modules[0].path == absoluteResult.modules[0].path,
                "相对/绝对路径: 同一入口的 path 应一致");
            check(relativeResult.modules[1].path == absoluteResult.modules[1].path,
                "相对/绝对路径: 同一依赖的 path 应一致");
            check(relativeResult.modules[0].name == "entry" &&
                relativeResult.modules[1].name == "pkg.util",
                "相对/绝对路径: 模块名应取自 module 声明");
            check(relativeResult.modules[0].imports.size() == 1 &&
                relativeResult.modules[0].imports[0] == "pkg.util",
                "相对/绝对路径: import 应解析为模块名 pkg.util");
        }
    }

    // 9) 缺省模块名：没有 module 声明时用相对路径去扩展名。
    void test_default_module_name()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "缺省模块名: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("plain.cn",
            "class Plain\n"
            "{\n"
            "}\n") &&
            dir.write("pkg/helper.cn",
                "class Helper\n"
                "{\n"
                "}\n");
        if (!check(wrote, "缺省模块名: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult plainResult = loader.load("plain.cn");
        if (!check(plainResult.ok(), "缺省模块名: plain.cn 应加载成功"))
        {
            print_diagnostics(plainResult, "缺省模块名 plain.cn");
        }
        else if (check(plainResult.modules.size() == 1, "缺省模块名: plain.cn 应加载 1 个文件"))
        {
            check(plainResult.modules[0].name == "plain",
                "缺省模块名: plain.cn 的模块名应为 plain");
        }

        const PackageLoadResult helperResult = loader.load((dir.path() / "pkg" / "helper.cn")
            .generic_string());
        if (!check(helperResult.ok(), "缺省模块名: pkg/helper.cn 应加载成功"))
        {
            print_diagnostics(helperResult, "缺省模块名 pkg/helper.cn");
        }
        else if (check(helperResult.modules.size() == 1,
            "缺省模块名: pkg/helper.cn 应加载 1 个文件"))
        {
            check(helperResult.modules[0].name == "pkg/helper",
                "缺省模块名: 相对路径去扩展名应得到 pkg/helper");
        }
    }

    // 10) 解析失败：文件仍记录，root 为空，诊断并入结果。
    void test_parse_failure_recorded()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "解析失败: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("bad.cn",
            "module bad;\n"
            "class {\n"
            "}\n") &&
            dir.write("main.cn",
                "module app;\n"
                "import bad;\n");
        if (!check(wrote, "解析失败: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("main.cn");
        check(!result.ok(), "解析失败: ok() 应为 false");
        check(result.modules.size() == 2, "解析失败: 两个文件都应记录");
        check(count_diagnostics(result, "E5003") == 0, "解析失败: 不应报 E5003");

        const ModuleFile* bad = find_module(result, "bad");
        if (check(bad != nullptr, "解析失败: 缺少 bad 模块"))
        {
            check(bad->root == nullptr, "解析失败: root 应为空");
            check(bad->arena != nullptr, "解析失败: arena 仍应持有可能的节点");
        }

        bool has_syntax_error = false;
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (!diagnostic.code.empty() && diagnostic.code[0] == 'E' &&
                diagnostic.code.size() > 1 && diagnostic.code[1] == '2')
            {
                has_syntax_error = true;
            }
        }
        check(has_syntax_error, "解析失败: 应并入 E2xxx 语法诊断");
    }

    // 11) 按路径去重：两个模块 import 同一个文件，只加载一次。
    void test_path_dedup()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "路径去重: 创建临时目录失败"))
        {
            return;
        }

        const bool wrote = dir.write("shared.cn",
            "module shared;\n"
            "export class Shared\n"
            "{\n"
            "}\n") &&
            dir.write("left.cn",
                "module left;\n"
                "import shared;\n") &&
            dir.write("right.cn",
                "module right;\n"
                "import shared;\n") &&
            dir.write("app.cn",
                "module app;\n"
                "import left;\n"
                "import right;\n");
        if (!check(wrote, "路径去重: 写入源文件失败"))
        {
            return;
        }

        ModuleLoader loader(dir.text());
        const PackageLoadResult result = loader.load("app.cn");
        if (!result.ok())
        {
            print_diagnostics(result, "路径去重");
            return;
        }

        check(result.modules.size() == 4, "路径去重: shared 只应加载一次（共 4 个文件）");
        check(count_modules(result, "shared") == 1, "路径去重: 模块名 shared 应唯一");

        const ModuleFile* left = find_module(result, "left");
        const ModuleFile* right = find_module(result, "right");
        if (left != nullptr)
        {
            check(left->imports.size() == 1 && left->imports[0] == "shared",
                "路径去重: left.imports 应为 {shared}");
        }
        if (right != nullptr)
        {
            check(right->imports.size() == 1 && right->imports[0] == "shared",
                "路径去重: right.imports 应为 {shared}");
        }
    }

    // 12) import 路径形式：a.b 与 a/b.cn（以及 a/b）解析到同一文件；越界路径被拒绝。
    void test_import_path_forms()
    {
        TempDirectory dir;
        if (!check(dir.valid(), "import 路径形式: 创建临时目录失败"))
        {
            return;
        }

        // 与 ModuleLoader 内部保持一致：root 先规范化（Windows 8.3 短名会被展开）。
        const fs::path root = module_loader_detail::normalize_path(dir.path());
        const fs::path expected = module_loader_detail::normalize_path(root / "a" / "b.cn");
        check(module_loader_detail::resolve_import_path(root, "a.b") == expected,
            "import 路径形式: a.b 应解析为 a/b.cn");
        check(module_loader_detail::resolve_import_path(root, "a/b.cn") == expected,
            "import 路径形式: a/b.cn 应原样解析");
        check(module_loader_detail::resolve_import_path(root, "a/b") == expected,
            "import 路径形式: a/b 应自动补 .cn");
        check(module_loader_detail::resolve_import_path(root, "a.cn") ==
            module_loader_detail::normalize_path(root / "a.cn"),
            "import 路径形式: a.cn 应解析为根目录下的 a.cn");
        check(module_loader_detail::resolve_import_path(root, "").empty(),
            "import 路径形式: 空路径应拒绝");
        check(module_loader_detail::resolve_import_path(root, "../escape.cn").empty(),
            "import 路径形式: 越出 root 的路径应拒绝");
    }

    int run_all()
    {
        test_two_modules();
        test_transitive();
        test_missing_import();
        test_duplicate_module_name();
        test_import_cycle();
        test_self_import_cycle();
        test_missing_entry();
        test_relative_and_absolute();
        test_default_module_name();
        test_parse_failure_recorded();
        test_path_dedup();
        test_import_path_forms();

        if (g_failures == 0)
        {
            std::cout << "PASS: module_loader\n";
            return 0;
        }

        std::cout << "FAIL: module_loader (" << g_failures << " 处失败)\n";
        return 1;
    }
}

int main()
{
    return run_all();
}
