#pragma once

// ============================================================================
// 递归下降 Parser：Token 流 -> AstNode 树（Coordination 语言前端）
//
// 设计约束：
//   * 纯头文件、无异常：任何输入都返回 AstKind::Module 根节点，错误进 DiagnosticBag；
//   * 每个循环都必须让游标前进（parse() 与块/成员循环另有"无进展就强制消费"兜底），
//     禁止死循环；
//   * 不做自动分号插入：语句/声明终结符必须是 ';'，缺失即报 E2xxx；
//   * 只负责语法，不做名字解析、类型检查与语义校验（sema 日后消费本树）。
//
// 诊断码（E2xxx 段，供工具链稳定引用）：
//   E2001 期望指定 token（常见：缺少 ';'、':'、'('）
//   E2002 非预期 token / 无法识别的声明或成员（恢复时跳到同步点）
//   E2003 结构未闭合（缺少 '}'、')'、'>'、']'，或类型声明缺少 '{'）
//   E2004 期望类型
//   E2005 期望表达式
//   E2006 期望标识符（声明名、字段名、参数名、import 名等）
//
// ----------------------------------------------------------------------------
// AST 约定（供 sema 使用；Ast.h 不修改，所有约定均写在此处）
//
// 通用：
//   * AstNode::text 只用于"名字/原文/运算符/少量结构化前缀"，绝不存入完整源码；
//   * 节点 span 覆盖该结构从首 token 到末 token 的源码区间（近似，够诊断用）；
//   * 错误节点统一为 AstKind::Error，text 为空，span 指向出错 token；
//     错误节点会尽量挂到当前父节点下，便于消费方跳过。
//
// 根节点：
//   * Module：span 覆盖整个源文件；孩子按源码顺序排列：
//     ImportDecl / ExportDecl / 各类声明。
//   * 显式 `module a.b;` 不产生孩子节点（AstKind 无 ModuleDecl），
//     模块名写入根节点 text（点分路径，如 "a.b"）；未声明时 text 为空。
//
// 导入与导出：
//   * ImportDecl：text = 点分模块路径（如 "math.vector"）；flags 含 exported 表示重导出；
//     孩子为每个非限定导入项，kind = NamedType：
//       - "A"        普通导入项
//       - "A as B"   起别名（保留 " as " 原文形式）
//       - "as B"     整模块别名 `import m as B;`（唯一孩子）
//     无花括号的 `import m;` 没有孩子。
//   * ExportDecl：text = "export"，唯一孩子 = 被导出声明（ImportDecl 或普通声明）；
//     被导出节点自身的 flags 同时带 AstFlags::exported。
//
// 类型声明：
//   * ClassDecl / InterfaceDecl / ConceptDecl：
//       text = 名字；flags = exported/open/final_/hidden 等；
//       孩子顺序：AnnotationUse* -> TypeParamList? -> 父类型（NamedType/GenericType）* -> 成员*。
//   * AnnotationDecl：text = 名字；flags 可含 repeatable/exported；
//       孩子：AnnotationUse* -> ParamList。
//   * TypeAliasDecl / DistinctTypeDecl：text = 名字；孩子：AnnotationUse* -> 目标类型；
//       DistinctTypeDecl 的 flags 同时带 AstFlags::distinct。
//
// 成员（Class/Interface/Concept 内）：
//   * FieldDecl：text = "mutable:名字" 或 "const:名字"（语言里 mutable 不是关键字，
//       AstFlags 也无法表达可变性，故按约定用 text 前缀；未写修饰符时按语言规范默认 const）。
//       孩子：AnnotationUse* -> Type -> 初始化表达式?；flags 含 open/final_/hidden/comptime 等。
//   * MethodDecl：text = 名字；flags 含 override_/final_/hidden/comptime 等；
//       孩子：AnnotationUse* -> ParamList -> 返回 Type? -> BlockStmt?（声明无函数体时省略）。
//   * OperatorDecl：text = 运算符原文（"+"、"=="、"[]"、"[]="、"()" 等），其余同 MethodDecl。
//   * ConstructorDecl：text = "init"；孩子：AnnotationUse* -> ParamList -> BlockStmt?。
//   * DestructorDecl：text = "deinit"；孩子：AnnotationUse* -> BlockStmt?。
//   * 成员带标注时，AnnotationUse 永远排在该成员孩子的最前面；无标注时上述顺序即下标顺序。
//
// 模块级函数与变量（语言规范示例使用；任务要求之外的有用补充）：
//   * FunctionDecl：text = 名字；flags 含 exported/comptime 等；
//       孩子：AnnotationUse* -> ParamList -> 返回 Type? -> BlockStmt?。
//   * ConstantDecl（const）/ VariableDecl（mutable）：text = 名字；flags 含 exported/comptime；
//       孩子：AnnotationUse* -> Type? -> 初始化表达式?。
//
// 参数与类型参数：
//   * ParamList / Param：Param.text = "const:名字" / "mutable:名字"（默认 const）；
//       Param 孩子：AnnotationUse* -> Type -> 默认值表达式?。
//   * TypeParamList / TypeParam：TypeParam.text = "名字" / "out:名字" / "in:名字"；
//       TypeParam 孩子为约束类型（`T: A + B` 时依次是 NamedType A、NamedType B）。
//
// 标注：
//   * AnnotationUse：text = 标注名；唯一孩子 AnnotationArgList（无括号时为空节点）。
//   * AnnotationArgList：孩子为实参。实参一般是表达式；以 '['、'&'、'*'、'dyn'、'void'
//     开头时按类型解析（覆盖 `@reflect([]character(8))` 这类写法），其余按表达式。
//
// 类型节点：
//   * NamedType：text = 名字（含 "void" / "module" 两个关键字形态）。
//   * GenericType：text = 名字；孩子：TypeArgList（`Name<T, U>` 的类型实参）
//       或 ArgumentList（`integer(32)` 这类编译期类型应用的实参，孩子按表达式解析）；
//       两种形态由孩子的 kind 区分。
//   * ReferenceType：孩子 = 被引用类型（`&T`）。
//   * MutableReferenceType：孩子 = 被引用类型（`&mut T`）。
//   * PointerType：孩子 = 指针目标类型；text = "const" 表示 `*const T`，否则空。
//   * ArrayType：`[T; N]` 孩子 = 元素类型、长度表达式；`[]T` 时 text = "slice"，
//       唯一孩子 = 元素类型（AstKind 无 SliceType，故复用 ArrayType 加 text 标记）。
//   * DynType：孩子 = 约束类型（`dyn I`）。
//   * FunctionType：孩子 = TypeArgList（参数类型，可为空）+ 返回类型（`(A, B): R`）。
//
// 语句：
//   * BlockStmt：孩子 = 语句；空语句（单独的 ';'）也表示为无孩子的 BlockStmt。
//   * ExprStmt：唯一孩子 = 表达式。
//   * IfStmt：孩子 = 条件表达式、then 语句、else 语句?（固定前两位）。
//   * WhileStmt：孩子 = 条件表达式、循环体语句。
//   * ForStmt：text = 三段存在性掩码 "XYZ"（X=init、Y=cond、Z=step，'1' 存在）；
//       孩子按 init? -> cond? -> step? -> body 排列，body 恒为最后一个。
//       用掩码是为了让 `for (;; step)` 与 `for (; cond;)` 可区分。
//       init 若为局部声明则是 LocalDeclStmt，若为表达式则包成 ExprStmt；cond/step 为裸表达式。
//   * ReturnStmt：孩子 = 返回表达式?。
//   * BreakStmt / ContinueStmt：无孩子。
//   * LocalDeclStmt：text = "const:名字" / "mutable:名字"；
//       孩子：Type? -> 初始化表达式?（支持 `const x = 1;` 的推断写法）。
//
// 表达式：
//   * LiteralExpr：text = 字面量原文（整数/浮点/字符串/字符/true/false/null）。
//   * IdentifierExpr：text = 名字；ThisExpr / SuperExpr：无孩子、text 空。
//   * UnaryExpr：text = 运算符原文；孩子 = 操作数。
//   * BinaryExpr / AssignExpr：text = 运算符原文；孩子 = 左、右操作数。
//   * ConditionalExpr：text = "?"；孩子 = 条件、真值、假值。
//   * CallExpr：孩子 = 被调用表达式、ArgumentList。
//   * ArgumentList：孩子 = 实参表达式。
//   * MemberExpr：text = 成员名；孩子 = 基表达式。
//   * IndexExpr：孩子 = 基表达式、下标表达式。
//   * NewExpr：孩子 = 类型、ArgumentList（无括号时为空 ArgumentList）。
//   * ParenExpr：孩子 = 括号内表达式（保留括号，不折叠）。
//
// 运算符优先级（低 -> 高，与任务约定一致）：
//   = += -= *= /= %=（右结合） -> ?: -> || -> && -> == != -> < <= > >=
//   -> + - -> * / % -> 一元 + - ! ~ -> 后缀 () [] . -> 基本表达式。
//
// 暂未解析（会以 E2xxx 报告并恢复，留待后续迭代）：
//   * 泛型函数/方法的类型参数列表（`f<T>(...)`；类型声明的 TypeParamList 已支持）；
//   * if 表达式、comptime if / comptime for；
//   * 表达式中的 is / as，以及取地址 &、解引用 * 等；
//   * interface/class 之外的其它声明形态（property、static 等）。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Lexer.h"
#include "Span.h"
#include "Token.h"

namespace coordination::compiler
{
    class Parser
    {
    public:
        Parser(SourceId source, std::string_view text, DiagnosticBag& diagnostics)
            : source_(source),
              text_(text),
              diagnostics_(diagnostics),
              tokens_(Lexer(source, text, diagnostics).tokenize())
        {
        }

        // 解析整个源文本，恒返回 Module 根节点（失败时为部分树）。
        // 词法诊断已在构造时进入同一个 DiagnosticBag；语法诊断在此追加。
        AstNode* parse(AstArena& arena)
        {
            arena_ = &arena;
            root_ = arena_->make(AstKind::Module,
                Span::from_bounds(source_, 0u, static_cast<std::uint32_t>(text_.size())));
            module_seen_ = false;
            top_level_started_ = false;
            index_ = 0;

            while (!at_end_token())
            {
                const std::size_t before = index_;
                AstNode* item = parse_top_level_item();
                if (item != nullptr)
                {
                    arena_->adopt(root_, item);
                    top_level_started_ = true;
                }
                if (index_ == before)
                {
                    // 兜底：任何分支都不允许零进展，否则强制消费一个 token。
                    error_at_current("E2002", "非预期的 token，无法开始解析顶层声明");
                    advance();
                }
            }
            return root_;
        }

    private:
        // 声明修饰符集合：flags 承载 AstFlags，const/mutable 另行记录（字段/参数需要）。
        struct Modifiers
        {
            AstFlags flags = AstFlags::none;
            bool has_const = false;
            bool has_mutable = false;
        };

        // ---------------- token 游标 ----------------

        const Token& current() const noexcept
        {
            return tokens_[index_];
        }

        // 向前看 offset 个 token；越过文件尾时返回 EndOfFile。
        const Token& peek_token(std::size_t offset = 1) const noexcept
        {
            const std::size_t position = index_ + offset;
            return position < tokens_.size() ? tokens_[position] : tokens_.back();
        }

        bool check(TokenKind kind) const noexcept
        {
            return current().kind == kind;
        }

        bool at_end_token() const noexcept
        {
            return check(TokenKind::EndOfFile);
        }

        // 返回当前 token 并前进；已到 EndOfFile 时不再前进（避免越界）。
        const Token& advance() noexcept
        {
            const Token& token = tokens_[index_];
            if (index_ + 1 < tokens_.size())
            {
                ++index_;
            }
            return token;
        }

        bool match(TokenKind kind) noexcept
        {
            if (!check(kind))
            {
                return false;
            }
            advance();
            return true;
        }

        bool is_identifier_text(std::string_view text) const noexcept
        {
            return check(TokenKind::Identifier) && std::string_view(current().text) == text;
        }

        // mutable 不是关键字（Token.h 无 KwMutable），按标识符 + 后随标识符识别为修饰符。
        bool is_mutable_modifier() const noexcept
        {
            return is_identifier_text("mutable") && peek_token(1).kind == TokenKind::Identifier;
        }

        bool is_local_decl_start() const noexcept
        {
            return check(TokenKind::KwConst) || is_mutable_modifier();
        }

        // ---------------- 诊断 ----------------

        void error_at_current(std::string code, std::string message)
        {
            diagnostics_.error(std::move(code), std::move(message), current().span);
        }

        bool expect(TokenKind kind, std::string code, std::string message)
        {
            if (match(kind))
            {
                return true;
            }
            error_at_current(std::move(code), std::move(message));
            return false;
        }

        // 期望闭合符号；到文件尾时用 E2003 表达"结构未闭合"。
        bool expect_closing(TokenKind kind, char closing, std::string_view construct)
        {
            if (match(kind))
            {
                return true;
            }
            std::string message("`");
            message.append(construct.data(), construct.size());
            message += "` 未闭合：缺少 '";
            message += closing;
            message += "'";
            diagnostics_.error("E2003", std::move(message), current().span);
            return false;
        }

        bool expect_semicolon()
        {
            return expect(TokenKind::Semicolon, "E2001", "期望 ';'（语句或声明必须以分号结束）");
        }

        // ---------------- 源码区间 ----------------

        Span last_span() const noexcept
        {
            return index_ > 0 ? tokens_[index_ - 1].span : current().span;
        }

        // 从 start 扩展到最近一个已消费 token。若尚未消费 token，
        // 不能把 start 之前的 token 合进来（否则 span 会向文件头膨胀）。
        Span span_from(Span start) const noexcept
        {
            Span end = start;
            if (index_ > 0)
            {
                const Span previous = tokens_[index_ - 1].span;
                if (previous.begin >= start.begin)
                {
                    end = previous;
                }
            }
            return Span::covering(start, end);
        }

        // ---------------- AST 构造 ----------------

        AstNode* make_node(AstKind kind, Span span, std::string text = {},
            AstFlags flags = AstFlags::none)
        {
            return arena_->make(kind, span, std::move(text), flags);
        }

        AstNode* make_error(Span span)
        {
            return make_node(AstKind::Error, span);
        }

        void attach(AstNode* parent, AstNode* child)
        {
            if (parent != nullptr && child != nullptr)
            {
                arena_->adopt(parent, child);
            }
        }

        void attach_annotations(AstNode* parent, const std::vector<AstNode*>& annotations)
        {
            for (AstNode* annotation : annotations)
            {
                attach(parent, annotation);
            }
        }

        // 导出声明统一包一层 ExportDecl；错误节点不包装。
        AstNode* wrap_export(AstNode* inner, Span export_span, bool exported)
        {
            if (!exported || inner == nullptr || inner->kind == AstKind::Error)
            {
                return inner;
            }
            AstNode* wrapper = make_node(AstKind::ExportDecl, Span::covering(export_span, inner->span), "export");
            attach(wrapper, inner);
            return wrapper;
        }

        // ---------------- 顶层 ----------------

        AstNode* parse_top_level_item()
        {
            std::vector<AstNode*> annotations;
            AstFlags flags = AstFlags::none;
            Span export_span{};
            bool exported = false;

            // 标注、export 与其它修饰符允许交错出现（例如 `@a export class`）。
            for (;;)
            {
                if (check(TokenKind::At))
                {
                    annotations.push_back(parse_annotation_use());
                    continue;
                }
                if (check(TokenKind::KwExport))
                {
                    export_span = Span::covering(export_span, advance().span);
                    flags = flags | AstFlags::exported;
                    exported = true;
                    continue;
                }
                if (match(TokenKind::KwOpen))
                {
                    flags = flags | AstFlags::open;
                    continue;
                }
                if (match(TokenKind::KwFinal))
                {
                    flags = flags | AstFlags::final_;
                    continue;
                }
                if (match(TokenKind::KwHide))
                {
                    flags = flags | AstFlags::hidden;
                    continue;
                }
                if (match(TokenKind::KwComptime))
                {
                    flags = flags | AstFlags::comptime;
                    continue;
                }
                if (match(TokenKind::KwRepeatable))
                {
                    flags = flags | AstFlags::repeatable;
                    continue;
                }
                break;
            }

            if (check(TokenKind::KwModule))
            {
                if (exported || !annotations.empty() || flags != AstFlags::none)
                {
                    error_at_current("E2002", "module 声明不能带标注或修饰符");
                }
                return parse_module_decl();
            }

            if (check(TokenKind::KwImport))
            {
                if (!annotations.empty())
                {
                    error_at_current("E2002", "import 声明不能带标注");
                }
                AstNode* import_decl = parse_import_decl(flags);
                return wrap_export(import_decl, export_span, exported);
            }

            AstNode* declaration = nullptr;
            if (check(TokenKind::KwClass))
            {
                declaration = parse_type_decl(AstKind::ClassDecl, annotations, flags);
            }
            else if (check(TokenKind::KwInterface))
            {
                declaration = parse_type_decl(AstKind::InterfaceDecl, annotations, flags);
            }
            else if (check(TokenKind::KwConcept))
            {
                declaration = parse_type_decl(AstKind::ConceptDecl, annotations, flags);
            }
            else if (check(TokenKind::KwAnnotation))
            {
                declaration = parse_annotation_decl(annotations, flags);
            }
            else if (check(TokenKind::KwType))
            {
                declaration = parse_type_alias(annotations, flags, false);
            }
            else if (check(TokenKind::KwDistinct))
            {
                declaration = parse_type_alias(annotations, flags, true);
            }
            else if (check(TokenKind::KwConst) || is_mutable_modifier())
            {
                declaration = parse_variable_decl(annotations, flags, true);
            }
            else if (check(TokenKind::KwOperator))
            {
                Modifiers mods;
                mods.flags = flags;
                declaration = parse_operator_decl(annotations, mods);
            }
            else if (check(TokenKind::Identifier) && peek_token(1).kind == TokenKind::LParen)
            {
                declaration = parse_function(annotations, flags);
            }

            if (declaration != nullptr)
            {
                return wrap_export(declaration, export_span, exported);
            }

            if (exported || !annotations.empty())
            {
                error_at_current("E2002", "标注或 export 之后缺少声明");
            }
            else
            {
                error_at_current("E2002", "非预期的 token：此处应开始一条声明");
            }
            const Span error_span = current().span;
            synchronize_top_level();
            return make_error(error_span);
        }

        // `module a.b;`：不产生孩子节点，名字写入 Module 根节点的 text。
        AstNode* parse_module_decl()
        {
            const Span start = current().span;
            advance(); // module
            const std::string path = parse_dotted_path();
            if (!expect_semicolon())
            {
                synchronize_top_level();
            }

            if (module_seen_ || top_level_started_)
            {
                diagnostics_.error("E2002", "module 声明只能出现一次，且必须位于文件开头", start);
            }
            else
            {
                module_seen_ = true;
                if (root_ != nullptr)
                {
                    root_->text = path;
                }
            }
            return nullptr;
        }

        AstNode* parse_import_decl(AstFlags flags)
        {
            const Span start = current().span;
            advance(); // import

            AstNode* node = make_node(AstKind::ImportDecl, start, {}, flags);
            node->text = parse_dotted_path();

            if (check(TokenKind::Dot) && peek_token(1).kind == TokenKind::LBrace)
            {
                advance(); // '.'
                advance(); // '{'
                while (!check(TokenKind::RBrace) && !at_end_token())
                {
                    if (check(TokenKind::Identifier))
                    {
                        const Token& name = advance();
                        std::string item_text = name.text;
                        Span item_span = name.span;
                        if (match(TokenKind::KwAs))
                        {
                            item_text += " as ";
                            if (check(TokenKind::Identifier))
                            {
                                const Token& alias = advance();
                                item_text += alias.text;
                                item_span = Span::covering(item_span, alias.span);
                            }
                            else
                            {
                                error_at_current("E2006", "import 别名应为标识符");
                            }
                        }
                        attach(node, make_node(AstKind::NamedType, item_span, std::move(item_text)));
                    }
                    else
                    {
                        error_at_current("E2006", "import 花括号内应为导入名");
                        if (!check(TokenKind::RBrace))
                        {
                            advance();
                        }
                    }

                    if (!match(TokenKind::Comma))
                    {
                        break;
                    }
                    if (check(TokenKind::RBrace))
                    {
                        break; // 允许尾随逗号
                    }
                }
                expect_closing(TokenKind::RBrace, '}', "import 列表");
            }
            else if (match(TokenKind::KwAs))
            {
                // 整模块别名：唯一孩子 NamedType，text = "as 别名"。
                std::string item_text = "as ";
                Span item_span = last_span();
                if (check(TokenKind::Identifier))
                {
                    const Token& alias = advance();
                    item_text += alias.text;
                    item_span = Span::covering(item_span, alias.span);
                }
                else
                {
                    error_at_current("E2006", "import 别名应为标识符");
                }
                attach(node, make_node(AstKind::NamedType, item_span, std::move(item_text)));
            }

            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        // 点分路径：a.b.c；只消费"点后紧跟标识符"的片段，
        // 这样 import 的 `.{` 与 module 结尾的 ';' 都能正确停下。
        std::string parse_dotted_path()
        {
            std::string path;
            if (!check(TokenKind::Identifier))
            {
                error_at_current("E2006", "期望模块名标识符");
                return path;
            }
            path = advance().text;
            while (check(TokenKind::Dot) && peek_token(1).kind == TokenKind::Identifier)
            {
                advance(); // '.'
                path += '.';
                path += advance().text;
            }
            return path;
        }

        // ---------------- 声明 ----------------

        AstNode* parse_type_decl(AstKind kind, const std::vector<AstNode*>& annotations, AstFlags flags)
        {
            const Span start = current().span;
            advance(); // class / interface / concept

            AstNode* node = make_node(kind, start, {}, flags);
            attach_annotations(node, annotations);

            if (check(TokenKind::Identifier))
            {
                node->text = advance().text;
            }
            else
            {
                error_at_current("E2006", "类型声明缺少名字");
            }

            if (check(TokenKind::Lt))
            {
                attach(node, parse_type_param_list());
            }

            if (match(TokenKind::Colon))
            {
                // 父类 / 父接口列表：逗号分隔，直到 '{'。
                for (;;)
                {
                    attach(node, parse_type());
                    if (!match(TokenKind::Comma))
                    {
                        break;
                    }
                    if (check(TokenKind::LBrace) || at_end_token())
                    {
                        break;
                    }
                }
            }

            if (!check(TokenKind::LBrace))
            {
                error_at_current("E2003", "类型声明缺少 '{'");
                node->span = span_from(start);
                return node;
            }
            advance(); // '{'

            while (!check(TokenKind::RBrace) && !at_end_token())
            {
                while (match(TokenKind::Semicolon))
                {
                }
                if (check(TokenKind::RBrace) || at_end_token())
                {
                    break;
                }

                const std::size_t before = index_;
                AstNode* member = parse_member();
                attach(node, member);
                while (match(TokenKind::Semicolon))
                {
                }
                if (index_ == before)
                {
                    error_at_current("E2002", "无法识别的成员声明");
                    advance();
                }
            }

            expect_closing(TokenKind::RBrace, '}', "类型声明");
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_annotation_decl(const std::vector<AstNode*>& annotations, AstFlags flags)
        {
            const Span start = current().span;
            advance(); // annotation

            AstNode* node = make_node(AstKind::AnnotationDecl, start, {}, flags);
            attach_annotations(node, annotations);

            if (check(TokenKind::Identifier))
            {
                node->text = advance().text;
            }
            else
            {
                error_at_current("E2006", "annotation 声明缺少名字");
            }

            attach(node, parse_param_list());
            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        // `type A = B;` 或 `distinct type A = B;`
        AstNode* parse_type_alias(const std::vector<AstNode*>& annotations, AstFlags flags, bool distinct)
        {
            const Span start = current().span;
            if (distinct)
            {
                advance(); // distinct
                flags = flags | AstFlags::distinct;
                expect(TokenKind::KwType, "E2001", "distinct 之后应为 type");
            }
            else
            {
                advance(); // type
            }

            AstNode* node = make_node(distinct ? AstKind::DistinctTypeDecl : AstKind::TypeAliasDecl, start, {}, flags);
            attach_annotations(node, annotations);

            if (check(TokenKind::Identifier))
            {
                node->text = advance().text;
            }
            else
            {
                error_at_current("E2006", "类型别名缺少名字");
            }

            if (match(TokenKind::Assign))
            {
                attach(node, parse_type());
            }
            else
            {
                error_at_current("E2001", "类型别名缺少 '='");
            }

            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        // const/mutable 变量声明：
        //   模块级 -> ConstantDecl / VariableDecl，text = 名字；
        //   语句级 -> LocalDeclStmt，text = "const:名字" / "mutable:名字"（与字段约定一致）。
        AstNode* parse_variable_decl(const std::vector<AstNode*>& annotations, AstFlags flags, bool top_level)
        {
            const bool is_mutable = is_mutable_modifier();
            const Span start = current().span;
            advance(); // const 或 mutable

            AstNode* node = nullptr;
            if (top_level)
            {
                node = make_node(is_mutable ? AstKind::VariableDecl : AstKind::ConstantDecl, start, {}, flags);
            }
            else
            {
                node = make_node(AstKind::LocalDeclStmt, start);
            }
            attach_annotations(node, annotations);

            if (check(TokenKind::Identifier))
            {
                const Token& name = advance();
                if (top_level)
                {
                    node->text = name.text;
                }
                else
                {
                    node->text = is_mutable ? "mutable:" + name.text : "const:" + name.text;
                }
            }
            else
            {
                error_at_current("E2006", "变量声明缺少名字");
            }

            bool has_type = false;
            bool has_initializer = false;
            if (match(TokenKind::Colon))
            {
                attach(node, parse_type());
                has_type = true;
            }
            if (match(TokenKind::Assign))
            {
                attach(node, parse_expression());
                has_initializer = true;
            }
            if (!has_type && !has_initializer)
            {
                error_at_current("E2002", top_level ? "模块级变量缺少类型或初始化器"
                                                    : "局部变量缺少类型或初始化器");
            }

            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        // 顶层函数：name(params): Type { ... } / ;（语言规范示例使用该形态）。
        AstNode* parse_function(const std::vector<AstNode*>& annotations, AstFlags flags)
        {
            const Span start = current().span;
            const Token& name = advance(); // Identifier

            AstNode* node = make_node(AstKind::FunctionDecl, start, name.text, flags);
            attach_annotations(node, annotations);
            attach(node, parse_param_list());
            if (match(TokenKind::Colon))
            {
                attach(node, parse_type());
            }
            attach(node, parse_body_or_semicolon("函数"));
            node->span = span_from(start);
            return node;
        }

        // ---------------- 成员 ----------------

        AstNode* parse_member()
        {
            const std::vector<AstNode*> annotations = parse_annotation_uses();
            const Modifiers mods = parse_member_modifiers();

            if (check(TokenKind::KwInit))
            {
                return parse_constructor(annotations, mods);
            }
            if (check(TokenKind::KwDeinit))
            {
                return parse_destructor(annotations, mods);
            }
            if (check(TokenKind::KwOperator))
            {
                return parse_operator_decl(annotations, mods);
            }
            if (check(TokenKind::Identifier))
            {
                const TokenKind next = peek_token(1).kind;
                if (next == TokenKind::LParen)
                {
                    return parse_method(annotations, mods);
                }
                if (next == TokenKind::Colon)
                {
                    return parse_field(annotations, mods);
                }
            }

            const Span error_span = current().span;
            error_at_current("E2002", "无法识别的成员声明");
            synchronize_member();
            return make_error(error_span);
        }

        AstNode* parse_field(const std::vector<AstNode*>& annotations, const Modifiers& mods)
        {
            const Span start = current().span;
            const Token& name = advance(); // Identifier

            AstNode* node = make_node(AstKind::FieldDecl, start);
            node->flags = mods.flags;
            node->text = (mods.has_mutable ? "mutable:" : "const:") + name.text;
            attach_annotations(node, annotations);

            expect(TokenKind::Colon, "E2001", "字段声明缺少 ':'");
            attach(node, parse_type());
            if (match(TokenKind::Assign))
            {
                attach(node, parse_expression());
            }

            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_method(const std::vector<AstNode*>& annotations, const Modifiers& mods)
        {
            const Span start = current().span;
            const Token& name = advance(); // Identifier

            AstNode* node = make_node(AstKind::MethodDecl, start, name.text, mods.flags);
            attach_annotations(node, annotations);
            attach(node, parse_param_list());
            if (match(TokenKind::Colon))
            {
                attach(node, parse_type());
            }
            attach(node, parse_body_or_semicolon("方法"));
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_constructor(const std::vector<AstNode*>& annotations, const Modifiers& mods)
        {
            const Span start = current().span;
            advance(); // init

            AstNode* node = make_node(AstKind::ConstructorDecl, start, "init", mods.flags);
            attach_annotations(node, annotations);
            attach(node, parse_param_list());

            if (match(TokenKind::Colon))
            {
                error_at_current("E2002", "构造器不能声明返回类型");
                parse_type(); // 语法上吃掉，保持恢复
            }

            attach(node, parse_body_or_semicolon("构造器"));
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_destructor(const std::vector<AstNode*>& annotations, const Modifiers& mods)
        {
            const Span start = current().span;
            advance(); // deinit

            AstNode* node = make_node(AstKind::DestructorDecl, start, "deinit", mods.flags);
            attach_annotations(node, annotations);

            // deinit 允许写 `()`，也允许直接跟函数体。
            if (match(TokenKind::LParen))
            {
                expect_closing(TokenKind::RParen, ')', "deinit 参数列表");
            }

            attach(node, parse_body_or_semicolon("析构器"));
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_operator_decl(const std::vector<AstNode*>& annotations, const Modifiers& mods)
        {
            const Span start = current().span;
            advance(); // operator

            const std::string operator_text = parse_operator_token();
            AstNode* node = make_node(AstKind::OperatorDecl, start, operator_text, mods.flags);
            attach_annotations(node, annotations);
            attach(node, parse_param_list());
            if (match(TokenKind::Colon))
            {
                attach(node, parse_type());
            }
            attach(node, parse_body_or_semicolon("运算符"));
            node->span = span_from(start);
            return node;
        }

        // 运算符原文：`+`、`==`、`[]`、`[]=`、`()` 等。
        std::string parse_operator_token()
        {
            if (match(TokenKind::LBracket))
            {
                expect_closing(TokenKind::RBracket, ']', "下标运算符");
                if (match(TokenKind::Assign))
                {
                    return "[]=";
                }
                return "[]";
            }
            if (match(TokenKind::LParen))
            {
                expect_closing(TokenKind::RParen, ')', "调用运算符");
                return "()";
            }
            if (is_overloadable_operator(current().kind))
            {
                return advance().text;
            }
            error_at_current("E2002", "operator 之后应为可重载的运算符");
            return std::string();
        }

        Modifiers parse_member_modifiers()
        {
            Modifiers mods;
            for (;;)
            {
                if (match(TokenKind::KwOverride))
                {
                    mods.flags = mods.flags | AstFlags::override_;
                    continue;
                }
                if (match(TokenKind::KwHide))
                {
                    mods.flags = mods.flags | AstFlags::hidden;
                    continue;
                }
                if (match(TokenKind::KwOpen))
                {
                    mods.flags = mods.flags | AstFlags::open;
                    continue;
                }
                if (match(TokenKind::KwFinal))
                {
                    mods.flags = mods.flags | AstFlags::final_;
                    continue;
                }
                if (match(TokenKind::KwComptime))
                {
                    mods.flags = mods.flags | AstFlags::comptime;
                    continue;
                }
                if (match(TokenKind::KwConst))
                {
                    mods.has_const = true;
                    continue;
                }
                if (is_mutable_modifier())
                {
                    advance();
                    mods.has_mutable = true;
                    continue;
                }
                break;
            }

            if (mods.has_const && mods.has_mutable)
            {
                error_at_current("E2002", "同一声明不能同时使用 const 与 mutable");
            }
            return mods;
        }

        // 方法/函数/运算符的结尾：函数体、分号，或报错。
        AstNode* parse_body_or_semicolon(std::string_view construct)
        {
            if (check(TokenKind::LBrace))
            {
                return parse_block();
            }
            if (match(TokenKind::Semicolon))
            {
                return nullptr;
            }

            std::string message(construct.data(), construct.size());
            message += "声明缺少函数体或 ';'";
            diagnostics_.error("E2001", std::move(message), current().span);
            return nullptr;
        }

        // ---------------- 参数与类型参数 ----------------

        AstNode* parse_param_list()
        {
            const Span start = current().span;
            AstNode* list = make_node(AstKind::ParamList, start);

            if (!expect(TokenKind::LParen, "E2001", "期望 '(' 以开始参数列表"))
            {
                list->span = start;
                return list;
            }

            while (!check(TokenKind::RParen) && !at_end_token())
            {
                attach(list, parse_param());
                if (!match(TokenKind::Comma))
                {
                    break;
                }
                if (check(TokenKind::RParen))
                {
                    break; // 允许尾随逗号
                }
            }

            expect_closing(TokenKind::RParen, ')', "参数列表");
            list->span = span_from(start);
            return list;
        }

        AstNode* parse_param()
        {
            const std::vector<AstNode*> annotations = parse_annotation_uses();
            const Span start = current().span;
            bool is_const = false;
            bool is_mutable = false;
            for (;;)
            {
                if (match(TokenKind::KwConst))
                {
                    is_const = true;
                    continue;
                }
                if (is_mutable_modifier())
                {
                    advance();
                    is_mutable = true;
                    continue;
                }
                break;
            }

            AstNode* node = make_node(AstKind::Param, start);
            node->text = is_mutable ? "mutable:" : "const:";
            attach_annotations(node, annotations);

            if (check(TokenKind::Identifier))
            {
                node->text += advance().text;
            }
            else
            {
                error_at_current("E2006", "参数缺少名字");
            }

            if (is_const && is_mutable)
            {
                error_at_current("E2002", "参数不能同时使用 const 与 mutable");
            }

            expect(TokenKind::Colon, "E2001", "参数声明缺少 ':'");
            attach(node, parse_type());
            if (match(TokenKind::Assign))
            {
                attach(node, parse_expression());
            }

            node->span = span_from(start);
            return node;
        }

        AstNode* parse_type_param_list()
        {
            const Span start = current().span;
            advance(); // '<'

            AstNode* list = make_node(AstKind::TypeParamList, start);
            while (!check(TokenKind::Gt) && !at_end_token())
            {
                attach(list, parse_type_param());
                if (!match(TokenKind::Comma))
                {
                    break;
                }
                if (check(TokenKind::Gt))
                {
                    break; // 允许尾随逗号
                }
            }

            expect_closing(TokenKind::Gt, '>', "类型参数列表");
            list->span = span_from(start);
            return list;
        }

        AstNode* parse_type_param()
        {
            const Span start = current().span;
            std::string prefix;
            if (match(TokenKind::KwOut))
            {
                prefix = "out:";
            }
            else if (match(TokenKind::KwIn))
            {
                prefix = "in:";
            }

            AstNode* node = make_node(AstKind::TypeParam, start);
            if (check(TokenKind::Identifier))
            {
                node->text = prefix + advance().text;
            }
            else
            {
                error_at_current("E2006", "类型参数缺少名字");
            }

            if (match(TokenKind::Colon))
            {
                // 约束列表：`T: A + B`
                for (;;)
                {
                    attach(node, parse_type());
                    if (!match(TokenKind::Plus))
                    {
                        break;
                    }
                }
            }

            node->span = span_from(start);
            return node;
        }

        // ---------------- 语句 ----------------

        AstNode* parse_block()
        {
            const Span start = current().span;
            AstNode* block = make_node(AstKind::BlockStmt, start);

            if (!expect(TokenKind::LBrace, "E2003", "语句块缺少 '{'"))
            {
                block->span = span_from(start);
                return block;
            }

            for (;;)
            {
                // 块内孤立的分号没有语义，直接跳过，不产生节点。
                while (match(TokenKind::Semicolon))
                {
                }
                if (check(TokenKind::RBrace) || at_end_token())
                {
                    break;
                }

                const std::size_t before = index_;
                AstNode* statement = parse_statement();
                attach(block, statement);
                if (index_ == before)
                {
                    error_at_current("E2002", "非预期的 token，无法开始解析语句");
                    advance();
                }
            }

            expect_closing(TokenKind::RBrace, '}', "语句块");
            block->span = span_from(start);
            return block;
        }

        AstNode* parse_statement()
        {
            switch (current().kind)
            {
            case TokenKind::LBrace:
                return parse_block();
            case TokenKind::KwIf:
                return parse_if_statement();
            case TokenKind::KwWhile:
                return parse_while_statement();
            case TokenKind::KwFor:
                return parse_for_statement();
            case TokenKind::KwReturn:
                return parse_return_statement();
            case TokenKind::KwBreak:
                return parse_jump_statement(AstKind::BreakStmt);
            case TokenKind::KwContinue:
                return parse_jump_statement(AstKind::ContinueStmt);
            case TokenKind::KwConst:
                return parse_variable_decl({}, AstFlags::none, false);
            case TokenKind::Semicolon:
            {
                const Span span = advance().span;
                return make_node(AstKind::BlockStmt, span); // 空语句
            }
            default:
                break;
            }

            if (is_mutable_modifier())
            {
                return parse_variable_decl({}, AstFlags::none, false);
            }
            return parse_expression_statement();
        }

        AstNode* parse_expression_statement()
        {
            const Span start = current().span;
            AstNode* expression = parse_expression();
            AstNode* statement = make_node(AstKind::ExprStmt, Span::covering(start, expression->span));
            attach(statement, expression);
            expect_semicolon();
            statement->span = span_from(start);
            return statement;
        }

        AstNode* parse_return_statement()
        {
            const Span start = current().span;
            advance(); // return

            AstNode* node = make_node(AstKind::ReturnStmt, start);
            if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !check(TokenKind::KwElse) &&
                !at_end_token())
            {
                attach(node, parse_expression());
            }

            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_jump_statement(AstKind kind)
        {
            const Span start = current().span;
            advance(); // break / continue

            AstNode* node = make_node(kind, start);
            expect_semicolon();
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_if_statement()
        {
            const Span start = current().span;
            advance(); // if

            AstNode* node = make_node(AstKind::IfStmt, start);
            AstNode* condition = nullptr;
            if (match(TokenKind::LParen))
            {
                condition = parse_expression();
                expect_closing(TokenKind::RParen, ')', "if 条件");
            }
            else if (!check(TokenKind::LBrace) && !at_end_token())
            {
                // 语言规范中也出现无括号写法，这里宽容接受。
                condition = parse_expression();
            }
            else
            {
                error_at_current("E2001", "if 后缺少条件表达式");
            }
            if (condition == nullptr)
            {
                condition = make_error(current().span);
            }
            attach(node, condition);
            attach(node, parse_statement());

            if (match(TokenKind::KwElse))
            {
                attach(node, parse_statement());
            }

            node->span = span_from(start);
            return node;
        }

        AstNode* parse_while_statement()
        {
            const Span start = current().span;
            advance(); // while

            AstNode* node = make_node(AstKind::WhileStmt, start);
            AstNode* condition = nullptr;
            if (match(TokenKind::LParen))
            {
                condition = parse_expression();
                expect_closing(TokenKind::RParen, ')', "while 条件");
            }
            else if (!check(TokenKind::LBrace) && !at_end_token())
            {
                condition = parse_expression();
            }
            else
            {
                error_at_current("E2001", "while 后缺少条件表达式");
            }
            if (condition == nullptr)
            {
                condition = make_error(current().span);
            }
            attach(node, condition);
            attach(node, parse_statement());

            node->span = span_from(start);
            return node;
        }

        AstNode* parse_for_statement()
        {
            const Span start = current().span;
            advance(); // for

            AstNode* node = make_node(AstKind::ForStmt, start);
            const bool parenthesized = match(TokenKind::LParen);
            if (!parenthesized)
            {
                error_at_current("E2001", "for 后缺少 '('");
            }

            bool has_initializer = false;
            bool has_condition = false;
            bool has_step = false;

            // init 段
            if (check(TokenKind::Semicolon))
            {
                advance();
            }
            else if (is_local_decl_start())
            {
                attach(node, parse_variable_decl({}, AstFlags::none, false));
                has_initializer = true;
            }
            else if (!check(TokenKind::RParen) && !at_end_token())
            {
                const Span init_start = current().span;
                AstNode* expression = parse_expression();
                AstNode* statement = make_node(AstKind::ExprStmt, Span::covering(init_start, expression->span));
                attach(statement, expression);
                attach(node, statement);
                has_initializer = true;
                expect_semicolon();
            }
            else
            {
                expect_semicolon();
            }

            // cond 段
            if (check(TokenKind::Semicolon))
            {
                advance();
            }
            else if (!check(TokenKind::RParen) && !at_end_token())
            {
                attach(node, parse_expression());
                has_condition = true;
                expect_semicolon();
            }
            else
            {
                expect_semicolon();
            }

            // step 段
            if (!check(TokenKind::RParen) && !at_end_token())
            {
                attach(node, parse_expression());
                has_step = true;
            }

            if (parenthesized)
            {
                expect_closing(TokenKind::RParen, ')', "for 头部");
            }

            std::string mask = "000";
            if (has_initializer)
            {
                mask[0] = '1';
            }
            if (has_condition)
            {
                mask[1] = '1';
            }
            if (has_step)
            {
                mask[2] = '1';
            }
            node->text = mask;

            attach(node, parse_statement());
            node->span = span_from(start);
            return node;
        }

        // ---------------- 表达式（Pratt / 优先级爬升）----------------

        AstNode* parse_expression()
        {
            return parse_assignment();
        }

        AstNode* parse_assignment()
        {
            AstNode* left = parse_conditional();
            if (!is_assignment_operator(current().kind))
            {
                return left;
            }

            const Token& op = advance();
            AstNode* right = parse_assignment(); // 右结合
            AstNode* node = make_node(AstKind::AssignExpr, Span::covering(left->span, right->span), op.text);
            attach(node, left);
            attach(node, right);
            return node;
        }

        AstNode* parse_conditional()
        {
            AstNode* condition = parse_binary(1);
            if (!match(TokenKind::Question))
            {
                return condition;
            }

            AstNode* then_expression = parse_assignment();
            expect(TokenKind::Colon, "E2001", "条件表达式缺少 ':'");
            AstNode* else_expression = parse_conditional(); // 右结合

            AstNode* node = make_node(AstKind::ConditionalExpr,
                Span::covering(condition->span, else_expression->span), "?");
            attach(node, condition);
            attach(node, then_expression);
            attach(node, else_expression);
            return node;
        }

        // 通用二元运算符爬升；min_precedence 从 1 开始。
        AstNode* parse_binary(int min_precedence)
        {
            AstNode* left = parse_unary();
            for (;;)
            {
                const int precedence = binary_precedence(current().kind);
                if (precedence < min_precedence)
                {
                    break;
                }

                const Token& op = advance();
                AstNode* right = parse_binary(precedence + 1); // 全部左结合
                AstNode* node = make_node(AstKind::BinaryExpr, Span::covering(left->span, right->span), op.text);
                attach(node, left);
                attach(node, right);
                left = node;
            }
            return left;
        }

        AstNode* parse_unary()
        {
            if (is_unary_operator(current().kind))
            {
                const Token& op = advance();
                AstNode* operand = parse_unary();
                AstNode* node = make_node(AstKind::UnaryExpr, Span::covering(op.span, operand->span), op.text);
                attach(node, operand);
                return node;
            }
            return parse_postfix();
        }

        AstNode* parse_postfix()
        {
            AstNode* expression = parse_primary();
            for (;;)
            {
                if (check(TokenKind::LParen))
                {
                    AstNode* arguments = parse_argument_list();
                    AstNode* call = make_node(AstKind::CallExpr, Span::covering(expression->span, arguments->span));
                    attach(call, expression);
                    attach(call, arguments);
                    expression = call;
                    continue;
                }

                if (match(TokenKind::LBracket))
                {
                    AstNode* index = parse_expression();
                    expect_closing(TokenKind::RBracket, ']', "下标表达式");
                    AstNode* node = make_node(AstKind::IndexExpr, span_from(expression->span));
                    attach(node, expression);
                    attach(node, index);
                    expression = node;
                    continue;
                }

                if (match(TokenKind::Dot))
                {
                    const Span dot_span = last_span();
                    AstNode* node = make_node(AstKind::MemberExpr, Span::covering(expression->span, dot_span));
                    attach(node, expression);
                    if (check(TokenKind::Identifier))
                    {
                        const Token& name = advance();
                        node->text = name.text;
                        node->span = Span::covering(node->span, name.span);
                    }
                    else
                    {
                        error_at_current("E2006", "成员访问 '.' 后应为标识符");
                    }
                    expression = node;
                    continue;
                }

                break;
            }
            return expression;
        }

        AstNode* parse_argument_list()
        {
            const Span start = current().span;
            AstNode* list = make_node(AstKind::ArgumentList, start);

            if (!expect(TokenKind::LParen, "E2001", "期望 '(' 以开始实参列表"))
            {
                list->span = start;
                return list;
            }

            while (!check(TokenKind::RParen) && !at_end_token())
            {
                attach(list, parse_expression());
                if (!match(TokenKind::Comma))
                {
                    break;
                }
                if (check(TokenKind::RParen))
                {
                    break; // 允许尾随逗号
                }
            }

            expect_closing(TokenKind::RParen, ')', "实参列表");
            list->span = span_from(start);
            return list;
        }

        AstNode* parse_primary()
        {
            switch (current().kind)
            {
            case TokenKind::IntegerLiteral:
            case TokenKind::FloatLiteral:
            case TokenKind::StringLiteral:
            case TokenKind::CharLiteral:
            case TokenKind::KwTrue:
            case TokenKind::KwFalse:
            case TokenKind::KwNull:
            {
                const Token& token = advance();
                return make_node(AstKind::LiteralExpr, token.span, token.text);
            }
            case TokenKind::Identifier:
            {
                const Token& token = advance();
                return make_node(AstKind::IdentifierExpr, token.span, token.text);
            }
            case TokenKind::KwThis:
            {
                const Token& token = advance();
                return make_node(AstKind::ThisExpr, token.span);
            }
            case TokenKind::KwSuper:
            {
                const Token& token = advance();
                return make_node(AstKind::SuperExpr, token.span);
            }
            case TokenKind::KwNew:
                return parse_new_expression();
            case TokenKind::LParen:
            {
                const Span start = current().span;
                advance(); // '('
                AstNode* inner = parse_expression();
                expect_closing(TokenKind::RParen, ')', "括号表达式");
                AstNode* node = make_node(AstKind::ParenExpr, span_from(start));
                attach(node, inner);
                return node;
            }
            default:
                break;
            }

            const Span error_span = current().span;
            error_at_current("E2005", "期望表达式");
            if (!is_expression_sync(current().kind))
            {
                advance(); // 跳过当前 token，保证调用方至少前进一次
            }
            return make_error(error_span);
        }

        AstNode* parse_new_expression()
        {
            const Span start = current().span;
            advance(); // new

            AstNode* type = parse_type(false); // 构造实参留给 ArgumentList
            AstNode* arguments = nullptr;
            if (check(TokenKind::LParen))
            {
                arguments = parse_argument_list();
            }
            else
            {
                arguments = make_node(AstKind::ArgumentList, last_span());
            }

            AstNode* node = make_node(AstKind::NewExpr, Span::covering(start, last_span()));
            attach(node, type);
            attach(node, arguments);
            return node;
        }

        // ---------------- 类型 ----------------

        // allow_application 控制是否把 `Name(args)` 解析成编译期类型应用
        // （`integer(32)`、`character(8)`）。`new` 后面的类型要关掉它，
        // 以免把构造实参误当成类型实参。
        AstNode* parse_type(bool allow_application = true)
        {
            const Span start = current().span;
            switch (current().kind)
            {
            case TokenKind::Amp:
            {
                advance(); // '&'
                const bool is_mutable = is_identifier_text("mut");
                if (is_mutable)
                {
                    advance(); // mut
                }
                AstNode* node = make_node(is_mutable ? AstKind::MutableReferenceType : AstKind::ReferenceType, start);
                attach(node, parse_type());
                node->span = span_from(start);
                return node;
            }
            case TokenKind::Star:
            {
                advance(); // '*'
                const bool is_const = match(TokenKind::KwConst);
                AstNode* node = make_node(AstKind::PointerType, start, is_const ? "const" : "");
                attach(node, parse_type());
                node->span = span_from(start);
                return node;
            }
            case TokenKind::LBracket:
                return parse_array_type();
            case TokenKind::KwDyn:
            {
                advance(); // dyn
                AstNode* node = make_node(AstKind::DynType, start);
                attach(node, parse_type());
                node->span = span_from(start);
                return node;
            }
            case TokenKind::LParen:
                return parse_function_type();
            case TokenKind::KwVoid:
            {
                const Token& token = advance();
                return make_node(AstKind::NamedType, token.span, "void");
            }
            case TokenKind::KwModule:
            {
                // 基本类型 `module` 与关键字同名，类型位置按 NamedType 处理。
                const Token& token = advance();
                return make_node(AstKind::NamedType, token.span, "module");
            }
            case TokenKind::Identifier:
                return parse_named_type(allow_application);
            default:
                break;
            }

            const Span error_span = current().span;
            error_at_current("E2004", "期望类型");
            if (!is_type_sync(current().kind))
            {
                advance();
            }
            return make_error(error_span);
        }

        AstNode* parse_named_type(bool allow_application)
        {
            const Span start = current().span;
            const Token& name = advance(); // Identifier

            if (!match(TokenKind::Lt))
            {
                if (allow_application && check(TokenKind::LParen))
                {
                    // 编译期类型应用：`integer(32)`、`character(8, unsigned)`。
                    // 实参是编译期值表达式，用 ArgumentList 承载。
                    AstNode* arguments = parse_argument_list();
                    AstNode* node = make_node(AstKind::GenericType, Span::covering(start, arguments->span), name.text);
                    attach(node, arguments);
                    return node;
                }
                return make_node(AstKind::NamedType, name.span, name.text);
            }

            const Span arguments_start = last_span(); // '<'
            AstNode* arguments = make_node(AstKind::TypeArgList, arguments_start);
            while (!check(TokenKind::Gt) && !at_end_token())
            {
                attach(arguments, parse_type());
                if (!match(TokenKind::Comma))
                {
                    break;
                }
                if (check(TokenKind::Gt))
                {
                    break; // 允许尾随逗号
                }
            }
            expect_closing(TokenKind::Gt, '>', "泛型实参列表");
            arguments->span = span_from(arguments_start);

            AstNode* node = make_node(AstKind::GenericType, span_from(start), name.text);
            attach(node, arguments);
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_array_type()
        {
            const Span start = current().span;
            advance(); // '['

            if (match(TokenKind::RBracket))
            {
                // 切片：[]T，用 ArrayType + text="slice" 表示。
                AstNode* node = make_node(AstKind::ArrayType, start, "slice");
                attach(node, parse_type());
                node->span = span_from(start);
                return node;
            }

            AstNode* node = make_node(AstKind::ArrayType, start);
            attach(node, parse_type());
            if (match(TokenKind::Semicolon))
            {
                attach(node, parse_expression());
            }
            expect_closing(TokenKind::RBracket, ']', "数组类型");
            node->span = span_from(start);
            return node;
        }

        AstNode* parse_function_type()
        {
            const Span start = current().span;
            advance(); // '('

            AstNode* parameters = make_node(AstKind::TypeArgList, start);
            while (!check(TokenKind::RParen) && !at_end_token())
            {
                attach(parameters, parse_type());
                if (!match(TokenKind::Comma))
                {
                    break;
                }
                if (check(TokenKind::RParen))
                {
                    break;
                }
            }
            expect_closing(TokenKind::RParen, ')', "函数类型参数列表");
            parameters->span = span_from(start);

            AstNode* node = make_node(AstKind::FunctionType, start);
            attach(node, parameters);
            if (match(TokenKind::Colon))
            {
                attach(node, parse_type());
            }
            else
            {
                error_at_current("E2001", "函数类型缺少返回类型");
                attach(node, make_node(AstKind::NamedType, current().span, "void"));
            }
            node->span = span_from(start);
            return node;
        }

        // ---------------- 标注 ----------------

        std::vector<AstNode*> parse_annotation_uses()
        {
            std::vector<AstNode*> annotations;
            while (check(TokenKind::At))
            {
                annotations.push_back(parse_annotation_use());
            }
            return annotations;
        }

        AstNode* parse_annotation_use()
        {
            const Span start = current().span;
            advance(); // '@'

            AstNode* node = make_node(AstKind::AnnotationUse, start);
            if (check(TokenKind::Identifier))
            {
                node->text = advance().text;
            }
            else
            {
                error_at_current("E2006", "标注 '@' 后应为标注名");
            }

            const Span args_start = last_span();
            AstNode* arguments = make_node(AstKind::AnnotationArgList, args_start);
            attach(node, arguments);

            if (match(TokenKind::LParen))
            {
                while (!check(TokenKind::RParen) && !at_end_token())
                {
                    attach(arguments, parse_annotation_argument());
                    if (!match(TokenKind::Comma))
                    {
                        break;
                    }
                    if (check(TokenKind::RParen))
                    {
                        break;
                    }
                }
                expect_closing(TokenKind::RParen, ')', "标注参数列表");
            }

            arguments->span = Span::covering(args_start, last_span());
            node->span = span_from(start);
            return node;
        }

        // 标注实参：类型位置的写法（[]T、&T、*T、dyn I、void）按类型解析，其余按表达式。
        // `@target(function, class, var)` 中的 class 是关键字但在此处表示目标种类名，
        // 因此按标识符处理。
        AstNode* parse_annotation_argument()
        {
            switch (current().kind)
            {
            case TokenKind::LBracket:
            case TokenKind::Amp:
            case TokenKind::Star:
            case TokenKind::KwDyn:
            case TokenKind::KwVoid:
                return parse_type();
            case TokenKind::KwClass:
            {
                const Token& token = advance();
                return make_node(AstKind::IdentifierExpr, token.span, token.text);
            }
            default:
                return parse_expression();
            }
        }

        // ---------------- 恢复与分类辅助 ----------------

        void synchronize_top_level()
        {
            while (!at_end_token() && !check(TokenKind::RBrace))
            {
                if (starts_top_level_declaration(current().kind))
                {
                    return;
                }
                if (match(TokenKind::Semicolon))
                {
                    return;
                }
                advance();
            }
        }

        void synchronize_member()
        {
            while (!at_end_token() && !check(TokenKind::RBrace))
            {
                if (check(TokenKind::At))
                {
                    return;
                }
                if (match(TokenKind::Semicolon))
                {
                    return;
                }
                advance();
            }
        }

        static bool starts_top_level_declaration(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::KwExport:
            case TokenKind::KwImport:
            case TokenKind::KwModule:
            case TokenKind::KwInterface:
            case TokenKind::KwClass:
            case TokenKind::KwConcept:
            case TokenKind::KwAnnotation:
            case TokenKind::KwType:
            case TokenKind::KwDistinct:
            case TokenKind::KwOperator:
            case TokenKind::KwConst:
            case TokenKind::KwOpen:
            case TokenKind::KwFinal:
            case TokenKind::KwHide:
            case TokenKind::KwComptime:
            case TokenKind::KwRepeatable:
            case TokenKind::At:
            case TokenKind::Identifier:
                return true;
            default:
                return false;
            }
        }

        static bool is_assignment_operator(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::Assign:
            case TokenKind::PlusAssign:
            case TokenKind::MinusAssign:
            case TokenKind::StarAssign:
            case TokenKind::SlashAssign:
            case TokenKind::PercentAssign:
                return true;
            default:
                return false;
            }
        }

        static bool is_unary_operator(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::Plus:
            case TokenKind::Minus:
            case TokenKind::Bang:
            case TokenKind::Tilde:
                return true;
            default:
                return false;
            }
        }

        static bool is_overloadable_operator(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::Plus:
            case TokenKind::Minus:
            case TokenKind::Star:
            case TokenKind::Slash:
            case TokenKind::Percent:
            case TokenKind::Eq:
            case TokenKind::Ne:
            case TokenKind::Lt:
            case TokenKind::Le:
            case TokenKind::Gt:
            case TokenKind::Ge:
            case TokenKind::Bang:
            case TokenKind::Tilde:
                return true;
            default:
                return false;
            }
        }

        static int binary_precedence(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::PipePipe: return 1;
            case TokenKind::AmpAmp: return 2;
            case TokenKind::Eq:
            case TokenKind::Ne: return 3;
            case TokenKind::Lt:
            case TokenKind::Le:
            case TokenKind::Gt:
            case TokenKind::Ge: return 4;
            case TokenKind::Plus:
            case TokenKind::Minus: return 5;
            case TokenKind::Star:
            case TokenKind::Slash:
            case TokenKind::Percent: return 6;
            default: return 0;
            }
        }

        // 表达式出错后可以原地停住的 token（不消费，交给上层恢复）。
        static bool is_expression_sync(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::Semicolon:
            case TokenKind::RBrace:
            case TokenKind::RParen:
            case TokenKind::RBracket:
            case TokenKind::Comma:
            case TokenKind::Colon:
            case TokenKind::KwElse:
            case TokenKind::EndOfFile:
                return true;
            default:
                return false;
            }
        }

        // 类型出错后可以原地停住的 token。
        static bool is_type_sync(TokenKind kind) noexcept
        {
            switch (kind)
            {
            case TokenKind::Semicolon:
            case TokenKind::RBrace:
            case TokenKind::RParen:
            case TokenKind::RBracket:
            case TokenKind::Comma:
            case TokenKind::Colon:
            case TokenKind::Assign:
            case TokenKind::LBrace:
            case TokenKind::EndOfFile:
                return true;
            default:
                return false;
            }
        }

        // ---------------- 状态 ----------------

        SourceId source_;
        std::string_view text_;
        DiagnosticBag& diagnostics_;
        std::vector<Token> tokens_;

        AstArena* arena_ = nullptr;
        AstNode* root_ = nullptr;
        std::size_t index_ = 0;
        bool module_seen_ = false;
        bool top_level_started_ = false;
    };
}
