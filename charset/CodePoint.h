#pragma once

#include <cstddef>
#include <cstdint>

namespace coordination::charset
{
    // Unicode 码点使用 char32_t 表示，避免依赖平台上的 wchar_t 宽度。
    using CodePoint = char32_t;

    // 一次解码的结果。length 表示本次消耗的编码单元数；
    // 遇到非法或截断序列时 valid 为 false，length 至少为 1（除非 offset 已越界）。
    struct DecodeResult
    {
        CodePoint code_point{ 0 };
        size_t length{ 0 };
        bool valid{ false };
    };

    // 判断一个数值是否落在 UTF-16 代理区 U+D800..U+DFFF。
    [[nodiscard]] constexpr bool is_surrogate(uint32_t value) noexcept
    {
        return value >= 0xD800u && value <= 0xDFFFu;
    }

    // 标量值是 Unicode 中可独立编码的码点，不包含代理区且不超过 U+10FFFF。
    [[nodiscard]] constexpr bool is_scalar_value(uint32_t value) noexcept
    {
        return value <= 0x10FFFFu && !is_surrogate(value);
    }

    namespace detail
    {
        struct CodePointRange
        {
            uint32_t first;
            uint32_t last;
        };

        // 码点区间表。这里只覆盖常用区块，并未覆盖全部 Unicode：
        // 拉丁字母（含 Latin-1 Supplement、Latin Extended-A/B/Additional）、
        // 希腊字母、西里尔字母、平假名、片假名、谚文以及 CJK 统一表意文字。
        // 组合附加符号、Emoji、南亚文字等不在 is_letter 的覆盖范围内。
        inline constexpr CodePointRange kLetterRanges[] =
        {
            { 0x0041u, 0x005Au }, // A-Z
            { 0x0061u, 0x007Au }, // a-z
            { 0x00AAu, 0x00AAu }, // ª
            { 0x00B5u, 0x00B5u }, // µ
            { 0x00BAu, 0x00BAu }, // º
            { 0x00C0u, 0x00D6u }, // À-Ö
            { 0x00D8u, 0x00F6u }, // Ø-ö（排除 × U+00D7）
            { 0x00F8u, 0x00FFu }, // ø-ÿ（排除 ÷ U+00F7）
            { 0x0100u, 0x017Fu }, // Latin Extended-A
            { 0x0180u, 0x024Fu }, // Latin Extended-B
            { 0x0370u, 0x0373u }, // 希腊字母
            { 0x0376u, 0x0377u },
            { 0x037Au, 0x037Du },
            { 0x037Fu, 0x037Fu },
            { 0x0386u, 0x0386u },
            { 0x0388u, 0x038Au },
            { 0x038Cu, 0x038Cu },
            { 0x038Eu, 0x03A1u },
            { 0x03A3u, 0x03F5u },
            { 0x03F7u, 0x03FFu },
            { 0x0400u, 0x0481u }, // 西里尔字母
            { 0x048Au, 0x052Fu },
            { 0x1C80u, 0x1C88u }, // Cyrillic Extended-C
            { 0x1E00u, 0x1EFFu }, // Latin Extended Additional
            { 0x3041u, 0x3096u }, // 平假名
            { 0x309Du, 0x309Fu },
            { 0x30A1u, 0x30FAu }, // 片假名
            { 0x30FCu, 0x30FFu },
            { 0x3400u, 0x4DBFu }, // CJK 扩展 A
            { 0x4E00u, 0x9FFFu }, // CJK 统一表意文字
            { 0xA640u, 0xA66Du }, // Cyrillic Extended-B
            { 0xA680u, 0xA69Bu },
            { 0xAC00u, 0xD7A3u }, // 谚文音节
            { 0xF900u, 0xFAFFu }, // CJK 兼容表意文字
            { 0x20000u, 0x2A6DFu }, // CJK 扩展 B
            { 0x2A700u, 0x2EE5Fu }, // CJK 扩展 C/D/E/F/I 的主体
            { 0x30000u, 0x3134Au }  // CJK 扩展 G
        };

        // 十进制数字（Nd）只列常用区块，未覆盖全部 Unicode 数字。
        inline constexpr CodePointRange kDigitRanges[] =
        {
            { 0x0030u, 0x0039u }, // 0-9
            { 0x0660u, 0x0669u }, // 阿拉伯-印度数字
            { 0x06F0u, 0x06F9u }, // 扩展阿拉伯-印度数字
            { 0xFF10u, 0xFF19u }  // 全角数字
        };

        // Unicode White_Space 属性对应的常用空白字符。
        inline constexpr CodePointRange kWhitespaceRanges[] =
        {
            { 0x0009u, 0x000Du }, // TAB/LF/VT/FF/CR
            { 0x0020u, 0x0020u }, // SPACE
            { 0x0085u, 0x0085u }, // NEL
            { 0x00A0u, 0x00A0u }, // NBSP
            { 0x1680u, 0x1680u }, // OGHAM SPACE MARK
            { 0x2000u, 0x200Au }, // EN QUAD .. HAIR SPACE
            { 0x2028u, 0x2029u }, // LINE/PARAGRAPH SEPARATOR
            { 0x202Fu, 0x202Fu }, // NARROW NO-BREAK SPACE
            { 0x205Fu, 0x205Fu }, // MEDIUM MATHEMATICAL SPACE
            { 0x3000u, 0x3000u }  // IDEOGRAPHIC SPACE
        };

        template <std::size_t N>
        [[nodiscard]] constexpr bool in_ranges(uint32_t value, const CodePointRange (&ranges)[N]) noexcept
        {
            for (std::size_t index = 0; index < N; ++index)
            {
                if (value >= ranges[index].first && value <= ranges[index].last)
                {
                    return true;
                }
            }
            return false;
        }
    }

    [[nodiscard]] constexpr bool is_letter(CodePoint code_point) noexcept
    {
        const uint32_t value = static_cast<uint32_t>(code_point);
        return is_scalar_value(value) && detail::in_ranges(value, detail::kLetterRanges);
    }

    [[nodiscard]] constexpr bool is_digit(CodePoint code_point) noexcept
    {
        const uint32_t value = static_cast<uint32_t>(code_point);
        return is_scalar_value(value) && detail::in_ranges(value, detail::kDigitRanges);
    }

    [[nodiscard]] constexpr bool is_whitespace(CodePoint code_point) noexcept
    {
        const uint32_t value = static_cast<uint32_t>(code_point);
        return is_scalar_value(value) && detail::in_ranges(value, detail::kWhitespaceRanges);
    }

    // 标识符首字符：Unicode 字母或下划线。
    [[nodiscard]] constexpr bool is_identifier_start(CodePoint code_point) noexcept
    {
        return code_point == U'_' || is_letter(code_point);
    }

    // 标识符后续字符：首字符规则再加十进制数字。
    [[nodiscard]] constexpr bool is_identifier_continue(CodePoint code_point) noexcept
    {
        return is_identifier_start(code_point) || is_digit(code_point);
    }
}
