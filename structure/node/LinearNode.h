#pragma once

#include <utility>

namespace coordination::node
{
    // 单链表节点。
    // 只保存后继指针，不拥有 next 指向的节点；节点的生命周期由容器负责。
    template <typename T>
    struct LinearNode
    {
        using value_type = T;
        using node_type = LinearNode;

        T value{};
        node_type* next = nullptr;

        LinearNode() = default;

        explicit LinearNode(T v, node_type* n = nullptr)
            : value(std::move(v))
            , next(n)
        {
        }

        bool is_last() const noexcept
        {
            return next == nullptr;
        }
    };
}
