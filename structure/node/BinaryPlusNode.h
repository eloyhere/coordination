#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>

namespace coordination::node
{
    // 增广二叉节点（binary node "plus"）。
    // 在 BinaryNode 的基础上补充 parent 指针与子树统计量，
    // 支持「按秩查找第 k 小」「子树高度/平衡因子」等需要回溯或自底向上更新的操作。
    //
    // 注意：这里不是 B+ 树节点。如果确实需要 B+ 树的内部节点/叶节点，
    // 建议另建 BPlusInternalNode / BPlusLeafNode，不要在这个类型上叠加。
    template <typename T>
    struct BinaryPlusNode
    {
        using value_type = T;
        using node_type = BinaryPlusNode;

        T value{};
        node_type* left = nullptr;
        node_type* right = nullptr;
        node_type* parent = nullptr;

        std::size_t subtree_size = 1; // 以本节点为根的子树节点数
        int height = 1;               // 以本节点为根的子树高度，叶子为 1

        BinaryPlusNode() = default;

        explicit BinaryPlusNode(T v, node_type* p = nullptr)
            : value(std::move(v))
            , parent(p)
        {
        }

        static int height_of(const node_type* node) noexcept
        {
            return node != nullptr ? node->height : 0;
        }

        static std::size_t size_of(const node_type* node) noexcept
        {
            return node != nullptr ? node->subtree_size : 0;
        }

        // 孩子在旋转/插入/删除后发生变化时调用，自底向上刷新统计量。
        void pull() noexcept
        {
            subtree_size = 1 + size_of(left) + size_of(right);
            height = 1 + std::max(height_of(left), height_of(right));
        }

        int balance_factor() const noexcept
        {
            return height_of(left) - height_of(right);
        }

        bool is_leaf() const noexcept
        {
            return left == nullptr && right == nullptr;
        }
    };
}
