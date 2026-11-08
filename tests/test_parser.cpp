// Parser.h 自包含测试：不依赖第三方测试框架。
// 覆盖：空输入、最小 class（字段 + 方法）、interface 多继承、annotation 声明与应用、
// operator、init/deinit/override、if/while/for/return、表达式优先级与后缀链、
// 复合类型、import/export、module 声明，以及缺 ';' / 缺 '}' 的错误恢复。
// 全部通过时打印 "PASS: parser" 并返回 0，否则打印 "FAIL: ..." 并返回 1。

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "compiler/Parser.h"

using namespace coordination::compiler;

namespace
{
    constexpr SourceId kSource = 11;

    struct ParseResult
    {
        AstArena arena;
        DiagnosticBag diagnostics;
        AstNode* root = nullptr;
        std::string dumpText;
    };

    void parseInto(std::string_view source, ParseResult& result)
    {
        Parser parser(kSource, source, result.diagnostics);
        result.root = parser.parse(result.arena);
        result.dumpText = result.root != nullptr ? dump(*result.root) : std::string();
    }

    bool contains(const std::string& haystack, std::string_view needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    bool hasDiagnostic(const ParseResult& result, std::string_view code)
    {
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (std::string_view(diagnostic.code) == code)
            {
                return true;
            }
        }
        return false;
    }

    // 解析器自身的错误码必须全部落在 E2xxx 段。
    bool onlyParserErrors(const ParseResult& result)
    {
        for (const Diagnostic& diagnostic : result.diagnostics.diagnostics())
        {
            if (diagnostic.code.rfind("E2", 0) != 0)
            {
                return false;
            }
        }
        return true;
    }
}

// 失败的断言立即从当前测试函数返回 1；成功路径由 main 统一打印 PASS。
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cout << "FAIL: " << #condition << " (line " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (false)

namespace
{
    int testEmptyInput()
    {
        ParseResult result;
        parseInto("", result);

        CHECK(result.root != nullptr);
        CHECK(result.root->kind == AstKind::Module);
        CHECK(result.root->child_count() == 0);
        CHECK(result.root->text.empty());
        CHECK(result.root->span.source == kSource);
        CHECK(result.root->span.begin == 0u);
        CHECK(result.root->span.end == 0u);
        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.dumpText == "(Module)");

        // 只有空白与注释时同样是空模块，且不产生诊断。
        ParseResult trivia;
        parseInto("  \n// 行注释\n/* 块注释 */\n", trivia);
        CHECK(trivia.diagnostics.diagnostics().empty());
        CHECK(trivia.dumpText == "(Module)");
        return 0;
    }

    int testMinimalClass()
    {
        constexpr std::string_view source =
            "class Point {\n"
            "    mutable x: integer;\n"
            "    const y: integer = 0;\n"
            "    length(): integer {\n"
            "        return this.x;\n"
            "    }\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.root->span.end == static_cast<std::uint32_t>(source.size()));
        CHECK(result.dumpText == "(Module (ClassDecl Point (FieldDecl mutable:x (NamedType integer)) "
                                "(FieldDecl const:y (NamedType integer) (LiteralExpr 0)) "
                                "(MethodDecl length (ParamList) (NamedType integer) "
                                "(BlockStmt (ReturnStmt (MemberExpr x (ThisExpr)))))))");

        AstNode* declaration = result.root->child(0);
        CHECK(declaration != nullptr);
        CHECK(declaration->kind == AstKind::ClassDecl);
        CHECK(declaration->text == "Point");
        CHECK(declaration->flags == AstFlags::none);
        CHECK(declaration->child_count() == 3);

        AstNode* fieldX = declaration->child(0);
        CHECK(fieldX->kind == AstKind::FieldDecl);
        CHECK(fieldX->text == "mutable:x");
        CHECK(fieldX->child_count() == 1);
        CHECK(fieldX->child(0)->kind == AstKind::NamedType);
        CHECK(fieldX->child(0)->text == "integer");

        AstNode* fieldY = declaration->child(1);
        CHECK(fieldY->kind == AstKind::FieldDecl);
        CHECK(fieldY->text == "const:y");
        CHECK(fieldY->child_count() == 2);
        CHECK(fieldY->child(1)->kind == AstKind::LiteralExpr);
        CHECK(fieldY->child(1)->text == "0");

        AstNode* method = declaration->child(2);
        CHECK(method->kind == AstKind::MethodDecl);
        CHECK(method->text == "length");
        CHECK(method->child_count() == 3);
        CHECK(method->child(0)->kind == AstKind::ParamList);
        CHECK(method->child(1)->kind == AstKind::NamedType);
        CHECK(method->child(2)->kind == AstKind::BlockStmt);
        CHECK(method->child(2)->child(0)->kind == AstKind::ReturnStmt);
        CHECK(method->child(2)->child(0)->child(0)->kind == AstKind::MemberExpr);
        CHECK(method->child(2)->child(0)->child(0)->text == "x");
        return 0;
    }

    int testInterfaceInheritance()
    {
        constexpr std::string_view source =
            "interface Shape : Drawable, Measurable {\n"
            "    area(): f64;\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.dumpText == "(Module (InterfaceDecl Shape (NamedType Drawable) (NamedType Measurable) "
                                "(MethodDecl area (ParamList) (NamedType f64))))");

        AstNode* declaration = result.root->child(0);
        CHECK(declaration->kind == AstKind::InterfaceDecl);
        CHECK(declaration->text == "Shape");
        CHECK(declaration->child_count() == 3);
        CHECK(declaration->child(0)->kind == AstKind::NamedType);
        CHECK(declaration->child(0)->text == "Drawable");
        CHECK(declaration->child(1)->kind == AstKind::NamedType);
        CHECK(declaration->child(1)->text == "Measurable");
        CHECK(declaration->child(2)->kind == AstKind::MethodDecl);
        CHECK(declaration->child(2)->child_count() == 2); // 只有 ParamList + 返回类型，没有函数体
        return 0;
    }

    int testAnnotations()
    {
        constexpr std::string_view source =
            "repeatable annotation tag();\n"
            "annotation reflect(value: Type);\n"
            "@reflect(Point)\n"
            "class User {\n"
            "    @deprecated\n"
            "    mutable name: []character;\n"
            "}\n"
            "@target(function, class, var)\n"
            "class Tagged {\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.root->child_count() == 4);

        AstNode* tag = result.root->child(0);
        CHECK(tag->kind == AstKind::AnnotationDecl);
        CHECK(tag->text == "tag");
        CHECK(tag->has_flag(AstFlags::repeatable));
        CHECK(tag->child(0)->kind == AstKind::ParamList);
        CHECK(tag->child(0)->child_count() == 0);

        AstNode* reflect = result.root->child(1);
        CHECK(reflect->kind == AstKind::AnnotationDecl);
        CHECK(reflect->text == "reflect");
        CHECK(reflect->child(0)->kind == AstKind::ParamList);
        CHECK(reflect->child(0)->child_count() == 1);
        AstNode* parameter = reflect->child(0)->child(0);
        CHECK(parameter->kind == AstKind::Param);
        CHECK(parameter->text == "const:value");
        CHECK(parameter->child(0)->kind == AstKind::NamedType);
        CHECK(parameter->child(0)->text == "Type");

        AstNode* cls = result.root->child(2);
        CHECK(cls->kind == AstKind::ClassDecl);
        CHECK(cls->text == "User");
        CHECK(cls->child_count() == 2);
        CHECK(cls->child(0)->kind == AstKind::AnnotationUse);
        CHECK(cls->child(0)->text == "reflect");
        CHECK(cls->child(0)->child(0)->kind == AstKind::AnnotationArgList);
        CHECK(cls->child(0)->child(0)->child(0)->kind == AstKind::IdentifierExpr);
        CHECK(cls->child(0)->child(0)->child(0)->text == "Point");

        AstNode* field = cls->child(1);
        CHECK(field->kind == AstKind::FieldDecl);
        CHECK(field->text == "mutable:name");
        CHECK(field->child_count() == 2);
        CHECK(field->child(0)->kind == AstKind::AnnotationUse);
        CHECK(field->child(0)->text == "deprecated");
        CHECK(field->child(0)->child(0)->child_count() == 0);
        CHECK(field->child(1)->kind == AstKind::ArrayType);
        CHECK(field->child(1)->text == "slice");
        CHECK(field->child(1)->child(0)->text == "character");

        AstNode* tagged = result.root->child(3);
        CHECK(tagged->kind == AstKind::ClassDecl);
        CHECK(tagged->text == "Tagged");
        CHECK(contains(result.dumpText,
            "(AnnotationUse target (AnnotationArgList (IdentifierExpr function) "
            "(IdentifierExpr class) (IdentifierExpr var)))"));
        return 0;
    }

    int testOperators()
    {
        constexpr std::string_view source =
            "class Vec2 {\n"
            "    operator +(other: Vec2): Vec2 {\n"
            "        return this;\n"
            "    }\n"
            "    operator -(): Vec2;\n"
            "    operator [](index: integer): f64 {\n"
            "        return 0;\n"
            "    }\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());

        AstNode* cls = result.root->child(0);
        CHECK(cls->kind == AstKind::ClassDecl);
        CHECK(cls->child_count() == 3);

        const std::string_view expected[] = { "+", "-", "[]" };
        for (std::size_t i = 0; i < 3; ++i)
        {
            AstNode* op = cls->child(i);
            CHECK(op->kind == AstKind::OperatorDecl);
            CHECK(op->text == expected[i]);
            CHECK(op->child(0)->kind == AstKind::ParamList);
        }

        AstNode* plus = cls->child(0);
        CHECK(plus->child(0)->child_count() == 1);
        CHECK(plus->child(0)->child(0)->text == "const:other");
        CHECK(plus->child(1)->kind == AstKind::NamedType);
        CHECK(plus->child(2)->kind == AstKind::BlockStmt);

        AstNode* minus = cls->child(1);
        CHECK(minus->child(0)->child_count() == 0);
        CHECK(minus->child_count() == 2); // ParamList + 返回类型

        AstNode* index = cls->child(2);
        CHECK(index->child(0)->child(0)->text == "const:index");
        return 0;
    }

    int testDeclarations()
    {
        constexpr std::string_view source =
            "type Meters = integer;\n"
            "distinct type UserId = integer;\n"
            "concept Ordered {\n"
            "    operator <(other: Self): bool;\n"
            "}\n"
            "class Vec : Base, I1, I2 {\n"
            "    init(x: integer) {\n"
            "        this.x = x;\n"
            "    }\n"
            "    deinit {\n"
            "    }\n"
            "    override length(): integer {\n"
            "        return 0;\n"
            "    }\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.root->child_count() == 4);

        AstNode* alias = result.root->child(0);
        CHECK(alias->kind == AstKind::TypeAliasDecl);
        CHECK(alias->text == "Meters");
        CHECK(alias->child(0)->kind == AstKind::NamedType);
        CHECK(alias->child(0)->text == "integer");

        AstNode* distinct = result.root->child(1);
        CHECK(distinct->kind == AstKind::DistinctTypeDecl);
        CHECK(distinct->text == "UserId");
        CHECK(distinct->has_flag(AstFlags::distinct));

        AstNode* conceptDecl = result.root->child(2);
        CHECK(conceptDecl->kind == AstKind::ConceptDecl);
        CHECK(conceptDecl->text == "Ordered");
        CHECK(conceptDecl->child(0)->kind == AstKind::OperatorDecl);
        CHECK(conceptDecl->child(0)->text == "<");

        AstNode* cls = result.root->child(3);
        CHECK(cls->kind == AstKind::ClassDecl);
        CHECK(cls->text == "Vec");
        CHECK(cls->child_count() == 6); // 3 个父类型 + 3 个成员
        CHECK(cls->child(0)->kind == AstKind::NamedType);
        CHECK(cls->child(1)->kind == AstKind::NamedType);
        CHECK(cls->child(2)->kind == AstKind::NamedType);

        AstNode* constructor = cls->child(3);
        CHECK(constructor->kind == AstKind::ConstructorDecl);
        CHECK(constructor->text == "init");
        CHECK(constructor->child(0)->kind == AstKind::ParamList);
        CHECK(constructor->child(1)->kind == AstKind::BlockStmt);

        AstNode* destructor = cls->child(4);
        CHECK(destructor->kind == AstKind::DestructorDecl);
        CHECK(destructor->text == "deinit");
        CHECK(destructor->child(0)->kind == AstKind::BlockStmt);

        AstNode* method = cls->child(5);
        CHECK(method->kind == AstKind::MethodDecl);
        CHECK(method->text == "length");
        CHECK(method->has_flag(AstFlags::override_));
        return 0;
    }

    int testStatements()
    {
        constexpr std::string_view source =
            "run(): void {\n"
            "    if (x) {\n"
            "        return;\n"
            "    } else {\n"
            "        while (y) {\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    for (const i: integer = 0; i < 3; i += 1) {\n"
            "        continue;\n"
            "    }\n"
            "    for (;;) {\n"
            "        break;\n"
            "    }\n"
            "    for (i = 0; ; ) {\n"
            "    }\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(contains(result.dumpText, "(FunctionDecl run (ParamList) (NamedType void)"));
        CHECK(contains(result.dumpText,
            "(IfStmt (IdentifierExpr x) (BlockStmt (ReturnStmt)) "
            "(BlockStmt (WhileStmt (IdentifierExpr y) (BlockStmt (BreakStmt)))))"));
        CHECK(contains(result.dumpText,
            "(ForStmt 111 (LocalDeclStmt const:i (NamedType integer) (LiteralExpr 0)) "
            "(BinaryExpr < (IdentifierExpr i) (LiteralExpr 3)) "
            "(AssignExpr += (IdentifierExpr i) (LiteralExpr 1)) (BlockStmt (ContinueStmt)))"));
        CHECK(contains(result.dumpText, "(ForStmt 000 (BlockStmt (BreakStmt)))"));
        CHECK(contains(result.dumpText,
            "(ForStmt 100 (ExprStmt (AssignExpr = (IdentifierExpr i) (LiteralExpr 0))) (BlockStmt))"));

        AstNode* function = result.root->child(0);
        CHECK(function->kind == AstKind::FunctionDecl);
        AstNode* block = function->child(2);
        CHECK(block->kind == AstKind::BlockStmt);
        CHECK(block->child_count() == 4);

        AstNode* ifStatement = block->child(0);
        CHECK(ifStatement->kind == AstKind::IfStmt);
        CHECK(ifStatement->child_count() == 3);
        CHECK(ifStatement->child(1)->kind == AstKind::BlockStmt);
        CHECK(ifStatement->child(2)->kind == AstKind::BlockStmt);

        AstNode* forStatement = block->child(1);
        CHECK(forStatement->kind == AstKind::ForStmt);
        CHECK(forStatement->text == "111");
        CHECK(forStatement->child_count() == 4);
        CHECK(forStatement->child(0)->kind == AstKind::LocalDeclStmt);
        CHECK(forStatement->child(1)->kind == AstKind::BinaryExpr);
        CHECK(forStatement->child(2)->kind == AstKind::AssignExpr);
        CHECK(forStatement->child(3)->kind == AstKind::BlockStmt);
        return 0;
    }

    int testExpressionPrecedence()
    {
        constexpr std::string_view source =
            "f(): void {\n"
            "    1 + 2 * 3;\n"
            "    a = b = c;\n"
            "    a ? b : c;\n"
            "    -a + !b;\n"
            "    f(x)[i].m;\n"
            "    g();\n"
            "    new Point(1);\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(contains(result.dumpText,
            "(ExprStmt (BinaryExpr + (LiteralExpr 1) (BinaryExpr * (LiteralExpr 2) (LiteralExpr 3))))"));
        CHECK(contains(result.dumpText,
            "(ExprStmt (AssignExpr = (IdentifierExpr a) "
            "(AssignExpr = (IdentifierExpr b) (IdentifierExpr c))))"));
        CHECK(contains(result.dumpText,
            "(ExprStmt (ConditionalExpr ? (IdentifierExpr a) (IdentifierExpr b) (IdentifierExpr c)))"));
        CHECK(contains(result.dumpText,
            "(ExprStmt (BinaryExpr + (UnaryExpr - (IdentifierExpr a)) (UnaryExpr ! (IdentifierExpr b))))"));
        CHECK(contains(result.dumpText,
            "(ExprStmt (MemberExpr m (IndexExpr (CallExpr (IdentifierExpr f) "
            "(ArgumentList (IdentifierExpr x))) (IdentifierExpr i))))"));
        CHECK(contains(result.dumpText, "(ExprStmt (CallExpr (IdentifierExpr g) (ArgumentList)))"));
        CHECK(contains(result.dumpText,
            "(ExprStmt (NewExpr (NamedType Point) (ArgumentList (LiteralExpr 1))))"));
        return 0;
    }

    int testSpecPointExample()
    {
        constexpr std::string_view source =
            "export class Point {\n"
            "    mutable x: integer(32);\n"
            "    mutable y: integer(32);\n"
            "    init(x: integer(32), y: integer(32)) {\n"
            "        this.x = x;\n"
            "        this.y = y;\n"
            "    }\n"
            "    lengthSquared(): integer(32) {\n"
            "        return this.x * this.x + this.y * this.y;\n"
            "    }\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(contains(result.dumpText, "(ExportDecl export (ClassDecl Point "));
        CHECK(contains(result.dumpText,
            "(FieldDecl mutable:x (GenericType integer (ArgumentList (LiteralExpr 32))))"));
        CHECK(contains(result.dumpText,
            "(ConstructorDecl init (ParamList (Param const:x "
            "(GenericType integer (ArgumentList (LiteralExpr 32)))) (Param const:y "
            "(GenericType integer (ArgumentList (LiteralExpr 32))))"));
        CHECK(contains(result.dumpText,
            "(MethodDecl lengthSquared (ParamList) "
            "(GenericType integer (ArgumentList (LiteralExpr 32))) (BlockStmt (ReturnStmt "));
        CHECK(contains(result.dumpText, "(AssignExpr = (MemberExpr x (ThisExpr)) (IdentifierExpr x))"));

        AstNode* exported = result.root->child(0);
        CHECK(exported->kind == AstKind::ExportDecl);
        CHECK(exported->child(0)->kind == AstKind::ClassDecl);
        CHECK(exported->child(0)->text == "Point");
        CHECK(exported->child(0)->child_count() == 4); // 2 字段 + init + 方法
        return 0;
    }

    int testTypes()
    {
        constexpr std::string_view source =
            "class Types {\n"
            "    const a: &T;\n"
            "    const b: &mut T;\n"
            "    const c: *T;\n"
            "    const d: *const T;\n"
            "    const e: [T; N];\n"
            "    const f: []T;\n"
            "    const g: dyn I;\n"
            "    const h: (A, B): R;\n"
            "    const i: integer(32);\n"
            "    const j: []character(8);\n"
            "}\n"
            "interface Producer<out T> : Base<T> {\n"
            "    get(): T;\n"
            "}\n"
            "@reflect([]character(8))\n"
            "class Annotated {\n"
            "}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(contains(result.dumpText, "(FieldDecl const:a (ReferenceType (NamedType T)))"));
        CHECK(contains(result.dumpText, "(FieldDecl const:b (MutableReferenceType (NamedType T)))"));
        CHECK(contains(result.dumpText, "(FieldDecl const:c (PointerType (NamedType T)))"));
        CHECK(contains(result.dumpText, "(FieldDecl const:d (PointerType const (NamedType T)))"));
        CHECK(contains(result.dumpText,
            "(FieldDecl const:e (ArrayType (NamedType T) (IdentifierExpr N)))"));
        CHECK(contains(result.dumpText, "(FieldDecl const:f (ArrayType slice (NamedType T)))"));
        CHECK(contains(result.dumpText, "(FieldDecl const:g (DynType (NamedType I)))"));
        CHECK(contains(result.dumpText,
            "(FieldDecl const:h (FunctionType (TypeArgList (NamedType A) (NamedType B)) (NamedType R)))"));
        CHECK(contains(result.dumpText,
            "(FieldDecl const:i (GenericType integer (ArgumentList (LiteralExpr 32))))"));
        CHECK(contains(result.dumpText,
            "(FieldDecl const:j (ArrayType slice "
            "(GenericType character (ArgumentList (LiteralExpr 8)))))"));
        CHECK(contains(result.dumpText,
            "(InterfaceDecl Producer (TypeParamList (TypeParam out:T)) "
            "(GenericType Base (TypeArgList (NamedType T))) (MethodDecl get (ParamList) (NamedType T)))"));
        CHECK(contains(result.dumpText,
            "(ClassDecl Annotated (AnnotationUse reflect (AnnotationArgList "
            "(ArrayType slice (GenericType character (ArgumentList (LiteralExpr 8)))))))"));

        AstNode* producer = result.root->child(1);
        CHECK(producer->kind == AstKind::InterfaceDecl);
        CHECK(producer->child(0)->kind == AstKind::TypeParamList);
        CHECK(producer->child(0)->child(0)->text == "out:T");
        CHECK(producer->child(1)->kind == AstKind::GenericType);
        CHECK(producer->child(1)->child(0)->kind == AstKind::TypeArgList);
        return 0;
    }

    int testModuleImportExport()
    {
        constexpr std::string_view source =
            "module acme.core;\n"
            "import math.vector;\n"
            "import math.vector.{Vec3, dot as d};\n"
            "export import util;\n"
            "export class X : Base {}\n";

        ParseResult result;
        parseInto(source, result);

        CHECK(result.diagnostics.diagnostics().empty());
        CHECK(result.root->text == "acme.core");
        CHECK(result.root->child_count() == 4);
        CHECK(result.dumpText == "(Module acme.core (ImportDecl math.vector) "
                                "(ImportDecl math.vector (NamedType Vec3) (NamedType dot as d)) "
                                "(ExportDecl export (ImportDecl util)) "
                                "(ExportDecl export (ClassDecl X (NamedType Base))))");

        AstNode* plainImport = result.root->child(0);
        CHECK(plainImport->kind == AstKind::ImportDecl);
        CHECK(plainImport->text == "math.vector");
        CHECK(plainImport->child_count() == 0);

        AstNode* namedImport = result.root->child(1);
        CHECK(namedImport->kind == AstKind::ImportDecl);
        CHECK(namedImport->text == "math.vector");
        CHECK(namedImport->child_count() == 2);
        CHECK(namedImport->child(0)->kind == AstKind::NamedType);
        CHECK(namedImport->child(0)->text == "Vec3");
        CHECK(namedImport->child(1)->text == "dot as d");

        AstNode* reexport = result.root->child(2);
        CHECK(reexport->kind == AstKind::ExportDecl);
        CHECK(reexport->text == "export");
        CHECK(reexport->child_count() == 1);
        CHECK(reexport->child(0)->kind == AstKind::ImportDecl);
        CHECK(reexport->child(0)->text == "util");
        CHECK(reexport->child(0)->has_flag(AstFlags::exported));

        AstNode* exportedClass = result.root->child(3);
        CHECK(exportedClass->kind == AstKind::ExportDecl);
        CHECK(exportedClass->child_count() == 1);
        CHECK(exportedClass->child(0)->kind == AstKind::ClassDecl);
        CHECK(exportedClass->child(0)->text == "X");
        CHECK(exportedClass->child(0)->has_flag(AstFlags::exported));
        CHECK(exportedClass->child(0)->child(0)->kind == AstKind::NamedType);
        CHECK(exportedClass->child(0)->child(0)->text == "Base");
        return 0;
    }

    int testMissingSemicolon()
    {
        ParseResult result;
        parseInto("class A { const x: T }", result);

        CHECK(result.root != nullptr);
        CHECK(result.root->kind == AstKind::Module);
        CHECK(result.diagnostics.has_errors());
        CHECK(hasDiagnostic(result, "E2001"));
        CHECK(onlyParserErrors(result));
        CHECK(result.dumpText == "(Module (ClassDecl A (FieldDecl const:x (NamedType T))))");

        AstNode* cls = result.root->child(0);
        CHECK(cls->kind == AstKind::ClassDecl);
        CHECK(cls->child(0)->kind == AstKind::FieldDecl);
        return 0;
    }

    int testMissingBrace()
    {
        ParseResult result;
        parseInto("class A { const x: T;", result);

        CHECK(result.root != nullptr);
        CHECK(result.root->kind == AstKind::Module);
        CHECK(result.diagnostics.has_errors());
        CHECK(hasDiagnostic(result, "E2003"));
        CHECK(onlyParserErrors(result));
        CHECK(contains(result.dumpText, "(ClassDecl A (FieldDecl const:x (NamedType T)))"));

        // 即使结构未闭合，也必须返回 Module 并保留已经解析出来的部分树。
        CHECK(result.root->child(0)->kind == AstKind::ClassDecl);
        return 0;
    }

    int testTopLevelRecovery()
    {
        ParseResult result;
        parseInto("123; class A {}", result);

        CHECK(result.root != nullptr);
        CHECK(result.root->kind == AstKind::Module);
        CHECK(result.diagnostics.has_errors());
        CHECK(hasDiagnostic(result, "E2002"));
        CHECK(onlyParserErrors(result));
        CHECK(result.root->child_count() == 2);
        CHECK(result.root->child(0)->kind == AstKind::Error);
        CHECK(result.root->child(1)->kind == AstKind::ClassDecl);
        CHECK(result.root->child(1)->text == "A");
        return 0;
    }

    int testErrorRecoveryTermination()
    {
        // 各种截断/畸形输入：不允许抛异常、不允许死循环，且必须返回 Module。
        const std::string_view sources[] = {
            "@",
            "class",
            "class A {",
            "class A { const x: T",
            "class A { @ }",
            "class A { x }",
            "if",
            "while (",
            "for (",
            "for (;;",
            "return",
            "break",
            "type A =",
            "distinct type",
            "import",
            "import a.{",
            "export",
            "operator",
            "annotation",
            "a.",
            "a ? b",
            "new",
            "&",
            "x: T",
            "]",
            "f(",
            "f():",
            "f(): T {",
        };

        for (std::string_view source : sources)
        {
            ParseResult result;
            parseInto(source, result);
            CHECK(result.root != nullptr);
            CHECK(result.root->kind == AstKind::Module);
        }
        return 0;
    }
}

int main()
{
    struct TestCase
    {
        const char* name;
        int (*run)();
    };

    const TestCase cases[] = {
        { "empty input", testEmptyInput },
        { "minimal class", testMinimalClass },
        { "interface inheritance", testInterfaceInheritance },
        { "annotations", testAnnotations },
        { "operators", testOperators },
        { "declarations", testDeclarations },
        { "statements", testStatements },
        { "expression precedence", testExpressionPrecedence },
        { "spec point example", testSpecPointExample },
        { "types", testTypes },
        { "module import export", testModuleImportExport },
        { "missing semicolon", testMissingSemicolon },
        { "missing brace", testMissingBrace },
        { "top level recovery", testTopLevelRecovery },
        { "error recovery termination", testErrorRecoveryTermination },
    };

    for (const TestCase& item : cases)
    {
        if (item.run() != 0)
        {
            std::cout << "（失败用例：" << item.name << "）" << std::endl;
            return 1;
        }
    }

    std::cout << "PASS: parser" << std::endl;
    return 0;
}
