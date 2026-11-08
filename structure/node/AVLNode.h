#pragma once

#include <algorithm>
#include <utility>

namespace coordination::node
{
    // AVL 树节点：带父指针与子树高度，平衡因子 = 左高 - 右高。
    // 高度约定为空树 0、叶子 1，便于旋转后直接 pull() 更新。
    template <typename T>
    struct AVLNode
    {
        using value_type = T;
        using node_type = AVLNode;

        T value{};
        node_type* left = nullptr;
        node_type* right = nullptr;
        node_type* parent = nullptr;
        int height = 1;

        AVLNode() = default;

        explicit AVLNode(T v, node_type* p = nullptr)
            : value(std::move(v))
            , parent(p)
        {
        }

        static int height_of(const node_type* node) noexcept
        {
            return node != nullptr ? node->height : 0;
        }

        void pull() noexcept
        {
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
