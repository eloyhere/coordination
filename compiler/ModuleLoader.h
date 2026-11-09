#pragma once

// ============================================================================
// 多文件模块加载与 import 解析：入口文件 -> 递归加载 import 目标 -> 诊断/环检测。
//
// 解析规则：
//   * import 中的点分路径 a.b -> <root>/a/b.cn；
//   * 也接受 a/b.cn 形式（a/b 会自动补 .cn）；含 '/' 或 '\\' 的文本按文件路径处理；
//   * import 不允许越出 root；绝对路径只用于 load() 的入口参数；
//   * 每个文件都用 Driver::runFrontend 读取并解析，同一文件按规范化绝对路径去重；
//   * 模块名优先取 `module a.b;` 声明，缺省用相对 root 的路径去扩展名
//     （只去掉最后一个扩展名，分隔符原样保留，例如 pkg/util.cn -> "pkg/util"）；
//   * 循环 import 用 Dependency::findImportCycles 检测并报 E5001；模块名重复
//     （E5004）时依赖图节点不唯一，跳过环检测，避免把重名合并出的边误报成环。
//
// 诊断码（E5xxx 段）：
//   E5001 循环 import
//   E5003 文件缺失 / import 路径无法解析（入口文件不存在也用 E5003）
//   E5004 模块名重复
//
// 失败策略：
//   * 入口文件不存在：报 E5003，modules 为空；
//   * import 文件不存在：报 E5003，不产生对应 ModuleFile（依赖名仍保留在 imports）；
//   * 文件存在但词法/语法有错：仍记录 ModuleFile，root 置空，诊断并入结果；
//   * 所有接口不抛异常；AstNode* 仅在对应 ModuleFile.arena 存活期间有效。
//
// 加载顺序：入口在前，随后按源码 import 顺序做先序深度优先展开；
// 依赖已经在加载中或已加载时不会重复解析（按路径去重）。
// ============================================================================

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Dependency.h"
#include "Diagnostic.h"
#include "Driver.h"

namespace coordination::compiler
{
    // 一个已加载（或已尝试解析）的模块文件。
    struct ModuleFile
    {
        std::string path;          // 源文件路径（规范化为绝对路径，generic 分隔符）
        std::string name;          // 模块名（module 声明，缺省用相对路径去扩展名）
        std::unique_ptr<AstArena> arena;
        AstNode* root = nullptr;
        std::vector<std::string> imports;   // 解析后的模块名
    };

    // 一次包加载的完整结果。
    struct PackageLoadResult
    {
        std::string root;
        std::vector<ModuleFile> modules;    // 按加载顺序
        std::vector<Cycle> importCycles;    // E5001
        DiagnosticBag diagnostics;

        bool ok() const noexcept
        {
            return !diagnostics.has_errors();
        }
    };

    namespace module_loader_detail
    {
        inline char ascii_lower(char ch) noexcept
        {
            return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        }

        inline bool is_space(char ch) noexcept
        {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        }

        inline std::string trim(std::string_view text)
        {
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && is_space(text[begin]))
            {
                ++begin;
            }
            while (end > begin && is_space(text[end - 1]))
            {
                --end;
            }
            return std::string(text.substr(begin, end - begin));
        }

        // 是否以 .cn 结尾（扩展名比较不区分大小写）。
        inline bool ends_with_cn(std::string_view text) noexcept
        {
            if (text.size() < 3)
            {
                return false;
            }
            const std::size_t dot = text.size() - 3;
            return text[dot] == '.' &&
                ascii_lower(text[dot + 1]) == 'c' &&
                ascii_lower(text[dot + 2]) == 'n';
        }

        inline bool has_separator(std::string_view text) noexcept
        {
            return text.find('/') != std::string_view::npos ||
                text.find('\\') != std::string_view::npos;
        }

        // 规范化：优先 weakly_canonical（解析符号链接/消除 ".."），失败时退化为
        // absolute + lexically_normal。全部使用 error_code 重载，不抛异常。
        inline std::filesystem::path normalize_path(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return std::filesystem::path();
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (!ec && !normalized.empty())
            {
                return normalized;
            }

            normalized = std::filesystem::absolute(path, ec);
            if (ec || normalized.empty())
            {
                normalized = path;
            }
            return normalized.lexically_normal();
        }

        inline std::string path_text(const std::filesystem::path& path)
        {
            return path.generic_string();
        }

        // 是否为可读取的普通文件；目录、断链与无权限都按 false 处理。
        inline bool is_regular_file(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return false;
            }
            std::error_code ec;
            const std::filesystem::file_status status = std::filesystem::status(path, ec);
            return !ec && std::filesystem::is_regular_file(status);
        }

        // path 是否位于 root 之内（两者都应是 normalize_path 的结果）。
        inline bool is_within(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            const std::filesystem::path relative = path.lexically_relative(root);
            if (relative.empty())
            {
                return false;
            }
            const auto first = relative.begin();
            return first == relative.end() || *first != std::filesystem::path("..");
        }

        // 缺省模块名：相对 root 的路径去掉最后一个扩展名（例如 pkg/util.cn -> pkg/util）。
        // 文件在 root 之外（入口允许绝对路径）时退化为文件名。
        inline std::string default_module_name(const std::filesystem::path& root,
            const std::filesystem::path& path)
        {
            std::filesystem::path relative = path.lexically_relative(root);
            bool outside = relative.empty();
            if (!outside)
            {
                const auto first = relative.begin();
                outside = first != relative.end() && *first == std::filesystem::path("..");
            }
            if (outside)
            {
                relative = path.filename();
            }

            relative.replace_extension();
            std::string name = relative.generic_string();
            while (name.rfind("./", 0) == 0)
            {
                name.erase(0, 2);
            }
            if (name.empty())
            {
                name = path.stem().generic_string();
            }
            return name;
        }

        // 把 import 文本解析成绝对文件路径；返回空路径表示无法解析（空文本或越出 root）。
        inline std::filesystem::path resolve_import_path(const std::filesystem::path& root,
            std::string_view importText)
        {
            const std::string raw = trim(importText);
            if (raw.empty())
            {
                return std::filesystem::path();
            }

            const std::filesystem::path given(raw);
            std::filesystem::path candidate;
            if (given.is_absolute())
            {
                candidate = given;
            }
            else if (has_separator(raw) || ends_with_cn(raw))
            {
                // 文件路径形式：a/b.cn；缺扩展名时补 .cn。
                candidate = root / given;
                if (!ends_with_cn(raw))
                {
                    candidate += std::filesystem::path(".cn");
                }
            }
            else
            {
                // 点分模块路径：a.b -> a/b.cn。
                std::string filePath = raw;
                std::replace(filePath.begin(), filePath.end(), '.', '/');
                candidate = root / std::filesystem::path(filePath + ".cn");
            }

            candidate = normalize_path(candidate);
            if (candidate.empty() || !is_within(root, candidate))
            {
                return std::filesystem::path();
            }
            return candidate;
        }

        // 入口路径：绝对路径原样使用，相对路径相对 root 解析。
        inline std::filesystem::path resolve_entry_path(const std::filesystem::path& root,
            std::string_view entryPath)
        {
            const std::string text(entryPath);
            const std::filesystem::path given(text);
            if (given.is_absolute())
            {
                return normalize_path(given);
            }
            return normalize_path(root / given);
        }
    }

    // 多文件模块加载器：根目录固定，load() 每个入口独立加载并返回结果。
    class ModuleLoader
    {
    public:
        explicit ModuleLoader(std::string rootDirectory)
            : root_(std::move(rootDirectory))
            , rootPath_(module_loader_detail::normalize_path(std::filesystem::path(root_)))
        {
        }

        // 加载入口及其全部传递 import。entryPath 相对 root 或为绝对路径；
        // 不抛异常，所有问题进入返回值的 diagnostics。
        PackageLoadResult load(std::string_view entryPath)
        {
            PackageLoadResult result;
            result.root = root_;

            const std::filesystem::path entry =
                module_loader_detail::resolve_entry_path(rootPath_, entryPath);
            if (!module_loader_detail::is_regular_file(entry))
            {
                result.diagnostics.error("E5003",
                    "入口文件不存在或不是普通文件: " + module_loader_detail::path_text(entry), Span{});
                return result;
            }

            // 待加载栈：后进先出，配合"逆序压入 import"实现先序深度优先。
            std::vector<std::filesystem::path> pending;
            pending.push_back(entry);

            std::map<std::string, std::size_t, std::less<>> moduleByPath;   // 路径 -> modules 下标
            std::map<std::string, std::string, std::less<>> firstPathByName; // 模块名 -> 首个文件路径

            // import 目标路径的待回填记录：全部文件加载完后再写最终模块名。
            struct ImportRef
            {
                std::size_t module = 0;
                std::size_t index = 0;
                std::string pathKey;
            };
            std::vector<ImportRef> importRefs;

            bool duplicateName = false;

            while (!pending.empty())
            {
                const std::filesystem::path path = pending.back();
                pending.pop_back();

                const std::string pathKey = module_loader_detail::path_text(path);
                if (moduleByPath.find(pathKey) != moduleByPath.end())
                {
                    continue;
                }

                const std::size_t moduleIndex = result.modules.size();

                ModuleFile file;
                file.path = pathKey;

                FrontendResult frontend = runFrontend(file.path);
                for (const Diagnostic& diagnostic : frontend.diagnostics.diagnostics())
                {
                    result.diagnostics.add(diagnostic.severity, diagnostic.code,
                        diagnostic.message, diagnostic.span);
                }

                // 有词法/语法错误时仍记录文件，但 root 置空；模块名仍尽量取已解析的声明。
                const bool parsedOk = frontend.module != nullptr &&
                    !frontend.diagnostics.has_errors();
                if (frontend.module != nullptr && !frontend.module->text.empty())
                {
                    file.name = frontend.module->text;
                }
                else
                {
                    file.name = module_loader_detail::default_module_name(rootPath_, path);
                }
                file.root = parsedOk ? frontend.module : nullptr;

                const auto nameOwner = firstPathByName.find(file.name);
                if (nameOwner == firstPathByName.end())
                {
                    firstPathByName.emplace(file.name, file.path);
                }
                else
                {
                    duplicateName = true;
                    result.diagnostics.error("E5004",
                        "重复模块名 " + file.name + ": " + nameOwner->second + " 与 " + file.path,
                        Span{});
                }

                // 先把本文件登记进路径索引：自 import 才能解析回自己（环检测依赖它）。
                moduleByPath.emplace(pathKey, moduleIndex);

                std::vector<std::string> rawImports;
                if (frontend.module != nullptr)
                {
                    dependency_detail::append_imports(*frontend.module, rawImports);
                }

                std::vector<std::filesystem::path> discovered;
                for (const std::string& raw : rawImports)
                {
                    const std::filesystem::path target =
                        module_loader_detail::resolve_import_path(rootPath_, raw);
                    const bool loadable = module_loader_detail::is_regular_file(target);
                    if (!loadable)
                    {
                        const std::string shown = target.empty()
                            ? std::string("<无法解析>")
                            : module_loader_detail::path_text(target);
                        result.diagnostics.error("E5003",
                            "import 引用的模块文件不存在: " + raw + " (" + shown + ")", Span{});
                    }

                    // 可解析的目标先放缺省名占位，目标加载完成后回填它的真实模块名；
                    // 无法解析的依赖保留 import 原文，便于消费方看到缺失的模块名。
                    file.imports.push_back(loadable && !target.empty()
                        ? module_loader_detail::default_module_name(rootPath_, target)
                        : std::string(raw));

                    if (loadable)
                    {
                        ImportRef ref;
                        ref.module = moduleIndex;
                        ref.index = file.imports.size() - 1;
                        ref.pathKey = module_loader_detail::path_text(target);
                        importRefs.push_back(ref);

                        if (moduleByPath.find(ref.pathKey) == moduleByPath.end())
                        {
                            discovered.push_back(target);
                        }
                    }
                }

                file.arena = std::move(frontend.arena);
                result.modules.push_back(std::move(file));

                // 逆序压栈：出栈顺序与原源码 import 顺序一致。
                for (auto it = discovered.rbegin(); it != discovered.rend(); ++it)
                {
                    pending.push_back(*it);
                }
            }

            // 回填 import 的最终模块名（目标可能声明了与文件路径不同的名字）。
            for (const ImportRef& ref : importRefs)
            {
                const auto found = moduleByPath.find(ref.pathKey);
                if (found != moduleByPath.end() && ref.module < result.modules.size())
                {
                    ModuleFile& owner = result.modules[ref.module];
                    if (ref.index < owner.imports.size())
                    {
                        owner.imports[ref.index] = result.modules[found->second].name;
                    }
                }
            }

            // 模块名重复时依赖图节点不唯一，环检测没有确定语义，跳过 E5001。
            if (!duplicateName)
            {
                std::vector<ModuleNode> graph;
                graph.reserve(result.modules.size());
                for (const ModuleFile& file : result.modules)
                {
                    ModuleNode node;
                    node.name = file.name;
                    node.imports = file.imports;
                    graph.push_back(std::move(node));
                }

                result.importCycles = findImportCycles(graph);
                for (const Cycle& cycle : result.importCycles)
                {
                    result.diagnostics.error("E5001",
                        "循环 import: " + describeCycle(cycle), Span{});
                }
            }

            return result;
        }

        const std::string& root() const noexcept
        {
            return root_;
        }

    private:
        std::string root_;
        std::filesystem::path rootPath_;
    };
}
