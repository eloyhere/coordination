#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostic.h"
#include "Source.h"

namespace coordination::compiler
{
    struct PrintOptions
    {
        bool show_source_line = true;
        bool show_code = true;      // 是否输出 [E0001]
    };

    // 诊断打印的实现细节。单独放一个命名空间，避免与其他模块的 detail 冲突。
    namespace diagnostic_printer_detail
    {
        inline const char* severity_name(Severity severity) noexcept
        {
            switch (severity)
            {
            case Severity::error:
                return "error";
            case Severity::warning:
                return "warning";
            case Severity::note:
                return "note";
            }

            return "error";
        }

        inline const SourceFile* find_file(const SourceManager& sources, SourceId id)
        {
            if (!sources.contains(id))
            {
                return nullptr;
            }

            return &sources.file(id);
        }

        inline std::string_view display_name(const SourceFile& file)
        {
            return file.name.empty() ? std::string_view("<unknown>") : std::string_view(file.name);
        }

        // 把 span 格式化为 "path:line:col"；无法定位时退化为 "path" 或 "<unknown>"。
        // 只要 span 无效、source 不存在，或 [begin, end] 越出文本，就不再给行列。
        inline std::string format_location(const SourceManager& sources, Span span)
        {
            if (!span.valid())
            {
                return "<unknown>";
            }

            const SourceFile* source = find_file(sources, span.source);
            if (source == nullptr)
            {
                return "<unknown>";
            }

            std::string location;
            location += display_name(*source);

            const std::uint32_t size = static_cast<std::uint32_t>(source->text.size());
            if (span.begin > size || span.end > size)
            {
                return location;
            }

            const LineColumn position = sources.line_column(span.source, span.begin);
            location += ':';
            location += std::to_string(position.line);
            location += ':';
            location += std::to_string(position.column);
            return location;
        }

        // 在诊断头部之后追加源码行与 "^~~~" 下划线；无法定位时什么都不输出。
        // 限制：列号按 UTF-8 字节计算，多字节字符后的下划线可能与视觉位置错开；
        // 制表符按原样输出，未做展开。
        inline void append_source_line(std::string& output, const SourceManager& sources, Span span)
        {
            if (!span.valid())
            {
                return;
            }

            const SourceFile* source = find_file(sources, span.source);
            if (source == nullptr)
            {
                return;
            }

            const std::uint32_t size = static_cast<std::uint32_t>(source->text.size());
            if (span.begin > size || span.end > size)
            {
                return;
            }

            const LineColumn position = sources.line_column(span.source, span.begin);
            const std::string_view line = sources.line_text(span.source, position.line);
            const std::uint32_t line_begin = source->line_starts[position.line - 1];
            const std::uint32_t line_end = line_begin + static_cast<std::uint32_t>(line.size());

            // 下划线不跨行：超出本行的部分截断到行尾；空 span 至少画一个 '^'。
            const std::uint32_t highlight_begin = span.begin;
            std::uint32_t highlight_end = span.end < line_end ? span.end : line_end;
            if (highlight_end <= highlight_begin)
            {
                highlight_end = highlight_begin + 1;
            }

            const std::size_t indent = static_cast<std::size_t>(highlight_begin - line_begin);

            output += "    ";
            output.append(line);
            output += '\n';
            output += "    ";
            output.append(indent, ' ');
            output += '^';
            output.append(static_cast<std::size_t>(highlight_end - highlight_begin - 1), '~');
            output += '\n';
        }
    }

    // 单条诊断的文本形式（不以颜色标记结尾，但每条以 '\n' 结束）：
    //   path:line:col: error[E0001]: message
    //       <源码行>
    //       ^~~~~
    //     note: path:line:col
    // 无法定位主 span 时退化为一行的 "path: error[E0001]: message"。
    inline std::string format_diagnostic(const Diagnostic& diagnostic, const SourceManager& sources, PrintOptions options = {})
    {
        std::string output;
        output += diagnostic_printer_detail::format_location(sources, diagnostic.span);
        output += ": ";
        output += diagnostic_printer_detail::severity_name(diagnostic.severity);

        if (options.show_code && !diagnostic.code.empty())
        {
            output += '[';
            output += diagnostic.code;
            output += ']';
        }

        output += ": ";
        output += diagnostic.message;
        output += '\n';

        if (options.show_source_line)
        {
            diagnostic_printer_detail::append_source_line(output, sources, diagnostic.span);
        }

        // notes 按给定顺序逐行追加；无法定位的 note 打印 "<unknown>"。
        for (const Span& note : diagnostic.notes)
        {
            output += "  note: ";
            output += diagnostic_printer_detail::format_location(sources, note);
            output += '\n';
        }

        return output;
    }

    // 按输入顺序拼接多条诊断；空列表返回空串。
    inline std::string format_diagnostics(const std::vector<Diagnostic>& diagnostics, const SourceManager& sources, PrintOptions options = {})
    {
        std::string output;
        for (const Diagnostic& diagnostic : diagnostics)
        {
            output += format_diagnostic(diagnostic, sources, options);
        }

        return output;
    }
}
