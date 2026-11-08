#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "SyntaxKind.h"
#include "SyntaxNode.h"
#include "SyntaxSpan.h"

namespace coordination::syntax
{
    // 词法单元（叶子节点）：保留原文，供语法树打印和错误信息使用。
    class SyntaxToken final : public SyntaxNode
    {
    public:
        SyntaxToken(SyntaxKind kind, std::string source_text, SyntaxSpan span = {})
            : SyntaxNode(kind, span)
            , text_(std::move(source_text))
        {
        }

        std::string_view text() const noexcept override
        {
            return text_;
        }

    private:
        std::string text_;
    };
}
