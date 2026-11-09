#pragma once

// 标准库文本编码层的编码枚举与名称映射。
// 规范名称统一为小写：unknown、ascii、utf-8、utf-16le、utf-16be、utf-32le、utf-32be、latin1。
// encoding_from_name 对大小写不敏感，并忽略名称中的 '-'、'_' 与空白字符。
#include <cstddef>
#include <optional>
#include <string_view>

namespace coordination::text
{
    // 受支持的文本编码。unknown 表示无法识别或调用方未指定。
    enum class Encoding
    {
        unknown,
        ascii,
        utf8,
        utf16_le,
        utf16_be,
        utf32_le,
        utf32_be,
        latin1
    };

    namespace detail
    {
        // 只转换 ASCII 字母，避免依赖 locale。
        [[nodiscard]] constexpr char name_to_lower(char value) noexcept
        {
            if (value >= 'A' && value <= 'Z')
            {
                return static_cast<char>(value - 'A' + 'a');
            }
            return value;
        }

        // 名称中的分隔符不参与比较，因此 "UTF-16LE" 与 "utf_16_le" 等价。
        [[nodiscard]] constexpr bool is_name_separator(char value) noexcept
        {
            return value == '-' || value == '_' || value == ' ' || value == '\t';
        }

        // 已规范化名称的最大长度（"unknown" 与 "iso88591" 都远小于该值）。
        inline constexpr std::size_t kNormalizedNameCapacity = 24;
    }

    // 返回编码的规范名称；所有返回值都能被 encoding_from_name 重新解析。
    [[nodiscard]] inline constexpr std::string_view encoding_name(Encoding encoding) noexcept
    {
        switch (encoding)
        {
        case Encoding::ascii:
            return "ascii";
        case Encoding::utf8:
            return "utf-8";
        case Encoding::utf16_le:
            return "utf-16le";
        case Encoding::utf16_be:
            return "utf-16be";
        case Encoding::utf32_le:
            return "utf-32le";
        case Encoding::utf32_be:
            return "utf-32be";
        case Encoding::latin1:
            return "latin1";
        case Encoding::unknown:
            break;
        }
        return "unknown";
    }

    // 解析编码名称。大小写不敏感，忽略 '-'、'_' 与空白；
    // 另外接受 utf8、utf16、utf32、us-ascii、iso-8859-1 等常见别名，
    // 其中不带端序后缀的 utf16/utf32 按小端处理。
    // 名称为空或无法识别时返回 std::nullopt。
    [[nodiscard]] inline constexpr std::optional<Encoding> encoding_from_name(std::string_view name) noexcept
    {
        char normalized[detail::kNormalizedNameCapacity]{};
        std::size_t length = 0;
        for (const char raw : name)
        {
            const char value = detail::name_to_lower(raw);
            if (detail::is_name_separator(value))
            {
                continue;
            }
            if (length >= detail::kNormalizedNameCapacity)
            {
                return std::nullopt;
            }
            normalized[length] = value;
            ++length;
        }

        const std::string_view key(normalized, length);
        if (key == "unknown")
        {
            return std::optional<Encoding>(Encoding::unknown);
        }
        if (key == "ascii" || key == "usascii")
        {
            return std::optional<Encoding>(Encoding::ascii);
        }
        if (key == "utf8")
        {
            return std::optional<Encoding>(Encoding::utf8);
        }
        if (key == "utf16" || key == "utf16le")
        {
            return std::optional<Encoding>(Encoding::utf16_le);
        }
        if (key == "utf16be")
        {
            return std::optional<Encoding>(Encoding::utf16_be);
        }
        if (key == "utf32" || key == "utf32le")
        {
            return std::optional<Encoding>(Encoding::utf32_le);
        }
        if (key == "utf32be")
        {
            return std::optional<Encoding>(Encoding::utf32_be);
        }
        if (key == "latin1" || key == "iso88591")
        {
            return std::optional<Encoding>(Encoding::latin1);
        }
        return std::nullopt;
    }
}
