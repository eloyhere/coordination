// Fold.h 自包含测试：不依赖第三方测试框架。
// 覆盖：顶层 const 常量折叠、嵌套括号、常量引用、文本拼接/布尔、除零诊断 (E7001)、
// 预算耗尽、含调用/成员/下标的表达式不整体折叠、AST 父节点 children 替换与旧子树保留、
// 以及折叠前后 Sema（coordination check 的语义分析部分）结果不变。
// 全部通过时打印 "PASS: fold" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <iostream>
#include <string_view>

#include "compiler/Driver.h"
#include "compiler/Fold.h"
#include "compiler/Sema.h"

using namespace coordination::compiler;

// 失败的断言立即从当前测试函数返回 false；std::cout 由 main 统一输出结果。
#define CHECK(condition) \
    if (!(condition)) \
    { \
        std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")\n"; \
        return false; \
    }

namespace
{
    bool contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }

    std::size_t count_code(const DiagnosticBag& bag, std::string_view code)
    {
        std::size_t count = 0;
        for (const Diagnostic& diagnostic : bag.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                ++count;
            }
        }
        return count;
    }

    bool same_span(const Span& lhs, const Span& rhs) noexcept
    {
        return lhs.source == rhs.source && lhs.begin == rhs.begin && lhs.end == rhs.end;
    }

    // `const N = 1 + 2 * 3;` 折叠为 LiteralExpr 7，并替换声明中的初始化器。
    bool testConstantDeclaration()
    {
        const FrontendResult frontend = runFrontendFromText("fold_const.coord", "const N = 1 + 2 * 3;");
        CHECK(frontend.ok());
        CHECK(frontend.module != nullptr);

        AstNode* declaration = frontend.module->first_child(AstKind::ConstantDecl);
        CHECK(declaration != nullptr);
        CHECK(declaration->text == "N");
        CHECK(declaration->child_count() == 1u);
        CHECK(declaration->child(0) != nullptr);
        CHECK(declaration->child(0)->kind == AstKind::BinaryExpr);

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 1u);
        CHECK(result.diagnostics.diagnostics().empty());

        CHECK(declaration->child(0)->kind == AstKind::LiteralExpr);
        CHECK(declaration->child(0)->text == "7");
        CHECK(dump(*frontend.module) == "(Module (ConstantDecl N (LiteralExpr 7)))");
        return true;
    }

    // 嵌套括号整体折叠；顶层 const 引用参与求值；折叠节点沿用原表达式 span。
    bool testNestedParenthesesAndReferences()
    {
        constexpr std::string_view kSource =
            "const P = ((2 + 3) * (1 - 4));\n"
            "const A = 2 + 3;\n"
            "const B = A * 4;\n";
        const FrontendResult frontend = runFrontendFromText("fold_nested.coord", kSource);
        CHECK(frontend.ok());
        CHECK(frontend.module->child_count() == 3u);

        AstNode* p = frontend.module->child(0);
        CHECK(p != nullptr);
        const AstNode* oldInitializer = p->child(0);
        CHECK(oldInitializer != nullptr);
        const Span oldSpan = oldInitializer->span;
        CHECK(oldSpan.valid());

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 3u);

        CHECK(p->child(0) != oldInitializer);
        CHECK(p->child(0)->kind == AstKind::LiteralExpr);
        CHECK(p->child(0)->text == "-15");
        CHECK(same_span(p->child(0)->span, oldSpan));

        CHECK(frontend.module->child(1)->child(0)->text == "5");
        CHECK(frontend.module->child(2)->child(0)->text == "20");
        CHECK(contains(dump(*frontend.module), "(ConstantDecl B (LiteralExpr 20))"));
        return true;
    }

    // 文本拼接与布尔运算折叠后重新编码为 LiteralExpr 原文。
    bool testTextAndBoolean()
    {
        constexpr std::string_view kSource =
            "const S = \"ab\" + \"cd\";\n"
            "const T = true && !false;\n";
        const FrontendResult frontend = runFrontendFromText("fold_text.coord", kSource);
        CHECK(frontend.ok());

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 2u);
        CHECK(dump(*frontend.module) ==
            "(Module (ConstantDecl S (LiteralExpr \"abcd\")) (ConstantDecl T (LiteralExpr true)))");
        return true;
    }

    // 除零：保留原子树并写一条 E7001，不影响同一模块里的其它折叠。
    bool testDivisionByZero()
    {
        constexpr std::string_view kSource =
            "const Z = 1 / 0;\n"
            "const N = 2 + 3;\n";
        const FrontendResult frontend = runFrontendFromText("fold_zero.coord", kSource);
        CHECK(frontend.ok());
        CHECK(frontend.module->child_count() == 2u);

        AstNode* z = frontend.module->child(0);
        AstNode* n = frontend.module->child(1);
        CHECK(z != nullptr && n != nullptr);
        const AstNode* oldInitializer = z->child(0);
        CHECK(oldInitializer != nullptr);
        CHECK(oldInitializer->kind == AstKind::BinaryExpr);
        const Span oldSpan = oldInitializer->span;

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(!result.ok());
        CHECK(result.folded == 1u);
        CHECK(result.diagnostics.diagnostics().size() == 1u);
        CHECK(count_code(result.diagnostics, "E7001") == 1u);
        CHECK(contains(result.diagnostics.diagnostics().front().message, "除零"));
        CHECK(same_span(result.diagnostics.diagnostics().front().span, oldSpan));

        // 除零表达式保持原样（未被替换），同一模块的 `2 + 3` 仍折叠为 5。
        CHECK(z->child(0) == oldInitializer);
        CHECK(z->child(0)->kind == AstKind::BinaryExpr);
        CHECK(count_code(result.diagnostics, "E7001") == 1u);
        CHECK(n->child(0)->kind == AstKind::LiteralExpr);
        CHECK(n->child(0)->text == "5");
        CHECK(dump(*frontend.module) ==
            "(Module (ConstantDecl Z (BinaryExpr / (LiteralExpr 1) (LiteralExpr 0))) "
            "(ConstantDecl N (LiteralExpr 5)))");
        return true;
    }

    // 直接构造超过 ConstEvaluator 默认深度预算的纯常量表达式，验证预算错误走 E7001。
    bool testBudgetExhausted()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, Span{});
        AstNode* declaration = arena.make(AstKind::ConstantDecl, Span{}, "DEEP");

        AstNode* expression = arena.make(AstKind::LiteralExpr, Span{}, "1");
        for (int depth = 0; depth < 300; ++depth)
        {
            AstNode* unary = arena.make(AstKind::UnaryExpr, Span{}, "-");
            arena.adopt(unary, expression);
            expression = unary;
        }
        arena.adopt(declaration, expression);
        arena.adopt(module, declaration);

        const FoldResult result = foldConstants(arena, *module);
        CHECK(!result.ok());
        CHECK(result.folded == 0u);
        CHECK(result.diagnostics.diagnostics().size() == 1u);
        CHECK(count_code(result.diagnostics, "E7001") == 1u);
        CHECK(contains(result.diagnostics.diagnostics().front().message, "预算"));

        // 求值失败时整棵原子树保持原样。
        CHECK(declaration->child(0) == expression);
        CHECK(expression->kind == AstKind::UnaryExpr);
        CHECK(expression->parent == declaration);
        return true;
    }

    // 含调用/成员/下标的表达式整体不折叠，但内部纯常量子表达式仍可折叠。
    bool testImpureExpressionsNotFolded()
    {
        constexpr std::string_view kSource =
            "const C = compute(1 + 2) * 3;\n"
            "const M = value.field + 1;\n"
            "const I = items[1 + 1];\n";
        const FrontendResult frontend = runFrontendFromText("fold_impure.coord", kSource);
        CHECK(frontend.ok());
        CHECK(frontend.module->child_count() == 3u);

        AstNode* c = frontend.module->child(0);
        AstNode* m = frontend.module->child(1);
        AstNode* i = frontend.module->child(2);
        CHECK(c != nullptr && m != nullptr && i != nullptr);

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 2u);

        // 调用表达式保留，实参 1 + 2 被折叠成 3。
        CHECK(c->child(0)->kind == AstKind::BinaryExpr);
        CHECK(c->child(0)->child(0)->kind == AstKind::CallExpr);
        CHECK(contains(dump(*frontend.module),
            "(CallExpr (IdentifierExpr compute) (ArgumentList (LiteralExpr 3)))"));

        // 成员表达式整体保留。
        CHECK(m->child(0)->kind == AstKind::BinaryExpr);
        CHECK(m->child(0)->child(0)->kind == AstKind::MemberExpr);
        CHECK(m->child(0)->child(0)->text == "field");

        // 下标表达式整体保留，下标 1 + 1 被折叠成 2。
        CHECK(i->child(0)->kind == AstKind::IndexExpr);
        CHECK(i->child(0)->child(1)->kind == AstKind::LiteralExpr);
        CHECK(i->child(0)->child(1)->text == "2");
        return true;
    }

    // 函数体内折叠：父节点 children 被替换，新字面量沿用 span，旧子树仍留在 arena。
    bool testAstReplacement()
    {
        constexpr std::string_view kSource =
            "run(): integer(64)\n"
            "{\n"
            "    return 1 + 2 * 3;\n"
            "}\n";
        const FrontendResult frontend = runFrontendFromText("fold_ast.coord", kSource);
        CHECK(frontend.ok());

        AstNode* function = frontend.module->first_child(AstKind::FunctionDecl);
        CHECK(function != nullptr);
        CHECK(function->child_count() >= 1u);
        AstNode* body = function->child(function->child_count() - 1u);
        CHECK(body != nullptr);
        CHECK(body->kind == AstKind::BlockStmt);
        CHECK(body->child_count() == 1u);
        AstNode* returnStatement = body->child(0);
        CHECK(returnStatement != nullptr);
        CHECK(returnStatement->kind == AstKind::ReturnStmt);
        AstNode* oldExpression = returnStatement->child(0);
        CHECK(oldExpression != nullptr);
        CHECK(oldExpression->kind == AstKind::BinaryExpr);

        const Span oldSpan = oldExpression->span;
        const std::size_t oldNodeCount = frontend.arena->node_count();
        const std::size_t oldChildCount = oldExpression->child_count();
        AstNode* oldRight = oldExpression->child(1);
        CHECK(oldChildCount == 2u);
        CHECK(oldRight != nullptr);

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 1u);

        AstNode* literal = returnStatement->child(0);
        CHECK(literal != oldExpression);
        CHECK(literal->kind == AstKind::LiteralExpr);
        CHECK(literal->text == "7");
        CHECK(literal->parent == returnStatement);
        CHECK(same_span(literal->span, oldSpan));

        // 旧子树没有销毁：仍由 arena 拥有，children 完整，只是 parent 被断开。
        CHECK(oldExpression->parent == nullptr);
        CHECK(oldExpression->child_count() == oldChildCount);
        CHECK(oldExpression->child(1) == oldRight);
        CHECK(oldRight->parent == oldExpression);
        CHECK(frontend.arena->node_count() == oldNodeCount + 1u);
        CHECK(contains(dump(*frontend.module), "(ReturnStmt (LiteralExpr 7))"));
        return true;
    }

    // 折叠前后 Sema 的结果一致：coordination check 的语义分析不受折叠影响。
    bool testCheckSemanticsUnchanged()
    {
        constexpr std::string_view kSource =
            "const N = 1 + 2 * 3;\n"
            "\n"
            "run(): integer(64)\n"
            "{\n"
            "    const value = N + 4;\n"
            "    if (value > 10)\n"
            "    {\n"
            "        return value;\n"
            "    }\n"
            "    return 0;\n"
            "}\n";
        const FrontendResult frontend = runFrontendFromText("fold_check.coord", kSource);
        CHECK(frontend.ok());

        const SemaResult before = analyze(*frontend.module);
        CHECK(before.ok());
        CHECK(before.diagnostics.diagnostics().empty());
        const std::size_t beforeSymbols = before.symbols.size();
        const std::size_t beforeCount = before.symbolCount;
        const std::size_t beforeTypes = before.typeCount;

        const FoldResult result = foldConstants(*frontend.arena, *frontend.module);
        CHECK(result.ok());
        CHECK(result.folded == 2u);
        CHECK(count_code(result.diagnostics, "E7001") == 0u);

        const SemaResult after = analyze(*frontend.module);
        CHECK(after.ok());
        CHECK(after.diagnostics.diagnostics().empty());
        CHECK(after.symbols.size() == beforeSymbols);
        CHECK(after.symbolCount == beforeCount);
        CHECK(after.typeCount == beforeTypes);

        CHECK(contains(dump(*frontend.module), "(ConstantDecl N (LiteralExpr 7))"));
        CHECK(contains(dump(*frontend.module), "(LocalDeclStmt const:value (LiteralExpr 11))"));
        return true;
    }
}

int main()
{
    bool passed = true;
    if (!testConstantDeclaration())
    {
        passed = false;
    }
    if (!testNestedParenthesesAndReferences())
    {
        passed = false;
    }
    if (!testTextAndBoolean())
    {
        passed = false;
    }
    if (!testDivisionByZero())
    {
        passed = false;
    }
    if (!testBudgetExhausted())
    {
        passed = false;
    }
    if (!testImpureExpressionsNotFolded())
    {
        passed = false;
    }
    if (!testAstReplacement())
    {
        passed = false;
    }
    if (!testCheckSemanticsUnchanged())
    {
        passed = false;
    }

    if (!passed)
    {
        std::cout << "FAIL: fold\n";
        return 1;
    }
    std::cout << "PASS: fold\n";
    return 0;
}
