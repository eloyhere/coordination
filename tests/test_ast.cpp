// Ast.h 自包含测试：不依赖第三方测试框架。
// 覆盖 arena 的 make/make_child/adopt、父子关系与顺序、flags 组合、访问器边界、
// 重复 adopt 异常、节点地址稳定性、dump 的稳定 S 表达式输出与 ast_kind_name。
// 全部通过时打印 "PASS: ast" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "compiler/Ast.h"

namespace
{
    using coordination::compiler::AstArena;
    using coordination::compiler::AstFlags;
    using coordination::compiler::AstKind;
    using coordination::compiler::AstNode;
    using coordination::compiler::Span;
    using coordination::compiler::ast_kind_name;
    using coordination::compiler::dump;
    using coordination::compiler::has_flag;

    // 编译期验证 flags 运算与 kind 名称确实可用于常量表达式。
    static_assert(has_flag(AstFlags::exported | AstFlags::hidden, AstFlags::hidden));
    static_assert(!has_flag(AstFlags::exported, AstFlags::hidden));
    static_assert((AstFlags::exported & AstFlags::exported) == AstFlags::exported);
    static_assert(ast_kind_name(AstKind::Module) == "Module");
    static_assert(ast_kind_name(AstKind::Error) == "Error");

    // 测试统一使用 SourceId = 1 的源码区间。
    Span make_span(std::uint32_t begin, std::uint32_t end)
    {
        return Span::from_bounds(1u, begin, end);
    }
}

// 失败的断言立即从当前测试函数返回 false；std::cout 由调用方统一输出结果。
#define CHECK(condition) \
    if (!(condition)) \
    { \
        std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")\n"; \
        return false; \
    }

namespace
{
    bool test_make()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, make_span(0u, 100u));
        CHECK(module != nullptr);
        CHECK(module->kind == AstKind::Module);
        CHECK(module->span.source == 1u);
        CHECK(module->span.begin == 0u);
        CHECK(module->span.end == 100u);
        CHECK(module->text.empty());
        CHECK(module->flags == AstFlags::none);
        CHECK(module->parent == nullptr);
        CHECK(module->child_count() == 0u);
        CHECK(arena.node_count() == 1u);

        AstNode* identifier = arena.make(AstKind::IdentifierExpr, make_span(5u, 8u), "abc", AstFlags::comptime);
        CHECK(identifier->kind == AstKind::IdentifierExpr);
        CHECK(identifier->text == "abc");
        CHECK(identifier->flags == AstFlags::comptime);
        CHECK(identifier->parent == nullptr);
        CHECK(arena.node_count() == 2u);
        return true;
    }

    bool test_parent_child_and_order()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, make_span(0u, 200u));
        AstNode* class_decl = arena.make_child(module, AstKind::ClassDecl, make_span(10u, 190u), "Point",
            AstFlags::exported | AstFlags::open);
        CHECK(class_decl->parent == module);
        CHECK(module->child_count() == 1u);
        CHECK(module->child(0) == class_decl);

        AstNode* field = arena.make_child(class_decl, AstKind::FieldDecl, make_span(20u, 30u), "x");
        AstNode* method = arena.make_child(class_decl, AstKind::MethodDecl, make_span(40u, 100u), "area");
        CHECK(field->parent == class_decl);
        CHECK(method->parent == class_decl);
        CHECK(class_decl->child_count() == 2u);
        CHECK(class_decl->child(0) == field);
        CHECK(class_decl->child(1) == method);
        CHECK(class_decl->child(2) == nullptr);
        CHECK(class_decl->child(1000u) == nullptr);

        CHECK(module->first_child(AstKind::ClassDecl) == class_decl);
        CHECK(class_decl->first_child(AstKind::FieldDecl) == field);
        CHECK(class_decl->first_child(AstKind::MethodDecl) == method);
        CHECK(class_decl->first_child(AstKind::ConstructorDecl) == nullptr);
        CHECK(module->first_child(AstKind::Module) == nullptr);

        // 先 make 再 adopt：手动挂接也必须设置 parent 并追加到末尾。
        AstNode* param_list = arena.make(AstKind::ParamList, make_span(50u, 70u));
        CHECK(param_list->parent == nullptr);
        CHECK(arena.adopt(method, param_list) == param_list);
        CHECK(param_list->parent == method);
        CHECK(method->child_count() == 1u);
        CHECK(method->child(0) == param_list);
        CHECK(method->first_child(AstKind::ParamList) == param_list);

        CHECK(arena.node_count() == 5u);
        return true;
    }

    bool test_flags()
    {
        const AstFlags exported_and_open = AstFlags::exported | AstFlags::open;
        CHECK(has_flag(exported_and_open, AstFlags::exported));
        CHECK(has_flag(exported_and_open, AstFlags::open));
        CHECK(!has_flag(exported_and_open, AstFlags::final_));
        CHECK((exported_and_open & AstFlags::exported) == AstFlags::exported);
        CHECK((exported_and_open & AstFlags::hidden) == AstFlags::none);
        CHECK((AstFlags::none | AstFlags::hidden) == AstFlags::hidden);
        CHECK((AstFlags::hidden & AstFlags::hidden) == AstFlags::hidden);

        const AstFlags combined = AstFlags::exported | AstFlags::final_ | AstFlags::repeatable;
        AstArena arena;
        AstNode* decl = arena.make(AstKind::ClassDecl, make_span(0u, 10u), "C", combined);
        CHECK(decl->has_flag(AstFlags::exported));
        CHECK(decl->has_flag(AstFlags::final_));
        CHECK(decl->has_flag(AstFlags::repeatable));
        CHECK(!decl->has_flag(AstFlags::hidden));
        CHECK(!decl->has_flag(AstFlags::override_));
        CHECK(!decl->has_flag(AstFlags::distinct));
        CHECK(!decl->has_flag(AstFlags::comptime));
        return true;
    }

    bool test_duplicate_adopt()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, make_span(0u, 100u));
        AstNode* class_decl = arena.make_child(module, AstKind::ClassDecl, make_span(10u, 90u), "Point");
        AstNode* field = arena.make_child(class_decl, AstKind::FieldDecl, make_span(20u, 30u), "x");

        // 同一个子节点第二次 adopt 必须抛 std::logic_error，且树保持原状。
        bool duplicate_threw = false;
        try
        {
            arena.adopt(module, field);
        }
        catch (const std::logic_error&)
        {
            duplicate_threw = true;
        }
        CHECK(duplicate_threw);
        CHECK(field->parent == class_decl);
        CHECK(class_decl->child_count() == 1u);
        CHECK(module->child_count() == 1u);

        // 重复挂到同一个父节点也应被拒绝。
        bool same_parent_threw = false;
        try
        {
            arena.adopt(class_decl, field);
        }
        catch (const std::logic_error&)
        {
            same_parent_threw = true;
        }
        CHECK(same_parent_threw);
        CHECK(class_decl->child_count() == 1u);

        // 空指针参数属于调用错误，抛 std::invalid_argument。
        bool null_parent_threw = false;
        try
        {
            arena.adopt(nullptr, field);
        }
        catch (const std::invalid_argument&)
        {
            null_parent_threw = true;
        }
        CHECK(null_parent_threw);
        CHECK(field->parent == class_decl);

        // make_child 在分配前校验父节点，失败时不应留下孤儿节点。
        AstArena empty_arena;
        bool null_make_child_threw = false;
        try
        {
            empty_arena.make_child(nullptr, AstKind::Error, make_span(0u, 0u));
        }
        catch (const std::invalid_argument&)
        {
            null_make_child_threw = true;
        }
        CHECK(null_make_child_threw);
        CHECK(empty_arena.node_count() == 0u);
        return true;
    }

    bool test_node_address_stability()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, make_span(0u, 250u));
        AstNode* class_decl = arena.make_child(module, AstKind::ClassDecl, make_span(10u, 240u), "Stable");
        AstNode* field = arena.make_child(class_decl, AstKind::FieldDecl, make_span(20u, 30u), "value");

        // arena 增长时 unique_ptr 不移动节点本体，早先的裸指针必须继续有效。
        for (std::uint32_t index = 0u; index < 256u; ++index)
        {
            arena.make(AstKind::Error, make_span(index, index + 1u));
        }
        CHECK(arena.node_count() == 259u);
        CHECK(module->kind == AstKind::Module);
        CHECK(class_decl->text == "Stable");
        CHECK(field->text == "value");
        CHECK(field->parent == class_decl);
        CHECK(class_decl->parent == module);
        CHECK(module->child(0) == class_decl);
        CHECK(class_decl->child(0) == field);
        return true;
    }

    bool test_dump()
    {
        AstArena arena;
        AstNode* module = arena.make(AstKind::Module, make_span(0u, 120u));
        AstNode* class_decl = arena.make_child(module, AstKind::ClassDecl, make_span(10u, 110u), "Point");
        AstNode* field = arena.make_child(class_decl, AstKind::FieldDecl, make_span(20u, 30u), "x");
        AstNode* method = arena.make_child(class_decl, AstKind::MethodDecl, make_span(40u, 100u), "area");
        AstNode* params = arena.make_child(method, AstKind::ParamList, make_span(50u, 70u));
        AstNode* param = arena.make_child(params, AstKind::Param, make_span(51u, 60u), "radius");

        CHECK(field != nullptr);
        CHECK(param != nullptr);
        CHECK(method->child_count() == 1u);
        CHECK(params->child_count() == 1u);
        CHECK(dump(*module) == "(Module (ClassDecl Point (FieldDecl x) (MethodDecl area (ParamList (Param radius)))))");

        // 空 text 不输出占位符，也不产生多余空格。
        AstNode* block = arena.make(AstKind::BlockStmt, make_span(110u, 120u));
        CHECK(dump(*block) == "(BlockStmt)");

        // 字面量原文直接作为 text 输出。
        AstNode* literal = arena.make(AstKind::LiteralExpr, make_span(100u, 102u), "42");
        CHECK(dump(*literal) == "(LiteralExpr 42)");

        // 手动塞入的 nullptr 子节点统一输出 (Error)。
        AstArena broken_arena;
        AstNode* call = broken_arena.make(AstKind::CallExpr, make_span(0u, 3u), "f");
        call->children.push_back(nullptr);
        CHECK(dump(*call) == "(CallExpr f (Error))");
        return true;
    }

    bool test_kind_names()
    {
        CHECK(ast_kind_name(AstKind::Module) == "Module");
        CHECK(ast_kind_name(AstKind::ClassDecl) == "ClassDecl");
        CHECK(ast_kind_name(AstKind::FieldDecl) == "FieldDecl");
        CHECK(ast_kind_name(AstKind::MethodDecl) == "MethodDecl");
        CHECK(ast_kind_name(AstKind::ParamList) == "ParamList");
        CHECK(ast_kind_name(AstKind::NamedType) == "NamedType");
        CHECK(ast_kind_name(AstKind::LiteralExpr) == "LiteralExpr");
        CHECK(ast_kind_name(AstKind::Error) == "Error");
        CHECK(!ast_kind_name(static_cast<AstKind>(0xFFFFu)).empty());
        return true;
    }
}

int main()
{
    bool passed = true;
    passed = test_make() && passed;
    passed = test_parent_child_and_order() && passed;
    passed = test_flags() && passed;
    passed = test_duplicate_adopt() && passed;
    passed = test_node_address_stability() && passed;
    passed = test_dump() && passed;
    passed = test_kind_names() && passed;

    if (!passed)
    {
        std::cout << "FAIL: ast\n";
        return 1;
    }

    std::cout << "PASS: ast\n";
    return 0;
}
