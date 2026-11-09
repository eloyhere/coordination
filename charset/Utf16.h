#pragma once

#include "CodePoint.h"

#include <cstddef>
#include <string_view>

namespace coordination::charset
{
    // 从 input[offset] 开始解码一个 UTF-16 码点。
    // BMP 码点消耗一个单元；合法代理对消耗两个单元；
    // 孤立代理、错误的代理组合以及截断的代理对均返回 valid=false、length=1。
    // offset 已到达或越过末尾时返回 length=0。
    [[nodiscard]] inline DecodeResult decode_utf16(std::u16string_view input, size_t offset = 0) noexcept
    {
        if (offset >= input.size())
        {
            return { 0, 0, false };
        }

        const uint32_t first = static_cast<uint32_t>(input[offset]);
        if (!is_surrogate(first))
        {
            return { static_cast<CodePoint>(first), 1, true };
        }

        // 低代理不可能作为序列首单元出现。
        if (first >= 0xDC00u)
        {
            return { 0, 1, false };
        }

        if (offset + 1 >= input.size())
        {
            return { 0, 1, false };
        }

        const uint32_t second = static_cast<uint32_t>(input[offset + 1]);
        if (second < 0xDC00u || second > 0xDFFFu)
        {
            return { 0, 1, false };
        }

        const uint32_t code_point = 0x10000u
            + ((first - 0xD800u) << 10)
            + (second - 0xDC00u);
        return { static_cast<CodePoint>(code_point), 2, true };
    }

    // 将码点编码为 UTF-16，最多写入 2 个单元。
    // BMP 码点返回 1，非 BMP 标量值返回 2；代理区或越界码点返回 0。
    [[nodiscard]] inline size_t encode_utf16(CodePoint code_point, char16_t out[2]) noexcept
    {
        if (out == nullptr)
        {
            return 0;
        }

        const uint32_t value = static_cast<uint32_t>(code_point);
        if (!is_scalar_value(value))
        {
            return 0;
        }

        if (value <= 0xFFFFu)
        {
            out[0] = static_cast<char16_t>(value);
            return 1;
        }

        const uint32_t adjusted = value - 0x10000u;
        out[0] = static_cast<char16_t>(0xD800u + (adjusted >> 10));
        out[1] = static_cast<char16_t>(0xDC00u + (adjusted & 0x3FFu));
        return 2;
    }
}
