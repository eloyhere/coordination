#pragma once

// ============================================================================
// 依赖图与继承图：模块导入/导出、类型继承边的收集、增量缓存与环检测。
//
// 本文件只消费 Ast.h 的通用 AST（节点结构约定见 Parser.h 顶部注释），
// 不修改 AST、不抛异常；所有公开接口位于 coordination::compiler。
//
// 图模型：
//   * 模块图：节点 = 模块名，边 = ImportDecl 的目标路径（`export import` 重导出
//     同样计入）；"a.b" 这样的点分路径整串作为节点名，不做拆分。
//   * 继承图：节点 = class/interface 名，边 = `:` 后的每个父类型（多继承 = 多条边；
//     基类型名无法提取时丢弃该条边，例如函数类型）。
//
// 环检测：
//   * 返回全部基本环（简单环），每个环首尾同名且从字典序最小的节点起；
//   * 同一环只出现一次；自环 A->A 表示为 {"A", "A"}；菱形结构不是环；
//   * 实现见 dependency_detail::find_cycles（DFS 三色标记 + 栈路径）。
//
// fingerprint：
//   * 导出符号名（class/interface/concept/annotation/type/distinct、函数、运算符、
//     变量与常量）排序去重后做 FNV-1a 64 位哈希，输出 16 位小写十六进制；
//   * 只取决于导出集合，与模块名、import、声明正文无关。
// ============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ast.h"

namespace coordination::compiler
{
    // 模块图节点：模块名 + 直接依赖（import 的目标路径）+ 导出集合指纹。
    struct ModuleNode
    {
        std::string name;
        std::vector<std::string> imports;
        std::string fingerprint;
    };

    // 继承图节点：类型名 + 全部直接父类型（class/interface 的 `:` 列表）。
    struct TypeNode
    {
        std::string name;
        std::vector<std::string> bases;
    };

    // 一个基本环：path 首尾同名，例如 {"a", "b", "a"}；从字典序最小的节点起。
    struct Cycle
    {
        std::vector<std::string> path;
    };

    namespace dependency_detail
    {
        inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
        inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        // 64 位哈希转 16 位小写十六进制。
        inline std::string to_hex64(std::uint64_t value)
        {
            static constexpr char kDigits[] = "0123456789abcdef";
            std::string text(16, '0');
            for (std::size_t i = 0; i < 16; ++i)
            {
                text[15 - i] = kDigits[static_cast<std::size_t>(value & 0x0FULL)];
                value >>= 4;
            }
            return text;
        }

        // 导出名集合 -> 指纹：排序去重后逐个做 FNV-1a 64（名字之间补 '\n' 分隔，
        // 避免 "ab" + "c" 与 "a" + "bc" 撞成同一输入）。
        inline std::string fingerprint_of(std::vector<std::string> names)
        {
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());

            std::uint64_t hash = kFnvOffsetBasis;
            for (const std::string& name : names)
            {
                for (const char ch : name)
                {
                    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
                    hash *= kFnvPrime;
                }
                hash ^= static_cast<std::uint64_t>('\n');
                hash *= kFnvPrime;
            }
            return to_hex64(hash);
        }

        // 去掉 Parser 用 text 前缀承载的修饰符（"const:名字" / "mutable:名字" 等）。
        inline std::string_view strip_declaration_prefix(std::string_view text) noexcept
        {
            const std::size_t colon = text.find(':');
            if (colon == std::string_view::npos)
            {
                return text;
            }
            const std::string_view prefix = text.substr(0, colon);
            if (prefix == "const" || prefix == "mutable" || prefix == "out" || prefix == "in")
            {
                return text.substr(colon + 1);
            }
            return text;
        }

        // 是否为类型引用节点（继承边只可能由这些节点构成）。
        inline bool is_type_ast(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::NamedType:
            case AstKind::GenericType:
            case AstKind::PointerType:
            case AstKind::ReferenceType:
            case AstKind::MutableReferenceType:
            case AstKind::ArrayType:
            case AstKind::DynType:
            case AstKind::FunctionType:
                return true;
            default:
                return false;
            }
        }

        // 顶层声明里可能带导出名的种类；ImportDecl（重导出）与成员不在其中。
        inline bool is_export_symbol_kind(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::ClassDecl:
            case AstKind::InterfaceDecl:
            case AstKind::ConceptDecl:
            case AstKind::AnnotationDecl:
            case AstKind::TypeAliasDecl:
            case AstKind::DistinctTypeDecl:
            case AstKind::FunctionDecl:
            case AstKind::OperatorDecl:
            case AstKind::VariableDecl:
            case AstKind::ConstantDecl:
                return true;
            default:
                return false;
            }
        }

        // 父类型引用 -> 名字；包装类型（指针/引用/dyn/数组）取最内层名字。
        inline std::string type_reference_name(const AstNode& node)
        {
            switch (node.kind)
            {
            case AstKind::NamedType:
            case AstKind::GenericType:
                return node.text;
            case AstKind::PointerType:
            case AstKind::ReferenceType:
            case AstKind::MutableReferenceType:
            case AstKind::DynType:
            case AstKind::ArrayType:
            {
                const AstNode* inner = node.child(0);
                return inner == nullptr ? std::string() : type_reference_name(*inner);
            }
            default:
                return std::string();
            }
        }

        // 追加一个非空且尚未出现过的名字（保持源码顺序）。
        inline void push_unique(std::vector<std::string>& values, std::string value)
        {
            if (value.empty())
            {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end())
            {
                values.push_back(std::move(value));
            }
        }

        // 收集模块的导出符号名；`export` 包裹与声明自身的 exported 标志都算导出。
        inline std::vector<std::string> collect_export_names(const AstNode& module)
        {
            std::vector<std::string> names;
            if (module.kind != AstKind::Module)
            {
                return names;
            }

            for (const AstNode* item : module.children)
            {
                if (item == nullptr)
                {
                    continue;
                }

                const AstNode* declaration = item;
                if (item->kind == AstKind::ExportDecl)
                {
                    declaration = item->child(0);
                }
                if (declaration == nullptr || declaration->kind == AstKind::ImportDecl)
                {
                    // 重导出整模块不产生本地符号名。
                    continue;
                }
                if (!is_export_symbol_kind(declaration->kind))
                {
                    continue;
                }

                const bool exported = item->kind == AstKind::ExportDecl ||
                    declaration->has_flag(AstFlags::exported);
                if (!exported)
                {
                    continue;
                }

                const std::string_view name = strip_declaration_prefix(declaration->text);
                if (!name.empty())
                {
                    names.emplace_back(name);
                }
            }
            return names;
        }

        // 追加一条 import 的模块路径（去重，保持源码顺序）。
        inline void append_import(const AstNode* declaration, std::vector<std::string>& imports)
        {
            if (declaration == nullptr || declaration->kind != AstKind::ImportDecl)
            {
                return;
            }
            push_unique(imports, declaration->text);
        }

        // 收集模块直接依赖：顶层 ImportDecl，以及 ExportDecl 包裹的重导出 import。
        inline void append_imports(const AstNode& module, std::vector<std::string>& imports)
        {
            if (module.kind != AstKind::Module)
            {
                return;
            }
            for (const AstNode* item : module.children)
            {
                if (item == nullptr)
                {
                    continue;
                }
                append_import(item, imports);
                if (item->kind == AstKind::ExportDecl)
                {
                    append_import(item->child(0), imports);
                }
            }
        }

        // 收集 class/interface 的继承边；concept 与其它声明不进入继承图。
        // 父类型是类型声明孩子里的直接类型节点（孩子顺序见 Parser.h 的 AST 约定），
        // 因此直接孩子过滤即可，无需深入成员内部。
        inline void collect_type_nodes(const AstNode& module, std::vector<TypeNode>& types)
        {
            if (module.kind != AstKind::Module)
            {
                return;
            }

            for (const AstNode* item : module.children)
            {
                if (item == nullptr)
                {
                    continue;
                }

                const AstNode* declaration = item;
                if (item->kind == AstKind::ExportDecl)
                {
                    declaration = item->child(0);
                }
                if (declaration == nullptr ||
                    (declaration->kind != AstKind::ClassDecl && declaration->kind != AstKind::InterfaceDecl))
                {
                    continue;
                }
                if (declaration->text.empty())
                {
                    continue;
                }

                TypeNode node;
                node.name = declaration->text;
                for (const AstNode* child : declaration->children)
                {
                    if (child == nullptr || !is_type_ast(child->kind))
                    {
                        continue;
                    }
                    push_unique(node.bases, type_reference_name(*child));
                }
                types.push_back(std::move(node));
            }
        }

        // 迭代式 DFS 栈帧：node 为当前节点，next 为下一条待检查的出边下标。
        struct CycleSearchFrame
        {
            std::size_t node = 0;
            std::size_t next = 0;
        };

        // 枚举全部基本环。
        //
        // 算法：对每个可能的最小起点 start，只在"名字不小于 start"的子图里做 DFS，
        // 并且只把回到 start 的边记为一个环。因为环上所有节点都 >= start 且含 start，
        // 该环只会在它的最小节点这一轮被发现，天然满足"从字典序最小节点起"和
        // "同一环只报一次"。
        //
        // 三色语义：0 白 = 尚未进入；1 灰 = 正在当前栈路径上；2 黑 = 已展开完。
        // 为了枚举全部基本环，黑色节点仍允许重入（只是不能进入灰色节点，保证路径
        // 始终简单、搜索必然终止）——只用三色剪枝只能判定"有无环"，会漏掉共享后缀
        // 的环（例如 A->B->D->A 与 A->C->D->A）。
        inline std::vector<Cycle> find_cycles(
            const std::vector<std::pair<std::string, std::vector<std::string>>>& edges)
        {
            std::vector<std::string> names;
            for (const auto& edge : edges)
            {
                names.push_back(edge.first);
                for (const std::string& target : edge.second)
                {
                    names.push_back(target);
                }
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());

            std::map<std::string, std::size_t, std::less<>> index;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                index.emplace(names[i], i);
            }

            // 相邻表：同一对 (from, to) 的重边只保留一条，避免同一环被重复报告。
            std::vector<std::vector<std::size_t>> adjacency(names.size());
            for (const auto& edge : edges)
            {
                const auto from = index.find(edge.first);
                if (from == index.end())
                {
                    continue;
                }
                for (const std::string& target : edge.second)
                {
                    const auto to = index.find(target);
                    if (to == index.end())
                    {
                        continue;
                    }
                    adjacency[from->second].push_back(to->second);
                }
            }
            for (std::vector<std::size_t>& targets : adjacency)
            {
                std::sort(targets.begin(), targets.end());
                targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
            }

            std::vector<Cycle> cycles;
            std::vector<int> color(names.size(), 0);
            std::vector<std::size_t> path;
            std::vector<CycleSearchFrame> stack;

            for (std::size_t start = 0; start < names.size(); ++start)
            {
                std::fill(color.begin(), color.end(), 0);
                path.clear();
                stack.clear();

                path.push_back(start);
                color[start] = 1;
                stack.push_back(CycleSearchFrame{start, 0});

                while (!stack.empty())
                {
                    const std::size_t node = stack.back().node;
                    if (stack.back().next >= adjacency[node].size())
                    {
                        color[node] = 2;
                        stack.pop_back();
                        path.pop_back();
                        continue;
                    }

                    const std::size_t target = adjacency[node][stack.back().next];
                    ++stack.back().next;

                    if (target < start)
                    {
                        // 环的最小节点必须不小于 start，否则留给更小的起点那一轮。
                        continue;
                    }
                    if (target == start)
                    {
                        Cycle cycle;
                        cycle.path.reserve(path.size() + 1);
                        for (const std::size_t on_path : path)
                        {
                            cycle.path.push_back(names[on_path]);
                        }
                        cycle.path.push_back(names[start]);
                        cycles.push_back(std::move(cycle));
                        continue;
                    }
                    if (color[target] == 1)
                    {
                        // 目标在当前路径上：继续会形成非基本环，跳过。
                        continue;
                    }

                    color[target] = 1;
                    path.push_back(target);
                    stack.push_back(CycleSearchFrame{target, 0});
                }
            }

            std::sort(cycles.begin(), cycles.end(), [](const Cycle& lhs, const Cycle& rhs)
            {
                return lhs.path < rhs.path;
            });
            cycles.erase(std::unique(cycles.begin(), cycles.end(), [](const Cycle& lhs, const Cycle& rhs)
            {
                return lhs.path == rhs.path;
            }), cycles.end());
            return cycles;
        }
    }

    // 从一个 Module 节点收集模块名（根节点 text，可能为空）、直接依赖与导出指纹。
    // 只返回一个元素；根节点不是 Module 时返回空。
    inline std::vector<ModuleNode> collectModules(const AstNode& module)
    {
        std::vector<ModuleNode> modules;
        if (module.kind != AstKind::Module)
        {
            return modules;
        }

        ModuleNode node;
        node.name = module.text;
        dependency_detail::append_imports(module, node.imports);
        node.fingerprint = dependency_detail::fingerprint_of(
            dependency_detail::collect_export_names(module));
        modules.push_back(std::move(node));
        return modules;
    }

    // 从一个 Module 节点收集 class/interface 的继承边（`:` 后的父类型，多继承多条边）。
    inline std::vector<TypeNode> collectTypes(const AstNode& module)
    {
        std::vector<TypeNode> types;
        dependency_detail::collect_type_nodes(module, types);
        return types;
    }

    // 检测循环 import：modules 中同名条目会合并到同一节点（出边取并集）。
    inline std::vector<Cycle> findImportCycles(const std::vector<ModuleNode>& modules)
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> edges;
        edges.reserve(modules.size());
        for (const ModuleNode& module : modules)
        {
            edges.emplace_back(module.name, module.imports);
        }
        return dependency_detail::find_cycles(edges);
    }

    // 检测循环继承：自继承、二元环、多继承环都会被报告；菱形继承不是环。
    inline std::vector<Cycle> findInheritanceCycles(const std::vector<TypeNode>& types)
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> edges;
        edges.reserve(types.size());
        for (const TypeNode& type : types)
        {
            edges.emplace_back(type.name, type.bases);
        }
        return dependency_detail::find_cycles(edges);
    }

    // 便于诊断与测试的文本形式："a -> b -> a"；空环返回空串。
    inline std::string describeCycle(const Cycle& cycle)
    {
        std::string text;
        for (std::size_t i = 0; i < cycle.path.size(); ++i)
        {
            if (i != 0)
            {
                text += " -> ";
            }
            text += cycle.path[i];
        }
        return text;
    }

    // 导入导出 + 继承边的增量缓存：按模块名保存最近一次 update 的结果。
    // 查询接口都不抛异常；未登记模块返回空结果（fingerprint 返回空串）。
    class DependencyCache
    {
    public:
        // 重新收集 module 的导出集合/依赖/继承边与 fingerprint，整体替换旧记录。
        // moduleName 是缓存键（可与 Module 根节点的 text 不同，便于调用方显式指定）。
        void update(std::string_view moduleName, const AstNode& module)
        {
            Entry entry;
            dependency_detail::append_imports(module, entry.imports);
            entry.fingerprint = dependency_detail::fingerprint_of(
                dependency_detail::collect_export_names(module));
            dependency_detail::collect_type_nodes(module, entry.types);
            entries_[std::string(moduleName)] = std::move(entry);
        }

        bool has(std::string_view moduleName) const
        {
            return entries_.find(moduleName) != entries_.end();
        }

        // 导出符号集合的指纹；未登记模块返回空串。
        std::string fingerprint(std::string_view moduleName) const
        {
            const auto found = entries_.find(moduleName);
            return found == entries_.end() ? std::string() : found->second.fingerprint;
        }

        // 直接依赖（import 路径，去重且保持源码顺序）；未登记模块返回空。
        std::vector<std::string> dependencies(std::string_view moduleName) const
        {
            const auto found = entries_.find(moduleName);
            return found == entries_.end() ? std::vector<std::string>() : found->second.imports;
        }

        // 继承图直接边；未登记模块返回空。
        std::vector<TypeNode> types(std::string_view moduleName) const
        {
            const auto found = entries_.find(moduleName);
            return found == entries_.end() ? std::vector<TypeNode>() : found->second.types;
        }

        std::size_t size() const noexcept
        {
            return entries_.size();
        }

    private:
        struct Entry
        {
            std::vector<std::string> imports;
            std::vector<TypeNode> types;
            std::string fingerprint;
        };

        // std::map + std::less<>：支持 string_view 直接查找，且遍历顺序稳定。
        std::map<std::string, Entry, std::less<>> entries_;
    };
}
