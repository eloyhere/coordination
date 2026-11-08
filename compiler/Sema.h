#pragma once

// ============================================================================
// 语义分析 MVP：声明收集、作用域与名称解析、重复/未定义诊断。
//
// 本文件只消费 Ast.h 里的通用 AST（节点结构约定见 Parser.h 顶部注释），
// 不修改 AST、不抛异常，全部问题写入 SemaResult::diagnostics。
//
// 诊断码（E4xxx 段，供工具链稳定引用）：
//   E4001 同一作用域内声明重名（顶层声明、类型成员、参数、局部变量）
//   E4002 类型名无法解析（字段/方法/参数/局部变量/类型别名/父类型/类型参数约束）
//   E4003 标识符或 this/super 成员无法解析（表达式）
//   E4004 break / continue 出现在循环之外
//   E4005 void 函数（返回类型为 NamedType("void") 或缺失）中出现带值的 return
//   E4006 标注 @ 名字无法解析（未声明为 annotation）
//
// 分析流程（两遍，因此允许前向引用）：
//   1) 收集全部顶层声明（class/interface/concept/annotation/type/distinct、
//      函数与全局变量），同时检测顶层重名；
//   2) 再遍历每个声明的父类型、成员、参数与函数体：
//      显式作用域栈 + 类成员表 + 顶层符号表，按"局部 -> 参数 -> 类成员 -> 顶层"
//      的顺序解析名字。
//
// MVP 限制（刻意不做，留给后续迭代）：
//   * 不做完整类型推导/类型兼容检查，只判断"类型名/标识符是否存在"；
//   * 不解析 import 的跨模块语义：遇到 ImportDecl 直接跳过整棵子树；
//   * 不解析标注实参（既可能是表达式也可能是类型 AST），只检查标注名；
//   * 不构建类继承层次：super 只按"位于类型成员内"判定，不校验父类型成员；
//     非 this 基址的成员访问（obj.field）不做成员存在性检查；
//   * 不处理方法重载：成员重名一律按 E4001 报（含同名不同参数的构造器/方法）；
//   * 不做控制流分析（未使用变量、return 覆盖所有路径等）；
//   * 尚未生成 Module 符号（Parser 不产生 module 声明节点，名字只存在根节点 text 中）。
// ============================================================================

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Span.h"

namespace coordination::compiler
{
    // 符号种类。MVP 只覆盖声明收集所需的最小集合。
    enum class SymbolKind
    {
        Module = 0,
        Class,
        Interface,
        Concept,
        Annotation,
        TypeAlias,
        Function,
        Field,
        Method,
        Parameter,
        Variable,
        Constant,
    };

    // 一条已收集的符号。declaration 指向 AstArena 内的节点，
    // 生命周期由 arena 决定（SemaResult 本身不持有 AST）。
    struct Symbol
    {
        std::string name;
        SymbolKind kind = SymbolKind::Variable;
        AstNode* declaration = nullptr;
        Span span{};
        bool exported = false;
    };

    namespace sema_detail
    {
        // 顶层声明节点：parent 为 Module，或被 ExportDecl 包一层后挂在 Module 下。
        inline bool is_top_level_declaration(const AstNode* declaration) noexcept
        {
            if (declaration == nullptr)
            {
                return false;
            }
            if (declaration->kind == AstKind::Module)
            {
                return true;
            }
            const AstNode* parent = declaration->parent;
            if (parent == nullptr)
            {
                return false;
            }
            if (parent->kind == AstKind::Module)
            {
                return true;
            }
            return parent->kind == AstKind::ExportDecl && parent->parent != nullptr &&
                   parent->parent->kind == AstKind::Module;
        }
    }

    // 一次语义分析的完整结果。
    struct SemaResult
    {
        std::size_t symbolCount = 0;          // 收集到的全部符号数
        std::size_t typeCount = 0;            // 其中的顶层类型声明数
        DiagnosticBag diagnostics;
        std::vector<Symbol> symbols;          // 全部符号（按收集顺序），便于查询与测试

        bool ok() const noexcept
        {
            return !diagnostics.has_errors();
        }

        // 按名字查第一个顶层符号；找不到返回 nullptr。
        // 只表示"该声明存在"，种类由调用方检查 Symbol::kind。
        const Symbol* findTopLevel(std::string_view name) const
        {
            for (const Symbol& symbol : symbols)
            {
                if (std::string_view(symbol.name) == name &&
                    sema_detail::is_top_level_declaration(symbol.declaration))
                {
                    return &symbol;
                }
            }
            return nullptr;
        }
    };

    namespace sema_detail
    {
        // ---------------- AST 分类辅助 ----------------

        inline bool is_type_node(AstKind kind) noexcept
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

        inline bool is_member_node(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::FieldDecl:
            case AstKind::MethodDecl:
            case AstKind::ConstructorDecl:
            case AstKind::DestructorDecl:
            case AstKind::OperatorDecl:
            case AstKind::PropertyDecl:
                return true;
            default:
                return false;
            }
        }

        // 声明名：去掉 "const:" / "mutable:" / "out:" / "in:" 前缀。
        // Parser 用这些前缀承载修饰符（见 Parser.h 的 AST 约定）。
        inline std::string_view declaration_name(std::string_view text) noexcept
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

        // 语言内建类型名（MVP 允许的类型名集合）。
        inline bool is_builtin_type_name(std::string_view name) noexcept
        {
            return name == "integer" || name == "character" || name == "timestamp" ||
                   name == "module" || name == "unknown" || name == "void" || name == "bool";
        }

        inline bool is_type_symbol_kind(SymbolKind kind) noexcept
        {
            switch (kind)
            {
            case SymbolKind::Class:
            case SymbolKind::Interface:
            case SymbolKind::Concept:
            case SymbolKind::Annotation:
            case SymbolKind::TypeAlias:
                return true;
            default:
                return false;
            }
        }

        inline bool is_top_level_declaration_kind(AstKind kind) noexcept
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

        inline SymbolKind top_level_symbol_kind(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::ClassDecl:
                return SymbolKind::Class;
            case AstKind::InterfaceDecl:
                return SymbolKind::Interface;
            case AstKind::ConceptDecl:
                return SymbolKind::Concept;
            case AstKind::AnnotationDecl:
                return SymbolKind::Annotation;
            case AstKind::TypeAliasDecl:
            case AstKind::DistinctTypeDecl:
                return SymbolKind::TypeAlias;
            case AstKind::FunctionDecl:
            case AstKind::OperatorDecl:
                return SymbolKind::Function;
            case AstKind::ConstantDecl:
                return SymbolKind::Constant;
            case AstKind::VariableDecl:
            default:
                return SymbolKind::Variable;
            }
        }

        // ---------------- 分析器 ----------------

        class Analyzer
        {
        public:
            // 分析整棵 Module 树；module 必须在 arena 生命周期内保持有效。
            SemaResult run(AstNode& module)
            {
                scopes_.clear();
                scopes_.emplace_back(); // 模块级占位作用域，保证作用域栈始终非空

                collect_top_level(module);
                for (AstNode* declaration : top_level_declarations_)
                {
                    check_top_level_declaration(declaration);
                }

                scopes_.pop_back();
                result_.symbolCount = result_.symbols.size();
                return std::move(result_);
            }

        private:
            // ---------------- 符号与作用域 ----------------

            Symbol make_symbol(std::string name, SymbolKind kind, AstNode* declaration) const
            {
                Symbol symbol;
                symbol.name = std::move(name);
                symbol.kind = kind;
                symbol.declaration = declaration;
                symbol.span = declaration != nullptr ? declaration->span : Span{};
                symbol.exported = declaration != nullptr && declaration->has_flag(AstFlags::exported);
                return symbol;
            }

            // 在当前作用域声明一个名字；重复声明报 E4001 并保留首次声明。
            void declare_local(std::string name, SymbolKind kind, AstNode* declaration)
            {
                Symbol symbol = make_symbol(std::move(name), kind, declaration);
                auto& scope = scopes_.back();
                const auto existing = scope.find(symbol.name);
                if (existing != scope.end())
                {
                    result_.diagnostics.error("E4001", "声明重复：'" + symbol.name + "'", symbol.span);
                }
                else
                {
                    scope.emplace(symbol.name, symbol);
                }
                result_.symbols.push_back(std::move(symbol));
            }

            // 名字解析顺序：局部（作用域栈自内向外）-> 类成员 -> 顶层。
            const Symbol* find_symbol(std::string_view name) const
            {
                const std::string key(name);
                for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
                {
                    const auto found = scope->find(key);
                    if (found != scope->end())
                    {
                        return &found->second;
                    }
                }
                const auto member = class_members_.find(key);
                if (member != class_members_.end())
                {
                    return &member->second;
                }
                const auto global = top_level_.find(key);
                if (global != top_level_.end())
                {
                    return &global->second;
                }
                return nullptr;
            }

            bool is_known_type_name(std::string_view name) const
            {
                if (is_builtin_type_name(name))
                {
                    return true;
                }
                const std::string key(name);
                if (type_params_.find(key) != type_params_.end())
                {
                    return true;
                }
                const auto found = top_level_.find(key);
                return found != top_level_.end() && is_type_symbol_kind(found->second.kind);
            }

            // ---------------- 第一遍：收集顶层声明 ----------------

            void collect_top_level(AstNode& module)
            {
                for (AstNode* item : module.children)
                {
                    if (item == nullptr)
                    {
                        continue;
                    }
                    if (item->kind == AstKind::ImportDecl)
                    {
                        // MVP 限制：不做 import 的跨模块解析，整棵 import 子树跳过。
                        continue;
                    }
                    if (item->kind == AstKind::ExportDecl)
                    {
                        AstNode* inner = item->child(0);
                        if (inner == nullptr || inner->kind == AstKind::ImportDecl)
                        {
                            continue;
                        }
                        collect_top_level_declaration(inner);
                        continue;
                    }
                    collect_top_level_declaration(item);
                }
            }

            void collect_top_level_declaration(AstNode* declaration)
            {
                if (declaration == nullptr || !is_top_level_declaration_kind(declaration->kind))
                {
                    return;
                }
                top_level_declarations_.push_back(declaration);

                const std::string_view name = declaration_name(declaration->text);
                if (name.empty())
                {
                    // 缺少名字时 Parser 已经报过 E2006，这里不再叠加诊断。
                    return;
                }

                Symbol symbol = make_symbol(std::string(name), top_level_symbol_kind(declaration->kind),
                    declaration);
                if (top_level_.find(symbol.name) != top_level_.end())
                {
                    result_.diagnostics.error("E4001", "顶层声明重复：'" + symbol.name + "'", declaration->span);
                }
                else
                {
                    top_level_.emplace(symbol.name, symbol);
                }

                if (is_type_symbol_kind(symbol.kind))
                {
                    ++result_.typeCount;
                }
                result_.symbols.push_back(std::move(symbol));
            }

            // ---------------- 第二遍：逐个检查声明 ----------------

            void check_top_level_declaration(AstNode* declaration)
            {
                if (declaration == nullptr)
                {
                    return;
                }
                switch (declaration->kind)
                {
                case AstKind::ClassDecl:
                case AstKind::InterfaceDecl:
                case AstKind::ConceptDecl:
                    check_type_declaration(declaration);
                    break;
                case AstKind::AnnotationDecl:
                    check_annotation_declaration(declaration);
                    break;
                case AstKind::TypeAliasDecl:
                case AstKind::DistinctTypeDecl:
                    check_annotation_uses(declaration);
                    for (AstNode* child : declaration->children)
                    {
                        if (child != nullptr && is_type_node(child->kind))
                        {
                            check_type(child);
                        }
                    }
                    break;
                case AstKind::FunctionDecl:
                case AstKind::OperatorDecl:
                    check_callable(declaration);
                    break;
                case AstKind::VariableDecl:
                case AstKind::ConstantDecl:
                    check_annotation_uses(declaration);
                    check_variable_like(declaration);
                    break;
                default:
                    break;
                }
            }

            // ---------------- 类型声明与成员 ----------------

            void check_type_declaration(AstNode* declaration)
            {
                check_annotation_uses(declaration);

                type_params_.clear();
                AstNode* type_param_list = declaration->first_child(AstKind::TypeParamList);
                if (type_param_list != nullptr)
                {
                    for (AstNode* type_param : type_param_list->children)
                    {
                        if (type_param == nullptr || type_param->kind != AstKind::TypeParam)
                        {
                            continue;
                        }
                        check_annotation_uses(type_param);
                        const std::string_view name = declaration_name(type_param->text);
                        if (!name.empty())
                        {
                            type_params_.insert(std::string(name));
                        }
                    }
                    // 约束类型在全部类型参数入表后再解析，允许约束之间互相引用。
                    for (AstNode* type_param : type_param_list->children)
                    {
                        if (type_param == nullptr)
                        {
                            continue;
                        }
                        for (AstNode* constraint : type_param->children)
                        {
                            if (constraint != nullptr && is_type_node(constraint->kind))
                            {
                                check_type(constraint);
                            }
                        }
                    }
                }

                // 孩子顺序：AnnotationUse* -> TypeParamList? -> 父类型* -> 成员*。
                std::vector<AstNode*> parents;
                std::vector<AstNode*> members;
                for (AstNode* child : declaration->children)
                {
                    if (child == nullptr)
                    {
                        continue;
                    }
                    if (is_type_node(child->kind))
                    {
                        parents.push_back(child);
                    }
                    else if (is_member_node(child->kind))
                    {
                        members.push_back(child);
                    }
                }

                for (AstNode* parent : parents)
                {
                    check_type(parent);
                }

                // 先收集全部成员，再检查成员：成员之间允许前向引用（如 this.build()）。
                class_members_.clear();
                in_type_ = true;
                for (AstNode* member : members)
                {
                    collect_member(member);
                }
                for (AstNode* member : members)
                {
                    check_member(member);
                }
                in_type_ = false;
                class_members_.clear();
                type_params_.clear();
            }

            void collect_member(AstNode* member)
            {
                SymbolKind kind = SymbolKind::Field;
                switch (member->kind)
                {
                case AstKind::FieldDecl:
                case AstKind::PropertyDecl:
                    kind = SymbolKind::Field;
                    break;
                case AstKind::MethodDecl:
                case AstKind::OperatorDecl:
                case AstKind::ConstructorDecl:
                case AstKind::DestructorDecl:
                    kind = SymbolKind::Method;
                    break;
                default:
                    return;
                }

                const std::string_view name = declaration_name(member->text);
                if (name.empty())
                {
                    return;
                }

                Symbol symbol = make_symbol(std::string(name), kind, member);
                if (class_members_.find(symbol.name) != class_members_.end())
                {
                    result_.diagnostics.error("E4001", "类型成员重复：'" + symbol.name + "'", member->span);
                }
                else
                {
                    class_members_.emplace(symbol.name, symbol);
                }
                result_.symbols.push_back(std::move(symbol));
            }

            void check_member(AstNode* member)
            {
                switch (member->kind)
                {
                case AstKind::FieldDecl:
                case AstKind::PropertyDecl:
                    check_annotation_uses(member);
                    check_variable_like(member);
                    break;
                case AstKind::MethodDecl:
                case AstKind::OperatorDecl:
                case AstKind::ConstructorDecl:
                    check_callable(member);
                    break;
                case AstKind::DestructorDecl:
                    check_destructor(member);
                    break;
                default:
                    break;
                }
            }

            // 标注声明：`annotation a(...);`，只检查标注名与参数。
            void check_annotation_declaration(AstNode* declaration)
            {
                check_annotation_uses(declaration);
                check_scope_body(declaration->first_child(AstKind::ParamList), nullptr, nullptr);
            }

            // ---------------- 可调用声明（函数/方法/运算符/构造器）----------------

            void check_callable(AstNode* declaration)
            {
                check_annotation_uses(declaration);

                AstNode* params = declaration->first_child(AstKind::ParamList);
                AstNode* body = declaration->first_child(AstKind::BlockStmt);
                AstNode* return_type = nullptr;
                for (AstNode* child : declaration->children)
                {
                    if (child == nullptr || child->kind == AstKind::AnnotationUse ||
                        child->kind == AstKind::ParamList || child->kind == AstKind::BlockStmt)
                    {
                        continue;
                    }
                    if (is_type_node(child->kind) || child->kind == AstKind::Error)
                    {
                        return_type = child; // 返回类型最多一个
                    }
                }

                check_scope_body(params, return_type, body);
            }

            void check_destructor(AstNode* declaration)
            {
                check_annotation_uses(declaration);
                AstNode* body = declaration->first_child(AstKind::BlockStmt);

                const bool saved_returns_void = returns_void_;
                const int saved_loop_depth = loop_depth_;
                returns_void_ = true;
                loop_depth_ = 0;
                if (body != nullptr)
                {
                    check_statement(body);
                }
                returns_void_ = saved_returns_void;
                loop_depth_ = saved_loop_depth;
            }

            // 参数作用域 + 返回类型 + 函数体。构造器/析构器没有返回类型，按 void 处理。
            void check_scope_body(AstNode* params, AstNode* return_type, AstNode* body)
            {
                const bool saved_returns_void = returns_void_;
                const int saved_loop_depth = loop_depth_;
                returns_void_ = is_void_return_type(return_type);
                loop_depth_ = 0;

                scopes_.emplace_back();
                declare_params(params);
                check_params(params);
                if (return_type != nullptr && return_type->kind != AstKind::Error)
                {
                    check_type(return_type);
                }
                if (body != nullptr)
                {
                    check_statement(body);
                }
                scopes_.pop_back();

                returns_void_ = saved_returns_void;
                loop_depth_ = saved_loop_depth;
            }

            static bool is_void_return_type(const AstNode* return_type) noexcept
            {
                if (return_type == nullptr)
                {
                    return true;
                }
                if (return_type->kind == AstKind::Error)
                {
                    // 语法错误已由 Parser 报过，避免再叠加 E4005。
                    return false;
                }
                return return_type->kind == AstKind::NamedType && return_type->text == "void";
            }

            void declare_params(AstNode* params)
            {
                if (params == nullptr)
                {
                    return;
                }
                for (AstNode* param : params->children)
                {
                    if (param == nullptr || param->kind != AstKind::Param)
                    {
                        continue;
                    }
                    check_annotation_uses(param);
                    const std::string_view name = declaration_name(param->text);
                    if (!name.empty())
                    {
                        declare_local(std::string(name), SymbolKind::Parameter, param);
                    }
                }
            }

            void check_params(AstNode* params)
            {
                if (params == nullptr)
                {
                    return;
                }
                // 类型先查；默认值等全部参数入作用域后再查，允许引用其它参数。
                for (AstNode* param : params->children)
                {
                    if (param == nullptr || param->kind != AstKind::Param)
                    {
                        continue;
                    }
                    AstNode* type = nullptr;
                    AstNode* default_value = nullptr;
                    param_parts(*param, type, default_value);
                    if (type != nullptr && type->kind != AstKind::Error)
                    {
                        check_type(type);
                    }
                }
                for (AstNode* param : params->children)
                {
                    if (param == nullptr || param->kind != AstKind::Param)
                    {
                        continue;
                    }
                    AstNode* type = nullptr;
                    AstNode* default_value = nullptr;
                    param_parts(*param, type, default_value);
                    if (default_value != nullptr)
                    {
                        check_expression(default_value);
                    }
                }
            }

            // ---------------- 变量式声明（字段/全局变量/局部变量）----------------

            // 孩子顺序：AnnotationUse* -> Type? -> 初始化表达式?。
            // 字段的 Type 恒存在，局部变量/全局变量允许省略（由初始化器推断）。
            void check_variable_like(AstNode* declaration)
            {
                const std::size_t start = first_non_annotation_index(*declaration);
                std::size_t index = start;
                if (index < declaration->children.size())
                {
                    AstNode* child = declaration->children[index];
                    if (child != nullptr && (is_type_node(child->kind) || child->kind == AstKind::Error))
                    {
                        check_type(child);
                        ++index;
                    }
                }
                if (index < declaration->children.size())
                {
                    check_expression(declaration->children[index]);
                }
            }

            void check_local_declaration(AstNode* statement)
            {
                check_annotation_uses(statement);
                const std::string_view raw_text = statement->text;
                const std::string_view name = declaration_name(raw_text);
                if (!name.empty())
                {
                    const bool is_constant = raw_text.rfind("const:", 0) == 0;
                    declare_local(std::string(name), is_constant ? SymbolKind::Constant : SymbolKind::Variable,
                        statement);
                }
                check_variable_like(statement);
            }

            // ---------------- 语句 ----------------

            void check_statement(AstNode* statement)
            {
                if (statement == nullptr)
                {
                    return;
                }
                switch (statement->kind)
                {
                case AstKind::BlockStmt:
                    scopes_.emplace_back();
                    for (AstNode* child : statement->children)
                    {
                        check_statement(child);
                    }
                    scopes_.pop_back();
                    break;
                case AstKind::ExprStmt:
                    check_expression(statement->child(0));
                    break;
                case AstKind::LocalDeclStmt:
                    check_local_declaration(statement);
                    break;
                case AstKind::IfStmt:
                    check_expression(statement->child(0));
                    check_scoped_statement(statement->child(1));
                    check_scoped_statement(statement->child(2));
                    break;
                case AstKind::WhileStmt:
                    check_expression(statement->child(0));
                    ++loop_depth_;
                    check_scoped_statement(statement->child(1));
                    --loop_depth_;
                    break;
                case AstKind::ForStmt:
                    check_for_statement(statement);
                    break;
                case AstKind::ReturnStmt:
                    check_return_statement(statement);
                    break;
                case AstKind::BreakStmt:
                    if (loop_depth_ == 0)
                    {
                        result_.diagnostics.error("E4004", "'break' 只能出现在循环体内", statement->span);
                    }
                    break;
                case AstKind::ContinueStmt:
                    if (loop_depth_ == 0)
                    {
                        result_.diagnostics.error("E4004", "'continue' 只能出现在循环体内", statement->span);
                    }
                    break;
                case AstKind::Error:
                    break;
                default:
                    // 兜底：Parser 未来新增的语句形态先按表达式遍历一次子节点。
                    check_expression(statement);
                    break;
                }
            }

            // 无花括号的分支/循环体也构成隐式作用域，避免不同分支的局部名互相冲突。
            void check_scoped_statement(AstNode* statement)
            {
                if (statement == nullptr)
                {
                    return;
                }
                if (statement->kind == AstKind::BlockStmt)
                {
                    check_statement(statement);
                    return;
                }
                scopes_.emplace_back();
                check_statement(statement);
                scopes_.pop_back();
            }

            void check_for_statement(AstNode* statement)
            {
                const std::string_view mask = statement->text;
                const bool has_initializer = mask.size() > 0 && mask[0] == '1';
                const bool has_condition = mask.size() > 1 && mask[1] == '1';
                const bool has_step = mask.size() > 2 && mask[2] == '1';
                std::size_t index = 0;

                scopes_.emplace_back();
                if (has_initializer && index < statement->child_count())
                {
                    check_statement(statement->children[index]);
                    ++index;
                }
                if (has_condition && index < statement->child_count())
                {
                    check_expression(statement->children[index]);
                    ++index;
                }

                ++loop_depth_;
                if (has_step && index < statement->child_count())
                {
                    check_expression(statement->children[index]);
                    ++index;
                }
                if (index < statement->child_count())
                {
                    check_scoped_statement(statement->children[index]);
                }
                --loop_depth_;
                scopes_.pop_back();
            }

            void check_return_statement(AstNode* statement)
            {
                AstNode* value = statement->child(0);
                if (returns_void_ && value != nullptr && value->kind != AstKind::Error)
                {
                    result_.diagnostics.error("E4005", "void 函数不能返回带值的 return", statement->span);
                }
                check_expression(value);
            }

            // ---------------- 表达式 ----------------

            void check_expression(AstNode* node)
            {
                if (node == nullptr || node->kind == AstKind::Error)
                {
                    return;
                }
                switch (node->kind)
                {
                case AstKind::IdentifierExpr:
                    check_identifier_reference(*node);
                    break;
                case AstKind::ThisExpr:
                    check_this_reference(*node, "this");
                    break;
                case AstKind::SuperExpr:
                    check_this_reference(*node, "super");
                    break;
                case AstKind::LiteralExpr:
                    break;
                case AstKind::UnaryExpr:
                case AstKind::ParenExpr:
                    check_expression(node->child(0));
                    break;
                case AstKind::BinaryExpr:
                case AstKind::AssignExpr:
                case AstKind::IndexExpr:
                    check_expression(node->child(0));
                    check_expression(node->child(1));
                    break;
                case AstKind::ConditionalExpr:
                    check_expression(node->child(0));
                    check_expression(node->child(1));
                    check_expression(node->child(2));
                    break;
                case AstKind::CallExpr:
                    check_expression(node->child(0));
                    check_argument_list(node->child(1));
                    break;
                case AstKind::MemberExpr:
                    check_member_expression(node);
                    break;
                case AstKind::NewExpr:
                    check_new_expression(node);
                    break;
                case AstKind::ArgumentList:
                    check_argument_list(node);
                    break;
                case AstKind::CastExpr:
                    // CastExpr 目前 Parser 不生成，保留处理以便后续扩展。
                    for (AstNode* child : node->children)
                    {
                        if (child == nullptr)
                        {
                            continue;
                        }
                        if (is_type_node(child->kind))
                        {
                            check_type(child);
                        }
                        else
                        {
                            check_expression(child);
                        }
                    }
                    break;
                default:
                    if (is_type_node(node->kind))
                    {
                        // 类型节点出现在表达式位置（例如标注实参）时按类型处理。
                        check_type(node);
                    }
                    break;
                }
            }

            void check_argument_list(AstNode* node)
            {
                if (node == nullptr)
                {
                    return;
                }
                if (node->kind != AstKind::ArgumentList)
                {
                    check_expression(node);
                    return;
                }
                for (AstNode* argument : node->children)
                {
                    check_expression(argument);
                }
            }

            void check_member_expression(AstNode* node)
            {
                AstNode* base = node->child(0);
                check_expression(base);

                // `this.member` 可以在类成员表中解析；其它基表达式需要类型信息，MVP 跳过。
                if (in_type_ && base != nullptr && base->kind == AstKind::ThisExpr && !node->text.empty())
                {
                    if (class_members_.find(node->text) == class_members_.end())
                    {
                        result_.diagnostics.error("E4003", "无法解析成员 '" + node->text + "'", node->span);
                    }
                }
            }

            void check_new_expression(AstNode* node)
            {
                for (AstNode* child : node->children)
                {
                    if (child == nullptr)
                    {
                        continue;
                    }
                    if (is_type_node(child->kind))
                    {
                        check_type(child);
                    }
                    else
                    {
                        check_argument_list(child);
                    }
                }
            }

            void check_identifier_reference(const AstNode& node)
            {
                if (node.text.empty())
                {
                    return;
                }
                if (find_symbol(node.text) != nullptr)
                {
                    return;
                }
                result_.diagnostics.error("E4003", "无法解析标识符 '" + node.text + "'", node.span);
            }

            void check_this_reference(const AstNode& node, const char* keyword)
            {
                if (in_type_)
                {
                    return;
                }
                result_.diagnostics.error("E4003",
                    std::string("'") + keyword + "' 只能出现在类型成员中", node.span);
            }

            // ---------------- 类型 ----------------

            void check_type(AstNode* node)
            {
                if (node == nullptr)
                {
                    return;
                }
                switch (node->kind)
                {
                case AstKind::NamedType:
                    check_type_name(*node);
                    break;
                case AstKind::GenericType:
                    check_type_name(*node);
                    check_type_arg_list(node->first_child(AstKind::TypeArgList));
                    break;
                case AstKind::PointerType:
                case AstKind::ReferenceType:
                case AstKind::MutableReferenceType:
                case AstKind::DynType:
                    check_type(node->child(0));
                    break;
                case AstKind::ArrayType:
                {
                    // 孩子：元素类型 + 可选的数组长度表达式。
                    bool first = true;
                    for (AstNode* child : node->children)
                    {
                        if (child == nullptr)
                        {
                            continue;
                        }
                        if (first)
                        {
                            check_type(child);
                            first = false;
                        }
                        else
                        {
                            check_expression(child);
                        }
                    }
                    break;
                }
                case AstKind::FunctionType:
                    // 孩子：TypeArgList（参数类型）+ 返回类型。
                    for (AstNode* child : node->children)
                    {
                        if (child == nullptr)
                        {
                            continue;
                        }
                        if (child->kind == AstKind::TypeArgList)
                        {
                            check_type_arg_list(child);
                        }
                        else
                        {
                            check_type(child);
                        }
                    }
                    break;
                case AstKind::Error:
                    break;
                default:
                    break;
                }
            }

            void check_type_arg_list(AstNode* list)
            {
                if (list == nullptr)
                {
                    return;
                }
                for (AstNode* argument : list->children)
                {
                    check_type(argument);
                }
            }

            void check_type_name(const AstNode& type_node)
            {
                if (type_node.text.empty())
                {
                    return;
                }
                if (is_known_type_name(type_node.text))
                {
                    return;
                }
                result_.diagnostics.error("E4002", "无法解析类型名 '" + type_node.text + "'", type_node.span);
            }

            // ---------------- 标注 ----------------

            void check_annotation_uses(const AstNode* node)
            {
                if (node == nullptr)
                {
                    return;
                }
                for (const AstNode* child : node->children)
                {
                    if (child == nullptr || child->kind != AstKind::AnnotationUse)
                    {
                        continue;
                    }
                    check_annotation_use(*child);
                }
            }

            void check_annotation_use(const AstNode& annotation)
            {
                // MVP 限制：不解析标注实参（其中可能混有类型 AST 与表达式），只检查标注名。
                if (annotation.text.empty())
                {
                    return;
                }
                const auto found = top_level_.find(annotation.text);
                if (found == top_level_.end() || found->second.kind != SymbolKind::Annotation)
                {
                    result_.diagnostics.error("E4006", "无法解析标注 '@" + annotation.text + "'", annotation.span);
                }
            }

            // ---------------- 通用小工具 ----------------

            static std::size_t first_non_annotation_index(const AstNode& node) noexcept
            {
                std::size_t index = 0;
                while (index < node.children.size() && node.children[index] != nullptr &&
                       node.children[index]->kind == AstKind::AnnotationUse)
                {
                    ++index;
                }
                return index;
            }

            static void param_parts(const AstNode& param, AstNode*& type, AstNode*& default_value) noexcept
            {
                type = nullptr;
                default_value = nullptr;
                const std::size_t start = first_non_annotation_index(param);
                if (start < param.children.size())
                {
                    type = param.children[start];
                }
                if (start + 1 < param.children.size())
                {
                    default_value = param.children[start + 1];
                }
            }

            // ---------------- 状态 ----------------

            SemaResult result_;
            std::vector<std::unordered_map<std::string, Symbol>> scopes_;
            std::unordered_map<std::string, Symbol> top_level_;
            std::unordered_map<std::string, Symbol> class_members_;
            std::unordered_set<std::string> type_params_;
            std::vector<AstNode*> top_level_declarations_;
            bool in_type_ = false;
            int loop_depth_ = 0;
            bool returns_void_ = false;
        };
    }

    // 分析一棵 Module 子树，返回符号、类型计数与诊断。
    // module 必须在 AstArena 存活期间传入；结果中的 declaration 指针同样依赖 arena。
    inline SemaResult analyze(AstNode& module)
    {
        sema_detail::Analyzer analyzer;
        return analyzer.run(module);
    }
}
