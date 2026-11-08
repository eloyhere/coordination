#pragma once

#include <utility>

namespace coordination::node
{
    enum class NodeColor : unsigned char
    {
        red,
        black,
    };

    // 红黑树节点：带父指针与颜色标记。
    // 不保存高度，平衡性由颜色约束保证，旋转后只需重新着色。
    template <typename T>
    struct RBNode
    {
        using value_type = T;
        using node_type = RBNode;

        T value{};
        node_type* left = nullptr;
        node_type* right = nullptr;
        node_type* parent = nullptr;
        NodeColor color = NodeColor::red;

        RBNode() = default;

        explicit RBNode(T v, NodeColor c = NodeColor::red, node_type* p = nullptr)
            : value(std::move(v))
            , parent(p)
            , color(c)
        {
        }

        bool is_red() const noexcept
        {
            return color == NodeColor::red;
        }

        bool is_black() const noexcept
        {
            return color == NodeColor::black;
        }

        bool is_leaf() const noexcept
        {
            return left == nullptr && right == nullptr;
        }
    };
}
