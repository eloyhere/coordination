#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace coordination::node
{
    // 动态层数的跳表节点。
    //
    // forward.size() 就是节点当前的层数，第 i 层指向该层的下一个节点。
    // 层数一般由 LevelGenerator.h 中的 random_level 按几何分布生成，
    // 因此也叫「平均层数节点」：期望层数约为 log_{1/p}(n)。
    //
    // 每个节点单独堆分配一个 forward 数组，层数不确定时用它；
    // 层数上限固定、希望省掉这次堆分配时用 SkipNode。
    template <typename T>
    struct AverageLevelNode
    {
        using value_type = T;
        using node_type = AverageLevelNode;

        T value{};
        std::vector<node_type*> forward; // 第 i 层指向的下一个节点

        AverageLevelNode() = default;

        explicit AverageLevelNode(T v, std::size_t level = 1)
            : value(std::move(v))
            , forward(level, nullptr)
        {
        }

        std::size_t level() const noexcept
        {
            return forward.size();
        }

        bool has_level(std::size_t i) const noexcept
        {
            return i < forward.size();
        }

        // 扩容到至少 level 层，新增的层初始为空。
        void ensure_level(std::size_t level)
        {
            if (level > forward.size())
            {
                forward.resize(level, nullptr);
            }
        }
    };
}
