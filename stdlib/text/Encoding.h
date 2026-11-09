#pragma once

// 标准库文本编码层：编码识别、编码转换与换行符规范化。
// 依赖 charset 模块的 UTF-8/UTF-16 编解码、码点校验与 BOM 工具，全部为纯头文件实现，
// 不依赖平台 API，也不主动抛出异常（std::string/vector 的分配失败除外）。
//
// 转换约定：
//   * 统一以 UTF-32 码点序列作为中间表示；
//   * 源文本开头与源编码匹配的 BOM 会被剥离，输出不带 BOM；
//   * strict 为 true 时遇到首个非法序列即停止，输出保留此前的合法前缀；
//     否则用 replacement 替换每个非法或不可表示的序列；
//   * replacement 按码点 U+00XX 写入（XX 为其字节值），对 ascii/latin1 直接写入该字节。
#include "charset/Charset.h"
#include "stdlib/text/EncodingNames.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace coordination::text
{
    // 编码识别结果。bom_length 为识别到的 BOM 字节数，没有 BOM 时为 0。
    struct Detection
    {
        Encoding encoding{ Encoding::unknown };
        double confidence{ 0.0 };
        std::size_t bom_length{ 0 };
    };

    // 转换选项。strict 为 true 时遇到非法序列立即停止，不写入替换字符。
    struct ConvertOptions
    {
        bool strict{ false };
        char replacement{ '?' };
    };

    // 转换结果。replaced 统计非 strict 模式下写入的替换字符个数。
    struct ConvertResult
    {
        std::string bytes;
        bool had_errors{ false };
        std::size_t replaced{ 0 };
    };

    namespace detail
    {
        // 各类判定结果的置信度。BOM 与纯 ASCII 是确定性结论；
        // UTF-8 通过严格校验时置信度较高；NUL 分布只是启发式；latin1 为兜底。
        inline constexpr double kConfidenceBom = 1.0;
        inline constexpr double kConfidenceAscii = 1.0;
        inline constexpr double kConfidenceUtf8 = 0.95;
        inline constexpr double kConfidenceLatin1 = 0.2;
        inline constexpr double kConfidenceZeroBase = 0.6;
        inline constexpr double kConfidenceZeroSpan = 0.3;

        // NUL 分布的判定阈值：高位字节位置的 0 占比不低于 0.8，
        // 低位字节位置的 0 占比不高于 0.2。
        inline constexpr double kZeroHighThreshold = 0.8;
        inline constexpr double kZeroLowThreshold = 0.2;

        // 非法序列在非 strict 模式下的占位码点。它不是合法标量值，
        // 编码阶段会把它直接写成 replacement，避免与真实的 U+FFFD 混淆。
        inline constexpr char32_t kReplacementMarker = static_cast<char32_t>(0xFFFFFFFFu);

        // 返回编码在文本开头可能出现的 BOM；ascii/latin1/unknown 没有 BOM。
        [[nodiscard]] constexpr std::string_view encoding_bom(Encoding encoding) noexcept
        {
            switch (encoding)
            {
            case Encoding::utf8:
                return "\xEF\xBB\xBF";
            case Encoding::utf16_le:
                return "\xFF\xFE";
            case Encoding::utf16_be:
                return "\xFE\xFF";
            case Encoding::utf32_le:
                // 含 NUL 的字面量无法依赖 strlen 推导长度，必须显式给出。
                return std::string_view("\xFF\xFE\x00\x00", 4);
            case Encoding::utf32_be:
                return std::string_view("\x00\x00\xFE\xFF", 4);
            case Encoding::ascii:
            case Encoding::latin1:
            case Encoding::unknown:
                break;
            }
            return {};
        }

        // 跳过输入开头与 source 匹配的 BOM；不匹配时原样返回。
        [[nodiscard]] inline std::string_view skip_bom(std::string_view bytes, Encoding source) noexcept
        {
            const std::string_view bom = encoding_bom(source);
            if (!bom.empty() && bytes.size() >= bom.size() && bytes.substr(0, bom.size()) == bom)
            {
                return bytes.substr(bom.size());
            }
            return bytes;
        }

        // 严格校验 UTF-8，并统计长度大于 1 的合法多字节序列个数。
        [[nodiscard]] inline bool validate_utf8(std::string_view bytes, std::size_t& multibyte) noexcept
        {
            multibyte = 0;
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const charset::DecodeResult step = charset::decode_utf8(bytes, offset);
                if (!step.valid || step.length == 0)
                {
                    return false;
                }
                if (step.length > 1)
                {
                    ++multibyte;
                }
                offset += step.length;
            }
            return true;
        }

        // 判断是否全部为 ASCII 字节（含 NUL）。
        [[nodiscard]] inline bool is_ascii(std::string_view bytes) noexcept
        {
            for (const char raw : bytes)
            {
                if (static_cast<unsigned char>(raw) > 0x7Fu)
                {
                    return false;
                }
            }
            return true;
        }

        // 0 字节按位置模 4 的分布，用于推断 UTF-16/32 的端序。
        struct ZeroProfile
        {
            std::size_t slots[4]{};
            std::size_t zeros[4]{};
            std::size_t nul_bytes{ 0 };

            [[nodiscard]] double ratio(std::size_t residue) const noexcept
            {
                if (slots[residue] == 0)
                {
                    return 0.0;
                }
                return static_cast<double>(zeros[residue]) / static_cast<double>(slots[residue]);
            }

            // 同一奇偶性位置上的 0 占比，用于 UTF-16 判定。
            [[nodiscard]] double parity_ratio(std::size_t parity) const noexcept
            {
                const std::size_t total = slots[parity] + slots[parity + 2];
                if (total == 0)
                {
                    return 0.0;
                }
                const std::size_t nul = zeros[parity] + zeros[parity + 2];
                return static_cast<double>(nul) / static_cast<double>(total);
            }
        };

        [[nodiscard]] inline ZeroProfile build_zero_profile(std::string_view bytes) noexcept
        {
            ZeroProfile profile;
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                const std::size_t residue = index % 4;
                ++profile.slots[residue];
                if (bytes[index] == '\0')
                {
                    ++profile.zeros[residue];
                    ++profile.nul_bytes;
                }
            }
            return profile;
        }

        // 按 NUL 分布推断 UTF-16/32 端序；无法判断时返回 Encoding::unknown。
        // 该启发式只适用于以 ASCII 为主、存在 0 填充字节的文本。
        [[nodiscard]] inline Encoding detect_by_zero_profile(const ZeroProfile& profile, std::size_t size) noexcept
        {
            if (size >= 4)
            {
                const double utf32_le_high = (profile.ratio(1) + profile.ratio(2) + profile.ratio(3)) / 3.0;
                if (utf32_le_high >= kZeroHighThreshold && profile.ratio(0) <= kZeroLowThreshold)
                {
                    return Encoding::utf32_le;
                }

                const double utf32_be_high = (profile.ratio(0) + profile.ratio(1) + profile.ratio(2)) / 3.0;
                if (utf32_be_high >= kZeroHighThreshold && profile.ratio(3) <= kZeroLowThreshold)
                {
                    return Encoding::utf32_be;
                }
            }

            if (size >= 2)
            {
                if (profile.parity_ratio(1) >= kZeroHighThreshold && profile.parity_ratio(0) <= kZeroLowThreshold)
                {
                    return Encoding::utf16_le;
                }
                if (profile.parity_ratio(0) >= kZeroHighThreshold && profile.parity_ratio(1) <= kZeroLowThreshold)
                {
                    return Encoding::utf16_be;
                }
            }

            return Encoding::unknown;
        }

        // 计算 NUL 启发式命中的置信度，取值约为 0.84 ~ 0.9。
        [[nodiscard]] inline double zero_confidence(const ZeroProfile& profile, Encoding encoding) noexcept
        {
            double score = 0.0;
            switch (encoding)
            {
            case Encoding::utf32_le:
                score = (profile.ratio(1) + profile.ratio(2) + profile.ratio(3)) / 3.0;
                break;
            case Encoding::utf32_be:
                score = (profile.ratio(0) + profile.ratio(1) + profile.ratio(2)) / 3.0;
                break;
            case Encoding::utf16_le:
                score = profile.parity_ratio(1);
                break;
            case Encoding::utf16_be:
                score = profile.parity_ratio(0);
                break;
            default:
                return 0.0;
            }
            return kConfidenceZeroBase + kConfidenceZeroSpan * score;
        }

        // 解码后的中间表示。
        struct DecodedText
        {
            std::vector<char32_t> code_points;
            bool had_errors{ false };
            std::size_t replaced{ 0 };
        };

        // 记录一次非法序列；返回 false 表示严格模式下应当停止。
        [[nodiscard]] inline bool record_error(DecodedText& decoded, const ConvertOptions& options)
        {
            decoded.had_errors = true;
            if (options.strict)
            {
                return false;
            }
            ++decoded.replaced;
            decoded.code_points.push_back(kReplacementMarker);
            return true;
        }

        // 把源字节解码成 UTF-32 码点序列。
        [[nodiscard]] inline DecodedText decode_to_utf32(std::string_view bytes, Encoding source, const ConvertOptions& options)
        {
            DecodedText decoded;

            if (source == Encoding::unknown)
            {
                decoded.had_errors = true;
                return decoded;
            }

            switch (source)
            {
            case Encoding::ascii:
            {
                decoded.code_points.reserve(bytes.size());
                for (const char raw : bytes)
                {
                    const unsigned char byte = static_cast<unsigned char>(raw);
                    if (byte > 0x7Fu)
                    {
                        if (!record_error(decoded, options))
                        {
                            return decoded;
                        }
                        continue;
                    }
                    decoded.code_points.push_back(static_cast<char32_t>(byte));
                }
                return decoded;
            }

            case Encoding::latin1:
            {
                decoded.code_points.reserve(bytes.size());
                for (const char raw : bytes)
                {
                    // latin1 的每个字节都对应 U+0000..U+00FF，不存在非法序列。
                    decoded.code_points.push_back(static_cast<char32_t>(static_cast<unsigned char>(raw)));
                }
                return decoded;
            }

            case Encoding::utf8:
            {
                const std::string_view body = skip_bom(bytes, source);
                decoded.code_points.reserve(body.size());
                std::size_t offset = 0;
                while (offset < body.size())
                {
                    const charset::DecodeResult step = charset::decode_utf8(body, offset);
                    if (step.valid)
                    {
                        decoded.code_points.push_back(step.code_point);
                        offset += step.length;
                        continue;
                    }
                    if (!record_error(decoded, options))
                    {
                        return decoded;
                    }
                    offset += (step.length == 0 ? 1 : step.length);
                }
                return decoded;
            }

            case Encoding::utf16_le:
            case Encoding::utf16_be:
            {
                const std::string_view body = skip_bom(bytes, source);
                const bool little_endian = (source == Encoding::utf16_le);

                // 先按端序还原 UTF-16 编码单元，再复用 charset 的代理对解码。
                std::u16string units;
                units.reserve(body.size() / 2);
                for (std::size_t offset = 0; offset + 1 < body.size(); offset += 2)
                {
                    const std::uint32_t first = static_cast<unsigned char>(body[offset]);
                    const std::uint32_t second = static_cast<unsigned char>(body[offset + 1]);
                    const std::uint32_t unit = little_endian ? (first | (second << 8)) : ((first << 8) | second);
                    units.push_back(static_cast<char16_t>(unit));
                }

                decoded.code_points.reserve(units.size());
                std::size_t unit_offset = 0;
                while (unit_offset < units.size())
                {
                    const charset::DecodeResult step = charset::decode_utf16(units, unit_offset);
                    if (step.valid)
                    {
                        decoded.code_points.push_back(step.code_point);
                        unit_offset += step.length;
                        continue;
                    }
                    if (!record_error(decoded, options))
                    {
                        return decoded;
                    }
                    unit_offset += (step.length == 0 ? 1 : step.length);
                }

                // 末尾残留的单字节无法构成 UTF-16 单元。
                if (body.size() % 2 != 0)
                {
                    if (!record_error(decoded, options))
                    {
                        return decoded;
                    }
                }
                return decoded;
            }

            case Encoding::utf32_le:
            case Encoding::utf32_be:
            {
                const std::string_view body = skip_bom(bytes, source);
                const bool little_endian = (source == Encoding::utf32_le);
                decoded.code_points.reserve(body.size() / 4);

                std::size_t offset = 0;
                while (offset + 3 < body.size())
                {
                    const std::uint32_t first = static_cast<unsigned char>(body[offset]);
                    const std::uint32_t second = static_cast<unsigned char>(body[offset + 1]);
                    const std::uint32_t third = static_cast<unsigned char>(body[offset + 2]);
                    const std::uint32_t fourth = static_cast<unsigned char>(body[offset + 3]);
                    const std::uint32_t value = little_endian
                        ? (first | (second << 8) | (third << 16) | (fourth << 24))
                        : ((first << 24) | (second << 16) | (third << 8) | fourth);

                    if (charset::is_scalar_value(value))
                    {
                        decoded.code_points.push_back(static_cast<char32_t>(value));
                    }
                    else if (!record_error(decoded, options))
                    {
                        return decoded;
                    }
                    offset += 4;
                }

                // 末尾不足 4 字节的残缺序列。
                if (offset < body.size())
                {
                    if (!record_error(decoded, options))
                    {
                        return decoded;
                    }
                }
                return decoded;
            }

            case Encoding::unknown:
                break;
            }

            decoded.had_errors = true;
            return decoded;
        }

        // 把一个合法码点追加到目标编码；目标无法表示时返回 false。
        [[nodiscard]] inline bool append_code_point(std::string& output, Encoding target, char32_t code_point)
        {
            const std::uint32_t value = static_cast<std::uint32_t>(code_point);
            switch (target)
            {
            case Encoding::ascii:
                if (value > 0x7Fu)
                {
                    return false;
                }
                output.push_back(static_cast<char>(value));
                return true;

            case Encoding::latin1:
                if (value > 0xFFu)
                {
                    return false;
                }
                output.push_back(static_cast<char>(value));
                return true;

            case Encoding::utf8:
            {
                char buffer[4]{};
                const std::size_t length = charset::encode_utf8(code_point, buffer);
                if (length == 0)
                {
                    return false;
                }
                output.append(buffer, length);
                return true;
            }

            case Encoding::utf16_le:
            case Encoding::utf16_be:
            {
                char16_t buffer[2]{};
                const std::size_t length = charset::encode_utf16(code_point, buffer);
                if (length == 0)
                {
                    return false;
                }
                for (std::size_t index = 0; index < length; ++index)
                {
                    const std::uint32_t unit = static_cast<char16_t>(buffer[index]);
                    if (target == Encoding::utf16_le)
                    {
                        output.push_back(static_cast<char>(unit & 0xFFu));
                        output.push_back(static_cast<char>((unit >> 8) & 0xFFu));
                    }
                    else
                    {
                        output.push_back(static_cast<char>((unit >> 8) & 0xFFu));
                        output.push_back(static_cast<char>(unit & 0xFFu));
                    }
                }
                return true;
            }

            case Encoding::utf32_le:
            case Encoding::utf32_be:
            {
                if (!charset::is_scalar_value(value))
                {
                    return false;
                }
                if (target == Encoding::utf32_le)
                {
                    output.push_back(static_cast<char>(value & 0xFFu));
                    output.push_back(static_cast<char>((value >> 8) & 0xFFu));
                    output.push_back(static_cast<char>((value >> 16) & 0xFFu));
                    output.push_back(static_cast<char>((value >> 24) & 0xFFu));
                }
                else
                {
                    output.push_back(static_cast<char>((value >> 24) & 0xFFu));
                    output.push_back(static_cast<char>((value >> 16) & 0xFFu));
                    output.push_back(static_cast<char>((value >> 8) & 0xFFu));
                    output.push_back(static_cast<char>(value & 0xFFu));
                }
                return true;
            }

            case Encoding::unknown:
                break;
            }

            return false;
        }

        // 直接写入替换字符，不做目标编码的可表示性检查。
        inline void append_replacement(std::string& output, Encoding target, char replacement)
        {
            if (target == Encoding::ascii || target == Encoding::latin1)
            {
                output.push_back(replacement);
                return;
            }
            const char32_t code_point = static_cast<char32_t>(static_cast<unsigned char>(replacement));
            (void)append_code_point(output, target, code_point);
        }

        // 把 UTF-32 码点序列编码为目标编码，并累计错误与替换次数。
        inline void encode_from_utf32(const std::vector<char32_t>& code_points, Encoding target,
            const ConvertOptions& options, ConvertResult& result)
        {
            for (const char32_t code_point : code_points)
            {
                // 解码阶段插入的占位符只负责写出替换字符，替换次数已经统计过。
                if (code_point == kReplacementMarker)
                {
                    append_replacement(result.bytes, target, options.replacement);
                    continue;
                }

                if (append_code_point(result.bytes, target, code_point))
                {
                    continue;
                }

                result.had_errors = true;
                if (options.strict)
                {
                    return;
                }
                ++result.replaced;
                append_replacement(result.bytes, target, options.replacement);
            }
        }
    }

    // 识别文本编码。判定顺序：
    //   1. BOM（UTF-8/UTF-16/UTF-32）—— 高置信；
    //   2. 无 BOM 时严格校验 UTF-8，只有出现多字节序列才认定为 UTF-8；
    //   3. 含 0 字节时按 NUL 分布判断 UTF-16/32 的端序；
    //   4. 纯 ASCII 字节序列 → ascii（空输入同样视为 ascii）；
    //   5. 其余情况 → latin1（低置信兜底）。
    [[nodiscard]] inline Detection detect_encoding(std::string_view bytes)
    {
        Detection detection;

        const std::size_t bom = charset::bom_length(bytes);
        if (bom != 0)
        {
            detection.bom_length = bom;
            detection.confidence = detail::kConfidenceBom;
            if (bom == 3)
            {
                detection.encoding = Encoding::utf8;
            }
            else if (bom == 4)
            {
                // 4 字节 BOM 只可能是 UTF-32：FF FE 00 00 为小端，00 00 FE FF 为大端。
                detection.encoding = (static_cast<unsigned char>(bytes[0]) == 0xFFu)
                    ? Encoding::utf32_le
                    : Encoding::utf32_be;
            }
            else
            {
                detection.encoding = (static_cast<unsigned char>(bytes[0]) == 0xFEu)
                    ? Encoding::utf16_be
                    : Encoding::utf16_le;
            }
            return detection;
        }

        std::size_t multibyte = 0;
        if (detail::validate_utf8(bytes, multibyte) && multibyte > 0)
        {
            detection.encoding = Encoding::utf8;
            detection.confidence = detail::kConfidenceUtf8;
            return detection;
        }

        const detail::ZeroProfile profile = detail::build_zero_profile(bytes);
        if (profile.nul_bytes > 0)
        {
            const Encoding zero_encoding = detail::detect_by_zero_profile(profile, bytes.size());
            if (zero_encoding != Encoding::unknown)
            {
                detection.encoding = zero_encoding;
                detection.confidence = detail::zero_confidence(profile, zero_encoding);
                return detection;
            }
        }

        if (detail::is_ascii(bytes))
        {
            detection.encoding = Encoding::ascii;
            detection.confidence = detail::kConfidenceAscii;
            return detection;
        }

        detection.encoding = Encoding::latin1;
        detection.confidence = detail::kConfidenceLatin1;
        return detection;
    }

    // 在任意两种受支持编码之间转换，内部以 UTF-32 为中间表示。
    // from/to 为 unknown 时不产生输出，并置 had_errors。
    [[nodiscard]] inline ConvertResult convert(std::string_view bytes, Encoding from, Encoding to,
        ConvertOptions options = {})
    {
        ConvertResult result;
        if (from == Encoding::unknown || to == Encoding::unknown)
        {
            result.had_errors = true;
            return result;
        }

        const detail::DecodedText decoded = detail::decode_to_utf32(bytes, from, options);
        result.had_errors = decoded.had_errors;
        result.replaced = decoded.replaced;
        detail::encode_from_utf32(decoded.code_points, to, options, result);
        return result;
    }

    // 把任意受支持编码的文本转为 UTF-8。
    [[nodiscard]] inline std::string to_utf8(std::string_view bytes, Encoding from, ConvertOptions options = {})
    {
        return convert(bytes, from, Encoding::utf8, options).bytes;
    }

    // 把 UTF-8 文本转为目标编码。
    [[nodiscard]] inline std::string from_utf8(std::string_view utf8, Encoding to, ConvertOptions options = {})
    {
        return convert(utf8, Encoding::utf8, to, options).bytes;
    }

    // 规范化换行符：to_lf 时把 CRLF 与单独的 CR 统一为 LF，
    // 否则把 LF 与单独的 CR 统一为 CRLF。按字节处理，适用于 UTF-8/ASCII 文本。
    [[nodiscard]] inline std::string normalize_newlines(std::string_view text, bool to_lf = true)
    {
        std::string result;
        result.reserve(text.size());

        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const char value = text[index];
            if (value == '\r')
            {
                // CRLF 作为一个整体处理，避免重复转换。
                if (index + 1 < text.size() && text[index + 1] == '\n')
                {
                    ++index;
                }
                if (to_lf)
                {
                    result.push_back('\n');
                }
                else
                {
                    result.append("\r\n");
                }
            }
            else if (value == '\n')
            {
                if (to_lf)
                {
                    result.push_back('\n');
                }
                else
                {
                    result.append("\r\n");
                }
            }
            else
            {
                result.push_back(value);
            }
        }

        return result;
    }
}
