#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Span.h"

namespace coordination::compiler
{
    // 具体语法树的节点类别。统一使用一个通用节点结构（green tree 风格），
    // 不为每种语法结构定义强类型子类；类型安全由 AstNode 的访问器辅助函数提供。
    // 枚举值一旦发布即保持稳定（工具、序列化与测试依赖它），新增只能追加。
    enum class AstKind : std::uint16_t
    {
        // 顶层
        Module, ImportDecl, ExportDecl,
        // 类型声明
        ClassDecl, InterfaceDecl, ConceptDecl, AnnotationDecl, TypeAliasDecl, DistinctTypeDecl,
        // 成员与声明
        FieldDecl, MethodDecl, ConstructorDecl, DestructorDecl, OperatorDecl, PropertyDecl,
        FunctionDecl, VariableDecl, ConstantDecl,
        // 参数与类型参数
        ParamList, Param, TypeParamList, TypeParam, ArgumentList, TypeArgList,
        // 标注
        AnnotationUse, AnnotationArgList,
        // 类型
        NamedType, GenericType, PointerType, ReferenceType, MutableReferenceType, ArrayType, DynType, FunctionType,
        // 语句
        BlockStmt, ExprStmt, IfStmt, WhileStmt, ForStmt, ReturnStmt, BreakStmt, ContinueStmt, LocalDeclStmt,
        // 表达式
        LiteralExpr, IdentifierExpr, UnaryExpr, BinaryExpr, AssignExpr, CallExpr, MemberExpr,
        IndexExpr, ConditionalExpr, ThisExpr, SuperExpr, NewExpr, CastExpr, ParenExpr,
        // 其它
        Error,
    };

    // 节点标志位，可按位组合。具体语义由 kind 与 parser/sema 共同约定。
    enum class AstFlags : std::uint32_t
    {
        none = 0,
        exported = 1u << 0,
        override_ = 1u << 1,
        hidden = 1u << 2,
        open = 1u << 3,
        final_ = 1u << 4,
        distinct = 1u << 5,
        comptime = 1u << 6,
        repeatable = 1u << 7,
    };

    constexpr AstFlags operator|(AstFlags lhs, AstFlags rhs) noexcept
    {
        return static_cast<AstFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    constexpr AstFlags operator&(AstFlags lhs, AstFlags rhs) noexcept
    {
        return static_cast<AstFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }

    // 判断 value 是否包含 flag 的全部位；flag 为 none 时返回 false。
    constexpr bool has_flag(AstFlags value, AstFlags flag) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0u;
    }

    // 枚举名的稳定文本形式（用于 dump、日志与测试）。完整覆盖所有 AstKind。
    constexpr std::string_view ast_kind_name(AstKind kind) noexcept
    {
        switch (kind)
        {
        case AstKind::Module:
            return "Module";
        case AstKind::ImportDecl:
            return "ImportDecl";
        case AstKind::ExportDecl:
            return "ExportDecl";
        case AstKind::ClassDecl:
            return "ClassDecl";
        case AstKind::InterfaceDecl:
            return "InterfaceDecl";
        case AstKind::ConceptDecl:
            return "ConceptDecl";
        case AstKind::AnnotationDecl:
            return "AnnotationDecl";
        case AstKind::TypeAliasDecl:
            return "TypeAliasDecl";
        case AstKind::DistinctTypeDecl:
            return "DistinctTypeDecl";
        case AstKind::FieldDecl:
            return "FieldDecl";
        case AstKind::MethodDecl:
            return "MethodDecl";
        case AstKind::ConstructorDecl:
            return "ConstructorDecl";
        case AstKind::DestructorDecl:
            return "DestructorDecl";
        case AstKind::OperatorDecl:
            return "OperatorDecl";
        case AstKind::PropertyDecl:
            return "PropertyDecl";
        case AstKind::FunctionDecl:
            return "FunctionDecl";
        case AstKind::VariableDecl:
            return "VariableDecl";
        case AstKind::ConstantDecl:
            return "ConstantDecl";
        case AstKind::ParamList:
            return "ParamList";
        case AstKind::Param:
            return "Param";
        case AstKind::TypeParamList:
            return "TypeParamList";
        case AstKind::TypeParam:
            return "TypeParam";
        case AstKind::ArgumentList:
            return "ArgumentList";
        case AstKind::TypeArgList:
            return "TypeArgList";
        case AstKind::AnnotationUse:
            return "AnnotationUse";
        case AstKind::AnnotationArgList:
            return "AnnotationArgList";
        case AstKind::NamedType:
            return "NamedType";
        case AstKind::GenericType:
            return "GenericType";
        case AstKind::PointerType:
            return "PointerType";
        case AstKind::ReferenceType:
            return "ReferenceType";
        case AstKind::MutableReferenceType:
            return "MutableReferenceType";
        case AstKind::ArrayType:
            return "ArrayType";
        case AstKind::DynType:
            return "DynType";
        case AstKind::FunctionType:
            return "FunctionType";
        case AstKind::BlockStmt:
            return "BlockStmt";
        case AstKind::ExprStmt:
            return "ExprStmt";
        case AstKind::IfStmt:
            return "IfStmt";
        case AstKind::WhileStmt:
            return "WhileStmt";
        case AstKind::ForStmt:
            return "ForStmt";
        case AstKind::ReturnStmt:
            return "ReturnStmt";
        case AstKind::BreakStmt:
            return "BreakStmt";
        case AstKind::ContinueStmt:
            return "ContinueStmt";
        case AstKind::LocalDeclStmt:
            return "LocalDeclStmt";
        case AstKind::LiteralExpr:
            return "LiteralExpr";
        case AstKind::IdentifierExpr:
            return "IdentifierExpr";
        case AstKind::UnaryExpr:
            return "UnaryExpr";
        case AstKind::BinaryExpr:
            return "BinaryExpr";
        case AstKind::AssignExpr:
            return "AssignExpr";
        case AstKind::CallExpr:
            return "CallExpr";
        case AstKind::MemberExpr:
            return "MemberExpr";
        case AstKind::IndexExpr:
            return "IndexExpr";
        case AstKind::ConditionalExpr:
            return "ConditionalExpr";
        case AstKind::ThisExpr:
            return "ThisExpr";
        case AstKind::SuperExpr:
            return "SuperExpr";
        case AstKind::NewExpr:
            return "NewExpr";
        case AstKind::CastExpr:
            return "CastExpr";
        case AstKind::ParenExpr:
            return "ParenExpr";
        case AstKind::Error:
            return "Error";
        default:
            return "Unknown";
        }
    }

    // 通用 AST 节点：只有 kind / span / 原文 / 标志位 / 子节点。
    // 原文仅用于标识符名、字面量原文与运算符原文，其它节点为空。
    struct AstNode
    {
        AstKind kind = AstKind::Error;
        Span span{};
        std::string text;
        AstFlags flags = AstFlags::none;
        AstNode* parent = nullptr;
        std::vector<AstNode*> children;

        // 按下标取直接子节点；越界返回 nullptr。
        AstNode* child(std::size_t index) const noexcept
        {
            return index < children.size() ? children[index] : nullptr;
        }

        // 返回第一个指定 kind 的直接子节点；没有时返回 nullptr。
        AstNode* first_child(AstKind wanted) const noexcept
        {
            for (AstNode* node : children)
            {
                if (node != nullptr && node->kind == wanted)
                {
                    return node;
                }
            }
            return nullptr;
        }

        std::size_t child_count() const noexcept
        {
            return children.size();
        }

        bool has_flag(AstFlags flag) const noexcept
        {
            return coordination::compiler::has_flag(flags, flag);
        }
    };

    // AST 节点 arena：拥有全部节点，节点地址在 arena 生命周期内保持稳定。
    // 禁止拷贝，避免 parent/children 指针指向被拷贝的节点。
    class AstArena
    {
    public:
        AstArena() = default;
        AstArena(const AstArena&) = delete;
        AstArena& operator=(const AstArena&) = delete;
        ~AstArena();

        // 创建一个游离节点（parent 为空），由 arena 持有。
        AstNode* make(AstKind kind, Span span, std::string text = {}, AstFlags flags = AstFlags::none)
        {
            nodes_.push_back(std::make_unique<AstNode>());
            AstNode* node = nodes_.back().get();
            node->kind = kind;
            node->span = span;
            node->text = std::move(text);
            node->flags = flags;
            return node;
        }

        // 创建节点并立即挂到 parent 末尾；parent 不能为空。
        AstNode* make_child(AstNode* parent, AstKind kind, Span span, std::string text = {},
            AstFlags flags = AstFlags::none)
        {
            if (parent == nullptr)
            {
                throw std::invalid_argument("AstArena::make_child 需要非空父节点");
            }
            return adopt(parent, make(kind, span, std::move(text), flags));
        }

        // 把 child 挂到 parent 末尾并设置 parent，返回 child。
        // 节点已属于另一个父节点（重复 adopt）时抛 std::logic_error。
        AstNode* adopt(AstNode* parent, AstNode* child)
        {
            if (parent == nullptr || child == nullptr)
            {
                throw std::invalid_argument("AstArena::adopt 需要非空父节点与子节点");
            }
            if (child->parent != nullptr)
            {
                throw std::logic_error("AstNode 已有父节点，不能重复 adopt");
            }
            child->parent = parent;
            parent->children.push_back(child);
            return child;
        }

        std::size_t node_count() const noexcept
        {
            return nodes_.size();
        }

    private:
        // unique_ptr 保证 vector 扩容时节点本身不移动，因此裸指针长期有效。
        std::vector<std::unique_ptr<AstNode>> nodes_;
    };

    inline AstArena::~AstArena() = default;

    namespace detail
    {
        inline void append_dump(const AstNode* node, std::string& out)
        {
            if (node == nullptr)
            {
                out += "(Error)";
                return;
            }

            out += '(';
            out += ast_kind_name(node->kind);
            if (!node->text.empty())
            {
                out += ' ';
                out += node->text;
            }

            for (const AstNode* child : node->children)
            {
                out += ' ';
                append_dump(child, out);
            }

            out += ')';
        }
    }

    // 把子树序列化为 S 表达式，例如 (Module (ClassDecl Point (FieldDecl x)))。
    // 空 text 被跳过；nullptr 子节点输出 (Error)。文本不做转义，仅用于调试。
    inline std::string dump(const AstNode& root)
    {
        std::string out;
        detail::append_dump(&root, out);
        return out;
    }
}
