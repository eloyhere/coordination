#pragma once

#include "CodePoint.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace coordination::charset
{
    namespace detail
    {
        [[nodiscard]] constexpr bool is_utf8_continuation(uint32_t byte) noexcept
        {
            return byte >= 0x80u && byte <= 0xBFu;
        }

        [[nodiscard]] constexpr DecodeResult invalid_decode(size_t length) noexcept
        {
            return { 0, length, false };
        }
    }

    // 从 input[offset] 开始解码一个 UTF-8 码点。
    // 严格拒绝 overlong 编码、代理区码点、大于 U+10FFFF 的码点、非法首字节、
    // 非法续字节以及被截断的序列；失败时 valid=false、length=1。
    // offset 已到达或越过末尾时返回 length=0。
    [[nodiscard]] inline DecodeResult decode_utf8(std::string_view input, size_t offset = 0) noexcept
    {
        if (offset >= input.size())
        {
            return detail::invalid_decode(0);
        }

        const uint32_t first = static_cast<uint32_t>(
            static_cast<unsigned char>(input[offset]));

        // 单字节 ASCII 分支。
        if (first <= 0x7Fu)
        {
            return { static_cast<CodePoint>(first), 1, true };
        }

        size_t length = 0;
        uint32_t minimum = 0;
        if (first >= 0xC2u && first <= 0xDFu)
        {
            length = 2;
            minimum = 0x80u;
        }
        else if (first >= 0xE0u && first <= 0xEFu)
        {
            length = 3;
            minimum = 0x800u;
        }
        else if (first >= 0xF0u && first <= 0xF4u)
        {
            length = 4;
            minimum = 0x10000u;
        }
        else
        {
            // 0x80..0xBF 是孤立续字节；0xC0/0xC1 必然 overlong；
            // 0xF5..0xFF 超出 Unicode 范围。
            return detail::invalid_decode(1);
        }

        if (length > input.size() - offset)
        {
            return detail::invalid_decode(1);
        }

        const uint32_t second = static_cast<uint32_t>(
            static_cast<unsigned char>(input[offset + 1]));
        if (!detail::is_utf8_continuation(second))
        {
            return detail::invalid_decode(1);
        }

        uint32_t code_point = 0;
        if (length == 2)
        {
            code_point = ((first & 0x1Fu) << 6) | (second & 0x3Fu);
        }
        else
        {
            const uint32_t third = static_cast<uint32_t>(
                static_cast<unsigned char>(input[offset + 2]));
            if (!detail::is_utf8_continuation(third))
            {
                return detail::invalid_decode(1);
            }

            if (length == 3)
            {
                code_point = ((first & 0x0Fu) << 12)
                    | ((second & 0x3Fu) << 6)
                    | (third & 0x3Fu);
            }
            else
            {
                const uint32_t fourth = static_cast<uint32_t>(
                    static_cast<unsigned char>(input[offset + 3]));
                if (!detail::is_utf8_continuation(fourth))
                {
                    return detail::invalid_decode(1);
                }

                code_point = ((first & 0x07u) << 18)
                    | ((second & 0x3Fu) << 12)
                    | ((third & 0x3Fu) << 6)
                    | (fourth & 0x3Fu);
            }
        }

        // 三重校验可同时拦住 overlong、代理区以及超出 U+10FFFF 的码点。
        if (code_point < minimum || !is_scalar_value(code_point))
        {
            return detail::invalid_decode(1);
        }

        return { static_cast<CodePoint>(code_point), length, true };
    }

    // 将码点编码为 UTF-8，最多写入 4 字节。非法码点返回 0，out 不写入内容。
    [[nodiscard]] inline size_t encode_utf8(CodePoint code_point, char out[4]) noexcept
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

        if (value <= 0x7Fu)
        {
            out[0] = static_cast<char>(value);
            return 1;
        }

        if (value <= 0x7FFu)
        {
            out[0] = static_cast<char>(0xC0u | (value >> 6));
            out[1] = static_cast<char>(0x80u | (value & 0x3Fu));
            return 2;
        }

        if (value <= 0xFFFFu)
        {
            out[0] = static_cast<char>(0xE0u | (value >> 12));
            out[1] = static_cast<char>(0x80u | ((value >> 6) & 0x3Fu));
            out[2] = static_cast<char>(0x80u | (value & 0x3Fu));
            return 3;
        }

        out[0] = static_cast<char>(0xF0u | (value >> 18));
        out[1] = static_cast<char>(0x80u | ((value >> 12) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | ((value >> 6) & 0x3Fu));
        out[3] = static_cast<char>(0x80u | (value & 0x3Fu));
        return 4;
    }

    // 便捷重载：非法码点返回空字符串。
    [[nodiscard]] inline std::string encode_utf8(CodePoint code_point)
    {
        char buffer[4]{};
        const size_t length = encode_utf8(code_point, buffer);
        return std::string(buffer, length);
    }

    // 统计 UTF-8 文本中的码点数。非法序列按一个解码单元计一次，并向前推进 1 字节。
    [[nodiscard]] inline size_t utf8_length(std::string_view input) noexcept
    {
        size_t length = 0;
        size_t offset = 0;
        while (offset < input.size())
        {
            const DecodeResult result = decode_utf8(input, offset);
            offset += (result.length == 0 ? 1 : result.length);
            ++length;
        }
        return length;
    }

    // 将 UTF-8 文本转换为码点序列。每个非法序列以一个 U+FFFD 代替。
    [[nodiscard]] inline std::vector<CodePoint> to_code_points(std::string_view input)
    {
        std::vector<CodePoint> code_points;
        code_points.reserve(utf8_length(input));

        size_t offset = 0;
        while (offset < input.size())
        {
            const DecodeResult result = decode_utf8(input, offset);
            if (result.valid)
            {
                code_points.push_back(result.code_point);
            }
            else
            {
                code_points.push_back(static_cast<CodePoint>(0xFFFDu));
            }
            offset += (result.length == 0 ? 1 : result.length);
        }
        return code_points;
    }

    // 将码点序列编码为 UTF-8。非法码点（代理区或越界）会被跳过。
    [[nodiscard]] inline std::string from_code_points(const std::vector<CodePoint>& code_points)
    {
        std::string result;
        result.reserve(code_points.size());

        char buffer[4]{};
        for (const CodePoint code_point : code_points)
        {
            const size_t length = encode_utf8(code_point, buffer);
            if (length != 0)
            {
                result.append(buffer, length);
            }
        }
        return result;
    }

    // 检测开头的 BOM，返回其字节长度；没有 BOM 时返回 0。
    // 依次识别 UTF-8、UTF-32BE/LE 与 UTF-16BE/LE 的 BOM。
    [[nodiscard]] inline size_t bom_length(std::string_view input) noexcept
    {
        if (input.size() >= 3
            && static_cast<unsigned char>(input[0]) == 0xEFu
            && static_cast<unsigned char>(input[1]) == 0xBBu
            && static_cast<unsigned char>(input[2]) == 0xBFu)
        {
            return 3;
        }

        // 先判断 UTF-32LE，避免把它的前两字节误判成 UTF-16LE。
        if (input.size() >= 4
            && static_cast<unsigned char>(input[0]) == 0xFFu
            && static_cast<unsigned char>(input[1]) == 0xFEu
            && static_cast<unsigned char>(input[2]) == 0x00u
            && static_cast<unsigned char>(input[3]) == 0x00u)
        {
            return 4;
        }

        if (input.size() >= 4
            && static_cast<unsigned char>(input[0]) == 0x00u
            && static_cast<unsigned char>(input[1]) == 0x00u
            && static_cast<unsigned char>(input[2]) == 0xFEu
            && static_cast<unsigned char>(input[3]) == 0xFFu)
        {
            return 4;
        }

        if (input.size() >= 2
            && static_cast<unsigned char>(input[0]) == 0xFEu
            && static_cast<unsigned char>(input[1]) == 0xFFu)
        {
            return 2;
        }

        if (input.size() >= 2
            && static_cast<unsigned char>(input[0]) == 0xFFu
            && static_cast<unsigned char>(input[1]) == 0xFEu)
        {
            return 2;
        }

        return 0;
    }

    // 去掉开头可能存在的 BOM；没有 BOM 时原样返回。
    [[nodiscard]] inline std::string_view strip_bom(std::string_view input) noexcept
    {
        return input.substr(bom_length(input));
    }
}
