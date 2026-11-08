#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "SyntaxKind.h"
#include "SyntaxNode.h"

namespace coordination::syntax
{
    namespace detail
    {
        inline void write_s_expression(const SyntaxNode& node, std::string& out)
        {
            out.push_back('(');
            out.append(kind_name(node.kind()));

            const std::string_view node_text = node.text();
            if (!node_text.empty())
            {
                out.push_back(' ');
                out.append(node_text);
            }

            for (std::size_t i = 0; i < node.child_count(); ++i)
            {
                out.push_back(' ');
                write_s_expression(node.child(i), out);
            }

            out.push_back(')');
        }
    }

    // 以 S 表达式打印语法树，用于调试和测试断言。
    // 例：(Program (ExpressionStatement (BinaryExpression + (LiteralExpression 1) (LiteralExpression 2))))
    inline std::string dump(const SyntaxNode& root)
    {
        std::string out;
        out.reserve(64);
        detail::write_s_expression(root, out);
        return out;
    }

    // 缩进打印，便于人眼查看层级。
    inline void dump_tree(const SyntaxNode& node, std::string& out, int depth = 0)
    {
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
        out.append(kind_name(node.kind()));

        const std::string_view node_text = node.text();
        if (!node_text.empty())
        {
            out.push_back(' ');
            out.append(node_text);
        }
        out.push_back('\n');

        for (std::size_t i = 0; i < node.child_count(); ++i)
        {
            dump_tree(node.child(i), out, depth + 1);
        }
    }

    inline std::string dump_tree(const SyntaxNode& root)
    {
        std::string out;
        dump_tree(root, out, 0);
        return out;
    }
}
