#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace coordination::node
{
    // 定长层数的跳表节点。
    //
    // 每层的后继指针直接放在 std::array 里，节点本身一次分配搞定，
    // 适合 max_levels 在编译期已知的跳表（LinkedSkipList）。
    template <typename T, std::size_t MaxLevels = 32>
    struct SkipNode
    {
        static_assert(MaxLevels > 0, "跳表节点至少需要一层");

        using value_type = T;
        using node_type = SkipNode;

        static constexpr std::size_t max_levels = MaxLevels;

        T value{};
        std::size_t level = 1;
        std::array<node_type*, MaxLevels> forward{};

        SkipNode() = default;

        explicit SkipNode(T v, std::size_t lvl = 1)
            : value(std::move(v))
        {
            set_level(lvl);
        }

        void set_level(std::size_t lvl) noexcept
        {
            level = std::clamp<std::size_t>(lvl, 1, MaxLevels);
        }

        node_type*& operator[](std::size_t i) noexcept
        {
            return forward[i];
        }

        node_type* operator[](std::size_t i) const noexcept
        {
            return forward[i];
        }
    };
}
