#include "charset/Charset.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    using namespace coordination::charset;

    // ---------------------------------------------------------------
    // 1. 标量值与代理区
    // ---------------------------------------------------------------
    CHECK(is_scalar_value(0u));
    CHECK(is_scalar_value(0xD7FFu));
    CHECK(!is_scalar_value(0xD800u));
    CHECK(!is_scalar_value(0xDFFFu));
    CHECK(is_scalar_value(0xE000u));
    CHECK(is_scalar_value(0x10FFFFu));
    CHECK(!is_scalar_value(0x110000u));
    CHECK(!is_surrogate(0x0041u));
    CHECK(is_surrogate(0xD800u));
    CHECK(is_surrogate(0xDBFFu));
    CHECK(is_surrogate(0xDC00u));
    CHECK(is_surrogate(0xDFFFu));
    CHECK(!is_surrogate(0xE000u));

    // ---------------------------------------------------------------
    // 2. UTF-8 边界值与 1..4 字节往返
    // ---------------------------------------------------------------
    struct Utf8Boundary
    {
        CodePoint value;
        const char* bytes;
        size_t length;
    };

    const Utf8Boundary utf8_boundaries[] =
    {
        { static_cast<CodePoint>(0x0000u), "\x00", 1 },
        { static_cast<CodePoint>(0x007Fu), "\x7F", 1 },
        { static_cast<CodePoint>(0x0080u), "\xC2\x80", 2 },
        { static_cast<CodePoint>(0x07FFu), "\xDF\xBF", 2 },
        { static_cast<CodePoint>(0x0800u), "\xE0\xA0\x80", 3 },
        { static_cast<CodePoint>(0x4E2Du), "\xE4\xB8\xAD", 3 },
        { static_cast<CodePoint>(0xFFFFu), "\xEF\xBF\xBF", 3 },
        { static_cast<CodePoint>(0x10000u), "\xF0\x90\x80\x80", 4 },
        { static_cast<CodePoint>(0x1F600u), "\xF0\x9F\x98\x80", 4 },
        { static_cast<CodePoint>(0x10FFFFu), "\xF4\x8F\xBF\xBF", 4 }
    };

    for (const Utf8Boundary& boundary : utf8_boundaries)
    {
        const std::string expected(boundary.bytes, boundary.length);
        CHECK(encode_utf8(boundary.value) == expected);

        char buffer[4]{};
        const size_t encoded_length = encode_utf8(boundary.value, buffer);
        CHECK(encoded_length == boundary.length);
        CHECK(std::string(buffer, encoded_length) == expected);

        const DecodeResult decoded = decode_utf8(expected, 0);
        CHECK(decoded.valid);
        CHECK(decoded.code_point == boundary.value);
        CHECK(decoded.length == boundary.length);
    }

    // 带 offset 的解码。
    const DecodeResult offset_decoded = decode_utf8("A\xC3\xA9", 1);
    CHECK(offset_decoded.valid);
    CHECK(offset_decoded.code_point == static_cast<CodePoint>(0x00E9u));
    CHECK(offset_decoded.length == 2);
    const DecodeResult out_of_range = decode_utf8("ABC", 3);
    CHECK(!out_of_range.valid);
    CHECK(out_of_range.length == 0);

    // 非法编码：overlong、代理区、超范围、非法首/续字节、截断。
    const std::string invalid_utf8[] =
    {
        std::string("\x80", 1),
        std::string("\xC0\x80", 2),
        std::string("\xC1\xBF", 2),
        std::string("\xE0\x80\x80", 3),
        std::string("\xE0\x9F\xBF", 3),
        std::string("\xED\xA0\x80", 3), // UTF-16 高代理 U+D800
        std::string("\xED\xBF\xBF", 3), // UTF-16 低代理 U+DFFF
        std::string("\xF0\x80\x80\x80", 4),
        std::string("\xF4\x90\x80\x80", 4), // U+110000
        std::string("\xF5\x80\x80\x80", 4),
        std::string("\xE2\x82", 2),         // 截断的三字节序列
        std::string("\xF0\x9F\x98", 3),     // 截断的四字节序列
        std::string("\xE2\x28\xA1", 3)      // 非法续字节
    };

    for (const std::string& invalid : invalid_utf8)
    {
        const DecodeResult result = decode_utf8(invalid, 0);
        CHECK(!result.valid);
        CHECK(result.length == 1);
    }

    // 编码非法码点。
    CHECK(encode_utf8(static_cast<CodePoint>(0xD800u)).empty());
    CHECK(encode_utf8(static_cast<CodePoint>(0xDFFFu)).empty());
    CHECK(encode_utf8(static_cast<CodePoint>(0x110000u)).empty());
    char invalid_buffer[4]{};
    CHECK(encode_utf8(static_cast<CodePoint>(0x110000u), invalid_buffer) == 0);

    // ---------------------------------------------------------------
    // 3. utf8_length / to_code_points / from_code_points
    // ---------------------------------------------------------------
    CHECK(utf8_length("") == 0);
    CHECK(utf8_length("hello") == 5);
    CHECK(utf8_length("A\xE4\xB8\xAD") == 2);
    CHECK(utf8_length("\xF0\x9F\x98\x80") == 1);
    CHECK(utf8_length(std::string_view("\xFF", 1)) == 1);

    const std::vector<CodePoint> mixed_code_points =
        to_code_points("A\xE4\xB8\xAD\xF0\x9F\x98\x80");
    const std::vector<CodePoint> expected_code_points =
    {
        static_cast<CodePoint>(0x0041u),
        static_cast<CodePoint>(0x4E2Du),
        static_cast<CodePoint>(0x1F600u)
    };
    CHECK(mixed_code_points == expected_code_points);
    CHECK(to_code_points("").empty());

    const std::vector<CodePoint> invalid_code_points = to_code_points(std::string_view("\xFF", 1));
    CHECK(invalid_code_points.size() == 1);
    CHECK(invalid_code_points[0] == static_cast<CodePoint>(0xFFFDu));

    CHECK(from_code_points({}) == "");
    CHECK(from_code_points(expected_code_points) == "A\xE4\xB8\xAD\xF0\x9F\x98\x80");
    CHECK(from_code_points({ static_cast<CodePoint>(0xD800u),
                             static_cast<CodePoint>(0x0041u) }) == "A");

    // ---------------------------------------------------------------
    // 4. UTF-16 编解码、代理对与孤立代理
    // ---------------------------------------------------------------
    char16_t utf16_buffer[2]{};

    CHECK(encode_utf16(static_cast<CodePoint>(0x0041u), utf16_buffer) == 1);
    CHECK(utf16_buffer[0] == static_cast<char16_t>(0x0041u));

    CHECK(encode_utf16(static_cast<CodePoint>(0x4E2Du), utf16_buffer) == 1);
    CHECK(utf16_buffer[0] == static_cast<char16_t>(0x4E2Du));

    CHECK(encode_utf16(static_cast<CodePoint>(0x10000u), utf16_buffer) == 2);
    CHECK(utf16_buffer[0] == static_cast<char16_t>(0xD800u));
    CHECK(utf16_buffer[1] == static_cast<char16_t>(0xDC00u));

    CHECK(encode_utf16(static_cast<CodePoint>(0x1F600u), utf16_buffer) == 2);
    CHECK(utf16_buffer[0] == static_cast<char16_t>(0xD83Du));
    CHECK(utf16_buffer[1] == static_cast<char16_t>(0xDE00u));

    CHECK(encode_utf16(static_cast<CodePoint>(0x10FFFFu), utf16_buffer) == 2);
    CHECK(utf16_buffer[0] == static_cast<char16_t>(0xDBFFu));
    CHECK(utf16_buffer[1] == static_cast<char16_t>(0xDFFFu));

    CHECK(encode_utf16(static_cast<CodePoint>(0xD800u), utf16_buffer) == 0);
    CHECK(encode_utf16(static_cast<CodePoint>(0x110000u), utf16_buffer) == 0);

    const std::u16string surrogate_pair =
    {
        static_cast<char16_t>(0xD83D),
        static_cast<char16_t>(0xDE00)
    };
    const DecodeResult pair_result = decode_utf16(surrogate_pair, 0);
    CHECK(pair_result.valid);
    CHECK(pair_result.code_point == static_cast<CodePoint>(0x1F600u));
    CHECK(pair_result.length == 2);

    const std::u16string lone_high(1, static_cast<char16_t>(0xD800));
    const std::u16string lone_low(1, static_cast<char16_t>(0xDC00));
    const std::u16string high_then_letter =
    {
        static_cast<char16_t>(0xD800),
        static_cast<char16_t>(0x0041)
    };
    const DecodeResult lone_high_result = decode_utf16(lone_high, 0);
    const DecodeResult lone_low_result = decode_utf16(lone_low, 0);
    const DecodeResult high_then_letter_result = decode_utf16(high_then_letter, 0);
    CHECK(!lone_high_result.valid);
    CHECK(lone_high_result.length == 1);
    CHECK(!lone_low_result.valid);
    CHECK(lone_low_result.length == 1);
    CHECK(!high_then_letter_result.valid);
    CHECK(high_then_letter_result.length == 1);

    const DecodeResult utf16_out_of_range = decode_utf16(u"", 0);
    CHECK(!utf16_out_of_range.valid);
    CHECK(utf16_out_of_range.length == 0);

    // BMP 边界与偏移。
    const DecodeResult utf16_bmp = decode_utf16(u"A\xFFFF", 1);
    CHECK(utf16_bmp.valid);
    CHECK(utf16_bmp.code_point == static_cast<CodePoint>(0xFFFFu));
    CHECK(utf16_bmp.length == 1);

    // ---------------------------------------------------------------
    // 5. 分类表
    // ---------------------------------------------------------------
    CHECK(is_letter(U'A'));
    CHECK(is_letter(U'Z'));
    CHECK(is_letter(U'a'));
    CHECK(is_letter(U'z'));
    CHECK(!is_letter(U'1'));
    CHECK(!is_letter(U'@'));
    CHECK(is_letter(static_cast<CodePoint>(0x00C9u))); // É
    CHECK(!is_letter(static_cast<CodePoint>(0x00D7u))); // ×
    CHECK(is_letter(static_cast<CodePoint>(0x03B1u))); // α
    CHECK(is_letter(static_cast<CodePoint>(0x0416u))); // Ж
    CHECK(is_letter(static_cast<CodePoint>(0x4E2Du))); // 中
    CHECK(!is_letter(static_cast<CodePoint>(0x20ACu))); // €

    CHECK(is_digit(U'0'));
    CHECK(is_digit(U'9'));
    CHECK(!is_digit(U'A'));
    CHECK(!is_digit(static_cast<CodePoint>(0x00B2u)));

    CHECK(is_whitespace(U' '));
    CHECK(is_whitespace(U'\t'));
    CHECK(is_whitespace(U'\n'));
    CHECK(is_whitespace(U'\r'));
    CHECK(!is_whitespace(U'A'));
    CHECK(is_whitespace(static_cast<CodePoint>(0x00A0u))); // NBSP
    CHECK(is_whitespace(static_cast<CodePoint>(0x3000u))); // 全角空格
    CHECK(!is_whitespace(static_cast<CodePoint>(0x200Bu))); // 零宽空格

    CHECK(is_identifier_start(U'_'));
    CHECK(is_identifier_start(U'A'));
    CHECK(is_identifier_start(static_cast<CodePoint>(0x03B1u)));
    CHECK(is_identifier_start(static_cast<CodePoint>(0x4E2Du)));
    CHECK(!is_identifier_start(U'1'));
    CHECK(!is_identifier_start(U'-'));
    CHECK(is_identifier_continue(U'_'));
    CHECK(is_identifier_continue(U'9'));
    CHECK(is_identifier_continue(static_cast<CodePoint>(0x4E2Du)));
    CHECK(!is_identifier_continue(U'-'));
    CHECK(!is_identifier_start(static_cast<CodePoint>(0xD800u)));

    // ---------------------------------------------------------------
    // 6. BOM
    // ---------------------------------------------------------------
    CHECK(bom_length("") == 0);
    CHECK(bom_length("hello") == 0);
    CHECK(bom_length("\xEF\xBB\xBFhello") == 3);
    CHECK(strip_bom("\xEF\xBB\xBFhello") == "hello");
    CHECK(bom_length("\xEF\xBB\xBF") == 3);
    CHECK(strip_bom("\xEF\xBB\xBF").empty());

    const std::string_view utf16be_bom("\xFE\xFF" "\x00" "A", 4);
    CHECK(bom_length(utf16be_bom) == 2); // UTF-16BE
    CHECK(strip_bom(utf16be_bom) == std::string_view("\x00" "A", 2));

    const std::string_view utf16le_bom("\xFF\xFE" "A\x00", 4);
    CHECK(bom_length(utf16le_bom) == 2); // UTF-16LE
    CHECK(strip_bom(utf16le_bom) == std::string_view("A\x00", 2));

    const std::string_view utf32be_bom("\x00\x00\xFE\xFF" "A\x00\x00\x00", 8);
    const std::string_view utf32le_bom("\xFF\xFE\x00\x00" "A\x00\x00\x00", 8);
    CHECK(bom_length(utf32be_bom) == 4); // UTF-32BE
    CHECK(bom_length(utf32le_bom) == 4); // UTF-32LE

    // ---------------------------------------------------------------
    // 7. 汇总
    // ---------------------------------------------------------------
    if (g_failures == 0)
    {
        std::cout << "PASS: charset\n";
        return 0;
    }

    std::cout << "FAIL: charset (" << g_failures << " failure(s))\n";
    return 1;
}
