#include "stdlib/text/Encoding.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

using namespace coordination::text;

// 编译期验证名称函数确实满足 constexpr 契约。
static_assert(encoding_name(Encoding::utf8) == "utf-8");
static_assert(encoding_from_name("utf-16be").has_value());
static_assert(*encoding_from_name("utf-16be") == Encoding::utf16_be);

namespace
{
    int g_failures = 0;

    void check(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL: line " << line << ": " << expression << '\n';
        }
    }

    // 按字节值构造字符串，便于书写含 NUL 或非 ASCII 字节的样本。
    std::string make_bytes(std::initializer_list<unsigned int> values)
    {
        std::string result;
        result.reserve(values.size());
        for (const unsigned int value : values)
        {
            result.push_back(static_cast<char>(value & 0xFFu));
        }
        return result;
    }

    std::string utf16_bytes(std::u16string_view units, bool little_endian)
    {
        std::string result;
        result.reserve(units.size() * 2);
        for (const char16_t unit : units)
        {
            const std::uint32_t value = static_cast<std::uint32_t>(unit);
            if (little_endian)
            {
                result.push_back(static_cast<char>(value & 0xFFu));
                result.push_back(static_cast<char>((value >> 8) & 0xFFu));
            }
            else
            {
                result.push_back(static_cast<char>((value >> 8) & 0xFFu));
                result.push_back(static_cast<char>(value & 0xFFu));
            }
        }
        return result;
    }

    std::string utf32_bytes(std::u32string_view code_points, bool little_endian)
    {
        std::string result;
        result.reserve(code_points.size() * 4);
        for (const char32_t code_point : code_points)
        {
            const std::uint32_t value = static_cast<std::uint32_t>(code_point);
            if (little_endian)
            {
                result.push_back(static_cast<char>(value & 0xFFu));
                result.push_back(static_cast<char>((value >> 8) & 0xFFu));
                result.push_back(static_cast<char>((value >> 16) & 0xFFu));
                result.push_back(static_cast<char>((value >> 24) & 0xFFu));
            }
            else
            {
                result.push_back(static_cast<char>((value >> 24) & 0xFFu));
                result.push_back(static_cast<char>((value >> 16) & 0xFFu));
                result.push_back(static_cast<char>((value >> 8) & 0xFFu));
                result.push_back(static_cast<char>(value & 0xFFu));
            }
        }
        return result;
    }

    // 解析名称并比对，避免在失败路径上调用 optional::value()。
    bool is_named(std::string_view name, Encoding expected)
    {
        const std::optional<Encoding> parsed = encoding_from_name(name);
        return parsed.has_value() && *parsed == expected;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    // ---------------------------------------------------------------
    // 1. 编码名称与解析往返
    // ---------------------------------------------------------------
    CHECK(encoding_name(Encoding::unknown) == "unknown");
    CHECK(encoding_name(Encoding::ascii) == "ascii");
    CHECK(encoding_name(Encoding::utf8) == "utf-8");
    CHECK(encoding_name(Encoding::utf16_le) == "utf-16le");
    CHECK(encoding_name(Encoding::utf16_be) == "utf-16be");
    CHECK(encoding_name(Encoding::utf32_le) == "utf-32le");
    CHECK(encoding_name(Encoding::utf32_be) == "utf-32be");
    CHECK(encoding_name(Encoding::latin1) == "latin1");

    const Encoding all_encodings[] =
    {
        Encoding::unknown,
        Encoding::ascii,
        Encoding::utf8,
        Encoding::utf16_le,
        Encoding::utf16_be,
        Encoding::utf32_le,
        Encoding::utf32_be,
        Encoding::latin1
    };
    for (const Encoding encoding : all_encodings)
    {
        CHECK(is_named(encoding_name(encoding), encoding));
    }

    CHECK(is_named("UTF-8", Encoding::utf8));
    CHECK(is_named("utf8", Encoding::utf8));
    CHECK(is_named("UTF_16_LE", Encoding::utf16_le));
    CHECK(is_named("utf-16-le", Encoding::utf16_le));
    CHECK(is_named("UTF-16BE", Encoding::utf16_be));
    CHECK(is_named("utf16", Encoding::utf16_le));
    CHECK(is_named("utf32", Encoding::utf32_le));
    CHECK(is_named("UTF-32BE", Encoding::utf32_be));
    CHECK(is_named("us-ascii", Encoding::ascii));
    CHECK(is_named("ISO-8859-1", Encoding::latin1));
    CHECK(!encoding_from_name("").has_value());
    CHECK(!encoding_from_name("ebcdic").has_value());
    CHECK(!encoding_from_name("utf-7").has_value());

    // ---------------------------------------------------------------
    // 2. BOM 检测与 bom_length
    // ---------------------------------------------------------------
    Detection detection = detect_encoding(make_bytes({ 0xEF, 0xBB, 0xBF }) + "Hello");
    CHECK(detection.encoding == Encoding::utf8);
    CHECK(detection.bom_length == 3u);
    CHECK(detection.confidence == 1.0);

    detection = detect_encoding(make_bytes({ 0xFF, 0xFE }) + utf16_bytes(u"Hi", true));
    CHECK(detection.encoding == Encoding::utf16_le);
    CHECK(detection.bom_length == 2u);
    CHECK(detection.confidence == 1.0);

    detection = detect_encoding(make_bytes({ 0xFE, 0xFF }) + utf16_bytes(u"Hi", false));
    CHECK(detection.encoding == Encoding::utf16_be);
    CHECK(detection.bom_length == 2u);

    detection = detect_encoding(make_bytes({ 0xFF, 0xFE, 0x00, 0x00 }) + utf32_bytes(U"Hi", true));
    CHECK(detection.encoding == Encoding::utf32_le);
    CHECK(detection.bom_length == 4u);

    detection = detect_encoding(make_bytes({ 0x00, 0x00, 0xFE, 0xFF }) + utf32_bytes(U"Hi", false));
    CHECK(detection.encoding == Encoding::utf32_be);
    CHECK(detection.bom_length == 4u);

    // BOM 优先于内容：只有 BOM 加纯 ASCII 内容时仍判为 Unicode。
    detection = detect_encoding(make_bytes({ 0xEF, 0xBB, 0xBF }));
    CHECK(detection.encoding == Encoding::utf8);
    CHECK(detection.bom_length == 3u);

    // UTF-16LE BOM + 无 NUL 的 CJK 内容，只能靠 BOM 判定。
    const std::string utf16le_chinese = make_bytes({ 0xFF, 0xFE, 0x2D, 0x4E, 0x87, 0x65 });
    detection = detect_encoding(utf16le_chinese);
    CHECK(detection.encoding == Encoding::utf16_le);
    CHECK(detection.bom_length == 2u);
    CHECK(to_utf8(utf16le_chinese, Encoding::utf16_le) == "\xE4\xB8\xAD\xE6\x96\x87");

    // ---------------------------------------------------------------
    // 3. 无 BOM 的启发式检测
    // ---------------------------------------------------------------
    const std::string sample = "Hello, \xE4\xB8\x96\xE7\x95\x8C! \xF0\x9F\x98\x80";

    // 以多字节序列为主的 UTF-8。
    detection = detect_encoding(sample);
    CHECK(detection.encoding == Encoding::utf8);
    CHECK(detection.bom_length == 0u);
    CHECK(detection.confidence > 0.9);
    CHECK(detection.confidence < 1.0);

    // 纯 ASCII。
    detection = detect_encoding("plain ascii text\n");
    CHECK(detection.encoding == Encoding::ascii);
    CHECK(detection.confidence == 1.0);
    CHECK(detection.bom_length == 0u);

    // 空输入视为纯 ASCII。
    detection = detect_encoding(std::string_view());
    CHECK(detection.encoding == Encoding::ascii);
    CHECK(detection.confidence == 1.0);

    // UTF-16LE：ASCII 内容产生奇数位 NUL。
    const std::string utf16le_hello = make_bytes({ 0x48, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F, 0x00 });
    detection = detect_encoding(utf16le_hello);
    CHECK(detection.encoding == Encoding::utf16_le);
    CHECK(detection.bom_length == 0u);
    CHECK(detection.confidence > 0.5);
    CHECK(detection.confidence < 1.0);

    // UTF-16BE：ASCII 内容产生偶数位 NUL。
    const std::string utf16be_hello = make_bytes({ 0x00, 0x48, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F });
    detection = detect_encoding(utf16be_hello);
    CHECK(detection.encoding == Encoding::utf16_be);
    CHECK(detection.confidence > 0.5);
    CHECK(detection.confidence < 1.0);

    // UTF-32LE：后三个字节为 0 的 4 字节单元。
    const std::string utf32le_hi = make_bytes({ 0x48, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00 });
    detection = detect_encoding(utf32le_hi);
    CHECK(detection.encoding == Encoding::utf32_le);
    CHECK(detection.confidence > 0.5);
    CHECK(detection.confidence < 1.0);

    // UTF-32BE：前三字节为 0 的 4 字节单元。
    const std::string utf32be_hi = make_bytes({ 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x69 });
    detection = detect_encoding(utf32be_hi);
    CHECK(detection.encoding == Encoding::utf32_be);
    CHECK(detection.confidence > 0.5);
    CHECK(detection.confidence < 1.0);

    // latin1：非法 UTF-8 且没有 NUL，只能低置信兜底。
    const std::string latin1_hello = make_bytes({ 0x48, 0xE9, 0x6C, 0x6C, 0x6F });
    detection = detect_encoding(latin1_hello);
    CHECK(detection.encoding == Encoding::latin1);
    CHECK(detection.confidence < 0.5);
    CHECK(detection.bom_length == 0u);

    // ---------------------------------------------------------------
    // 4. UTF-8 <-> UTF-16 <-> UTF-32 往返
    // ---------------------------------------------------------------
    const Encoding unicode_targets[] =
    {
        Encoding::utf8,
        Encoding::utf16_le,
        Encoding::utf16_be,
        Encoding::utf32_le,
        Encoding::utf32_be
    };
    for (const Encoding target : unicode_targets)
    {
        const std::string encoded = from_utf8(sample, target);
        CHECK(to_utf8(encoded, target) == sample);
    }

    CHECK(from_utf8("A", Encoding::utf16_le) == make_bytes({ 0x41, 0x00 }));
    CHECK(from_utf8("A", Encoding::utf16_be) == make_bytes({ 0x00, 0x41 }));
    CHECK(from_utf8("A", Encoding::utf32_le) == make_bytes({ 0x41, 0x00, 0x00, 0x00 }));
    CHECK(from_utf8("A", Encoding::utf32_be) == make_bytes({ 0x00, 0x00, 0x00, 0x41 }));
    CHECK(from_utf8("\xE4\xB8\xAD", Encoding::utf16_le) == make_bytes({ 0x2D, 0x4E }));
    CHECK(from_utf8("\xF0\x9F\x98\x80", Encoding::utf16_le) == make_bytes({ 0x3D, 0xD8, 0x00, 0xDE }));

    // convert 直接往返。
    const ConvertResult round_trip = convert(sample, Encoding::utf8, Encoding::utf32_be);
    CHECK(!round_trip.had_errors);
    CHECK(round_trip.bytes == utf32_bytes(U"Hello, \u4E16\u754C! \U0001F600", false));
    CHECK(convert(round_trip.bytes, Encoding::utf32_be, Encoding::utf8).bytes == sample);

    // latin1 与 UTF-8 互转。
    CHECK(to_utf8(latin1_hello, Encoding::latin1) == make_bytes({ 0x48, 0xC3, 0xA9, 0x6C, 0x6C, 0x6F }));
    CHECK(from_utf8(to_utf8(latin1_hello, Encoding::latin1), Encoding::latin1) == latin1_hello);
    CHECK(to_utf8(make_bytes({ 0xE9 }), Encoding::latin1) == "\xC3\xA9");

    // ASCII 直通。
    CHECK(to_utf8("plain ascii", Encoding::ascii) == "plain ascii");
    CHECK(from_utf8("plain ascii", Encoding::ascii) == "plain ascii");

    // ---------------------------------------------------------------
    // 5. BOM 剥离与输出不带 BOM
    // ---------------------------------------------------------------
    CHECK(to_utf8(make_bytes({ 0xEF, 0xBB, 0xBF }) + "Hi", Encoding::utf8) == "Hi");
    CHECK(to_utf8(make_bytes({ 0xFF, 0xFE }) + utf16_bytes(u"Hi", true), Encoding::utf16_le) == "Hi");
    CHECK(to_utf8(make_bytes({ 0xFE, 0xFF }) + utf16_bytes(u"Hi", false), Encoding::utf16_be) == "Hi");
    CHECK(to_utf8(make_bytes({ 0xFF, 0xFE, 0x00, 0x00 }) + utf32_bytes(U"Hi", true), Encoding::utf32_le) == "Hi");
    CHECK(to_utf8(make_bytes({ 0x00, 0x00, 0xFE, 0xFF }) + utf32_bytes(U"Hi", false), Encoding::utf32_be) == "Hi");
    CHECK(from_utf8("Hi", Encoding::utf16_le) == make_bytes({ 0x48, 0x00, 0x69, 0x00 }));

    // ---------------------------------------------------------------
    // 6. 非法序列：strict 停止、非 strict 替换
    // ---------------------------------------------------------------
    ConvertOptions strict_options;
    strict_options.strict = true;

    ConvertOptions star_options;
    star_options.replacement = '*';

    const std::string bad_utf8 = make_bytes({ 0x41, 0xFF, 0x42 });
    ConvertResult converted = convert(bad_utf8, Encoding::utf8, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "A?B");

    converted = convert(bad_utf8, Encoding::utf8, Encoding::utf8, strict_options);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 0u);
    CHECK(converted.bytes == "A");

    converted = convert(bad_utf8, Encoding::utf8, Encoding::utf8, star_options);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "A*B");

    // 孤立的 UTF-8 续字节。
    converted = convert(make_bytes({ 0x80 }), Encoding::utf8, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // 截断的 UTF-8 序列（E4 B8 缺第三字节）：两个字节各自失败。
    converted = convert(make_bytes({ 0xE4, 0xB8 }), Encoding::utf8, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 2u);
    CHECK(converted.bytes == "??");

    // 目标编码不可表示：UTF-8 中文无法写入 ASCII。
    converted = convert("\xE4\xB8\xAD", Encoding::utf8, Encoding::ascii);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // 目标编码不可表示：latin1 的 é 无法写入 ASCII。
    converted = convert(make_bytes({ 0xE9 }), Encoding::latin1, Encoding::ascii);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    converted = convert(make_bytes({ 0xE9 }), Encoding::latin1, Encoding::ascii, strict_options);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 0u);
    CHECK(converted.bytes.empty());

    // 孤立的 UTF-16 高代理。
    converted = convert(make_bytes({ 0x00, 0xD8 }), Encoding::utf16_le, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // UTF-16 尾部残留的单字节。
    converted = convert(make_bytes({ 0x41 }), Encoding::utf16_le, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // 合法的 UTF-16 代理对仍能正常解码为 UTF-8。
    converted = convert(make_bytes({ 0x3D, 0xD8, 0x00, 0xDE }), Encoding::utf16_le, Encoding::utf8);
    CHECK(!converted.had_errors);
    CHECK(converted.replaced == 0u);
    CHECK(converted.bytes == "\xF0\x9F\x98\x80");

    // 越界的 UTF-32 码点（U+110000）。
    converted = convert(make_bytes({ 0x00, 0x00, 0x11, 0x00 }), Encoding::utf32_le, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // 截断的 UTF-32 单元。
    converted = convert(make_bytes({ 0x41, 0x00 }), Encoding::utf32_le, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.replaced == 1u);
    CHECK(converted.bytes == "?");

    // unknown 编码不产生输出。
    converted = convert("x", Encoding::unknown, Encoding::utf8);
    CHECK(converted.had_errors);
    CHECK(converted.bytes.empty());

    // ---------------------------------------------------------------
    // 7. 换行符规范化
    // ---------------------------------------------------------------
    CHECK(normalize_newlines("a\r\nb\rc\nd") == "a\nb\nc\nd");
    CHECK(normalize_newlines("a\r\nb\rc\nd", false) == "a\r\nb\r\nc\r\nd");
    CHECK(normalize_newlines("a\nb") == "a\nb");
    CHECK(normalize_newlines("a\nb", false) == "a\r\nb");
    CHECK(normalize_newlines("") == "");
    CHECK(normalize_newlines("no newline") == "no newline");
    CHECK(normalize_newlines("\r\n\r\n") == "\n\n");
    CHECK(normalize_newlines("\r\r\n") == "\n\n");
    CHECK(normalize_newlines("\r\r\n", false) == "\r\n\r\n");
    CHECK(normalize_newlines("\n", false) == "\r\n");

    // ---------------------------------------------------------------
    // 汇总
    // ---------------------------------------------------------------
    if (g_failures == 0)
    {
        std::cout << "PASS: encoding\n";
        return 0;
    }

    std::cout << "FAIL: encoding (" << g_failures << " failed checks)\n";
    return 1;
}
