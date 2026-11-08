#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "SyntaxKind.h"
#include "SyntaxSpan.h"

namespace coordination::syntax
{
    // 语法树节点基类。
    //
    // 设计要点：
    //  - 父节点拥有子节点（unique_ptr），子节点只持有父节点的裸指针；
    //  - 节点头本身不带语义，具体内容由派生类补充，遍历代码只依赖 kind / children；
    //  - text() 返回节点自身的短文本（标识符、字面量、运算符……），
    //    因此叶子节点用 text() 就能还原源码片段。
    //
    // 用法：派生类在构造函数里调用 SyntaxNode(kind, span)，再用 make_child / add_child
    // 挂子节点；需要带类型的子节点访问时用 child_as<T>()。
    class SyntaxNode
    {
    public:
        virtual ~SyntaxNode() = default;

        SyntaxNode(const SyntaxNode&) = delete;
        SyntaxNode& operator=(const SyntaxNode&) = delete;

        SyntaxKind kind() const noexcept
        {
            return kind_;
        }

        const SyntaxSpan& span() const noexcept
        {
            return span_;
        }

        void set_span(SyntaxSpan span) noexcept
        {
            span_ = span;
        }

        SyntaxNode* parent() const noexcept
        {
            return parent_;
        }

        std::size_t child_count() const noexcept
        {
            return children_.size();
        }

        SyntaxNode& child(std::size_t i) noexcept
        {
            return *children_[i];
        }

        const SyntaxNode& child(std::size_t i) const noexcept
        {
            return *children_[i];
        }

        const std::vector<std::unique_ptr<SyntaxNode>>& children() const noexcept
        {
            return children_;
        }

        // 挂上已经构造好的子树，接转所有权。
        SyntaxNode& add_child(std::unique_ptr<SyntaxNode> node)
        {
            node->parent_ = this;
            children_.push_back(std::move(node));
            return *children_.back();
        }

        // 直接在本节点下构造子节点，返回派生类型引用。
        template <typename NodeT, typename... Args>
        NodeT& make_child(Args&&... args)
        {
            static_assert(std::is_base_of_v<SyntaxNode, NodeT>, "NodeT 必须派生自 SyntaxNode");
            auto node = std::make_unique<NodeT>(std::forward<Args>(args)...);
            node->parent_ = this;
            NodeT* raw = node.get();
            children_.push_back(std::move(node));
            return *raw;
        }

        template <typename NodeT>
        NodeT* child_as(std::size_t i) noexcept
        {
            return dynamic_cast<NodeT*>(children_[i].get());
        }

        template <typename NodeT>
        const NodeT* child_as(std::size_t i) const noexcept
        {
            return dynamic_cast<const NodeT*>(children_[i].get());
        }

        // 节点自身的短文本；token 返回原文，标识符返回名字，运算符节点返回符号。
        virtual std::string_view text() const noexcept
        {
            return {};
        }

    protected:
        explicit SyntaxNode(SyntaxKind kind, SyntaxSpan span = {}) noexcept
            : kind_(kind)
            , span_(span)
        {
        }

    private:
        SyntaxKind kind_;
        SyntaxSpan span_;
        SyntaxNode* parent_ = nullptr;
        std::vector<std::unique_ptr<SyntaxNode>> children_;
    };
}
