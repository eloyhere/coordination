#pragma once

// ============================================================================
// 最小端到端 driver：源文本 -> Lexer -> Parser -> AST + 诊断。
//
// 供 CLI 与 LSP 复用；本文件只做流程编排与结果打包，不实现词法/语法细节：
//   * 读取文件失败（E3001）与词法（E1xxx）/语法（E2xxx）错误都只进 diagnostics；
//   * Parser 的构造函数内部会完成唯一一次词法分析并把 token 保存在成员里，
//     所以这里直接构造 Parser，不再单独调用 Lexer（同一份源码只切一次）；
//   * 不依赖平台 API（只用 std::ifstream），也不抛异常。
// ============================================================================

#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Ast.h"
#include "Diagnostic.h"
#include "DiagnosticPrinter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Source.h"

namespace coordination::compiler
{
    // 一次前端运行的完整结果。
    //
    // 生命周期约定：
    //   * module 指向 arena 内部的节点，裸指针仅在 arena 存活期间有效；
    //   * arena 用 unique_ptr 持有（AstArena 不可拷贝），因此本结构可移动、不可拷贝；
    //     移动后 arena 的所有权转移，module 指向的节点地址不变（节点由 unique_ptr 持有，
    //     不随 arena 容器移动）；
    //   * sources 保存本次运行的源文件，renderDiagnostics 与后续 LSP 的行列换算依赖它。
    struct FrontendResult
    {
        SourceId source = kInvalidSource;
        std::string sourceText;                  // 源文件内容（与 sources 中保存的文本一致）
        std::unique_ptr<AstArena> arena;         // 必须用 unique_ptr：AstArena 不可拷贝
        AstNode* module = nullptr;               // 根节点（Module）
        DiagnosticBag diagnostics;
        SourceManager sources;                   // 源文件表：诊断定位与行列换算

        FrontendResult() = default;
        FrontendResult(const FrontendResult&) = delete;
        FrontendResult& operator=(const FrontendResult&) = delete;
        FrontendResult(FrontendResult&&) noexcept = default;
        FrontendResult& operator=(FrontendResult&&) noexcept = default;

        bool ok() const noexcept
        {
            return module != nullptr && !diagnostics.has_errors();
        }
    };

    namespace driver_detail
    {
        // 以二进制方式读入整个文件；失败返回 false，不抛异常。
        // 路径按字节原样交给 std::ifstream，不使用任何平台 API。
        inline bool read_file(std::string_view path, std::string& text)
        {
            std::ifstream stream(std::string(path), std::ios::binary);
            if (!stream.is_open())
            {
                return false;
            }

            std::string content;
            char buffer[4096];
            while (stream.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || stream.gcount() > 0)
            {
                content.append(buffer, static_cast<std::size_t>(stream.gcount()));
            }

            // 读满整个文件后 read() 会置位 eofbit/failbit，这是正常收尾；只有 badbit 才是真失败。
            if (stream.bad())
            {
                return false;
            }

            text = std::move(content);
            return true;
        }
    }

    // 从内存文本运行前端：词法、语法诊断都进入 result.diagnostics，恒有 Module 根节点。
    inline FrontendResult runFrontendFromText(std::string_view displayName, std::string_view text)
    {
        FrontendResult result;
        result.source = result.sources.add_file(std::string(displayName), std::string(text));
        result.sourceText = result.sources.file(result.source).text;
        result.arena = std::make_unique<AstArena>();

        // Parser::Parser 内部调用 Lexer::tokenize() 且只调用一次；这里把源文本交给它，
        // 词法诊断与语法诊断共用同一个 DiagnosticBag。sources 之后不再增删文件，
        // 因此交给 Parser 的 string_view 在整个解析期间保持有效。
        Parser parser(result.source, result.sources.text(result.source), result.diagnostics);
        result.module = parser.parse(*result.arena);
        return result;
    }

    // 读取 path 并运行前端；文件无法读取时产生 E3001（span 无效），不抛异常。
    inline FrontendResult runFrontend(std::string_view path)
    {
        std::string text;
        if (!driver_detail::read_file(path, text))
        {
            FrontendResult result;
            std::string message = "无法读取源文件: ";
            message.append(path.data(), path.size());
            result.diagnostics.error("E3001", std::move(message), Span{});
            return result;
        }

        return runFrontendFromText(path, text);
    }

    // 用 DiagnosticPrinter 渲染全部诊断；无诊断时返回空串。
    inline std::string renderDiagnostics(const FrontendResult& result)
    {
        if (result.sources.contains(result.source))
        {
            return format_diagnostics(result.diagnostics.diagnostics(), result.sources);
        }

        // 没有对应源文件（例如 E3001）：用已有文本建一份临时表；span 无效时
        // 打印机会退化为 "<unknown>"，不会解引用越界。
        SourceManager fallback;
        fallback.add_file("<unknown>", result.sourceText);
        return format_diagnostics(result.diagnostics.diagnostics(), fallback);
    }

    // 把 AST 序列化为 S 表达式；没有语法树时返回空串。
    inline std::string dumpAst(const FrontendResult& result)
    {
        if (!result.arena || result.module == nullptr)
        {
            return std::string();
        }

        return dump(*result.module);
    }
}
