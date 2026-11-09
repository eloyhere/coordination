#pragma once

// 字节区间工具：表示文件内的半开区间 [start, end)。
//
// 约定：
//   * start >= end 视为空区间：size() 为 0、empty() 为 true，不做无符号回绕。
//   * clamp()、intersect()、from_size() 的结果始终满足 start <= end。
//   * from_size() 在 start + length 溢出 uint64 时把 end 饱和到 UINT64_MAX，
//     调用方可比较实际长度与请求长度来识别饱和。

#include <cstdint>
#include <limits>

namespace coordination::file
{
    // 半开区间 [start, end)，单位为字节。
    struct Range
    {
        std::uint64_t start = 0;
        std::uint64_t end = 0;

        // 区间长度；start > end 时返回 0，避免无符号回绕。
        [[nodiscard]] constexpr std::uint64_t size() const noexcept
        {
            return end > start ? end - start : 0;
        }

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return end <= start;
        }

        // 半开区间语义：包含 start，不包含 end。
        [[nodiscard]] constexpr bool contains(std::uint64_t offset) const noexcept
        {
            return offset >= start && offset < end;
        }

        // 截断到 [0, limit)：start、end 分别取较小值，并消除倒置。
        [[nodiscard]] constexpr Range clamp(std::uint64_t limit) const noexcept
        {
            const std::uint64_t clamped_start = start < limit ? start : limit;
            const std::uint64_t clamped_end = end < limit ? end : limit;
            if (clamped_end < clamped_start)
            {
                return { clamped_start, clamped_start };
            }

            return { clamped_start, clamped_end };
        }

        // 交集；不相交时返回位于较大起点处的空区间。
        [[nodiscard]] constexpr Range intersect(const Range& other) const noexcept
        {
            const std::uint64_t common_start = start > other.start ? start : other.start;
            const std::uint64_t common_end = end < other.end ? end : other.end;
            if (common_end < common_start)
            {
                return { common_start, common_start };
            }

            return { common_start, common_end };
        }

        // 由起点与长度构造；长度溢出时 end 饱和到 UINT64_MAX。
        [[nodiscard]] static constexpr Range from_size(std::uint64_t start, std::uint64_t length) noexcept
        {
            constexpr std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
            const std::uint64_t end = length > maximum - start ? maximum : start + length;
            return { start, end };
        }

        [[nodiscard]] constexpr bool operator==(const Range& other) const noexcept = default;
    };
}
