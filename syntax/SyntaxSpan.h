#pragma once

#include <cstddef>

namespace coordination::syntax
{
    // 源码区间：从 offset 开始、长度为 length 的字节（或字符）范围。
    // 只记录偏移量，不记录行列；需要行列时由词法分析器另存行表换算。
    struct SyntaxSpan
    {
        std::size_t offset = 0;
        std::size_t length = 0;

        constexpr std::size_t end() const noexcept
        {
            return offset + length;
        }

        constexpr bool empty() const noexcept
        {
            return length == 0;
        }

        static constexpr SyntaxSpan from_bounds(std::size_t begin, std::size_t finish) noexcept
        {
            return SyntaxSpan{begin, finish > begin ? finish - begin : 0};
        }

        // 合并两个区间：覆盖两者的最小范围。
        static constexpr SyntaxSpan covering(SyntaxSpan lhs, SyntaxSpan rhs) noexcept
        {
            const std::size_t begin = lhs.offset < rhs.offset ? lhs.offset : rhs.offset;
            const std::size_t finish = lhs.end() > rhs.end() ? lhs.end() : rhs.end();
            return from_bounds(begin, finish);
        }
    };
}
