// coordination 命令行入口：
//   coordination version
//   coordination ast   <file.cn>            解析并打印语法树
//   coordination check <file.cn>            解析 + 语义分析 + 环检测（有 import 时按包加载）
//   coordination build <file.cn> [-o out.c] 前端 → 折叠 → HIR → C 源码（包级：合并 import 图）
//   coordination run   <exe>                执行并打印退出码
//
// 退出码：0 成功，1 发现错误，2 用法错误。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/BackendC.h"
#include "compiler/Dependency.h"
#include "compiler/DiagnosticPrinter.h"
#include "compiler/Driver.h"
#include "compiler/Fold.h"
#include "compiler/Hir.h"
#include "compiler/ModuleLoader.h"
#include "compiler/Sema.h"
#include "stdlib/process/Process.h"

namespace
{
    using namespace coordination::compiler;

    void print_usage()
    {
        std::fputs("usage:\n"
                   "  coordination version\n"
                   "  coordination ast   <file.cn>\n"
                   "  coordination check <file.cn>\n"
                   "  coordination build <file.cn> [-o out.c]\n"
                   "  coordination run   <exe>\n",
                   stderr);
    }

    void print_diagnostics(const FrontendResult& result)
    {
        const std::string text = renderDiagnostics(result);
        if (!text.empty())
        {
            std::fputs(text.c_str(), stderr);
        }
    }

    void print_bag(const DiagnosticBag& diagnostics, const SourceManager& sources)
    {
        const std::string text = format_diagnostics(diagnostics.diagnostics(), sources);
        if (!text.empty())
        {
            std::fputs(text.c_str(), stderr);
        }
    }

    // 包级诊断来自多个文件，没有共享 SourceManager，这里只输出错误码与信息。
    void print_raw(const DiagnosticBag& diagnostics)
    {
        for (const Diagnostic& diagnostic : diagnostics.diagnostics())
        {
            std::fprintf(stderr, "%s: %s\n", diagnostic.code.c_str(), diagnostic.message.c_str());
        }
    }

    bool has_imports(const FrontendResult& result)
    {
        if (result.module == nullptr)
        {
            return false;
        }
        for (const ModuleNode& module : collectModules(*result.module))
        {
            if (!module.imports.empty())
            {
                return true;
            }
        }
        return false;
    }

    std::string parent_directory(std::string_view path)
    {
        const std::filesystem::path value(path);
        const std::filesystem::path parent = value.parent_path();
        return parent.empty() ? std::string(".") : parent.string();
    }

    std::size_t check_inheritance_cycles(const FrontendResult& result)
    {
        if (result.module == nullptr)
        {
            return 0;
        }

        std::size_t errors = 0;
        for (const Cycle& cycle : findInheritanceCycles(collectTypes(*result.module)))
        {
            std::fprintf(stderr, "E5002: 循环继承: %s\n", describeCycle(cycle).c_str());
            ++errors;
        }
        return errors;
    }

    std::string default_c_path(std::string_view sourcePath)
    {
        std::string path(sourcePath);
        const std::size_t slash = path.find_last_of("/\\");
        const std::size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        {
            path.resize(dot);
        }
        path += ".c";
        return path;
    }

    bool write_text(const std::string& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            std::fprintf(stderr, "E3002: 无法写入 %s\n", path.c_str());
            return false;
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream.good())
        {
            std::fprintf(stderr, "E3002: 写入 %s 失败\n", path.c_str());
            return false;
        }
        return true;
    }

    int command_ast(std::string_view path)
    {
        FrontendResult result = runFrontend(path);
        print_diagnostics(result);
        if (!result.ok())
        {
            return 1;
        }

        std::printf("%s\n", dumpAst(result).c_str());
        return 0;
    }

    // 顶层声明名：type/class/interface/concept/annotation/function/const/mutable 都算。
    std::string declaration_name(const AstNode& declaration)
    {
        std::string text = declaration.text;
        const std::size_t colon = text.find(':');
        if (colon != std::string::npos)
        {
            text.erase(0, colon + 1);
        }
        return text;
    }

    void collect_declared_names(const AstNode& root, std::set<std::string>& names)
    {
        for (const AstNode* child : root.children)
        {
            const AstNode* declaration = child;
            if (child->kind == AstKind::ExportDecl && !child->children.empty())
            {
                declaration = child->children.front();
            }
            switch (declaration->kind)
            {
            case AstKind::ClassDecl:
            case AstKind::InterfaceDecl:
            case AstKind::ConceptDecl:
            case AstKind::AnnotationDecl:
            case AstKind::TypeAliasDecl:
            case AstKind::DistinctTypeDecl:
            case AstKind::FunctionDecl:
            case AstKind::VariableDecl:
            case AstKind::ConstantDecl:
                names.insert(declaration_name(*declaration));
                break;
            default:
                break;
            }
        }
    }

    // 跨模块标识符由包内其它模块声明时，Sema 的单模块视图无法解析，这里按包声明集过滤。
    bool is_external_identifier(const Diagnostic& diagnostic, const std::set<std::string>& declared)
    {
        if (diagnostic.code != "E4003")
        {
            return false;
        }
        const std::size_t open = diagnostic.message.find('\'');
        const std::size_t close = diagnostic.message.rfind('\'');
        if (open == std::string::npos || close == std::string::npos || close <= open + 1)
        {
            return false;
        }
        return declared.count(diagnostic.message.substr(open + 1, close - open - 1)) != 0;
    }

    // 包级检查：加载 import 图，报 E5001/E5003/E5004，再逐模块做语义与继承环检查。
    int command_check_package(std::string_view path)
    {
        ModuleLoader loader(parent_directory(path));
        PackageLoadResult package = loader.load(std::filesystem::path(path).filename().string());
        print_raw(package.diagnostics);

        std::size_t errors = package.diagnostics.has_errors() ? 1 : 0;
        for (const Cycle& cycle : package.importCycles)
        {
            std::fprintf(stderr, "E5001: 循环 import: %s\n", describeCycle(cycle).c_str());
            ++errors;
        }

        std::set<std::string> declared;
        for (const ModuleFile& file : package.modules)
        {
            if (file.root != nullptr)
            {
                collect_declared_names(*file.root, declared);
            }
        }

        std::size_t symbols = 0;
        for (ModuleFile& file : package.modules)
        {
            if (file.root == nullptr)
            {
                continue;
            }
            const FoldResult folded = foldConstants(*file.arena, *file.root);
            print_raw(folded.diagnostics);

            const SemaResult sema = analyze(*file.root);
            symbols += sema.symbolCount;
            bool moduleErrors = false;
            for (const Diagnostic& diagnostic : sema.diagnostics.diagnostics())
            {
                if (is_external_identifier(diagnostic, declared))
                {
                    continue;
                }
                std::fprintf(stderr, "%s: %s\n", diagnostic.code.c_str(), diagnostic.message.c_str());
                moduleErrors = moduleErrors || diagnostic.severity == Severity::error;
            }
            errors += moduleErrors ? 1 : 0;

            for (const Cycle& cycle : findInheritanceCycles(collectTypes(*file.root)))
            {
                std::fprintf(stderr, "E5002: 循环继承: %s\n", describeCycle(cycle).c_str());
                ++errors;
            }
        }

        if (errors == 0)
        {
            std::printf("ok: 包 %s（模块 %zu，符号 %zu）\n", package.root.c_str(), package.modules.size(), symbols);
        }
        return errors == 0 ? 0 : 1;
    }

    int command_check(std::string_view path)
    {
        FrontendResult result = runFrontend(path);
        print_diagnostics(result);
        if (!result.ok())
        {
            return 1;
        }

        if (has_imports(result))
        {
            return command_check_package(path);
        }

        const FoldResult folded = foldConstants(*result.arena, *result.module);
        print_bag(folded.diagnostics, result.sources);

        const SemaResult sema = analyze(*result.module);
        print_bag(sema.diagnostics, result.sources);
        const std::size_t cycleErrors = check_inheritance_cycles(result);
        const bool failed = result.diagnostics.has_errors() || sema.diagnostics.has_errors() || cycleErrors != 0;
        if (!failed)
        {
            std::printf("ok: %s（符号 %zu，类型 %zu）\n", std::string(path).c_str(), sema.symbolCount, sema.typeCount);
        }
        return failed ? 1 : 0;
    }

    // 包级构建：合并 import 图中所有模块的函数到一个 HirModule，再生成 C。
    int command_build_package(std::string_view path, std::string_view outPath)
    {
        ModuleLoader loader(parent_directory(path));
        PackageLoadResult package = loader.load(std::filesystem::path(path).filename().string());
        print_raw(package.diagnostics);

        bool failed = package.diagnostics.has_errors();
        for (const Cycle& cycle : package.importCycles)
        {
            std::fprintf(stderr, "E5001: 循环 import: %s\n", describeCycle(cycle).c_str());
            failed = true;
        }
        if (failed)
        {
            return 1;
        }

        HirModule combined;
        std::set<std::string> seen;
        for (ModuleFile& file : package.modules)
        {
            if (file.root == nullptr)
            {
                continue;
            }

            const FoldResult folded = foldConstants(*file.arena, *file.root);
            print_raw(folded.diagnostics);

            HirModule hir = lowerToHir(*file.root);
            print_raw(hir.diagnostics);
            for (HirFunction& function : hir.functions)
            {
                if (seen.insert(function.name).second)
                {
                    combined.functions.push_back(std::move(function));
                }
            }
        }

        CBackendOptions options;
        options.emitEntryPoint = true;
        options.emitComments = false;
        const CBackendResult generated = emitC(combined, options);
        print_raw(generated.diagnostics);
        if (!generated.ok())
        {
            return 1;
        }

        const std::string target = outPath.empty() ? default_c_path(path) : std::string(outPath);
        if (!write_text(target, generated.source))
        {
            return 1;
        }
        std::printf("generated: %s（模块 %zu）\n", target.c_str(), package.modules.size());
        return 0;
    }

    int command_build(std::string_view path, std::string_view outPath)
    {
        FrontendResult result = runFrontend(path);
        print_diagnostics(result);
        if (!result.ok())
        {
            return 1;
        }

        if (has_imports(result))
        {
            return command_build_package(path, outPath);
        }

        const FoldResult folded = foldConstants(*result.arena, *result.module);
        print_bag(folded.diagnostics, result.sources);

        HirModule hir = lowerToHir(*result.module);
        print_bag(hir.diagnostics, result.sources);

        CBackendOptions options;
        options.emitEntryPoint = true;
        options.emitComments = false;
        const CBackendResult generated = emitC(hir, options);
        print_bag(generated.diagnostics, result.sources);
        if (!generated.ok())
        {
            return 1;
        }

        const std::string target = outPath.empty() ? default_c_path(path) : std::string(outPath);
        if (!write_text(target, generated.source))
        {
            return 1;
        }
        std::printf("generated: %s\n", target.c_str());
        return 0;
    }

    int command_run(std::string_view executable)
    {
        coordination::process::Command command;
        command.program = std::string(executable);
        const coordination::process::ProcessResult result = coordination::process::run(command);
        if (!result.exited && !result.error.empty())
        {
            std::fprintf(stderr, "%s\n", result.error.c_str());
            return 1;
        }

        std::fwrite(result.stdout_data.data(), 1, result.stdout_data.size(), stdout);
        std::fwrite(result.stderr_data.data(), 1, result.stderr_data.size(), stderr);
        std::printf("exit code: %d\n", result.exit_code);
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        print_usage();
        return 2;
    }

    const std::string_view command = argv[1];
    if (command == "version")
    {
        std::printf("coordination 0.2.0\n");
        return 0;
    }

    if (command == "ast" || command == "check")
    {
        if (argc != 3)
        {
            print_usage();
            return 2;
        }
        return command == "ast" ? command_ast(argv[2]) : command_check(argv[2]);
    }

    if (command == "build")
    {
        if (argc != 3 && argc != 5)
        {
            print_usage();
            return 2;
        }
        std::string_view outPath;
        if (argc == 5)
        {
            if (std::string_view(argv[3]) != "-o")
            {
                print_usage();
                return 2;
            }
            outPath = argv[4];
        }
        return command_build(argv[2], outPath);
    }

    if (command == "run")
    {
        if (argc != 3)
        {
            print_usage();
            return 2;
        }
        return command_run(argv[2]);
    }

    print_usage();
    return 2;
}
