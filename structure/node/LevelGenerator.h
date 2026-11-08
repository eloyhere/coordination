#pragma once

#include <cmath>
#include <cstddef>
#include <random>

namespace coordination::node
{
    // 跳表节点的层数生成器。
    //
    // 以 promotion 的概率继续升层，返回 [1, max_level] 之间的层数，
    // promotion = 0.5 时每层期望元素数减半，等价于 p = 1/2 的经典跳表。

    inline std::size_t random_level(std::mt19937& rng, double promotion = 0.5, std::size_t max_level = 32)
    {
        if (max_level <= 1)
        {
            return 1;
        }

        std::uniform_real_distribution<double> dist(0.0, 1.0);
        std::size_t level = 1;
        while (level < max_level && dist(rng) < promotion)
        {
            ++level;
        }
        return level;
    }

    // 理想跳表在 n 个元素时的平均层数：1 + log_{1/p}(n)。
    // 仅用于估算内存开销，实际层数仍由 random_level 决定。
    inline double expected_level(std::size_t n, double promotion = 0.5)
    {
        if (n < 2 || promotion <= 0.0 || promotion >= 1.0)
        {
            return 1.0;
        }
        return 1.0 + std::log(static_cast<double>(n)) / std::log(1.0 / promotion);
    }
}
