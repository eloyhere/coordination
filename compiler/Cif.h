#pragma once

// ============================================================================
// 模块接口文件（CIF：Coordination Interface File）的序列化 / 解析 / 指纹。
//
// 用途：跨模块增量编译只需要读 .cif，不必重新解析被依赖模块的源码：
//   * 编译器把每个模块的公开接口（导出名字 + kind、直接 import、指纹、编译器版本）
//     写成一个确定性的文本文件；
//   * 下游模块重建时比较新旧接口文档（compatible）即可判断缓存是否失效；
//   * 指纹算法与 Dependency.h 的模块指纹完全一致，可与 DependencyCache 交叉校验。
//
// 文本格式（序列化结果确定性：键固定顺序、字段转义后不含空白、行尾 '\n'、无 BOM）：
//   format-version 1
//   compiler-version 0.1.0
//   module math.vector
//   fingerprint 0123456789abcdef
//   export class Vector
//   export function dot
//   import math.scalar
//   generic-body <T> { ... }
//
// 约定：
//   * 单值键 format-version / compiler-version / module / fingerprint 各出现一次，
//     顺序由 serializeCif 固定；parseCif 接受任意键顺序，并容忍行首尾空白、
//     连续空白/制表符、空行、CRLF 与文件头 BOM；
//   * 空值写成"只有键"的一行（module、compiler-version 允许为空：源文件可以没有
//     `module x;` 声明）；fingerprint 必须是 16 位小写十六进制；
//   * export 行固定两个字段：kind 在前、名字在后（例如 "class Point"）；
//     import 行固定一个字段（模块路径）；generic-body 行是一段自由文本（预留）；
//   * 字段转义：'\\' -> "\\\\"、空格 -> "\\s"、制表符 -> "\\t"、换行 -> "\\n"、
//     回车 -> "\\r"、其它控制字符 -> "\\xHH"（小写十六进制），保证一行一个字段，
//     并且反序列化能逐字节还原；
//   * 出错时 parseCif 返回 std::nullopt，并在 error（若非空）写入带行号的中文原因；
//     未知键、字段个数不对、非法转义、重复单值键、缺必填键、不支持的格式版本都报错；
//   * serializeCif 假设 CifExport 的 kind/name 非空；空值会导致生成的 export 行
//     字段不足，从而被 parseCif 拒绝（默认构造的 CifModule 不是合法接口文档）。
// ============================================================================

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Ast.h"
#include "Dependency.h"

namespace coordination::compiler
{
    // 一条导出：名字 + 文本 kind。kind 取值见 cif_detail::export_kind
    //（class/interface/concept/annotation/type/distinct/function/operator/
    // variable/constant），与 Dependency.h 的导出符号种类保持一致。
    struct CifExport
    {
        std::string name;
        std::string kind;
    };

    // 一个模块的接口文档，对应一个 .cif 文件。
    struct CifModule
    {
        int formatVersion = 1;                   // 格式版本，目前只支持 1
        std::string compilerVersion;             // 生成该文档的编译器版本，例如 "0.1.0"
        std::string name;                        // 模块名（点分路径；源文件未声明时为空）
        std::vector<CifExport> exports;          // 导出声明（名字 + kind），保持源码顺序
        std::vector<std::string> imports;        // 直接依赖的模块路径（去重、保持源码顺序）
        std::string fingerprint;                 // 导出集合指纹（16 位小写十六进制）
        std::vector<std::string> genericBodies;  // 预留：泛型体（本版本可为空）
    };

    namespace cif_detail
    {
        // 唯一支持的格式版本；更高版本只能由新编译器读取。
        inline constexpr int kSupportedFormatVersion = 1;

        // 指纹长度：64 位哈希 -> 16 位十六进制。
        inline constexpr std::size_t kFingerprintLength = 16;

        // 转义用的十六进制数字表（小写，保证序列化结果确定）。
        inline constexpr char kHexDigits[] = "0123456789abcdef";

        // 行内空白：空格、制表符、回车（CRLF 的 '\r' 在切行后仍需剔除）。
        inline bool is_space(char ch) noexcept
        {
            return ch == ' ' || ch == '\t' || ch == '\r';
        }

        // 去掉行首尾空白（不处理 '\n'，切行时已去掉）。
        inline std::string_view trim(std::string_view text)
        {
            std::size_t begin = 0;
            while (begin < text.size() && is_space(text[begin]))
            {
                ++begin;
            }
            std::size_t end = text.size();
            while (end > begin && is_space(text[end - 1]))
            {
                --end;
            }
            return text.substr(begin, end - begin);
        }

        // 按空白切分字段；连续空白视为一个分隔符（容忍手工编辑的多个空格）。
        inline std::vector<std::string_view> split_fields(std::string_view text)
        {
            std::vector<std::string_view> fields;
            std::size_t index = 0;
            while (index < text.size())
            {
                while (index < text.size() && is_space(text[index]))
                {
                    ++index;
                }
                if (index >= text.size())
                {
                    break;
                }
                const std::size_t begin = index;
                while (index < text.size() && !is_space(text[index]))
                {
                    ++index;
                }
                fields.push_back(text.substr(begin, index - begin));
            }
            return fields;
        }

        // 一个字段的转义：转义后字段内不含任何空白，可再按空白切分。
        inline std::string escape_field(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    out += "\\\\";
                    break;
                case ' ':
                    out += "\\s";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                default:
                {
                    const unsigned char byte = static_cast<unsigned char>(ch);
                    if (byte < 0x20u || byte == 0x7Fu)
                    {
                        // 其它控制字符无法直接放进文本文件，用 \xHH 保存。
                        out += "\\x";
                        out += kHexDigits[(byte >> 4) & 0x0Fu];
                        out += kHexDigits[byte & 0x0Fu];
                    }
                    else
                    {
                        out += ch;
                    }
                    break;
                }
                }
            }
            return out;
        }

        // 十六进制数字 -> 0..15；非法字符返回 -1。
        inline int hex_value(char ch) noexcept
        {
            if (ch >= '0' && ch <= '9')
            {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f')
            {
                return ch - 'a' + 10;
            }
            if (ch >= 'A' && ch <= 'F')
            {
                return ch - 'A' + 10;
            }
            return -1;
        }

        // 反转义一个字段；未知转义或缺字符的 \xHH 返回 false。
        inline bool unescape_field(std::string_view text, std::string& out)
        {
            out.clear();
            out.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const char ch = text[index];
                if (ch != '\\')
                {
                    out += ch;
                    continue;
                }
                if (index + 1 >= text.size())
                {
                    return false;
                }
                ++index;
                switch (text[index])
                {
                case '\\':
                    out += '\\';
                    break;
                case 's':
                    out += ' ';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 'x':
                {
                    if (index + 2 >= text.size())
                    {
                        return false;
                    }
                    const int high = hex_value(text[index + 1]);
                    const int low = hex_value(text[index + 2]);
                    if (high < 0 || low < 0)
                    {
                        return false;
                    }
                    out += static_cast<char>((high << 4) | low);
                    index += 2;
                    break;
                }
                default:
                    return false;
                }
            }
            return true;
        }

        // 是否为 16 位小写十六进制（指纹的规范形式）。
        inline bool is_fingerprint(std::string_view text) noexcept
        {
            if (text.size() != kFingerprintLength)
            {
                return false;
            }
            for (const char ch : text)
            {
                const bool digit = ch >= '0' && ch <= '9';
                const bool lower = ch >= 'a' && ch <= 'f';
                if (!digit && !lower)
                {
                    return false;
                }
            }
            return true;
        }

        // 严格解析十进制整数（整串必须都是数字），失败时不修改 value。
        inline bool parse_int(std::string_view text, int& value)
        {
            if (text.empty())
            {
                return false;
            }
            int parsed = 0;
            const char* begin = text.data();
            const char* end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc() || result.ptr != end)
            {
                return false;
            }
            value = parsed;
            return true;
        }

        // AST 声明种类 -> CIF 的 kind 文本；不可导出的种类返回空。
        inline std::string_view export_kind(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::ClassDecl:
                return "class";
            case AstKind::InterfaceDecl:
                return "interface";
            case AstKind::ConceptDecl:
                return "concept";
            case AstKind::AnnotationDecl:
                return "annotation";
            case AstKind::TypeAliasDecl:
                return "type";
            case AstKind::DistinctTypeDecl:
                return "distinct";
            case AstKind::FunctionDecl:
                return "function";
            case AstKind::OperatorDecl:
                return "operator";
            case AstKind::VariableDecl:
                return "variable";
            case AstKind::ConstantDecl:
                return "constant";
            default:
                return {};
            }
        }

        // 从 Module 根节点收集导出声明（名字 + kind）。
        //
        // 与 Dependency.h 的 collect_export_names 保持同一套判定：
        //   * 顶层声明本身带 exported 标志，或被 ExportDecl 包裹（Parser 两者都置位）；
        //   * ExportDecl 包裹的 ImportDecl 是重导出，不产生本地符号名；
        //   * 名字用 strip_declaration_prefix 去掉 const:/mutable: 等 text 前缀；
        //   * 保持源码顺序，不做去重（指纹内部去重）。
        inline std::vector<CifExport> collect_exports(const AstNode& module)
        {
            std::vector<CifExport> exports;
            if (module.kind != AstKind::Module)
            {
                return exports;
            }

            for (const AstNode* item : module.children)
            {
                if (item == nullptr)
                {
                    continue;
                }

                const AstNode* declaration = item;
                if (item->kind == AstKind::ExportDecl)
                {
                    declaration = item->child(0);
                }
                if (declaration == nullptr || declaration->kind == AstKind::ImportDecl)
                {
                    // 重导出整模块不产生本地符号名。
                    continue;
                }
                if (!dependency_detail::is_export_symbol_kind(declaration->kind))
                {
                    continue;
                }

                const bool exported = item->kind == AstKind::ExportDecl ||
                    declaration->has_flag(AstFlags::exported);
                if (!exported)
                {
                    continue;
                }

                const std::string_view kind = export_kind(declaration->kind);
                const std::string_view name = dependency_detail::strip_declaration_prefix(declaration->text);
                if (kind.empty() || name.empty())
                {
                    continue;
                }
                exports.push_back(CifExport{std::string(name), std::string(kind)});
            }
            return exports;
        }

        // 追加"键 [值]"一行；空值只写键，避免行尾空白。
        inline void append_field(std::string& out, std::string_view key, std::string_view value)
        {
            out += key;
            if (!value.empty())
            {
                out += ' ';
                out += escape_field(value);
            }
            out += '\n';
        }
    }

    // 导出集合指纹：名字排序去重后做 FNV-1a 64 位哈希，输出 16 位小写十六进制。
    // 名字之间补 '\n' 分隔，避免 "ab"+"c" 与 "a"+"bc" 撞成同一输入；算法与
    // Dependency.h 的模块指纹一致（同一导出集合得到相同指纹）。
    inline std::string computeFingerprint(const std::vector<CifExport>& exports)
    {
        std::vector<std::string> names;
        names.reserve(exports.size());
        for (const CifExport& entry : exports)
        {
            names.push_back(entry.name);
        }
        return dependency_detail::fingerprint_of(std::move(names));
    }

    // 从 AST 的 Module 根节点生成接口文档：模块名取根节点 text，导出与 import
    // 按源码顺序收集，指纹由导出集合计算，genericBodies 留空（本版本预留）。
    // 根节点不是 Module 时返回空接口文档（空名字/空指纹），不抛异常。
    inline CifModule makeCifFromAst(const AstNode& moduleRoot, std::string_view compilerVersion)
    {
        CifModule module;
        module.compilerVersion = std::string(compilerVersion);
        if (moduleRoot.kind != AstKind::Module)
        {
            return module;
        }

        module.name = moduleRoot.text;
        module.exports = cif_detail::collect_exports(moduleRoot);
        dependency_detail::append_imports(moduleRoot, module.imports);
        module.fingerprint = computeFingerprint(module.exports);
        return module;
    }

    // 序列化为确定性文本（键固定顺序，行尾统一 '\n'）。
    // CifExport 的 kind/name 必须非空；否则生成的 export 行字段不足，parseCif 会拒绝。
    inline std::string serializeCif(const CifModule& module)
    {
        std::string out;
        out += "format-version ";
        out += std::to_string(module.formatVersion);
        out += '\n';
        cif_detail::append_field(out, "compiler-version", module.compilerVersion);
        cif_detail::append_field(out, "module", module.name);
        cif_detail::append_field(out, "fingerprint", module.fingerprint);

        for (const CifExport& entry : module.exports)
        {
            // export 行固定两个字段：kind 在前、名字在后。
            out += "export";
            if (!entry.kind.empty() || !entry.name.empty())
            {
                out += ' ';
                out += cif_detail::escape_field(entry.kind);
            }
            if (!entry.name.empty())
            {
                out += ' ';
                out += cif_detail::escape_field(entry.name);
            }
            out += '\n';
        }

        for (const std::string& import : module.imports)
        {
            cif_detail::append_field(out, "import", import);
        }
        for (const std::string& body : module.genericBodies)
        {
            cif_detail::append_field(out, "generic-body", body);
        }
        return out;
    }

    // 解析 .cif 文本。成功返回接口文档；失败返回 std::nullopt，并在 error（若非空）
    // 写入带行号的中文原因。容忍 BOM、CRLF、空行、任意键顺序与多余空白。
    inline std::optional<CifModule> parseCif(std::string_view text, std::string* error = nullptr)
    {
        if (error != nullptr)
        {
            error->clear();
        }

        // 容忍 UTF-8 BOM：serializeCif 不写 BOM，但手工/工具保存的文件可能带。
        if (text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEFu &&
            static_cast<unsigned char>(text[1]) == 0xBBu &&
            static_cast<unsigned char>(text[2]) == 0xBFu)
        {
            text.remove_prefix(3);
        }

        CifModule module;
        bool hasFormatVersion = false;
        bool hasCompilerVersion = false;
        bool hasModuleName = false;
        bool hasFingerprint = false;

        std::size_t position = 0;
        std::size_t lineNumber = 0;
        while (position < text.size())
        {
            const std::size_t newline = text.find('\n', position);
            const bool lastLine = newline == std::string_view::npos;
            const std::string_view raw = lastLine
                ? text.substr(position)
                : text.substr(position, newline - position);
            position = lastLine ? text.size() : newline + 1;
            ++lineNumber;

            const std::string_view line = cif_detail::trim(raw);
            if (line.empty())
            {
                continue;
            }

            const std::size_t separator = line.find_first_of(" \t");
            const std::string_view key = separator == std::string_view::npos
                ? line
                : line.substr(0, separator);
            const std::string_view rest = separator == std::string_view::npos
                ? std::string_view()
                : cif_detail::trim(line.substr(separator + 1));

            // 统一带行号的错误出口：返回 nullopt 并写 error。
            const auto fail = [&](std::string message) -> std::optional<CifModule>
            {
                if (error != nullptr)
                {
                    *error = "第 " + std::to_string(lineNumber) + " 行: " + std::move(message);
                }
                return std::nullopt;
            };

            if (key == "format-version")
            {
                if (hasFormatVersion)
                {
                    return fail("重复的键 'format-version'");
                }
                int value = 0;
                if (!cif_detail::parse_int(rest, value))
                {
                    return fail("format-version 不是合法整数");
                }
                if (value != cif_detail::kSupportedFormatVersion)
                {
                    return fail("不支持的格式版本 " + std::to_string(value));
                }
                module.formatVersion = value;
                hasFormatVersion = true;
            }
            else if (key == "compiler-version")
            {
                if (hasCompilerVersion)
                {
                    return fail("重复的键 'compiler-version'");
                }
                std::string value;
                if (!cif_detail::unescape_field(rest, value))
                {
                    return fail("compiler-version 含非法转义序列");
                }
                module.compilerVersion = std::move(value);
                hasCompilerVersion = true;
            }
            else if (key == "module")
            {
                if (hasModuleName)
                {
                    return fail("重复的键 'module'");
                }
                std::string value;
                if (!cif_detail::unescape_field(rest, value))
                {
                    return fail("module 含非法转义序列");
                }
                module.name = std::move(value);
                hasModuleName = true;
            }
            else if (key == "fingerprint")
            {
                if (hasFingerprint)
                {
                    return fail("重复的键 'fingerprint'");
                }
                std::string value;
                if (!cif_detail::unescape_field(rest, value))
                {
                    return fail("fingerprint 含非法转义序列");
                }
                if (!cif_detail::is_fingerprint(value))
                {
                    return fail("fingerprint 必须是 16 位小写十六进制");
                }
                module.fingerprint = std::move(value);
                hasFingerprint = true;
            }
            else if (key == "export")
            {
                const std::vector<std::string_view> fields = cif_detail::split_fields(rest);
                if (fields.size() != 2)
                {
                    return fail("export 行需要 2 个字段（kind name），实际 " +
                        std::to_string(fields.size()) + " 个");
                }
                CifExport entry;
                if (!cif_detail::unescape_field(fields[0], entry.kind) ||
                    !cif_detail::unescape_field(fields[1], entry.name))
                {
                    return fail("export 行含非法转义序列");
                }
                if (entry.kind.empty() || entry.name.empty())
                {
                    return fail("export 的 kind 与名字不能为空");
                }
                module.exports.push_back(std::move(entry));
            }
            else if (key == "import")
            {
                const std::vector<std::string_view> fields = cif_detail::split_fields(rest);
                if (fields.size() != 1)
                {
                    return fail("import 行需要 1 个字段（模块路径），实际 " +
                        std::to_string(fields.size()) + " 个");
                }
                std::string value;
                if (!cif_detail::unescape_field(fields[0], value))
                {
                    return fail("import 行含非法转义序列");
                }
                if (value.empty())
                {
                    return fail("import 的模块路径不能为空");
                }
                module.imports.push_back(std::move(value));
            }
            else if (key == "generic-body")
            {
                std::string value;
                if (!cif_detail::unescape_field(rest, value))
                {
                    return fail("generic-body 含非法转义序列");
                }
                module.genericBodies.push_back(std::move(value));
            }
            else
            {
                return fail("未知的键 '" + std::string(key) + "'");
            }
        }

        // 缺必填键同样视为坏文档（不依赖默认值悄悄通过）。
        const auto report_missing = [error](std::string_view key) -> std::optional<CifModule>
        {
            if (error != nullptr)
            {
                *error = "缺少必填字段 '" + std::string(key) + "'";
            }
            return std::nullopt;
        };

        if (!hasFormatVersion)
        {
            return report_missing("format-version");
        }
        if (!hasCompilerVersion)
        {
            return report_missing("compiler-version");
        }
        if (!hasModuleName)
        {
            return report_missing("module");
        }
        if (!hasFingerprint)
        {
            return report_missing("fingerprint");
        }
        return module;
    }

    // 缓存的接口文档是否仍可用于当前模块：
    //   * 格式版本必须是受支持的那一个（新旧任意一侧不支持都不行）；
    //   * 编译器版本（生成接口的编译器）必须一致；
    //   * 导出集合指纹必须一致且非空。
    // 模块名与 import 不参与比较：前者由调用方按名字索引缓存，后者不影响本模块
    // 对外暴露的接口（依赖变化由更上层的模块图增量逻辑处理）。
    inline bool compatible(const CifModule& cached, const CifModule& current)
    {
        if (cached.formatVersion != cif_detail::kSupportedFormatVersion ||
            current.formatVersion != cif_detail::kSupportedFormatVersion)
        {
            return false;
        }
        if (cached.fingerprint.empty() || current.fingerprint.empty())
        {
            return false;
        }
        if (cached.compilerVersion != current.compilerVersion)
        {
            return false;
        }
        return cached.fingerprint == current.fingerprint;
    }
}
