#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace coordination::node
{
    // 数组式跳表节点。
    //
    // 不存指针而存「下一个节点在节点池中的下标」，因此节点可以整体放在
    // std::vector / 预分配数组里，便于序列化和紧凑存储（ArraySkipList）。
    // npos 表示该层没有后继。
    template <typename T, std::size_t MaxLevels = 32>
    struct ArraySkipNode
    {
        static_assert(MaxLevels > 0, "跳表节点至少需要一层");

        using value_type = T;
        using node_type = ArraySkipNode;
        using index_type = std::int32_t;

        static constexpr index_type npos = -1;
        static constexpr std::size_t max_levels = MaxLevels;

        T value{};
        std::size_t level = 1;
        std::array<index_type, MaxLevels> forward{};

        ArraySkipNode()
        {
            forward.fill(npos);
        }

        explicit ArraySkipNode(T v, std::size_t lvl = 1)
            : value(std::move(v))
        {
            forward.fill(npos);
            set_level(lvl);
        }

        void set_level(std::size_t lvl) noexcept
        {
            level = std::clamp<std::size_t>(lvl, 1, MaxLevels);
        }

        bool has_next(std::size_t i) const noexcept
        {
            return forward[i] != npos;
        }

        void link(std::size_t i, index_type next) noexcept
        {
            forward[i] = next;
        }
    };
}
