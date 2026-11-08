#pragma once

#include <cstdint>

namespace coordination::compiler
{
    // 源文件标识。虚拟文件（宏展开、模板实例、生成代码）也占一个 SourceId。
    using SourceId = std::uint32_t;

    inline constexpr SourceId kInvalidSource = static_cast<SourceId>(-1);

    // 源码区间：字节偏移 [begin, end)，配合 SourceId 定位到具体文件。
    // 行号/列号不进入 Span，由 SourceManager 按需换算（诊断与 LSP 用）。
    struct Span
    {
        SourceId source = kInvalidSource;
        std::uint32_t begin = 0;
        std::uint32_t end = 0;

        constexpr bool valid() const noexcept
        {
            return source != kInvalidSource;
        }

        constexpr std::uint32_t length() const noexcept
        {
            return end > begin ? end - begin : 0;
        }

        constexpr bool empty() const noexcept
        {
            return begin == end;
        }

        static constexpr Span from_bounds(SourceId id, std::uint32_t first, std::uint32_t last) noexcept
        {
            return Span{id, first, last > first ? last : first};
        }

        // 合并两个区间；跨文件时返回左边那个（调用方应保证同源）。
        static constexpr Span covering(Span lhs, Span rhs) noexcept
        {
            if (!lhs.valid())
            {
                return rhs;
            }
            if (!rhs.valid() || lhs.source != rhs.source)
            {
                return lhs;
            }
            const std::uint32_t first = lhs.begin < rhs.begin ? lhs.begin : rhs.begin;
            const std::uint32_t last = lhs.end > rhs.end ? lhs.end : rhs.end;
            return Span{lhs.source, first, last};
        }
    };

    // 1 基的行列位置，供诊断与编辑器使用。
    struct LineColumn
    {
        std::uint32_t line = 1;
        std::uint32_t column = 1;
    };
}
