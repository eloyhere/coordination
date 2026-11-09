#pragma once

// ============================================================================
// 最小 C 后端：把 HIR 翻译成一份自包含、可直接交给 C11 编译器的 C 源码。
//
// 契约：
//   * CBackendOptions::emitEntryPoint 为 true 时，若存在无参数的 main
//     （返回 integer64 或 void），就把它改名为 coord_main，并追加
//     `int main(void)` 调用它：integer64 时返回它的值，void 时返回 0；
//     找不到合格 main 时报 E8002，且不生成程序入口；
//   * CBackendOptions::emitComments 控制生成注释（文件头、函数说明、名称转义说明）；
//   * CBackendResult::ok() 只反映诊断里有没有错误：E8001 是警告，不影响 ok()。
//
// 类型映射：integer64 -> int64_t，boolean -> bool，text -> const char*，
// voidType -> void，unknown -> int64_t（同时写 E8001 警告，不阻止生成）。
//
// 生成规则：
//   * 先输出 #include <stdint.h>/<stdbool.h>/<stdio.h>/<string.h>；
//   * 再按 HIR 顺序输出函数原型，最后按同样顺序输出函数定义。C 语言要求
//     先声明后调用，原型保证“调用后定义的函数”也能通过 C11 编译；
//   * 函数签名形如 `int64_t add(int64_t a, bool b)`，无参函数写 `(void)`；
//   * 形参/局部变量沿用 HIR 名字；不是合法 C 标识符（或命中 C 关键字、
//     <stdbool.h>/<stdio.h> 的常用宏名）时转义为 `_esc_` + UTF-8 字节的
//     小写十六进制（例如 `int` -> `_esc_696e74`）；同一函数内重名再追加
//     `_2`、`_3` 后缀。发生了转义/重命名的名字会写在函数定义上方的注释里；
//   * 表达式一律加括号；&&/|| 直接用 C 的短路运算符；整数除法/取余用 C 的
//     `/`、`%`（向零截断，与 HIR 的整数语义一致）；
//   * text 是 const char*，直接用 == 只会比较指针；因此文本比较交给 strcmp：
//     == -> `strcmp(a, b) == 0`，< -> `strcmp(a, b) < 0`，其余类推；
//   * 字面量：整数加 `LL` 后缀；文本按 C 字符串转义（引号、反斜杠、
//     \n/\t/\r，其余控制字符写成三位八进制，例如 NUL -> `\000`）；
//   * 语句：return（无返回值时写 `return;`）、表达式语句、局部声明
//     （`int64_t x = ...;`）。
//
// 诊断码：
//   * E8001（警告）：unknown 类型降级为 int64_t，不阻止生成；
//   * E8002（错误）：未知 HIR 类型/操作码、void 形参、void 局部变量、
//     缺少程序入口等无法生成合法 C 的节点；跳过该节点（整个函数或单条语句）
//     后继续生成。
//
// 当前限制：
//   * 暂不支持文本拼接（text + text）：C 里两个 const char* 相加不合法，
//     报 E8002 并跳过该语句；其它 text 二元运算同理（比较除外）；
//   * 没有运行时支持库：不处理越界、除零、空指针等运行期错误；
//   * HIR 本身没有 if/循环（这些节点在下降阶段已被跳过），后端不做控制流重建。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Diagnostic.h"
#include "Hir.h"

namespace coordination::compiler
{
    // C 后端选项。emitEntryPoint 默认关闭：只翻译 HIR，不额外造程序入口。
    struct CBackendOptions
    {
        bool emitEntryPoint = false;
        bool emitComments = true;
    };

    // 一次 C 后端运行的完整结果：源码 + 诊断，二者都按值返回。
    struct CBackendResult
    {
        std::string source;
        DiagnosticBag diagnostics;

        bool ok() const noexcept
        {
            return !diagnostics.has_errors();
        }
    };

    namespace c_backend_detail
    {
        // 表示“没有找到”的数组下标。
        inline constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

        // 转义标识符用的十六进制数字表（小写，保证结果确定）。
        inline constexpr char kHexDigits[] = "0123456789abcdef";

        inline bool is_identifier_start(char character) noexcept
        {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') || character == '_';
        }

        inline bool is_identifier_continue(char character) noexcept
        {
            return is_identifier_start(character) || (character >= '0' && character <= '9');
        }

        // 是否符合 C 标识符的词法形式（不检查关键字）。
        inline bool has_c_identifier_form(std::string_view name) noexcept
        {
            if (name.empty() || !is_identifier_start(name.front()))
            {
                return false;
            }
            for (const char character : name)
            {
                if (!is_identifier_continue(character))
                {
                    return false;
                }
            }
            return true;
        }

        // C11 关键字，加上本后端固定包含的头文件里会展开成宏的常见名字。
        // 这些名字必须转义：否则生成的声明会被宏替换，或被 C 编译器拒绝。
        inline bool is_reserved_c_name(std::string_view name) noexcept
        {
            static constexpr std::string_view kReserved[] = {
                "auto", "break", "case", "char", "const", "continue", "default", "do",
                "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
                "int", "long", "register", "restrict", "return", "short", "signed", "sizeof",
                "static", "struct", "switch", "typedef", "union", "unsigned", "void",
                "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
                "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
                "bool", "true", "false", "NULL", "EOF", "stdin", "stdout", "stderr",
            };
            for (const std::string_view reserved : kReserved)
            {
                if (name == reserved)
                {
                    return true;
                }
            }
            return false;
        }

        // 把 HIR 名字映射成合法且互不冲突的 C 标识符：
        //   * 合法、非关键字、且不以保留前缀 `_esc_` 开头时原样保留；
        //   * 否则写成 `_esc_` + UTF-8 字节的小写十六进制（能逐字节还原，且无碰撞）。
        inline std::string escape_identifier(std::string_view name)
        {
            if (has_c_identifier_form(name) && !is_reserved_c_name(name) &&
                !name.starts_with("_esc_"))
            {
                return std::string(name);
            }

            std::string escaped = "_esc_";
            escaped.reserve(5u + name.size() * 2u);
            for (const char character : name)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                escaped.push_back(kHexDigits[byte >> 4u]);
                escaped.push_back(kHexDigits[byte & 0x0Fu]);
            }
            return escaped;
        }

        // 在同一作用域里取唯一名字：冲突时依次尝试 `base_2`、`base_3`……
        inline std::string unique_identifier(std::string_view base,
            std::unordered_set<std::string>& used)
        {
            std::string candidate(base);
            std::size_t suffix = 2;
            while (!used.insert(candidate).second)
            {
                candidate.assign(base.data(), base.size());
                candidate += '_';
                candidate += std::to_string(suffix);
                ++suffix;
            }
            return candidate;
        }

        // 按 C 字符串字面量规则转义并写入 out；控制字符用三位八进制，
        // 避免 `\x` 吞掉后面的十六进制数字。
        inline void append_c_string_literal(std::string_view text, std::string& out)
        {
            out.push_back('"');
            for (const char character : text)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                switch (byte)
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
                default:
                    if (byte < 0x20u || byte == 0x7Fu)
                    {
                        out.push_back('\\');
                        out.push_back(static_cast<char>('0' + ((byte >> 6u) & 0x07u)));
                        out.push_back(static_cast<char>('0' + ((byte >> 3u) & 0x07u)));
                        out.push_back(static_cast<char>('0' + (byte & 0x07u)));
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

        // 把 HIR 翻译成 C 源码的实现。实例只使用一次（run 之后即丢弃）。
        class Emitter
        {
        public:
            Emitter(const HirModule& module, CBackendOptions options)
                : module_(module), options_(options)
            {
            }

            CBackendResult run()
            {
                plan_module();

                append_preamble();
                append_prototypes();

                for (std::size_t index = 0; index < plans_.size(); ++index)
                {
                    append_definition(index);
                }
                append_entry_point();

                CBackendResult result;
                result.source = std::move(source_);
                result.diagnostics = std::move(diagnostics_);
                return result;
            }

        private:
            struct ParameterPlan
            {
                std::string hirName; // HIR 里的原始名字（注释用）
                std::string cName;   // 转换后的 C 标识符
                std::string cType;   // 转换后的 C 类型
            };

            struct FunctionPlan
            {
                const HirFunction* function = nullptr;
                std::string cName;
                std::string returnCType;
                std::vector<ParameterPlan> parameters;
                std::vector<std::string> slots; // 参数 + 全部局部变量，按 refIndex 排列
                std::vector<std::pair<std::string, std::string>> renamed; // 原名 -> C 名
                bool emit = false;
            };

            // ---------------- 规划 ----------------

            void plan_module()
            {
                plans_.reserve(module_.functions.size());
                for (const HirFunction& function : module_.functions)
                {
                    FunctionPlan plan;
                    plan.function = &function;
                    plans_.push_back(std::move(plan));
                }

                if (options_.emitEntryPoint)
                {
                    entryTarget_ = find_entry_target();
                    if (entryTarget_ == kNoIndex)
                    {
                        diagnostics_.error("E8002",
                            "emitEntryPoint 已启用，但找不到无参数且返回 integer64 或 void 的 main；"
                            "未生成程序入口",
                            Span{});
                    }
                }

                assign_names();

                for (std::size_t index = 0; index < plans_.size(); ++index)
                {
                    plan_signature(index);
                }
            }

            std::size_t find_entry_target() const
            {
                for (std::size_t index = 0; index < module_.functions.size(); ++index)
                {
                    const HirFunction& function = module_.functions[index];
                    if (function.name == "main" && function.parameters.empty() &&
                        (function.returnType == HirType::integer64 ||
                            function.returnType == HirType::voidType))
                    {
                        return index;
                    }
                }
                return kNoIndex;
            }

            // 先给所有函数分配 C 名字。生成程序入口时 "main" 留给追加的入口函数，
            // 被选中的 HIR main 改名为 coord_main，避免与入口重定义。
            void assign_names()
            {
                std::unordered_set<std::string> used;
                if (entryTarget_ != kNoIndex)
                {
                    used.insert("main");
                }

                for (std::size_t index = 0; index < plans_.size(); ++index)
                {
                    FunctionPlan& plan = plans_[index];
                    std::string base = index == entryTarget_
                        ? std::string("coord_main")
                        : escape_identifier(plan.function->name);
                    plan.cName = unique_identifier(base, used);
                }

                // HIR 调用按名字解析；同名函数以第一个为准（与函数定义顺序一致）。
                for (const FunctionPlan& plan : plans_)
                {
                    callNames_.emplace(plan.function->name, plan.cName);
                }
            }

            // 映射一个 HIR 类型；返回 false 表示该节点无法生成合法 C，调用方应跳过。
            bool map_type(HirType type, const std::string& context, bool allowVoid,
                std::string& out)
            {
                switch (type)
                {
                case HirType::voidType:
                    if (!allowVoid)
                    {
                        diagnostics_.error("E8002",
                            context + "为 void，不能出现在该位置，已跳过该节点", Span{});
                        return false;
                    }
                    out = "void";
                    return true;
                case HirType::integer64:
                    out = "int64_t";
                    return true;
                case HirType::boolean:
                    out = "bool";
                    return true;
                case HirType::text:
                    out = "const char*";
                    return true;
                case HirType::unknown:
                    out = "int64_t";
                    diagnostics_.warning("E8001",
                        context + "为 unknown，已降级为 int64_t", Span{});
                    return true;
                default:
                    diagnostics_.error("E8002",
                        context + "为未知 HIR 类型（值 " +
                            std::to_string(static_cast<int>(type)) + "），已跳过该节点",
                        Span{});
                    return false;
                }
            }

            void plan_signature(std::size_t index)
            {
                FunctionPlan& plan = plans_[index];
                const HirFunction& function = *plan.function;
                plan.emit = true;

                if (!map_type(function.returnType,
                        "函数 '" + function.name + "' 的返回类型", true, plan.returnCType))
                {
                    plan.emit = false;
                    return;
                }

                if (plan.cName != function.name)
                {
                    plan.renamed.emplace_back(function.name, plan.cName);
                }

                std::unordered_set<std::string> usedNames;
                usedNames.reserve(function.parameters.size() + function.body.size());
                for (const std::pair<std::string, HirType>& parameter : function.parameters)
                {
                    std::string parameterType;
                    if (!map_type(parameter.second,
                            "函数 '" + function.name + "' 的形参 '" + parameter.first + "' 的类型",
                            false, parameterType))
                    {
                        plan.emit = false;
                        return;
                    }

                    ParameterPlan planned;
                    planned.hirName = parameter.first;
                    planned.cName = unique_identifier(escape_identifier(parameter.first), usedNames);
                    planned.cType = std::move(parameterType);
                    if (planned.cName != planned.hirName)
                    {
                        plan.renamed.emplace_back(planned.hirName, planned.cName);
                    }
                    plan.parameters.push_back(std::move(planned));
                }

                for (const ParameterPlan& parameter : plan.parameters)
                {
                    plan.slots.push_back(parameter.cName);
                }

                // 槽位顺序必须与 HIR 的 refIndex 一致：参数之后按声明顺序追加局部变量。
                // 即使某条局部声明稍后因类型非法被跳过，它的槽位也要保留，否则下标会错位。
                for (const HirStmt& statement : function.body)
                {
                    if (statement.kind != HirStmtKind::localDecl)
                    {
                        continue;
                    }
                    const std::string slotName =
                        unique_identifier(escape_identifier(statement.name), usedNames);
                    plan.slots.push_back(slotName);
                    if (slotName != statement.name)
                    {
                        plan.renamed.emplace_back(statement.name, slotName);
                    }
                }
            }

            // ---------------- 源码输出 ----------------

            void append_preamble()
            {
                if (options_.emitComments)
                {
                    source_ += "/* 由 Coordination 编译器 C 后端从 HIR 生成，请勿手工修改。 */\n";
                    source_ += "/* 类型映射：integer64 -> int64_t，boolean -> bool，"
                        "text -> const char*，void -> void。 */\n\n";
                }
                source_ += "#include <stdint.h>\n";
                source_ += "#include <stdbool.h>\n";
                source_ += "#include <stdio.h>\n";
                source_ += "#include <string.h>\n\n";
            }

            void append_parameter_list(const FunctionPlan& plan)
            {
                if (plan.parameters.empty())
                {
                    source_ += "void";
                    return;
                }
                for (std::size_t index = 0; index < plan.parameters.size(); ++index)
                {
                    if (index != 0u)
                    {
                        source_ += ", ";
                    }
                    source_ += plan.parameters[index].cType;
                    source_ += ' ';
                    source_ += plan.parameters[index].cName;
                }
            }

            void append_prototypes()
            {
                bool hasFunction = false;
                for (const FunctionPlan& plan : plans_)
                {
                    if (plan.emit)
                    {
                        hasFunction = true;
                        break;
                    }
                }
                if (!hasFunction)
                {
                    return;
                }

                if (options_.emitComments)
                {
                    source_ += "/* 函数原型：C11 要求先声明后调用，原型放在定义之前。 */\n";
                }
                for (const FunctionPlan& plan : plans_)
                {
                    if (!plan.emit)
                    {
                        continue;
                    }
                    source_ += plan.returnCType;
                    source_ += ' ';
                    source_ += plan.cName;
                    source_ += '(';
                    append_parameter_list(plan);
                    source_ += ");\n";
                }
                source_ += '\n';
            }

            void append_definition(std::size_t index)
            {
                const FunctionPlan& plan = plans_[index];
                if (!plan.emit)
                {
                    return;
                }

                if (options_.emitComments)
                {
                    source_ += "/* HIR 函数 ";
                    source_ += plan.function->name;
                    source_ += " */\n";
                    append_rename_comment(plan);
                }

                source_ += plan.returnCType;
                source_ += ' ';
                source_ += plan.cName;
                source_ += '(';
                append_parameter_list(plan);
                source_ += ")\n{\n";

                std::size_t nextSlot = plan.parameters.size();
                for (const HirStmt& statement : plan.function->body)
                {
                    append_statement(plan, statement, nextSlot);
                }
                source_ += "}\n\n";
            }

            void append_rename_comment(const FunctionPlan& plan)
            {
                if (plan.renamed.empty())
                {
                    return;
                }

                source_ += "/* 名称改写（转义/重命名）：";
                for (std::size_t index = 0; index < plan.renamed.size(); ++index)
                {
                    if (index != 0u)
                    {
                        source_ += "；";
                    }
                    source_ += '\'';
                    source_ += plan.renamed[index].first;
                    source_ += "' -> '";
                    source_ += plan.renamed[index].second;
                    source_ += '\'';
                }
                source_ += "（非法 C 标识符转义为 _esc_ + UTF-8 字节小写十六进制，"
                    "程序入口改名为 coord_main，重名追加 _2、_3 后缀） */\n";
            }

            void append_entry_point()
            {
                if (entryTarget_ == kNoIndex || !plans_[entryTarget_].emit)
                {
                    return;
                }

                const FunctionPlan& plan = plans_[entryTarget_];
                if (options_.emitComments)
                {
                    source_ += "/* 程序入口：调用 HIR 函数 main（C 名 ";
                    source_ += plan.cName;
                    source_ += "）。 */\n";
                }

                source_ += "int main(void)\n{\n";
                if (plan.function->returnType == HirType::voidType)
                {
                    source_ += "    ";
                    source_ += plan.cName;
                    source_ += "();\n";
                    source_ += "    return 0;\n";
                }
                else
                {
                    source_ += "    return (int)";
                    source_ += plan.cName;
                    source_ += "();\n";
                }
                source_ += "}\n";
            }

            void append_line(const std::string& line)
            {
                source_ += "    ";
                source_ += line;
                source_ += '\n';
            }

            // 追加一条语句；无法生成时写 E8002 并跳过该语句（局部槽位仍然前进）。
            void append_statement(const FunctionPlan& plan, const HirStmt& statement,
                std::size_t& nextSlot)
            {
                switch (statement.kind)
                {
                case HirStmtKind::returnStmt:
                {
                    if (is_void_return(statement.value))
                    {
                        append_line("return;");
                        return;
                    }
                    std::string expression;
                    if (!append_expression(plan, statement.value, expression))
                    {
                        return;
                    }
                    append_line("return " + expression + ";");
                    return;
                }
                case HirStmtKind::exprStmt:
                {
                    std::string expression;
                    if (!append_expression(plan, statement.value, expression))
                    {
                        return;
                    }
                    append_line(expression + ";");
                    return;
                }
                case HirStmtKind::localDecl:
                {
                    const std::size_t slot = nextSlot;
                    ++nextSlot;
                    if (slot >= plan.slots.size())
                    {
                        diagnostics_.error("E8002",
                            "局部声明没有对应的槽位，已跳过该语句", Span{});
                        return;
                    }

                    std::string cType;
                    if (!map_type(statement.type,
                            "函数 '" + plan.function->name + "' 的局部变量 '" +
                                statement.name + "' 的类型",
                            false, cType))
                    {
                        return;
                    }

                    std::string expression;
                    if (!append_expression(plan, statement.value, expression))
                    {
                        return;
                    }
                    append_line(cType + " " + plan.slots[slot] + " = " + expression + ";");
                    return;
                }
                default:
                    diagnostics_.error("E8002", "未知 HIR 语句类别，已跳过该语句", Span{});
                    return;
                }
            }

            static bool is_void_return(const HirExpr& value) noexcept
            {
                return value.kind == HirExprKind::integerLiteral &&
                    value.type == HirType::voidType;
            }

            // ---------------- 表达式 ----------------

            bool append_expression(const FunctionPlan& plan, const HirExpr& expression,
                std::string& out)
            {
                switch (expression.kind)
                {
                case HirExprKind::integerLiteral:
                    out += std::to_string(expression.integer);
                    out += "LL";
                    return true;
                case HirExprKind::booleanLiteral:
                    out += expression.boolean ? "true" : "false";
                    return true;
                case HirExprKind::textLiteral:
                    append_c_string_literal(expression.text, out);
                    return true;
                case HirExprKind::localRef:
                    if (expression.refIndex >= plan.slots.size())
                    {
                        diagnostics_.error("E8002",
                            "局部引用下标 " + std::to_string(expression.refIndex) +
                                " 超出函数槽位数量 " + std::to_string(plan.slots.size()) +
                                "，已跳过该语句",
                            Span{});
                        return false;
                    }
                    out += plan.slots[expression.refIndex];
                    return true;
                case HirExprKind::binary:
                    return append_binary(plan, expression, out);
                case HirExprKind::unary:
                    return append_unary(plan, expression, out);
                case HirExprKind::call:
                    return append_call(plan, expression, out);
                default:
                    diagnostics_.error("E8002", "未知 HIR 表达式类别，已跳过该语句", Span{});
                    return false;
                }
            }

            static const char* binary_operator(HirBinaryOp op) noexcept
            {
                switch (op)
                {
                case HirBinaryOp::add:
                    return "+";
                case HirBinaryOp::sub:
                    return "-";
                case HirBinaryOp::mul:
                    return "*";
                case HirBinaryOp::div:
                    return "/";
                case HirBinaryOp::mod:
                    return "%";
                case HirBinaryOp::eq:
                    return "==";
                case HirBinaryOp::ne:
                    return "!=";
                case HirBinaryOp::lt:
                    return "<";
                case HirBinaryOp::le:
                    return "<=";
                case HirBinaryOp::gt:
                    return ">";
                case HirBinaryOp::ge:
                    return ">=";
                case HirBinaryOp::logicalAnd:
                    return "&&";
                case HirBinaryOp::logicalOr:
                    return "||";
                default:
                    return nullptr;
                }
            }

            // 文本比较映射到 strcmp 的返回值；非比较运算返回 nullptr。
            static const char* text_comparison(HirBinaryOp op) noexcept
            {
                switch (op)
                {
                case HirBinaryOp::eq:
                    return "== 0";
                case HirBinaryOp::ne:
                    return "!= 0";
                case HirBinaryOp::lt:
                    return "< 0";
                case HirBinaryOp::le:
                    return "<= 0";
                case HirBinaryOp::gt:
                    return "> 0";
                case HirBinaryOp::ge:
                    return ">= 0";
                default:
                    return nullptr;
                }
            }

            bool append_binary(const FunctionPlan& plan, const HirExpr& expression,
                std::string& out)
            {
                if (expression.operands.size() != 2u)
                {
                    diagnostics_.error("E8002",
                        "二元表达式的操作数数量不是 2，已跳过该语句", Span{});
                    return false;
                }

                const bool textOperands = expression.operands[0].type == HirType::text &&
                    expression.operands[1].type == HirType::text;
                if (textOperands)
                {
                    if (expression.binaryOp == HirBinaryOp::add)
                    {
                        diagnostics_.error("E8002",
                            "暂不支持文本拼接（text + text），已跳过该语句", Span{});
                        return false;
                    }
                    const char* const comparison = text_comparison(expression.binaryOp);
                    if (comparison == nullptr)
                    {
                        diagnostics_.error("E8002",
                            "文本不支持该二元运算，已跳过该语句", Span{});
                        return false;
                    }
                    return append_text_comparison(plan, expression, comparison, out);
                }

                const char* const op = binary_operator(expression.binaryOp);
                if (op == nullptr)
                {
                    diagnostics_.error("E8002",
                        "未知 HIR 二元操作码，已跳过该语句", Span{});
                    return false;
                }

                std::string left;
                std::string right;
                if (!append_expression(plan, expression.operands[0], left) ||
                    !append_expression(plan, expression.operands[1], right))
                {
                    return false;
                }

                out += '(';
                out += left;
                out += ' ';
                out += op;
                out += ' ';
                out += right;
                out += ')';
                return true;
            }

            bool append_text_comparison(const FunctionPlan& plan, const HirExpr& expression,
                const char* comparison, std::string& out)
            {
                std::string left;
                std::string right;
                if (!append_expression(plan, expression.operands[0], left) ||
                    !append_expression(plan, expression.operands[1], right))
                {
                    return false;
                }

                out += "(strcmp(";
                out += left;
                out += ", ";
                out += right;
                out += ") ";
                out += comparison;
                out += ')';
                return true;
            }

            static const char* unary_operator(HirUnaryOp op) noexcept
            {
                switch (op)
                {
                case HirUnaryOp::negate:
                    return "-";
                case HirUnaryOp::logicalNot:
                    return "!";
                case HirUnaryOp::bitwiseNot:
                    return "~";
                default:
                    return nullptr;
                }
            }

            bool append_unary(const FunctionPlan& plan, const HirExpr& expression,
                std::string& out)
            {
                if (expression.operands.size() != 1u)
                {
                    diagnostics_.error("E8002",
                        "一元表达式的操作数数量不是 1，已跳过该语句", Span{});
                    return false;
                }

                const char* const op = unary_operator(expression.unaryOp);
                if (op == nullptr)
                {
                    diagnostics_.error("E8002",
                        "未知 HIR 一元操作码，已跳过该语句", Span{});
                    return false;
                }

                if (expression.operands[0].type == HirType::text &&
                    expression.unaryOp != HirUnaryOp::logicalNot)
                {
                    diagnostics_.error("E8002",
                        "文本不支持该一元运算，已跳过该语句", Span{});
                    return false;
                }

                std::string operand;
                if (!append_expression(plan, expression.operands[0], operand))
                {
                    return false;
                }

                out += '(';
                out += op;
                out += operand;
                out += ')';
                return true;
            }

            bool append_call(const FunctionPlan& plan, const HirExpr& expression,
                std::string& out)
            {
                const auto found = callNames_.find(expression.callName);
                if (found != callNames_.end())
                {
                    out += found->second;
                }
                else
                {
                    out += escape_identifier(expression.callName);
                }

                out += '(';
                for (std::size_t index = 0; index < expression.operands.size(); ++index)
                {
                    if (index != 0u)
                    {
                        out += ", ";
                    }
                    std::string argument;
                    if (!append_expression(plan, expression.operands[index], argument))
                    {
                        return false;
                    }
                    out += argument;
                }
                out += ')';
                return true;
            }

            // ---------------- 状态 ----------------

            const HirModule& module_;
            CBackendOptions options_;
            std::vector<FunctionPlan> plans_;
            std::unordered_map<std::string, std::string> callNames_;
            std::size_t entryTarget_ = kNoIndex;
            std::string source_;
            DiagnosticBag diagnostics_;
        };
    }

    // 把 HIR 翻译成 C11 源码；诊断与源码都放在返回值里，不抛异常。
    inline CBackendResult emitC(const HirModule& module, CBackendOptions options = {})
    {
        c_backend_detail::Emitter emitter(module, options);
        return emitter.run();
    }
}
