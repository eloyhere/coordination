#pragma once

#include <cstddef>
#include <utility>

namespace coordination::node
{
    // 哈希桶（链地址法）节点。
    //
    // 同一个桶里的节点用 next 串成单链表；value 既可以是键（集合），
    // 也可以是 std::pair<const K, V>（映射）。hash 缓存可避免重哈希时重复计算。
    template <typename T>
    struct HashNode
    {
        using value_type = T;
        using node_type = HashNode;

        T value{};
        node_type* next = nullptr;
        std::size_t hash = 0; // 预计算的哈希值

        HashNode() = default;

        explicit HashNode(T v, node_type* n = nullptr, std::size_t h = 0)
            : value(std::move(v))
            , next(n)
            , hash(h)
        {
        }
    };
}
