#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Span.h"

namespace coordination::compiler
{
    enum class Severity
    {
        error,
        warning,
        note,
    };

    // 单条结构化诊断。错误码从第一天起稳定，工具（LSP/CI）依赖它。
    struct Diagnostic
    {
        Severity severity = Severity::error;
        std::string code;      // 例如 "E0001"，空表示无错误码
        std::string message;   // 中文或英文均可，但同一错误码文案风格保持一致
        Span span{};
        std::vector<Span> notes; // 附注位置（例如"模板实例化自这里"）
    };

    // 诊断收集器：只存数据，不负责格式化（格式化见 DiagnosticPrinter.h）。
    class DiagnosticBag
    {
    public:
        void add(Severity severity, std::string code, std::string message, Span span)
        {
            diagnostics_.push_back(Diagnostic{severity, std::move(code), std::move(message), span, {}});
        }

        void error(std::string code, std::string message, Span span)
        {
            add(Severity::error, std::move(code), std::move(message), span);
        }

        void warning(std::string code, std::string message, Span span)
        {
            add(Severity::warning, std::move(code), std::move(message), span);
        }

        const std::vector<Diagnostic>& diagnostics() const noexcept
        {
            return diagnostics_;
        }

        std::size_t error_count() const noexcept
        {
            std::size_t count = 0;
            for (const Diagnostic& diagnostic : diagnostics_)
            {
                if (diagnostic.severity == Severity::error)
                {
                    ++count;
                }
            }
            return count;
        }

        std::size_t warning_count() const noexcept
        {
            std::size_t count = 0;
            for (const Diagnostic& diagnostic : diagnostics_)
            {
                if (diagnostic.severity == Severity::warning)
                {
                    ++count;
                }
            }
            return count;
        }

        bool has_errors() const noexcept
        {
            return error_count() != 0;
        }

        void clear() noexcept
        {
            diagnostics_.clear();
        }

    private:
        std::vector<Diagnostic> diagnostics_;
    };
}
