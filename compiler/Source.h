#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Span.h"

namespace coordination::compiler
{
    // 单个源文件：拥有文本，并缓存每一行的起始字节偏移。
    // line_starts 至少含一个元素；空文件也有一行（起始偏移 0）。
    struct SourceFile
    {
        SourceId id = kInvalidSource;
        std::string name;
        std::string text;
        std::vector<std::uint32_t> line_starts;   // 每行起始 offset，第一个恒为 0
    };

    // 源文件管理器：按加入顺序分配从 0 递增的 SourceId，并负责行列换算。
    // 所有偏移与列号均按 UTF-8 字节计数（非 Unicode 码点）；文件大小上限为
    // UINT32_MAX 字节，超过时溢出（编译器的 Span 契约本身即 32 位偏移）。
    class SourceManager
    {
    public:
        // 复制文本并返回新 SourceId；id 从 0 开始递增。
        SourceId add_file(std::string name, std::string text)
        {
            SourceFile source;
            source.id = static_cast<SourceId>(files_.size());
            source.name = std::move(name);
            source.text = std::move(text);
            source.line_starts = build_line_starts(source.text);

            files_.push_back(std::move(source));
            return files_.back().id;
        }

        bool contains(SourceId id) const noexcept
        {
            return id < files_.size();
        }

        std::size_t file_count() const noexcept
        {
            return files_.size();
        }

        // 越界抛 std::out_of_range。
        const SourceFile& file(SourceId id) const
        {
            if (!contains(id))
            {
                throw std::out_of_range("SourceManager::file: SourceId out of range");
            }

            return files_[id];
        }

        // 越界抛 std::out_of_range。
        std::string_view text(SourceId id) const
        {
            return file(id).text;
        }

        // 越界抛 std::out_of_range。
        std::string_view name(SourceId id) const
        {
            return file(id).name;
        }

        // 1 基行列；offset 超出文本长度时 clamp 到文本末尾。
        // offset == text.size() 合法，表示文件末尾（EOF）位置。
        LineColumn line_column(SourceId id, std::uint32_t offset) const
        {
            const SourceFile& source = file(id);
            const std::uint32_t size = static_cast<std::uint32_t>(source.text.size());
            const std::uint32_t clamped = offset < size ? offset : size;

            // 第一个大于 clamped 的行首的前一行，就是 clamped 所在行。
            const auto it = std::upper_bound(source.line_starts.begin(), source.line_starts.end(), clamped);
            const std::size_t index = static_cast<std::size_t>(it - source.line_starts.begin()) - 1;

            return LineColumn{
                static_cast<std::uint32_t>(index + 1),
                clamped - source.line_starts[index] + 1};
        }

        // 1 基行号；返回不含行尾换行符（CRLF 会去掉 \r）的行文本。
        // 行号不存在返回空。越界 SourceId 抛 std::out_of_range。
        std::string_view line_text(SourceId id, std::uint32_t line) const
        {
            const SourceFile& source = file(id);
            if (line == 0 || line > source.line_starts.size())
            {
                return {};
            }

            const std::uint32_t begin = source.line_starts[line - 1];
            const std::uint32_t end = line < source.line_starts.size()
                ? source.line_starts[line]
                : static_cast<std::uint32_t>(source.text.size());

            std::string_view result(source.text.data() + begin, static_cast<std::size_t>(end - begin));
            if (!result.empty() && result.back() == '\n')
            {
                result.remove_suffix(1);
            }
            if (!result.empty() && result.back() == '\r')
            {
                result.remove_suffix(1);
            }

            return result;
        }

        // 按 span 取原文；span 无效、source 不在本管理器或偏移越界时返回空。
        // 越界部分会被 clamp 到文本末尾。
        std::string_view slice(Span span) const
        {
            if (!span.valid() || !contains(span.source))
            {
                return {};
            }

            const std::string_view source_text = text(span.source);
            const std::uint32_t size = static_cast<std::uint32_t>(source_text.size());
            const std::uint32_t begin = span.begin > size ? size : span.begin;
            const std::uint32_t end = span.end > size ? size : span.end;
            if (end <= begin)
            {
                return {};
            }

            return source_text.substr(begin, static_cast<std::size_t>(end - begin));
        }

    private:
        // 按 '\n' 划分行；"\r\n" 的 '\r' 留在行内，由 line_text 负责去掉。
        // 文本以 '\n' 结尾时额外产生一个空的末行，因此 EOF 落在该空行第 1 列。
        static std::vector<std::uint32_t> build_line_starts(const std::string& text)
        {
            std::vector<std::uint32_t> starts;
            starts.push_back(0);

            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\n')
                {
                    starts.push_back(static_cast<std::uint32_t>(index + 1));
                }
            }

            return starts;
        }

        std::vector<SourceFile> files_;
    };
}
