// ConstEval.h 自包含测试：不依赖第三方测试框架。
// 覆盖：从 Driver 解析常量声明并取出初始化表达式求值、算术优先级、括号、字面量进制、
// 一元与二元运算、比较、逻辑短路、三目、常量引用、文本拼接、类型错误、除零、
// 整数溢出、移位越界与步数/深度预算。直接构造 AST 的部分覆盖 Parser 尚未生成的位运算。
// 全部通过时打印 "PASS: const_eval" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "compiler/ConstEval.h"
#include "compiler/Driver.h"

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

    ConstValue integerValue(std::int64_t value)
    {
        ConstValue result;
        result.kind = ValueKind::integer;
        result.integer = value;
        return result;
    }

    ConstValue booleanValue(bool value)
    {
        ConstValue result;
        result.kind = ValueKind::boolean;
        result.boolean = value;
        return result;
    }

    ConstValue textValue(std::string value)
    {
        ConstValue result;
        result.kind = ValueKind::text;
        result.text = std::move(value);
        return result;
    }

    EvalResult sourceFailure(std::string message)
    {
        EvalResult result;
        result.error = std::move(message);
        return result;
    }

    // 用 const 声明把表达式交给真实前端解析，再取 ConstantDecl 的初始化表达式求值。
    // 模块级常量声明的孩子顺序为 Type? -> 初始化表达式?，因此最后一个孩子就是初始化器。
    EvalResult evaluateSource(std::string_view expression,
        std::map<std::string, ConstValue> constants = {}, EvalLimits limits = {})
    {
        std::string source = "const N: integer(64) = ";
        source.append(expression);
        source += ";";

        const FrontendResult frontend = runFrontendFromText("const_eval.coord", source);
        if (!frontend.ok() || frontend.module == nullptr)
        {
            return sourceFailure("测试源码解析失败: " + renderDiagnostics(frontend));
        }

        AstNode* declaration = frontend.module->first_child(AstKind::ConstantDecl);
        if (declaration == nullptr || declaration->child_count() == 0u)
        {
            return sourceFailure("测试源码缺少常量声明或初始化表达式");
        }

        const AstNode* initializer = declaration->child(declaration->child_count() - 1u);
        if (initializer == nullptr)
        {
            return sourceFailure("测试源码的初始化表达式为空");
        }

        ConstEvaluator evaluator(std::move(constants), limits);
        return evaluator.evaluate(*initializer);
    }

    AstNode* makeIntegerLiteral(AstArena& arena, std::int64_t value)
    {
        return arena.make(AstKind::LiteralExpr, Span{}, std::to_string(value));
    }

    AstNode* makeUnary(AstArena& arena, std::string op, AstNode* operand)
    {
        AstNode* node = arena.make(AstKind::UnaryExpr, Span{}, std::move(op));
        arena.adopt(node, operand);
        return node;
    }

    AstNode* makeBinary(AstArena& arena, std::string op, AstNode* left, AstNode* right)
    {
        AstNode* node = arena.make(AstKind::BinaryExpr, Span{}, std::move(op));
        arena.adopt(node, left);
        arena.adopt(node, right);
        return node;
    }

    bool testFrontendDeclaration()
    {
        constexpr std::string_view kSource = "const N: integer(64) = 1 + 2 * 3;";
        const FrontendResult frontend = runFrontendFromText("const_eval_decl.coord", kSource);
        CHECK(frontend.ok());
        CHECK(frontend.module != nullptr);
        CHECK(frontend.module->kind == AstKind::Module);

        AstNode* declaration = frontend.module->first_child(AstKind::ConstantDecl);
        CHECK(declaration != nullptr);
        CHECK(declaration->text == "N");
        CHECK(declaration->child_count() >= 2u);

        const AstNode* initializer = declaration->child(declaration->child_count() - 1u);
        CHECK(initializer != nullptr);
        CHECK(initializer->kind == AstKind::BinaryExpr);

        ConstEvaluator evaluator;
        const EvalResult result = evaluator.evaluate(*initializer);
        CHECK(result.ok);
        CHECK(result.error.empty());
        CHECK(result.steps > 0u);
        CHECK(result.value.valid());
        CHECK(result.value.kind == ValueKind::integer);
        CHECK(result.value.integer == 7);
        return true;
    }

    bool testArithmeticAndPrecedence()
    {
        EvalResult result = evaluateSource("1 + 2 * 3");
        CHECK(result.ok);
        CHECK(result.value.integer == 7);

        result = evaluateSource("(1 + 2) * 3");
        CHECK(result.ok);
        CHECK(result.value.integer == 9);

        result = evaluateSource("10 - 3 - 2");
        CHECK(result.ok);
        CHECK(result.value.integer == 5);

        result = evaluateSource("20 / 3");
        CHECK(result.ok);
        CHECK(result.value.integer == 6);

        result = evaluateSource("20 % 3");
        CHECK(result.ok);
        CHECK(result.value.integer == 2);

        result = evaluateSource("0x10 + 0b10 + 10");
        CHECK(result.ok);
        CHECK(result.value.integer == 28);

        result = evaluateSource("1_000 + 2_0");
        CHECK(result.ok);
        CHECK(result.value.integer == 1020);
        return true;
    }

    bool testUnaryOperators()
    {
        EvalResult result = evaluateSource("-5 + +3");
        CHECK(result.ok);
        CHECK(result.value.integer == -2);

        result = evaluateSource("~0");
        CHECK(result.ok);
        CHECK(result.value.integer == -1);

        result = evaluateSource("!false");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);

        result = evaluateSource("!(1 < 2)");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(!result.value.boolean);
        return true;
    }

    bool testComparisons()
    {
        EvalResult result = evaluateSource("1 + 2 == 3");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);

        result = evaluateSource("3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5 && 3 != 4");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);

        result = evaluateSource("\"apple\" < \"banana\"");
        CHECK(result.ok);
        CHECK(result.value.boolean);

        result = evaluateSource("true == false");
        CHECK(result.ok);
        CHECK(!result.value.boolean);
        return true;
    }

    bool testLogicalShortCircuit()
    {
        EvalResult result = evaluateSource("false && (1 / 0 == 0)");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(!result.value.boolean);

        result = evaluateSource("true || (1 / 0 == 0)");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);

        result = evaluateSource("true && false");
        CHECK(result.ok);
        CHECK(!result.value.boolean);

        result = evaluateSource("false || true");
        CHECK(result.ok);
        CHECK(result.value.boolean);
        return true;
    }

    bool testConditional()
    {
        EvalResult result = evaluateSource("true ? 42 : 1 / 0");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::integer);
        CHECK(result.value.integer == 42);

        result = evaluateSource("false ? 1 / 0 : 7");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::integer);
        CHECK(result.value.integer == 7);

        result = evaluateSource("5 > 3 ? \"yes\" : \"no\"");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::text);
        CHECK(result.value.text == "yes");

        result = evaluateSource("false ? 1 : true ? 2 : 3");
        CHECK(result.ok);
        CHECK(result.value.integer == 2);
        return true;
    }

    bool testConstants()
    {
        const std::map<std::string, ConstValue> constants = {
            {"BASE", integerValue(10)},
            {"FLAG", booleanValue(true)},
            {"NAME", textValue("hi")},
        };

        EvalResult result = evaluateSource("BASE * 2 + 1", constants);
        CHECK(result.ok);
        CHECK(result.value.integer == 21);

        result = evaluateSource("FLAG", constants);
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);

        result = evaluateSource("NAME + \"!\"", constants);
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::text);
        CHECK(result.value.text == "hi!");

        result = evaluateSource("MISSING", constants);
        CHECK(!result.ok);
        CHECK(result.value.kind == ValueKind::invalid);
        CHECK(contains(result.error, "未知常量"));

        std::map<std::string, ConstValue> with_invalid = constants;
        with_invalid["BROKEN"] = ConstValue{};
        result = evaluateSource("BROKEN", with_invalid);
        CHECK(!result.ok);
        CHECK(contains(result.error, "不是有效"));
        return true;
    }

    bool testTextConcatenation()
    {
        EvalResult result = evaluateSource("\"ab\" + \"cd\"");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::text);
        CHECK(result.value.text == "abcd");

        result = evaluateSource("\"a\\n\" + \"b\"");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::text);
        CHECK(result.value.text == "a\nb");

        result = evaluateSource("\"a\" == \"a\"");
        CHECK(result.ok);
        CHECK(result.value.kind == ValueKind::boolean);
        CHECK(result.value.boolean);
        return true;
    }

    bool testTypeErrors()
    {
        EvalResult result = evaluateSource("1 + true");
        CHECK(!result.ok);
        CHECK(contains(result.error, "类型不匹配"));

        result = evaluateSource("!1");
        CHECK(!result.ok);
        CHECK(contains(result.error, "类型不匹配"));

        result = evaluateSource("\"a\" - \"b\"");
        CHECK(!result.ok);
        CHECK(contains(result.error, "类型不匹配"));

        result = evaluateSource("1 && true");
        CHECK(!result.ok);
        CHECK(contains(result.error, "类型不匹配"));

        result = evaluateSource("1 ? 2 : 3");
        CHECK(!result.ok);
        CHECK(contains(result.error, "类型不匹配"));
        return true;
    }

    bool testDivisionAndOverflowErrors()
    {
        EvalResult result = evaluateSource("1 / 0");
        CHECK(!result.ok);
        CHECK(result.value.kind == ValueKind::invalid);
        CHECK(contains(result.error, "除零"));

        result = evaluateSource("1 % 0");
        CHECK(!result.ok);
        CHECK(contains(result.error, "除零"));

        result = evaluateSource("9223372036854775808");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));

        result = evaluateSource("9223372036854775807 + 1");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));

        result = evaluateSource("3037000500 * 3037000500");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));

        result = evaluateSource("(-9223372036854775807 - 1) - 1");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));

        result = evaluateSource("-(-9223372036854775807 - 1)");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));

        result = evaluateSource("(-9223372036854775807 - 1) / -1");
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));
        return true;
    }

    bool testBudget()
    {
        EvalResult result = evaluateSource("1 + 2", {}, EvalLimits{2u, 256u});
        CHECK(!result.ok);
        CHECK(contains(result.error, "求值预算"));
        CHECK(result.steps <= 2u);

        result = evaluateSource("1 + 2", {}, EvalLimits{100u, 1u});
        CHECK(!result.ok);
        CHECK(contains(result.error, "求值预算"));
        CHECK(contains(result.error, "深度"));
        return true;
    }

    bool testDirectAst()
    {
        ConstEvaluator evaluator;

        AstArena arena;
        AstNode* simple_sum = makeBinary(arena, "+",
            makeIntegerLiteral(arena, 40), makeIntegerLiteral(arena, 2));
        EvalResult result = evaluator.evaluate(*simple_sum);
        CHECK(result.ok);
        CHECK(result.value.integer == 42);

        AstNode* bitwise = makeBinary(arena, "|",
            makeBinary(arena, "&", makeIntegerLiteral(arena, 6), makeIntegerLiteral(arena, 3)),
            makeBinary(arena, "^", makeIntegerLiteral(arena, 1), makeIntegerLiteral(arena, 2)));
        result = evaluator.evaluate(*bitwise);
        CHECK(result.ok);
        CHECK(result.value.integer == 3);

        AstNode* shifted = makeBinary(arena, "+",
            makeBinary(arena, "<<", makeIntegerLiteral(arena, 1), makeIntegerLiteral(arena, 4)),
            makeIntegerLiteral(arena, 1));
        result = evaluator.evaluate(*shifted);
        CHECK(result.ok);
        CHECK(result.value.integer == 17);

        AstNode* negative_right_shift = makeBinary(arena, ">>",
            makeUnary(arena, "-", makeIntegerLiteral(arena, 8)), makeIntegerLiteral(arena, 2));
        result = evaluator.evaluate(*negative_right_shift);
        CHECK(result.ok);
        CHECK(result.value.integer == -2);

        AstNode* shift_count = makeBinary(arena, "<<",
            makeIntegerLiteral(arena, 1), makeIntegerLiteral(arena, 64));
        result = evaluator.evaluate(*shift_count);
        CHECK(!result.ok);
        CHECK(contains(result.error, "移位越界"));

        AstNode* negative_shift = makeBinary(arena, ">>",
            makeIntegerLiteral(arena, 1), makeUnary(arena, "-", makeIntegerLiteral(arena, 1)));
        result = evaluator.evaluate(*negative_shift);
        CHECK(!result.ok);
        CHECK(contains(result.error, "移位越界"));

        AstNode* shift_overflow = makeBinary(arena, "<<",
            makeIntegerLiteral(arena, 1), makeIntegerLiteral(arena, 63));
        result = evaluator.evaluate(*shift_overflow);
        CHECK(!result.ok);
        CHECK(contains(result.error, "溢出"));
        return true;
    }
}

int main()
{
    bool passed = true;
    if (!testFrontendDeclaration())
    {
        passed = false;
    }
    if (!testArithmeticAndPrecedence())
    {
        passed = false;
    }
    if (!testUnaryOperators())
    {
        passed = false;
    }
    if (!testComparisons())
    {
        passed = false;
    }
    if (!testLogicalShortCircuit())
    {
        passed = false;
    }
    if (!testConditional())
    {
        passed = false;
    }
    if (!testConstants())
    {
        passed = false;
    }
    if (!testTextConcatenation())
    {
        passed = false;
    }
    if (!testTypeErrors())
    {
        passed = false;
    }
    if (!testDivisionAndOverflowErrors())
    {
        passed = false;
    }
    if (!testBudget())
    {
        passed = false;
    }
    if (!testDirectAst())
    {
        passed = false;
    }

    if (!passed)
    {
        std::cout << "FAIL: const_eval\n";
        return 1;
    }
    std::cout << "PASS: const_eval\n";
    return 0;
}
