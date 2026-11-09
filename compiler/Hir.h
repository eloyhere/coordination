#pragma once

// ============================================================================
// 最小 HIR：把 AST 下降为类型化中间表示，供后续 C 后端消费。
//
// 设计目标：
//   * 只覆盖 C 后端起步所需的最小子集，不做控制流图、不做优化；
//   * 下降只读取 AST，不修改它，也不抛异常；所有问题写入 HirModule::diagnostics；
//   * 无法表示的节点统一报 E6001 并跳过（函数签名无法表示时跳过整个函数）。
//
// 支持子集：
//   * 顶层函数与类/接口/概念内的方法（方法体不能使用 this）；
//   * 参数与返回类型：integer、integer(32)、integer(64) 统一为 integer64，
//     bool -> boolean，void -> voidType，unknown 与其它 NamedType -> unknown；
//   * 语句：块（递归展开）、return、局部 const/mutable 声明（必须有初始化器）；
//   * 表达式：整数/布尔/文本字面量、局部名字引用、一元 + - ! ~、
//     二元 + - * / % == != < <= > >= && ||、直接函数名调用。
//
// 约定：
//   * HirExpr::refIndex 是“函数槽位”下标：参数按声明顺序占 0..N-1，
//     局部声明按下降顺序继续编号（参数 + 全部局部变量，与源码块作用域无关）；
//   * returnStmt 无返回值时 value.kind 保持默认 integerLiteral、value.type = voidType，
//     dumpHir 输出 "(return)"；
//   * 局部声明没有初始化器、参数默认值、无函数体的函数等暂不支持，报 E6001 并跳过；
//   * 类字段、属性、注解、类型参数不进入 HIR，静默忽略；
//     构造器/析构器/运算符报 E6001 并跳过；
//   * 未识别的命名类型与找不到签名的调用一律降级为 HirType::unknown，不额外报错；
//   * import 与顶层变量/常量/类型声明暂不支持，报 E6001 并跳过。
//
// dumpHir 输出稳定的 S 表达式文本（每个函数一段、每条语句一行），便于测试断言：
//   (module
//     (function add (param a integer64) (param b integer64) (result integer64)
//       (localDecl sum integer64 (binary add integer64 ...))
//       (return ...)
//     )
//   )
//
// 诊断码：
//   E6001 当前 HIR 子集无法表示的 AST 节点（消息中带节点类别）。
// ============================================================================

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"

namespace coordination::compiler
{
    // HIR 类型。integer(32)/integer(64) 在 HIR 中都表示为 integer64，
    // 因为当前 C 后端只计划使用 64 位整数。
    enum class HirType
    {
        voidType,
        integer64,
        boolean,
        text,
        unknown,
    };

    // 二元运算符。位运算与移位不在最小子集内，遇到时报 E6001。
    enum class HirBinaryOp
    {
        add,
        sub,
        mul,
        div,
        mod,
        eq,
        ne,
        lt,
        le,
        gt,
        ge,
        logicalAnd,
        logicalOr,
    };

    // 一元运算符。一元 '+' 直接折叠为其操作数，不产生 HIR 节点。
    enum class HirUnaryOp
    {
        negate,
        logicalNot,
        bitwiseNot,
    };

    // 表达式类别。
    enum class HirExprKind
    {
        integerLiteral,
        booleanLiteral,
        textLiteral,
        localRef,
        binary,
        unary,
        call,
    };

    // 一个 HIR 表达式。operands 的含义：
    //   * binary：依次为左操作数、右操作数；
    //   * unary：唯一元素为操作数；
    //   * call：依次为实参。
    struct HirExpr
    {
        HirExprKind kind;
        HirType type = HirType::unknown;
        std::int64_t integer = 0;
        bool boolean = false;
        std::string text;
        HirUnaryOp unaryOp{};
        HirBinaryOp binaryOp{};
        std::size_t refIndex = 0;
        std::vector<HirExpr> operands;
        std::string callName;
    };

    // 语句类别。局部声明与表达式语句的值都放在 value 中。
    enum class HirStmtKind
    {
        returnStmt,
        exprStmt,
        localDecl,
    };

    struct HirStmt
    {
        HirStmtKind kind;
        std::string name;
        HirType type = HirType::unknown;
        HirExpr value;
    };

    // 一个 HIR 函数。parameters 中的名字已去掉 Parser 的 const:/mutable: 前缀。
    struct HirFunction
    {
        std::string name;
        std::vector<std::pair<std::string, HirType>> parameters;
        HirType returnType = HirType::voidType;
        std::vector<HirStmt> body;
    };

    // 一次下降的完整结果。
    struct HirModule
    {
        std::vector<HirFunction> functions;
        DiagnosticBag diagnostics;

        bool ok() const noexcept
        {
            return !diagnostics.has_errors();
        }
    };

    namespace hir_detail
    {
        inline std::string_view hir_type_name(HirType type) noexcept
        {
            switch (type)
            {
            case HirType::voidType:
                return "void";
            case HirType::integer64:
                return "integer64";
            case HirType::boolean:
                return "boolean";
            case HirType::text:
                return "text";
            case HirType::unknown:
            default:
                return "unknown";
            }
        }

        inline std::string_view hir_binary_op_name(HirBinaryOp op) noexcept
        {
            switch (op)
            {
            case HirBinaryOp::add:
                return "add";
            case HirBinaryOp::sub:
                return "sub";
            case HirBinaryOp::mul:
                return "mul";
            case HirBinaryOp::div:
                return "div";
            case HirBinaryOp::mod:
                return "mod";
            case HirBinaryOp::eq:
                return "eq";
            case HirBinaryOp::ne:
                return "ne";
            case HirBinaryOp::lt:
                return "lt";
            case HirBinaryOp::le:
                return "le";
            case HirBinaryOp::gt:
                return "gt";
            case HirBinaryOp::ge:
                return "ge";
            case HirBinaryOp::logicalAnd:
                return "logicalAnd";
            case HirBinaryOp::logicalOr:
                return "logicalOr";
            default:
                return "unknown";
            }
        }

        inline std::string_view hir_unary_op_name(HirUnaryOp op) noexcept
        {
            switch (op)
            {
            case HirUnaryOp::negate:
                return "negate";
            case HirUnaryOp::logicalNot:
                return "logicalNot";
            case HirUnaryOp::bitwiseNot:
                return "bitwiseNot";
            default:
                return "unknown";
            }
        }

        // 去掉 Parser 用 text 前缀承载的修饰符（const:/mutable:/out:/in:）。
        inline std::string_view declaration_name(std::string_view text) noexcept
        {
            const std::size_t colon = text.find(':');
            if (colon == std::string_view::npos)
            {
                return text;
            }
            const std::string_view prefix = text.substr(0, colon);
            if (prefix == "const" || prefix == "mutable" || prefix == "out" || prefix == "in")
            {
                return text.substr(colon + 1);
            }
            return text;
        }

        inline bool is_type_node(AstKind kind) noexcept
        {
            switch (kind)
            {
            case AstKind::NamedType:
            case AstKind::GenericType:
            case AstKind::PointerType:
            case AstKind::ReferenceType:
            case AstKind::MutableReferenceType:
            case AstKind::ArrayType:
            case AstKind::DynType:
            case AstKind::FunctionType:
                return true;
            default:
                return false;
            }
        }

        inline std::size_t first_non_annotation_index(const AstNode& node) noexcept
        {
            std::size_t index = 0;
            while (index < node.children.size() && node.children[index] != nullptr &&
                node.children[index]->kind == AstKind::AnnotationUse)
            {
                ++index;
            }
            return index;
        }

        // 解析非负整数字面量（十进制、0x 十六进制、0b 二进制，允许 '_' 分隔）。
        // 超出 int64 上限时返回 false，由调用方报 E6001。
        inline bool parse_integer_literal(std::string_view text, std::int64_t& out) noexcept
        {
            int base = 10;
            std::size_t index = 0;
            if (text.size() >= 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                base = 16;
                index = 2u;
            }
            else if (text.size() >= 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
            {
                base = 2;
                index = 2u;
            }

            std::string digits;
            digits.reserve(text.size());
            for (; index < text.size(); ++index)
            {
                if (text[index] == '_')
                {
                    continue;
                }
                digits.push_back(text[index]);
            }
            if (digits.empty())
            {
                return false;
            }

            std::int64_t value = 0;
            const char* begin = digits.data();
            const char* end = begin + digits.size();
            const std::from_chars_result result = std::from_chars(begin, end, value, base);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                return false;
            }
            out = value;
            return true;
        }

        // 解码带双引号的字符串字面量；支持 \" \\ \n \t \r \0 \'。
        inline bool decode_string_literal(std::string_view text, std::string& out)
        {
            if (text.size() < 2u || text.front() != '"' || text.back() != '"')
            {
                return false;
            }

            std::string decoded;
            decoded.reserve(text.size() - 2u);
            for (std::size_t index = 1u; index + 1u < text.size(); ++index)
            {
                const char current = text[index];
                if (current == '"')
                {
                    return false;
                }
                if (current != '\\')
                {
                    decoded.push_back(current);
                    continue;
                }

                if (index + 2u >= text.size())
                {
                    return false;
                }
                const char escaped = text[++index];
                switch (escaped)
                {
                case '"':
                    decoded.push_back('"');
                    break;
                case '\'':
                    decoded.push_back('\'');
                    break;
                case '\\':
                    decoded.push_back('\\');
                    break;
                case 'n':
                    decoded.push_back('\n');
                    break;
                case 't':
                    decoded.push_back('\t');
                    break;
                case 'r':
                    decoded.push_back('\r');
                    break;
                case '0':
                    decoded.push_back('\0');
                    break;
                default:
                    return false;
                }
            }

            out = std::move(decoded);
            return true;
        }

        // 把文本按 C 风格转义后写入 out；用于 dump 的稳定性。
        inline void append_quoted_text(std::string_view text, std::string& out)
        {
            out.push_back('"');
            for (const char character : text)
            {
                switch (character)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                default:
                    if (static_cast<unsigned char>(character) < 0x20u)
                    {
                        static constexpr char kHexDigits[] = "0123456789abcdef";
                        out += "\\x";
                        out.push_back(kHexDigits[(static_cast<unsigned char>(character) >> 4u) & 0x0Fu]);
                        out.push_back(kHexDigits[static_cast<unsigned char>(character) & 0x0Fu]);
                    }
                    else
                    {
                        out.push_back(character);
                    }
                    break;
                }
            }
            out.push_back('"');
        }

        // 把 AST 下降为 HirModule 的实现。实例只使用一次（run 之后即丢弃）。
        class Lowerer
        {
        public:
            HirModule run(const AstNode& module_root)
            {
                collect_signatures(module_root);
                if (module_root.kind == AstKind::Module)
                {
                    for (const AstNode* child : module_root.children)
                    {
                        lower_top_level(child);
                    }
                }
                else
                {
                    // 允许直接传入单个声明，方便测试与后续增量下降。
                    lower_top_level(&module_root);
                }
                return std::move(module_);
            }

        private:
            struct Slot
            {
                std::string name;
                HirType type = HirType::unknown;
            };

            // ---------------- 诊断 ----------------

            void report_unsupported(const AstNode& node, std::string_view category)
            {
                std::string message = "HIR 暂不支持";
                message.append(category);
                message += "（节点 ";
                message.append(ast_kind_name(node.kind));
                message += "）";
                module_.diagnostics.error("E6001", std::move(message), node.span);
            }

            void report_unsupported_type(const AstNode& node, std::string_view role)
            {
                std::string message = "HIR 暂不支持";
                message.append(role);
                message += "类型";
                if (!node.text.empty())
                {
                    message += " '";
                    message.append(node.text);
                    message += '\'';
                }
                message += "（节点 ";
                message.append(ast_kind_name(node.kind));
                message += "）";
                module_.diagnostics.error("E6001", std::move(message), node.span);
            }

            // ---------------- 函数签名预收集（支持前向调用） ----------------

            void collect_signatures(const AstNode& node)
            {
                switch (node.kind)
                {
                case AstKind::Module:
                    for (const AstNode* child : node.children)
                    {
                        if (child != nullptr)
                        {
                            collect_signatures(*child);
                        }
                    }
                    break;
                case AstKind::ExportDecl:
                    if (node.child(0) != nullptr)
                    {
                        collect_signatures(*node.child(0));
                    }
                    break;
                case AstKind::ClassDecl:
                case AstKind::InterfaceDecl:
                case AstKind::ConceptDecl:
                    for (const AstNode* member : node.children)
                    {
                        if (member != nullptr && member->kind == AstKind::MethodDecl)
                        {
                            collect_signature(*member);
                        }
                    }
                    break;
                case AstKind::FunctionDecl:
                    collect_signature(node);
                    break;
                default:
                    break;
                }
            }

            void collect_signature(const AstNode& declaration)
            {
                const std::string_view name = declaration_name(declaration.text);
                if (name.empty())
                {
                    return;
                }

                HirType return_type = HirType::voidType;
                const AstNode* return_node = find_return_type(declaration);
                if (return_node != nullptr && !resolve_type(*return_node, return_type))
                {
                    return;
                }
                signatures_.emplace(std::string(name), return_type);
            }

            HirType lookup_return_type(std::string_view name) const
            {
                const auto found = signatures_.find(std::string(name));
                return found == signatures_.end() ? HirType::unknown : found->second;
            }

            // ---------------- 类型 ----------------

            static HirType named_type(std::string_view name) noexcept
            {
                if (name == "void")
                {
                    return HirType::voidType;
                }
                if (name == "bool" || name == "boolean")
                {
                    return HirType::boolean;
                }
                if (name == "integer")
                {
                    return HirType::integer64;
                }
                // unknown 以及类/别名等无法映射的命名类型统一降级。
                return HirType::unknown;
            }

            static bool resolve_integer_type(const AstNode& node, HirType& out) noexcept
            {
                if (node.text != "integer")
                {
                    return false;
                }
                const AstNode* arguments = node.first_child(AstKind::ArgumentList);
                if (arguments == nullptr || arguments->children.size() != 1u)
                {
                    return false;
                }
                const AstNode* argument = arguments->child(0);
                if (argument == nullptr || argument->kind != AstKind::LiteralExpr)
                {
                    return false;
                }

                std::int64_t width = 0;
                if (!parse_integer_literal(argument->text, width))
                {
                    return false;
                }
                if (width != 32 && width != 64)
                {
                    return false;
                }
                out = HirType::integer64;
                return true;
            }

            static bool resolve_type(const AstNode& node, HirType& out) noexcept
            {
                switch (node.kind)
                {
                case AstKind::NamedType:
                    out = named_type(node.text);
                    return true;
                case AstKind::GenericType:
                    return resolve_integer_type(node, out);
                default:
                    return false;
                }
            }

            bool lower_type(const AstNode& node, HirType& out, std::string_view role)
            {
                HirType resolved = HirType::unknown;
                if (!resolve_type(node, resolved))
                {
                    report_unsupported_type(node, role);
                    return false;
                }
                out = resolved;
                return true;
            }

            static const AstNode* find_return_type(const AstNode& declaration) noexcept
            {
                for (const AstNode* child : declaration.children)
                {
                    if (child == nullptr)
                    {
                        continue;
                    }
                    if (child->kind == AstKind::AnnotationUse || child->kind == AstKind::ParamList ||
                        child->kind == AstKind::BlockStmt)
                    {
                        continue;
                    }
                    return child;
                }
                return nullptr;
            }

            static void param_parts(const AstNode& parameter, const AstNode*& type,
                const AstNode*& default_value) noexcept
            {
                type = nullptr;
                default_value = nullptr;
                const std::size_t start = first_non_annotation_index(parameter);
                if (start < parameter.children.size())
                {
                    type = parameter.children[start];
                }
                if (start + 1u < parameter.children.size())
                {
                    default_value = parameter.children[start + 1u];
                }
            }

            // ---------------- 顶层声明 ----------------

            void lower_top_level(const AstNode* node)
            {
                if (node == nullptr)
                {
                    return;
                }
                switch (node->kind)
                {
                case AstKind::Module:
                    for (const AstNode* child : node->children)
                    {
                        lower_top_level(child);
                    }
                    break;
                case AstKind::ExportDecl:
                {
                    const AstNode* inner = node->child(0);
                    if (inner == nullptr)
                    {
                        report_unsupported(*node, "export 声明");
                    }
                    else
                    {
                        lower_top_level(inner);
                    }
                    break;
                }
                case AstKind::FunctionDecl:
                    lower_function(*node);
                    break;
                case AstKind::ClassDecl:
                case AstKind::InterfaceDecl:
                case AstKind::ConceptDecl:
                    lower_type_container(*node);
                    break;
                case AstKind::ImportDecl:
                    report_unsupported(*node, "import 声明");
                    break;
                case AstKind::Error:
                    break;
                default:
                    report_unsupported(*node, "顶层声明");
                    break;
                }
            }

            void lower_type_container(const AstNode& declaration)
            {
                for (const AstNode* member : declaration.children)
                {
                    if (member == nullptr)
                    {
                        continue;
                    }
                    switch (member->kind)
                    {
                    case AstKind::MethodDecl:
                        lower_function(*member);
                        break;
                    case AstKind::ConstructorDecl:
                    case AstKind::DestructorDecl:
                    case AstKind::OperatorDecl:
                        report_unsupported(*member, "类型成员");
                        break;
                    default:
                        // 字段、属性、注解、类型参数与父类型不参与 HIR，静默忽略。
                        break;
                    }
                }
            }

            // ---------------- 函数 ----------------

            void lower_function(const AstNode& declaration)
            {
                const std::string_view raw_name = declaration_name(declaration.text);
                if (raw_name.empty())
                {
                    report_unsupported(declaration, "函数声明");
                    return;
                }

                const AstNode* parameters = declaration.first_child(AstKind::ParamList);
                const AstNode* body = declaration.first_child(AstKind::BlockStmt);
                if (body == nullptr)
                {
                    report_unsupported(declaration, "没有函数体的函数");
                    return;
                }

                HirFunction function;
                function.name = std::string(raw_name);

                const AstNode* return_node = find_return_type(declaration);
                if (return_node != nullptr && !lower_type(*return_node, function.returnType, "返回"))
                {
                    return;
                }

                slots_.clear();
                scopes_.clear();
                scopes_.emplace_back(); // 参数作用域

                if (parameters != nullptr)
                {
                    for (const AstNode* parameter : parameters->children)
                    {
                        if (parameter == nullptr)
                        {
                            continue;
                        }
                        if (parameter->kind == AstKind::Error)
                        {
                            // 语法错误已由 Parser 报过，这里跳过整个函数。
                            return;
                        }
                        if (parameter->kind != AstKind::Param)
                        {
                            report_unsupported(*parameter, "参数");
                            return;
                        }

                        const AstNode* type_node = nullptr;
                        const AstNode* default_value = nullptr;
                        param_parts(*parameter, type_node, default_value);
                        if (default_value != nullptr)
                        {
                            report_unsupported(*default_value, "参数默认值");
                            return;
                        }

                        const std::string_view parameter_name = declaration_name(parameter->text);
                        if (parameter_name.empty() || type_node == nullptr)
                        {
                            report_unsupported(*parameter, "参数");
                            return;
                        }

                        HirType parameter_type = HirType::unknown;
                        if (!lower_type(*type_node, parameter_type, "参数"))
                        {
                            return;
                        }

                        const std::string name(parameter_name);
                        function.parameters.emplace_back(name, parameter_type);
                        declare_slot(name, parameter_type);
                    }
                }

                scopes_.emplace_back(); // 函数体块作用域
                for (const AstNode* statement : body->children)
                {
                    lower_statement(statement, function.body);
                }
                scopes_.pop_back();

                slots_.clear();
                scopes_.clear();
                module_.functions.push_back(std::move(function));
            }

            // ---------------- 语句 ----------------

            void lower_statement(const AstNode* statement, std::vector<HirStmt>& out)
            {
                if (statement == nullptr)
                {
                    return;
                }
                switch (statement->kind)
                {
                case AstKind::BlockStmt:
                    scopes_.emplace_back();
                    for (const AstNode* child : statement->children)
                    {
                        lower_statement(child, out);
                    }
                    scopes_.pop_back();
                    break;
                case AstKind::ReturnStmt:
                {
                    HirStmt lowered;
                    lowered.kind = HirStmtKind::returnStmt;
                    const AstNode* value = statement->child(0);
                    if (value == nullptr)
                    {
                        // 无返回值：用默认 kind + voidType 作为标记，dump 输出 "(return)"。
                        lowered.value.kind = HirExprKind::integerLiteral;
                        lowered.value.type = HirType::voidType;
                        lowered.value.integer = 0;
                    }
                    else if (!lower_expression(*value, lowered.value))
                    {
                        return;
                    }
                    out.push_back(std::move(lowered));
                    break;
                }
                case AstKind::ExprStmt:
                {
                    HirStmt lowered;
                    lowered.kind = HirStmtKind::exprStmt;
                    const AstNode* value = statement->child(0);
                    if (value == nullptr || value->kind == AstKind::Error ||
                        !lower_expression(*value, lowered.value))
                    {
                        return;
                    }
                    out.push_back(std::move(lowered));
                    break;
                }
                case AstKind::LocalDeclStmt:
                    lower_local_declaration(*statement, out);
                    break;
                case AstKind::Error:
                    break;
                default:
                    report_unsupported(*statement, "语句");
                    break;
                }
            }

            void lower_local_declaration(const AstNode& statement, std::vector<HirStmt>& out)
            {
                const std::string_view raw_name = declaration_name(statement.text);
                if (raw_name.empty())
                {
                    report_unsupported(statement, "局部变量声明");
                    return;
                }

                const std::size_t start = first_non_annotation_index(statement);
                std::size_t index = start;
                const AstNode* type_node = nullptr;
                if (index < statement.children.size() && statement.children[index] != nullptr &&
                    is_type_node(statement.children[index]->kind))
                {
                    type_node = statement.children[index];
                    ++index;
                }

                const AstNode* initializer = nullptr;
                if (index < statement.children.size())
                {
                    initializer = statement.children[index];
                }
                if (initializer == nullptr)
                {
                    report_unsupported(statement, "没有初始化器的局部声明");
                    return;
                }
                if (initializer->kind == AstKind::Error)
                {
                    return;
                }

                HirExpr value;
                if (!lower_expression(*initializer, value))
                {
                    return;
                }

                HirType type = value.type;
                if (type_node != nullptr && !lower_type(*type_node, type, "局部变量"))
                {
                    return;
                }

                HirStmt lowered;
                lowered.kind = HirStmtKind::localDecl;
                lowered.name = std::string(raw_name);
                lowered.type = type;
                lowered.value = std::move(value);
                out.push_back(std::move(lowered));
                declare_slot(std::string(raw_name), type);
            }

            // ---------------- 表达式 ----------------

            bool lower_expression(const AstNode& node, HirExpr& out)
            {
                switch (node.kind)
                {
                case AstKind::LiteralExpr:
                    return lower_literal(node, out);
                case AstKind::IdentifierExpr:
                    return lower_identifier(node, out);
                case AstKind::ParenExpr:
                {
                    const AstNode* inner = node.child(0);
                    if (inner == nullptr)
                    {
                        report_unsupported(node, "括号表达式");
                        return false;
                    }
                    return lower_expression(*inner, out);
                }
                case AstKind::UnaryExpr:
                    return lower_unary(node, out);
                case AstKind::BinaryExpr:
                    return lower_binary(node, out);
                case AstKind::CallExpr:
                    return lower_call(node, out);
                case AstKind::Error:
                    return false;
                default:
                    report_unsupported(node, "表达式");
                    return false;
                }
            }

            bool lower_literal(const AstNode& node, HirExpr& out)
            {
                if (node.text == "true" || node.text == "false")
                {
                    out = HirExpr{};
                    out.kind = HirExprKind::booleanLiteral;
                    out.type = HirType::boolean;
                    out.boolean = node.text == "true";
                    return true;
                }

                if (!node.text.empty() && node.text.front() == '"')
                {
                    std::string decoded;
                    if (!decode_string_literal(node.text, decoded))
                    {
                        report_unsupported(node, "文本字面量");
                        return false;
                    }
                    out = HirExpr{};
                    out.kind = HirExprKind::textLiteral;
                    out.type = HirType::text;
                    out.text = std::move(decoded);
                    return true;
                }

                if (!node.text.empty() && node.text.front() >= '0' && node.text.front() <= '9')
                {
                    std::int64_t value = 0;
                    if (!parse_integer_literal(node.text, value))
                    {
                        report_unsupported(node, "整数字面量");
                        return false;
                    }
                    out = HirExpr{};
                    out.kind = HirExprKind::integerLiteral;
                    out.type = HirType::integer64;
                    out.integer = value;
                    return true;
                }

                report_unsupported(node, "字面量");
                return false;
            }

            bool lower_identifier(const AstNode& node, HirExpr& out)
            {
                std::size_t index = 0;
                HirType type = HirType::unknown;
                if (node.text.empty() || !lookup_slot(node.text, index, type))
                {
                    report_unsupported(node, "未声明的标识符");
                    return false;
                }

                out = HirExpr{};
                out.kind = HirExprKind::localRef;
                out.type = type;
                out.refIndex = index;
                return true;
            }

            bool lower_unary(const AstNode& node, HirExpr& out)
            {
                const AstNode* operand_node = node.child(0);
                if (operand_node == nullptr)
                {
                    report_unsupported(node, "一元表达式");
                    return false;
                }

                HirUnaryOp op = HirUnaryOp::negate;
                if (node.text == "-")
                {
                    op = HirUnaryOp::negate;
                }
                else if (node.text == "!")
                {
                    op = HirUnaryOp::logicalNot;
                }
                else if (node.text == "~")
                {
                    op = HirUnaryOp::bitwiseNot;
                }
                else if (node.text == "+")
                {
                    // 一元 '+' 不产生节点，直接返回操作数。
                    return lower_expression(*operand_node, out);
                }
                else
                {
                    report_unsupported(node, "一元运算符");
                    return false;
                }

                HirExpr operand;
                if (!lower_expression(*operand_node, operand))
                {
                    return false;
                }

                HirExpr lowered;
                lowered.kind = HirExprKind::unary;
                lowered.unaryOp = op;
                lowered.type = infer_unary_type(op, operand.type);
                lowered.operands.push_back(std::move(operand));
                out = std::move(lowered);
                return true;
            }

            static bool map_binary_op(std::string_view text, HirBinaryOp& out) noexcept
            {
                if (text == "+")
                {
                    out = HirBinaryOp::add;
                }
                else if (text == "-")
                {
                    out = HirBinaryOp::sub;
                }
                else if (text == "*")
                {
                    out = HirBinaryOp::mul;
                }
                else if (text == "/")
                {
                    out = HirBinaryOp::div;
                }
                else if (text == "%")
                {
                    out = HirBinaryOp::mod;
                }
                else if (text == "==")
                {
                    out = HirBinaryOp::eq;
                }
                else if (text == "!=")
                {
                    out = HirBinaryOp::ne;
                }
                else if (text == "<")
                {
                    out = HirBinaryOp::lt;
                }
                else if (text == "<=")
                {
                    out = HirBinaryOp::le;
                }
                else if (text == ">")
                {
                    out = HirBinaryOp::gt;
                }
                else if (text == ">=")
                {
                    out = HirBinaryOp::ge;
                }
                else if (text == "&&")
                {
                    out = HirBinaryOp::logicalAnd;
                }
                else if (text == "||")
                {
                    out = HirBinaryOp::logicalOr;
                }
                else
                {
                    return false;
                }
                return true;
            }

            bool lower_binary(const AstNode& node, HirExpr& out)
            {
                HirBinaryOp op = HirBinaryOp::add;
                if (!map_binary_op(node.text, op))
                {
                    report_unsupported(node, "二元运算符");
                    return false;
                }

                const AstNode* left_node = node.child(0);
                const AstNode* right_node = node.child(1);
                if (left_node == nullptr || right_node == nullptr)
                {
                    report_unsupported(node, "二元表达式");
                    return false;
                }

                HirExpr left;
                HirExpr right;
                if (!lower_expression(*left_node, left) || !lower_expression(*right_node, right))
                {
                    return false;
                }

                HirExpr lowered;
                lowered.kind = HirExprKind::binary;
                lowered.binaryOp = op;
                lowered.type = infer_binary_type(op, left.type, right.type);
                lowered.operands.push_back(std::move(left));
                lowered.operands.push_back(std::move(right));
                out = std::move(lowered);
                return true;
            }

            bool lower_call(const AstNode& node, HirExpr& out)
            {
                const AstNode* callee = node.child(0);
                if (callee == nullptr || callee->kind != AstKind::IdentifierExpr ||
                    callee->text.empty())
                {
                    report_unsupported(node, "调用目标（仅支持直接函数名调用）");
                    return false;
                }

                HirExpr call;
                call.kind = HirExprKind::call;
                call.callName = callee->text;
                call.type = lookup_return_type(call.callName);

                const AstNode* arguments = node.first_child(AstKind::ArgumentList);
                if (arguments != nullptr)
                {
                    for (const AstNode* argument : arguments->children)
                    {
                        if (argument == nullptr || argument->kind == AstKind::Error)
                        {
                            return false;
                        }
                        HirExpr operand;
                        if (!lower_expression(*argument, operand))
                        {
                            return false;
                        }
                        call.operands.push_back(std::move(operand));
                    }
                }

                out = std::move(call);
                return true;
            }

            static HirType infer_unary_type(HirUnaryOp op, HirType operand) noexcept
            {
                switch (op)
                {
                case HirUnaryOp::negate:
                    return operand == HirType::integer64 ? HirType::integer64 : HirType::unknown;
                case HirUnaryOp::logicalNot:
                    return operand == HirType::boolean ? HirType::boolean : HirType::unknown;
                case HirUnaryOp::bitwiseNot:
                    return operand == HirType::integer64 ? HirType::integer64 : HirType::unknown;
                default:
                    return HirType::unknown;
                }
            }

            static HirType infer_binary_type(HirBinaryOp op, HirType left, HirType right) noexcept
            {
                switch (op)
                {
                case HirBinaryOp::add:
                    if (left == HirType::integer64 && right == HirType::integer64)
                    {
                        return HirType::integer64;
                    }
                    if (left == HirType::text && right == HirType::text)
                    {
                        return HirType::text;
                    }
                    return HirType::unknown;
                case HirBinaryOp::sub:
                case HirBinaryOp::mul:
                case HirBinaryOp::div:
                case HirBinaryOp::mod:
                    return left == HirType::integer64 && right == HirType::integer64
                        ? HirType::integer64
                        : HirType::unknown;
                case HirBinaryOp::eq:
                case HirBinaryOp::ne:
                    if (left == right &&
                        (left == HirType::integer64 || left == HirType::boolean ||
                            left == HirType::text))
                    {
                        return HirType::boolean;
                    }
                    return HirType::unknown;
                case HirBinaryOp::lt:
                case HirBinaryOp::le:
                case HirBinaryOp::gt:
                case HirBinaryOp::ge:
                    if ((left == HirType::integer64 && right == HirType::integer64) ||
                        (left == HirType::text && right == HirType::text))
                    {
                        return HirType::boolean;
                    }
                    return HirType::unknown;
                case HirBinaryOp::logicalAnd:
                case HirBinaryOp::logicalOr:
                    return left == HirType::boolean && right == HirType::boolean
                        ? HirType::boolean
                        : HirType::unknown;
                default:
                    return HirType::unknown;
                }
            }

            // ---------------- 槽位与作用域 ----------------

            void declare_slot(std::string name, HirType type)
            {
                const std::size_t index = slots_.size();
                slots_.push_back(Slot{std::move(name), type});
                if (scopes_.empty())
                {
                    scopes_.emplace_back();
                }
                scopes_.back()[slots_.back().name] = index;
            }

            bool lookup_slot(std::string_view name, std::size_t& index, HirType& type) const
            {
                const std::string key(name);
                for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
                {
                    const auto found = scope->find(key);
                    if (found != scope->end())
                    {
                        index = found->second;
                        type = index < slots_.size() ? slots_[index].type : HirType::unknown;
                        return true;
                    }
                }
                return false;
            }

            // ---------------- 状态 ----------------

            HirModule module_;
            std::unordered_map<std::string, HirType> signatures_;
            std::vector<Slot> slots_;
            std::vector<std::unordered_map<std::string, std::size_t>> scopes_;
        };

        // ---------------- dump ----------------

        inline void append_expression(const HirExpr& expression,
            const std::vector<std::string>& slots, std::string& out)
        {
            switch (expression.kind)
            {
            case HirExprKind::integerLiteral:
                out += "(integer ";
                out += std::to_string(expression.integer);
                out += ' ';
                out += hir_type_name(expression.type);
                out += ')';
                break;
            case HirExprKind::booleanLiteral:
                out += "(boolean ";
                out += expression.boolean ? "true" : "false";
                out += ' ';
                out += hir_type_name(expression.type);
                out += ')';
                break;
            case HirExprKind::textLiteral:
                out += "(text ";
                append_quoted_text(expression.text, out);
                out += ' ';
                out += hir_type_name(expression.type);
                out += ')';
                break;
            case HirExprKind::localRef:
                out += "(localRef ";
                out += std::to_string(expression.refIndex);
                out += ' ';
                if (expression.refIndex < slots.size())
                {
                    out += slots[expression.refIndex];
                }
                else
                {
                    out += '?';
                }
                out += ' ';
                out += hir_type_name(expression.type);
                out += ')';
                break;
            case HirExprKind::binary:
                out += "(binary ";
                out += hir_binary_op_name(expression.binaryOp);
                out += ' ';
                out += hir_type_name(expression.type);
                for (const HirExpr& operand : expression.operands)
                {
                    out += ' ';
                    append_expression(operand, slots, out);
                }
                out += ')';
                break;
            case HirExprKind::unary:
                out += "(unary ";
                out += hir_unary_op_name(expression.unaryOp);
                out += ' ';
                out += hir_type_name(expression.type);
                for (const HirExpr& operand : expression.operands)
                {
                    out += ' ';
                    append_expression(operand, slots, out);
                }
                out += ')';
                break;
            case HirExprKind::call:
                out += "(call ";
                out += expression.callName;
                out += ' ';
                out += hir_type_name(expression.type);
                for (const HirExpr& operand : expression.operands)
                {
                    out += ' ';
                    append_expression(operand, slots, out);
                }
                out += ')';
                break;
            default:
                out += "(unknownExpr)";
                break;
            }
        }

        inline void append_statement(const HirStmt& statement, std::vector<std::string>& slots,
            std::string& out)
        {
            switch (statement.kind)
            {
            case HirStmtKind::returnStmt:
                out += "(return";
                if (!(statement.value.kind == HirExprKind::integerLiteral &&
                        statement.value.type == HirType::voidType))
                {
                    out += ' ';
                    append_expression(statement.value, slots, out);
                }
                out += ')';
                break;
            case HirStmtKind::exprStmt:
                out += "(exprStmt ";
                append_expression(statement.value, slots, out);
                out += ')';
                break;
            case HirStmtKind::localDecl:
                out += "(localDecl ";
                out += statement.name;
                out += ' ';
                out += hir_type_name(statement.type);
                out += ' ';
                append_expression(statement.value, slots, out);
                out += ')';
                slots.push_back(statement.name);
                break;
            default:
                out += "(unknownStmt)";
                break;
            }
        }

        inline void append_function(const HirFunction& function, std::string& out)
        {
            out += "  (function ";
            out += function.name;

            std::vector<std::string> slots;
            slots.reserve(function.parameters.size());
            for (const std::pair<std::string, HirType>& parameter : function.parameters)
            {
                out += " (param ";
                out += parameter.first;
                out += ' ';
                out += hir_type_name(parameter.second);
                out += ')';
                slots.push_back(parameter.first);
            }

            out += " (result ";
            out += hir_type_name(function.returnType);
            out += ")\n";
            for (const HirStmt& statement : function.body)
            {
                out += "    ";
                append_statement(statement, slots, out);
                out += '\n';
            }
            out += "  )\n";
        }

        inline std::string dump_module(const HirModule& module)
        {
            std::string out = "(module\n";
            for (const HirFunction& function : module.functions)
            {
                append_function(function, out);
            }
            out += ")\n";
            return out;
        }
    }

    // 把一棵 Module 子树下降为 HIR。AST 由调用方持有，HIR 结果为纯值拷贝，
    // 不保留任何 AST 指针，因此 arena 销毁后仍可继续使用。
    inline HirModule lowerToHir(const AstNode& moduleRoot)
    {
        hir_detail::Lowerer lowerer;
        return lowerer.run(moduleRoot);
    }

    // 稳定的文本形式：每个函数一段、每条语句一行，便于测试做子串断言。
    inline std::string dumpHir(const HirModule& module)
    {
        return hir_detail::dump_module(module);
    }
}
