#pragma once

#include <cstddef>
#include <utility>

namespace coordination::node
{
    // 并查集节点。
    //
    // 并查集通常用数组实现，节点里存的 parent 是「父节点在数组中的下标」，
    // 因此本节点不持有子节点集合，is_root() 为真时它就是某棵树的根。
    template <typename T>
    struct DisjointSetNode
    {
        using value_type = T;
        using node_type = DisjointSetNode;
        using index_type = std::size_t;

        static constexpr index_type npos = static_cast<index_type>(-1);

        T value{};
        index_type parent = npos; // 父节点下标，npos 表示自身是根
        index_type rank = 0;      // 按秩合并用的秩
        index_type size = 1;      // 集合大小，仅根节点有效

        DisjointSetNode() = default;

        explicit DisjointSetNode(T v)
            : value(std::move(v))
        {
        }

        bool is_root() const noexcept
        {
            return parent == npos;
        }
    };
}
