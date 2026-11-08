#pragma once

#include <utility>

namespace coordination::node
{
    // 二叉节点：只有左右孩子，不保存父指针，也不带子树统计量。
    // 适合普通二叉搜索树、堆、表达式树等结构。
    template <typename T>
    struct BinaryNode
    {
        using value_type = T;
        using node_type = BinaryNode;

        T value{};
        node_type* left = nullptr;
        node_type* right = nullptr;

        BinaryNode() = default;

        explicit BinaryNode(T v, node_type* l = nullptr, node_type* r = nullptr)
            : value(std::move(v))
            , left(l)
            , right(r)
        {
        }

        bool is_leaf() const noexcept
        {
            return left == nullptr && right == nullptr;
        }
    };
}
