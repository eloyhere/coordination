#pragma once

#include <utility>

namespace coordination::node
{
    // 双向链表节点。
    // 同时保存前驱与后继指针，适合 LRU 这类需要 O(1) 摘除 + 头插的容器。
    // 不拥有相邻节点，链表首尾的哨兵由容器维护。
    template <typename T>
    struct DoubleLinearNode
    {
        using value_type = T;
        using node_type = DoubleLinearNode;

        T value{};
        node_type* prev = nullptr;
        node_type* next = nullptr;

        DoubleLinearNode() = default;

        explicit DoubleLinearNode(T v, node_type* p = nullptr, node_type* n = nullptr)
            : value(std::move(v))
            , prev(p)
            , next(n)
        {
        }

        // 把本节点从链表中摘下（不释放内存），返回原来的后继节点。
        node_type* unlink() noexcept
        {
            node_type* n = next;
            if (prev != nullptr)
            {
                prev->next = n;
            }
            if (n != nullptr)
            {
                n->prev = prev;
            }
            prev = nullptr;
            next = nullptr;
            return n;
        }

        // 把 node 接到本节点之后。
        void link_after(node_type* node) noexcept
        {
            node->prev = this;
            node->next = next;
            if (next != nullptr)
            {
                next->prev = node;
            }
            next = node;
        }

        // 把 node 接到本节点之前。
        void link_before(node_type* node) noexcept
        {
            node->next = this;
            node->prev = prev;
            if (prev != nullptr)
            {
                prev->next = node;
            }
            prev = node;
        }
    };
}
