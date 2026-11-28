#include "coord/codegen.h"
#include "coord/interp.h"
#include "coord/parser.h"
#include "coord/sema.h"
#include "coord/support.h"
#include "coord/syntax.h"

#include "coord/modules/atomic.h"
#include "coord/modules/annotation_registry.h"
#include "coord/modules/character.h"
#include "coord/modules/charset.h"
#include "coord/modules/compiler.h"
#include "coord/modules/dependency.h"
#include "coord/modules/file.h"
#include "coord/modules/identifier.h"
#include "coord/modules/hint.h"
#include "coord/modules/interface.h"
#include "coord/modules/processor.h"
#include "coord/modules/module.h"
#include "coord/modules/operator.h"
#include "coord/modules/descriptor.h"
#include "coord/modules/outline.h"
#include "coord/standard_library.h"
#include "coord/modules/package.h"
#include "coord/modules/project.h"
#include "coord/modules/scope.h"
#include "coord/modules/visibility.h"
#include "coord/modules/thread.h"
#include "coord/modules/unicode.h"

#include "coord/modules/archive.h"
#include "coord/modules/checksum.h"
#include "coord/modules/lockfile.h"
#include "coord/modules/manifest.h"
#include "coord/modules/mirror.h"
#include "coord/modules/platform.h"
#include "coord/modules/resolver.h"
#include "coord/modules/semver.h"
#include "coord/modules/toml.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": " #condition \
                << '\n';                                                     \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

struct Harness {
  coord::SourceManager sources;
  coord::DiagnosticEngine diagnostics;
  std::uint32_t fileId = 0;
  std::unique_ptr<coord::ModuleAst> module;
  std::unique_ptr<coord::Checker> checker;

  explicit Harness(std::string source) {
    fileId = sources.addFile("test.coo", std::move(source));
    coord::Lexer lexer(sources, fileId, diagnostics);
    coord::Parser parser(lexer.lexAll(), diagnostics);
    module = parser.parseModule(fileId);

    if (module && !diagnostics.hasErrors()) {
      const std::uint32_t standardId = sources.addFile(
          "<standard-annotations>",
          std::string(coord::standardAnnotationsSource()));
      coord::Lexer standardLexer(sources, standardId, diagnostics);
      coord::Parser standardParser(standardLexer.lexAll(), diagnostics);
      std::unique_ptr<coord::ModuleAst> standard =
          standardParser.parseModule(standardId);
      for (auto& decl : standard->decls) {
        decl->isStandard = true;
      }
      module->decls.insert(module->decls.begin(),
                           std::make_move_iterator(standard->decls.begin()),
                           std::make_move_iterator(standard->decls.end()));
    }

    if (!diagnostics.hasErrors()) {
      checker = std::make_unique<coord::Checker>(sources, diagnostics);
      checker->check(*module);
    }
    if (diagnostics.hasErrors() && std::getenv("COORD_TEST_DEBUG") != nullptr) {
      diagnostics.render(sources, std::cerr);
    }
  }

  [[nodiscard]] const coord::Decl* findClass(std::string_view name) const {
    if (!module) {
      return nullptr;
    }
    for (const auto& decl : module->decls) {
      if (decl->kind == coord::DeclKind::Class && decl->name == name) {
        return decl.get();
      }
    }
    return nullptr;
  }

  [[nodiscard]] int countCode(std::string_view code) const {
    int count = 0;
    for (const coord::Diagnostic& diagnostic : diagnostics.diagnostics()) {
      if (diagnostic.code == code) {
        ++count;
      }
    }
    return count;
  }
};

void testLexer() {
  coord::SourceManager sources;
  coord::DiagnosticEngine diagnostics;
  const std::uint32_t id =
      sources.addFile("lexer.coo", "class File { File(){} ~File(){} }");
  coord::Lexer lexer(sources, id, diagnostics);
  const std::vector<coord::Token> tokens = lexer.lexAll();
  CHECK(!diagnostics.hasErrors());
  CHECK(!tokens.empty());
  CHECK(tokens[0].kind == coord::TokKind::KwClass);
  CHECK(tokens[1].kind == coord::TokKind::Identifier);
  CHECK(tokens[1].text == "File");
  CHECK(std::any_of(tokens.begin(), tokens.end(), [](const coord::Token& t) {
    return t.kind == coord::TokKind::Tilde;
  }));
  CHECK(tokens.back().kind == coord::TokKind::End);
}

void testGenericDeclaration() {
  Harness harness("integer add<N extends integer>(const N& a, const N& b);");
  CHECK(!harness.diagnostics.hasErrors());
  int userDecls = 0;
  for (const auto& decl : harness.module->decls) {
    if (!decl->isStandard && !decl->isCore) {
      ++userDecls;
    }
  }
  CHECK(userDecls == 1);
  const coord::Decl* addPtr = nullptr;
  for (const auto& decl : harness.module->decls) {
    if (!decl->isStandard && !decl->isCore &&
        decl->kind == coord::DeclKind::Function && decl->name == "add") {
      addPtr = decl.get();
      break;
    }
  }
  CHECK(addPtr != nullptr);
  if (addPtr == nullptr) {
    return;
  }
  const coord::Decl& add = *addPtr;
  CHECK(add.kind == coord::DeclKind::Function);
  CHECK(add.name == "add");
  CHECK(add.generics.size() == 1);
  CHECK(add.generics[0].name == "N");
  CHECK(add.generics[0].bound != nullptr);
  CHECK(add.params.size() == 2);
  if (add.params.size() == 2) {
    CHECK(add.params[0].type->kind == coord::TypeExprKind::Ref);
    CHECK(add.params[0].type->constRef);
    CHECK(add.params[0].type->inner->name == "N");
  }
}

void testSynthesizedDestructor() {
  Harness harness(R"(
class A { A() {} ~A() {} }
class B { B() {} ~B() {} }
class Holder { A a; B b; }
)");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::Decl* holder = harness.findClass("Holder");
  CHECK(holder != nullptr);
  if (holder == nullptr) {
    return;
  }
  CHECK(!holder->hasUserDestructor);
  const coord::Decl* destructor = nullptr;
  for (const auto& member : holder->members) {
    if (member->kind == coord::DeclKind::Destructor) {
      destructor = member.get();
    }
  }
  CHECK(destructor != nullptr);
  if (destructor == nullptr) {
    return;
  }
  CHECK(destructor->synthesizedDestructor);
  CHECK(destructor->destroyOrder.size() == 2);
  if (destructor->destroyOrder.size() == 2) {
    CHECK(destructor->destroyOrder[0]->name == "b");
    CHECK(destructor->destroyOrder[1]->name == "a");
  }
}

void testMissingDestructionWarning() {
  Harness harness(R"(
class A { A() {} ~A() {} }
class Holder { A a; Holder() {} ~Holder() {} }
)");
  CHECK(!harness.diagnostics.hasErrors());
  CHECK(harness.countCode("W0001") == 1);
}

void testExplicitDestruction() {
  Harness harness(R"(
class A { A() {} ~A() {} }
class Holder { A a; Holder() {} ~Holder() { ~this.a; } }
)");
  CHECK(!harness.diagnostics.hasErrors());
  CHECK(harness.countCode("W0001") == 0);
}

void testCompileTimeEvaluation() {
  Harness harness(R"(
integer fib(const integer n) {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}
const integer FIB10 = fib(10);
)");
  CHECK(!harness.diagnostics.hasErrors());
  coord::Interpreter interpreter(*harness.module, harness.diagnostics);
  interpreter.evaluate();
  CHECK(!harness.diagnostics.hasErrors());
  const auto it = interpreter.constValues().find("FIB10");
  CHECK(it != interpreter.constValues().end());
  if (it != interpreter.constValues().end()) {
    CHECK(it->second.kind == coord::Value::Kind::Int);
    CHECK(it->second.intValue == 55);
  }
}

void testCodegen() {
  Harness harness(R"(
class File {
  string path;
public:
  File(string p) { this.path = p; }
  ~File() {}
  integer size() const { return 0; }
}
integer main() {
  File f("x");
  print(f.size());
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  coord::Interpreter interpreter(*harness.module, harness.diagnostics);
  interpreter.evaluate();
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, interpreter.constValues(), output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("class File") != std::string::npos);
  CHECK(generated.find("this->path") != std::string::npos);
  CHECK(generated.find("~File()") != std::string::npos);
  CHECK(generated.find("coord_rt::print") != std::string::npos);
  CHECK(generated.find("int main(") != std::string::npos);
}

void testInterface() {
  Harness harness(R"(
interface Drawable { void draw() const; }
class Point implements Drawable {
public:
  Point() {}
  override void draw() const {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("class Drawable") != std::string::npos);
  CHECK(generated.find("virtual void draw() const = 0;") != std::string::npos);
  CHECK(generated.find("class Point : public Drawable") != std::string::npos);
}

void testCallableInterfaceLambdaTarget() {
  Harness valid(R"(
interface Mapper {
  integer operator()(integer value);

  integer twice(integer value) {
    return value + value;
  }
}

integer apply(Mapper mapper, integer value) {
  return mapper(value);
}

integer main() {
  Mapper increment = [](integer value): integer -> value + 1;
  return apply(increment, 1);
}
)");
  CHECK(!valid.diagnostics.hasErrors());
  coord::Type* mapper = valid.checker->lookupType("Mapper");
  CHECK(mapper != nullptr);
  if (mapper != nullptr) {
    CHECK(mapper->functional);
    CHECK(mapper->functionalMethod == "operator()");
  }
  // Callable interfaces lower to function values; no C++ interface is emitted.
  std::ostringstream output;
  coord::CppEmitter emitter(*valid.module, {}, output);
  emitter.emit();
  CHECK(output.str().find("class Mapper") == std::string::npos);

  Harness invalid(R"(
interface Overloaded {
  void operator()();
  void operator()(integer value);
}

interface Mixed {
  void operator()();
  void other();
}

interface Empty {
  void only();
}

integer main() {
  Overloaded overloaded = [](): void -> {};
  Mixed mixed = [](): void -> {};
  Empty empty = [](): void -> {};
  return 0;
}
)");
  CHECK(invalid.countCode("E2079") == 3);
}

void testEnum() {
  Harness harness(R"(
enumerate Color {
  Red,
  Green = 3,
  Blue,
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::Decl* color = nullptr;
  for (const auto& decl : harness.module->decls) {
    if (decl->kind == coord::DeclKind::Enum && decl->name == "Color") {
      color = decl.get();
    }
  }
  CHECK(color != nullptr);
  if (color == nullptr) {
    return;
  }
  CHECK(color->variants.size() == 3);
  if (color->variants.size() == 3) {
    CHECK(color->variants[0].resolvedValue == 0);
    CHECK(color->variants[1].resolvedValue == 3);
    CHECK(color->variants[2].resolvedValue == 4);
  }
  coord::Type* type = harness.checker->lookupType("Color");
  CHECK(type != nullptr);
  if (type != nullptr) {
    CHECK(type->kind == coord::TypeKind::Enum);
    CHECK(type->variants.size() == 3);
  }
}

void testDuplicateEnumValue() {
  Harness harness("enumerate Bad { A = 1, B = 1 }");
  CHECK(harness.countCode("E2023") == 1);
}

void testEnumCodegen() {
  Harness harness(R"(
enumerate Color { Red, Green = 3, Blue }
integer main() {
  Color c = Color.Green;
  print(c);
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  coord::Interpreter interpreter(*harness.module, harness.diagnostics);
  interpreter.evaluate();
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, interpreter.constValues(), output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("enum class Color") != std::string::npos);
  CHECK(generated.find("Color::Green") != std::string::npos);
  CHECK(generated.find("std::is_enum_v") != std::string::npos);
}

void testPointerType() {
  Harness harness("void f(integer* p, const string* s, integer* q);");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::Decl* functionPtr = nullptr;
  for (const auto& decl : harness.module->decls) {
    if (!decl->isStandard && !decl->isCore &&
        decl->kind == coord::DeclKind::Function && decl->name == "f") {
      functionPtr = decl.get();
      break;
    }
  }
  CHECK(functionPtr != nullptr);
  if (functionPtr == nullptr) {
    return;
  }
  const coord::Decl& function = *functionPtr;
  CHECK(function.params.size() == 3);
  if (function.params.size() != 3) {
    return;
  }
  CHECK(function.params[0].type->kind == coord::TypeExprKind::Pointer);
  CHECK(function.params[0].type->inner->name == "integer");
  CHECK(function.params[1].type->kind == coord::TypeExprKind::Pointer);
  CHECK(function.params[1].type->inner->kind == coord::TypeExprKind::Const);
  CHECK(function.params[1].type->inner->inner->name == "string");
  CHECK(function.params[2].type->kind == coord::TypeExprKind::Pointer);
  CHECK(function.params[2].type->inner->name == "integer");
}

void testScopeShadowing() {
  Harness harness(R"(
integer main() {
  integer x = 1;
  {
    integer x = 2;
    print(x);
  }
  print(x);
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
}

void testScopeRelations() {
  Harness parent(R"(
integer main() {
    integer a = 1;
    {
        print(a);
        { print(a); }
    }
    return 0;
}
)");
  CHECK(!parent.diagnostics.hasErrors());

  Harness sibling(R"(
integer main() {
    { integer b = 2; }
    { print(b); }
    return 0;
}
)");
  CHECK(sibling.countCode("E2004") >= 1);

  Harness intersection(R"(
integer main() {
    integer a = 1;
    { print(a); }
    { print(a); }
    return 0;
}
)");
  CHECK(!intersection.diagnostics.hasErrors());

  Harness beforeDeclaration(R"(
integer main() {
    { print(a); }
    { integer a = 1; }
    return 0;
}
)");
  CHECK(beforeDeclaration.countCode("E2004") >= 1);
}

void testSameScopeRedeclaration() {
  Harness harness("integer main() { integer x = 1; integer x = 2;  return 0; }");
  CHECK(harness.countCode("E2002") >= 1);
}

void testUnicodeAndBom() {
  // UTF-8 BOM is stripped; the first token is still `class`.
  coord::SourceManager sources;
  coord::DiagnosticEngine diagnostics;
  const std::uint32_t id =
      sources.addFile("bom.coo", "\xEF\xBB\xBF" "class A {}");
  CHECK(sources.file(id).encoding == "utf-8");
  coord::Lexer lexer(sources, id, diagnostics);
  const std::vector<coord::Token> tokens = lexer.lexAll();
  CHECK(tokens.size() >= 3);
  if (tokens.size() >= 3) {
    CHECK(tokens[0].kind == coord::TokKind::KwClass);
    CHECK(tokens[1].text == "A");
  }

  // UTF-16LE with BOM transcodes to UTF-8.
  std::string utf16le = "\xFF\xFE";
  for (const char c : std::string("class A {}")) {
    utf16le.push_back(c);
    utf16le.push_back('\0');
  }
  const coord::DecodedSource decoded16 = coord::decodeSource(utf16le);
  CHECK(decoded16.encoding == "utf-16le");
  CHECK(decoded16.hadBom);
  CHECK(decoded16.utf8 == "class A {}");

  // UTF-32BE with BOM.
  std::string utf32be("\x00\x00\xFE\xFF", 4);
  for (const char c : std::string("void")) {
    utf32be.push_back('\0');
    utf32be.push_back('\0');
    utf32be.push_back('\0');
    utf32be.push_back(c);
  }
  const coord::DecodedSource decoded32 = coord::decodeSource(utf32be);
  CHECK(decoded32.encoding == "utf-32be");
  CHECK(decoded32.utf8 == "void");

  // Unicode identifier.
  coord::SourceManager unicodeSources;
  coord::DiagnosticEngine unicodeDiagnostics;
  const std::uint32_t unicodeId = unicodeSources.addFile(
      "unicode.coo", "void \xE4\xBD\xA0\xE5\xA5\xBD() {}");
  coord::Lexer unicodeLexer(unicodeSources, unicodeId, unicodeDiagnostics);
  const std::vector<coord::Token> unicodeTokens = unicodeLexer.lexAll();
  CHECK(!unicodeDiagnostics.hasErrors());
  CHECK(unicodeTokens.size() >= 2);
  if (unicodeTokens.size() >= 2) {
    CHECK(unicodeTokens[1].kind == coord::TokKind::Identifier);
    CHECK(unicodeTokens[1].text == "\xE4\xBD\xA0\xE5\xA5\xBD");
  }
}

void testModules() {
  using namespace coord;

  IdentifierTable identifiers;
  const Identifier alpha = identifiers.intern("alpha");
  CHECK(identifiers.intern("alpha") == alpha);
  CHECK(identifiers.text(alpha) == "alpha");

  ScopeStack<int> scopes;
  scopes.push();
  scopes.declare("x", 1);
  scopes.push();
  scopes.declare("x", 2);
  CHECK(scopes.lookup("x") != nullptr && *scopes.lookup("x") == 2);
  scopes.pop();
  CHECK(scopes.lookup("x") != nullptr && *scopes.lookup("x") == 1);
  scopes.pop();

  CHECK(findOperator("+") != nullptr && findOperator("+")->precedence == 8);
  CHECK(findOperator("*") != nullptr && findOperator("*")->precedence == 9);

  Character character(U'中');
  CHECK(!character.isAscii());
  CHECK(character.toUtf8() == "\xE4\xB8\xAD");

  std::size_t offset = 0;
  CodePoint codePoint = 0;
  CHECK(decodeUtf8("\xE4\xB8\xAD", offset, codePoint));
  CHECK(codePoint == U'中');
  CHECK(detectBom("\xEF\xBB\xBF").length == 3);

  Atomic<int> counter(1);
  (void)counter.fetchAdd(2);
  CHECK(counter.load() == 3);

  Stream stream("a\r\nb\rc\n");
  CHECK(stream.readByStream() == "a\nb\nc\n");
  CHECK(stream.lineCount() == 3);
  CHECK(stream.readByLine(0).has_value() && *stream.readByLine(0) == "a");
  CHECK(stream.readByLine(2).has_value() && *stream.readByLine(2) == "c");
  CHECK(!stream.readByLine(3).has_value());
  CHECK(stream.readByByteOffset(2).has_value() &&
        *stream.readByByteOffset(2) == "b\nc\n");
  CHECK(stream.writeByLine(1, "B"));
  CHECK(stream.readByStream() == "a\nB\nc\n");
  CHECK(stream.writeByByteOffset(2, "XY"));
  CHECK(stream.readByStream() == "a\nXYc\n");
  CHECK(stream.writeByStream("x\r\ny\r\n"));
  CHECK(stream.readByStream() == "x\ny\n");

  int threadValue = 0;
  {
    Thread thread([&threadValue] { threadValue = 7; });
    thread.join();
  }
  CHECK(threadValue == 7);

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "coord_modules_test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root / "src", error);
  CHECK(File::writeAll(root / "src" / "main.coo", "integer main() { return 0; }\n"));
  CHECK(File::writeAll(root / "coordination.toml",
                       "[dependencies]\n"
                       "foo = \"1.2\"\n"
                       "bar = { path = \"../bar\", version = \"0.1\" }\n"));

  File textFile(root / "stream.txt");
  CHECK(textFile.writeByStream("first\r\nsecond\r\n"));
  CHECK(textFile.readByStream().has_value());
  if (textFile.readByStream().has_value()) {
    CHECK(*textFile.readByStream() == "first\nsecond\n");
  }
  CHECK(textFile.readByLine(1).has_value() &&
        *textFile.readByLine(1) == "second");
  CHECK(textFile.writeByLine(0, "FIRST"));
  CHECK(textFile.readByByteOffset(6).has_value());

  const std::vector<Module> modules = ModuleScanner{}.scan(root);
  CHECK(modules.size() == 1);
  if (!modules.empty()) {
    CHECK(modules.front().name == "src.main");
  }

  const std::vector<Package> packages = PackageScanner{}.scan(root);
  CHECK(packages.size() == 1);
  if (!packages.empty()) {
    CHECK(packages.front().manifest.filename() == "coordination.toml");
  }

  const std::optional<Project> project = ProjectScanner{}.scan(root / "src");
  CHECK(project.has_value());
  if (project.has_value()) {
    CHECK(project->root == root);
  }

  const std::vector<Dependency> dependencies =
      DependencyScanner{}.scan(root / "coordination.toml");
  CHECK(dependencies.size() == 2);

  DependencyManager manager;
  Dependency first{"first", "1", {}, {"second"}};
  Dependency second{"second", "1", {}, {}};  manager.add(first);
  manager.add(second);
  bool ok = false;
  const std::vector<std::string> order = manager.topologicalOrder(ok);
  CHECK(ok);
  CHECK(order.size() == 2);
  if (order.size() == 2) {
    CHECK(order[0] == "second");
    CHECK(order[1] == "first");
  }

  DependencyManager cyclic;
  cyclic.add(Dependency{"a", "1", {}, {"b"}});
  cyclic.add(Dependency{"b", "1", {}, {"a"}});
  bool cycleOk = true;
  (void)cyclic.topologicalOrder(cycleOk);
  CHECK(!cycleOk);

  CompileOptions options;
  options.inputPath = (root / "src" / "main.coo").string();
  std::ostringstream compilerOut;
  std::ostringstream compilerErr;
  CHECK(Compiler{}.compile(options, compilerOut, compilerErr) == 0);

  std::filesystem::remove_all(root, error);
}

void testAccessControl() {
  Harness privateField(R"(
class A { private: integer x; }
integer main() { A a; print(a.x);  return 0; }
)");
  CHECK(privateField.countCode("E2030") >= 1);

  Harness publicField(R"(
class A { public: integer x; }
integer main() { A a; print(a.x);  return 0; }
)");
  CHECK(!publicField.diagnostics.hasErrors());

  Harness protectedInherited(R"(
class B { protected: integer p; }
class D extends B { public: integer f() { return this.p; } }
)");
  CHECK(!protectedInherited.diagnostics.hasErrors());

  Harness privateInherited(R"(
class B { private: integer p; }
class D extends B { public: integer f() { return this.p; } }
)");
  CHECK(privateInherited.countCode("E2030") >= 1);
}

void testInheritanceRules() {
  Harness finalClass("final class A {} class B extends A {}");
  CHECK(finalClass.countCode("E2031") >= 1);

  Harness finalMethod(R"(
class A { public: virtual final void f() {} }
class B extends A { public: override void f() {} }
)");
  CHECK(finalMethod.countCode("E2032") >= 1);

  Harness missingOverride(R"(
class A { public: virtual void f() {} }
class B extends A { public: void f() {} }
)");
  CHECK(missingOverride.countCode("E2035") >= 1);

  Harness overrideWithoutBase(R"(
class B { public: override void f() {} }
)");
  CHECK(overrideWithoutBase.countCode("E2033") >= 1);

  Harness covariant(R"(
class Animal { public: Animal() {} }
class Dog extends Animal { public: Dog() {} }
class Base { public: virtual Animal make() { return Animal(); } }
class Derived extends Base {
public:
  override Dog make() { return Dog(); }
}
)");
  CHECK(!covariant.diagnostics.hasErrors());

  Harness finalField(R"(
class A { public: final integer x; }
class B extends A { public: integer x; }
)");
  CHECK(finalField.countCode("E2032") >= 1);
}

void testAccessCodegen() {
  Harness harness(R"(
final class Sealed {
public:
  Sealed() {}
}
class Base {
public:
  Base() {}
  virtual void f() const {}
}
class Derived extends Base {
public:
  Derived() {}
  override void f() const {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("class Sealed final") != std::string::npos);
  CHECK(generated.find("virtual void f() const") != std::string::npos);
  CHECK(generated.find("override") != std::string::npos);
}

void testSuperAndInitializers() {
  Harness harness(R"(
class Animal {
public:
  Animal(integer legs) { this.legs = legs; }
  virtual void speak() const {}
  integer legs;
}
class Dog extends Animal {
public:
  Dog() : super(4), name("rex") {}
  override void speak() const { super.speak(); }
  string name;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("Animal(static_cast") != std::string::npos);
  CHECK(generated.find("this->Animal::speak()") != std::string::npos);
  CHECK(generated.find("name(std::string(\"rex\"))") != std::string::npos);

  Harness noBase("class A { A() : super(1) {} }");
  CHECK(noBase.countCode("E2036") >= 1);

  Harness unknownField("class A { A() : missing(1) {} }");
  CHECK(unknownField.countCode("E2038") >= 1);

  Harness privateDefault(R"(
class A { A() {} }
integer main() { A a;  return 0; }
)");
  CHECK(privateDefault.countCode("E2030") >= 1);

  Harness privateDestructor(R"(
class A {
public:
  A() {}
private:
  ~A() {}
}
integer main() { A a;  return 0; }
)");
  CHECK(privateDestructor.countCode("E2030") >= 1);
}

void testMetaAnnotations() {
  Harness harness(R"(
@Target(class, method, field)
@Repeatable
@Retention(invocation)
@Inherit
annotation Tag { string value() const; }

@Tag("a")
@Tag("b")
class A {
    @Tag("f") integer x;
    @Tag("m") void m() {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("// annotations: @Tag(\"a\")") != std::string::npos);
  CHECK(generated.find("// annotations: @Tag(\"f\")") != std::string::npos);

  Harness wrongTarget(R"(
@Target(class)
annotation OnlyClass { string value() const; }
@OnlyClass("x")
void f() {}
)");
  CHECK(wrongTarget.countCode("E2040") >= 1);

  Harness duplicate(R"(
annotation Once { string value() const; }
@Once("a")
@Once("b")
class B {}
)");
  CHECK(duplicate.countCode("E2042") >= 1);

  Harness unknown(R"(
@Bogus
class C {}
)");
  CHECK(unknown.countCode("E2041") >= 1);

  Harness metaOnClass(R"(
@Inherit
class D {}
)");
  CHECK(metaOnClass.countCode("E2040") >= 1);

  Harness metaOnAnnotation(R"(
@Meta
annotation Bad { string value() const; }
)");
  CHECK(metaOnAnnotation.countCode("E2043") >= 1);

  Harness retentionEnumOnAnnotation(R"(
@Retention(enum)
annotation BadRetention { string value() const; }
)");
  CHECK(retentionEnumOnAnnotation.countCode("E2021") >= 1);

  Harness badTarget(R"(
@Target(bogus)
annotation BadTarget { string value() const; }
)");
  CHECK(badTarget.countCode("E2044") >= 1);

  Harness retained(R"(
@Target(class)
@Retention(runtime)
annotation Tag { string value() const; }

@Tag("x")
class Marked {}

@Retention(enum)
enumerate Color { Red, Green }
)");
  CHECK(!retained.diagnostics.hasErrors());
  std::ostringstream retainedOutput;
  coord::CppEmitter retainedEmitter(*retained.module, {}, retainedOutput);
  retainedEmitter.emit();
  const std::string metadata = retainedOutput.str();
  CHECK(metadata.find("kRetainedAnnotations") != std::string::npos);
  CHECK(metadata.find("\"Marked\", \"Tag\"") != std::string::npos);
  CHECK(metadata.find("kColorVariants") != std::string::npos);
  CHECK(metadata.find("\"Green\", 1, 1") != std::string::npos);

  Harness documented(R"(
@Documented
annotation Audited { string value() const; }
)");
  CHECK(!documented.diagnostics.hasErrors());

  Harness documentedOnClass(R"(
@Documented
class Bad {}
)");
  CHECK(documentedOnClass.countCode("E2040") >= 1);
}

void testAnnotationInheritance() {
  Harness harness(R"(
@Target(class, method)
@Inherit
annotation Marker { string value() const; }

@Marker("base")
class Base {
public:
    @Marker("method")
    virtual void f() {}
}

class Derived extends Base {
public:
    override void f() {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("// inherited annotations: @Marker") !=
        std::string::npos);

  Harness directWins(R"(
@Target(class)
@Inherit
annotation Marker { string value() const; }

@Marker("base")
class Base {}

@Marker("derived")
class Derived extends Base {}
)");
  CHECK(!directWins.diagnostics.hasErrors());
  std::ostringstream directOutput;
  coord::CppEmitter directEmitter(*directWins.module, {}, directOutput);
  directEmitter.emit();
  CHECK(directOutput.str().find("// inherited annotations: @Marker") ==
        std::string::npos);

  Harness notInherited(R"(
@Target(class)
annotation Marker { string value() const; }

@Marker("base")
class Base {}

class Derived extends Base {}
)");
  CHECK(!notInherited.diagnostics.hasErrors());
  std::ostringstream noOutput;
  coord::CppEmitter noEmitter(*notInherited.module, {}, noOutput);
  noEmitter.emit();
  CHECK(noOutput.str().find("// inherited annotations: @Marker") ==
        std::string::npos);
}

void testOptionalNull() {
  Harness harness(R"(
integer main() {
  @Optional integer* pointer = null;
  @Optional integer* defaulted;
  if (pointer == null) { print("empty"); }
  if (defaulted == null) { print("defaulted"); }
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("nullptr") != std::string::npos);

  Harness nonOptional(R"(
integer main() {
  integer* pointer = null;
    return 0;
}
)");
  CHECK(nonOptional.countCode("E2046") >= 1);

  Harness wrongType(R"(
integer main() {
  @Optional integer value = null;
    return 0;
}
)");
  CHECK(wrongType.countCode("E2047") >= 1);

  Harness field(R"(
class Holder {
  @Optional integer* okay = null;
  integer* bad = null;
}
)");
  CHECK(field.countCode("E2046") >= 1);
}

void testConversionConstructors() {
  Harness harness(R"(
class B {
public:
  B() {}
}
class A {
public:
  A() {}
  A(B value) {}
}
class Point {
public:
  integer x;
  integer y;
}
integer main() {
  B b;
  A converted = b;
  A parentheses();
  Point point{1, 2};
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("A converted = b") != std::string::npos);
  CHECK(generated.find("Point point{") != std::string::npos);
  CHECK(generated.find("A parentheses = A()") != std::string::npos);

  Harness braceWithConstructor(R"(
class A {
public:
  A() {}
  integer x;
}
integer main() {
  A a{1};
    return 0;
}
)");
  CHECK(braceWithConstructor.countCode("E2049") >= 1);

  Harness privateConversion(R"(
class B { public: B() {} }
class A {
  A(B value) {}
}
integer main() {
  B b;
  A converted = b;
    return 0;
}
)");
  CHECK(privateConversion.diagnostics.hasErrors());
}

void testResultCodegen() {
  Harness harness(R"(
@Result<integer>
integer answer() {
  return 42;
}

@Result<integer>
integer caller() {
  answer();
  return answer();
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("coord_rt::Result<std::int64_t> answer()") !=
        std::string::npos);
  CHECK(generated.find("Result<std::int64_t>::success") !=
        std::string::npos);
  CHECK(generated.find("if (!__result0.ok) { return __result0; }") !=
        std::string::npos);
  CHECK(generated.find("return answer();") != std::string::npos);

  Harness unhandled(R"(
@Result<integer>
integer answer() { return 42; }
integer main() {
  answer();
    return 0;
}
)");
  CHECK(unhandled.countCode("E2050") >= 1);

  Harness both(R"(
@Result<integer>
@Expect<integer>
integer f() { return 1; }
)");
  CHECK(both.countCode("E2051") >= 1);

  Harness errorFlow(R"(
@Result<integer>
integer fail() {
  return error("boom");
}
@Result<integer>
integer main() {
  fail();
  return 0;
}
)");
  CHECK(!errorFlow.diagnostics.hasErrors());
  std::ostringstream errorOutput;
  coord::CppEmitter errorEmitter(*errorFlow.module, {}, errorOutput);
  errorEmitter.emit();
  const std::string errorGenerated = errorOutput.str();
  CHECK(errorGenerated.find("failure(std::string(\"boom\"))") !=
        std::string::npos);
  CHECK(errorGenerated.find("coord_rt::Result<std::int64_t> coord_main()") !=
        std::string::npos);
  CHECK(errorGenerated.find("__main_result") != std::string::npos);

  Harness errorOutside(R"(
integer main() {
  error("boom");
    return 0;
}
)");
  CHECK(errorOutside.countCode("E2052") >= 1);
}

void testOutlineAndHints() {
  Harness harness(R"(
@Target(method)
annotation Handler implements ProxyHandler {
  @Get
  void get() {}
}

@Proxy(Handler)
class Service {
public:
  integer field;
  void run() const {}
}

interface Runnable {
  void run();
}

enumerate Color {
  Red,
  Green;
  void describe() {}
}

@Result<integer>
integer compute() {
  return 1;
}

const integer LIMIT = 10;
)");
  CHECK(!harness.diagnostics.hasErrors());

  coord::outline::Outline outline =
      coord::outline::Outline::build(*harness.module);
  CHECK(outline.root.kind == coord::outline::OutlineKind::Module);
  CHECK(outline.root.children.size() == 6);

  const coord::outline::OutlineNode& service = outline.root.children[1];
  CHECK(service.kind == coord::outline::OutlineKind::Class);
  CHECK(service.isProxy);
  CHECK(service.hasAnnotation("Proxy"));
  CHECK(std::any_of(service.children.begin(), service.children.end(),
                    [](const coord::outline::OutlineNode& child) {
                      return child.kind == coord::outline::OutlineKind::Field;
                    }));
  CHECK(std::any_of(service.children.begin(), service.children.end(),
                    [](const coord::outline::OutlineNode& child) {
                      return child.kind == coord::outline::OutlineKind::Method;
                    }));
  CHECK(service.children[0].leaf);
  CHECK(service.children[0].kind == coord::outline::OutlineKind::Field);

  const auto leaves = outline.leaves();
  const auto hasLeaf = [&](std::string_view name) {
    return std::any_of(leaves.begin(), leaves.end(),
                       [&](const coord::outline::OutlineNode* node) {
                         return node->name == name;
                       });
  };
  CHECK(hasLeaf("field"));
  CHECK(hasLeaf("run"));
  CHECK(hasLeaf("compute"));
  CHECK(hasLeaf("LIMIT"));
  CHECK(hasLeaf("Red"));

  const coord::outline::OutlineNode* field =
      outline.at(service.children[0].span.begin);
  CHECK(field != nullptr);
  CHECK(field->name == "field");
  CHECK(field->property("kind") != nullptr);
  CHECK(*field->property("kind") == "field");

  coord::hint::HintEngine hints(outline);
  const auto all = hints.all();
  CHECK(hasLeaf("compute"));
  CHECK(std::any_of(all.begin(), all.end(),
                    [](const coord::hint::Hint& hint) {
                      return hint.label == "compute";
                    }));
  const auto contextual = hints.at(service.children[0].span.begin);
  CHECK(std::any_of(contextual.begin(), contextual.end(),
                    [](const coord::hint::Hint& hint) {
                      return hint.kind == coord::hint::HintKind::Signature;
                    }));
}

void testSignednessAnnotations() {
  Harness harness(R"(
integer main() {
  @Unsigned integer count = 1;
  @Signed integer delta = -1;
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("std::uint64_t count") != std::string::npos);
  CHECK(generated.find("std::int64_t delta") != std::string::npos);

  Harness both(R"(
integer main() {
  @Unsigned @Signed integer value = 1;
    return 0;
}
)");
  CHECK(both.countCode("E2053") >= 1);

  Harness wrongType(R"(
integer main() {
  @Unsigned string text = "x";
    return 0;
}
)");
  CHECK(wrongType.countCode("E2054") >= 1);
}

void testExpectCodegen() {
  Harness harness(R"(
@Expect<integer>
integer risky() {
  return 1;
}

@Expect<integer>
integer caller() {
  return risky();
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("coord_rt::Expect<std::int64_t> risky()") !=
        std::string::npos);

  Harness ignored(R"(
@Expect<integer>
integer risky() { return 1; }

@Result<integer>
integer caller() {
  risky();
  return 1;
}
)");
  CHECK(ignored.countCode("E2055") >= 1);

  Harness unhandled(R"(
@Expect<integer>
integer risky() { return 1; }

integer main() {
  risky();
    return 0;
}
)");
  CHECK(unhandled.countCode("E2050") >= 1);

  Harness assigned(R"(
@Expect<integer>
integer risky() { return 1; }

@Result<integer>
integer caller() {
  integer value = risky();
  return value;
}
)");
  CHECK(assigned.countCode("E2005") >= 1);

  Harness wrongFlavor(R"(
@Expect<integer>
integer risky() { return 1; }

@Result<integer>
integer caller() {
  return risky();
}
)");
  CHECK(wrongFlavor.countCode("E2005") >= 1);

  Harness printed(R"(
@Expect<integer>
integer risky() { return 1; }

@Result<integer>
integer caller() {
  print(risky());
  return 1;
}
)");
  CHECK(printed.countCode("E2055") >= 1);

  Harness api(R"(
@Expect<integer>
integer risky() { return 7; }

@Result<integer>
integer caller() {
  if (!risky().isOk()) { return 1; }
  integer value = risky().expect("must succeed");
  return value;
}
)");
  CHECK(!api.diagnostics.hasErrors());
  std::ostringstream apiOutput;
  coord::CppEmitter apiEmitter(*api.module, {}, apiOutput);
  apiEmitter.emit();
  const std::string apiGenerated = apiOutput.str();
  CHECK(apiGenerated.find("isOk()") != std::string::npos);
  CHECK(apiGenerated.find("expect(std::string(\"must succeed\"))") !=
        std::string::npos);

  Harness ifLet(R"(
integer main() {
  if let value = 1 {
    print(value);
  }
  return 0;
}
)");
  CHECK(ifLet.countCode("E1101") >= 1);
}

void testLengthAnnotations() {
  Harness harness(R"(
integer main() {
  @Unsigned @Length(32) integer wide = 1;
  @Signed @Length(16) integer narrow = 2;
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("std::uint32_t wide") != std::string::npos);
  CHECK(generated.find("std::int16_t narrow") != std::string::npos);

  Harness invalid(R"(
integer main() {
  @Length(7) integer bad = 1;
    return 0;
}
)");
  CHECK(invalid.countCode("E2056") >= 1);

  Harness wrongType(R"(
integer main() {
  @Length(32) string text = "x";
    return 0;
}
)");
  CHECK(wrongType.countCode("E2057") >= 1);

  Harness field(R"(
class Holder {
public:
  @Length(8) integer small;
}
)");
  CHECK(!field.diagnostics.hasErrors());
  std::ostringstream fieldOutput;
  coord::CppEmitter fieldEmitter(*field.module, {}, fieldOutput);
  fieldEmitter.emit();
  CHECK(fieldOutput.str().find("std::int8_t small") != std::string::npos);
}

void testFixTypes() {
  Harness harness(R"(
integer main() {
  @Power(2) fix price = 19.99;
  @Power(2) integer cents = 19.99;
  @Power(2) float ratio = 1.5;
  fix whole = 3;
  @Power(2) fix a = 1.5;
  @Power(2) fix b = 2.0;
  @Power(4) fix product = a * b;
  @Power(2) fix sum = a + b;
  @Power(3) fix finer = 1.500;
  @Power(3) fix aligned = a + finer;
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("coord_rt::Fix<2, 10, 64> price") !=
        std::string::npos);
  CHECK(generated.find("fromRaw(1999LL)") != std::string::npos);
  CHECK(generated.find("coord_rt::Fix<2, 10, 64> cents") !=
        std::string::npos);
  CHECK(generated.find("coord_rt::Fix<2, 10, 64> ratio") !=
        std::string::npos);
  CHECK(generated.find("coord_rt::Fix<0, 10, 64> whole") !=
        std::string::npos);
  CHECK(generated.find("coord_rt::Fix<4, 10, 64> product") !=
        std::string::npos);
  CHECK(generated.find("coord_rt::Fix<2, 10, 64> sum") !=
        std::string::npos);
  CHECK(generated.find("coord_rt::Fix<3, 10, 64> aligned") !=
        std::string::npos);

  Harness conversion(R"(
@Power(4) fix convert(fix x) {
  return x;
}

integer main() {
  @Power(2) fix price = 1.5;
  @Power(4) fix fine = price;
  @Radix(2) @Power(4) fix binary = price;
  @Length(16) @Power(2) fix wide = price;
  @Length(8) @Power(2) fix small = 1.25;
  print(convert(2));
  print(fine);
  print(binary);
  print(wide);
  print(small);
  return 0;
}
)");
  CHECK(!conversion.diagnostics.hasErrors());
  std::ostringstream conversionOutput;
  coord::CppEmitter conversionEmitter(*conversion.module, {},
                                      conversionOutput);
  conversionEmitter.emit();
  const std::string conversionText = conversionOutput.str();
  CHECK(conversionText.find("coord_rt::Fix<4, 10, 64> fine") !=
        std::string::npos);
  CHECK(conversionText.find("coord_rt::Fix<4, 2, 64> binary") !=
        std::string::npos);
  CHECK(conversionText.find("coord_rt::Fix<2, 10, 16> wide") !=
        std::string::npos);
  CHECK(conversionText.find("coord_rt::Fix<2, 10, 8> small") !=
        std::string::npos);

  Harness division(R"(
@Power(4) fix divide(@Power(2) fix x) {
  return x / 3;
}

integer main() {
  @Power(2) fix one = 1.0;
  @Power(2) fix three = 3.0;
  @Power(2) fix low = one / 3;
  @Power(4) fix high = one / 3;
  @Power(4) fix pair = one / three;
  print(divide(one));
  print(low);
  print(high);
  print(pair);
  return 0;
}
)");
  CHECK(!division.diagnostics.hasErrors());
  std::ostringstream divisionOutput;
  coord::CppEmitter divisionEmitter(*division.module, {}, divisionOutput);
  divisionEmitter.emit();
  const std::string divisionText = divisionOutput.str();
  CHECK(divisionText.find("fixDivideIntTo<4, 10, 64>") !=
        std::string::npos);
  CHECK(divisionText.find("fixDivideTo<4, 10, 64>") !=
        std::string::npos);

  Harness widths(R"(
integer main() {
  @Length(8) @Power(2) fix a = 1.0;
  @Length(8) @Power(2) fix b = 1.0;
  @Length(16) @Power(2) fix sum = a + b;
  @Length(16) @Power(2) fix c = 1.0;
  @Length(16) @Power(2) fix d = 1.0;
  @Length(32) @Power(4) fix product = c * d;
  @Length(8) @Power(2) fix shifted = a << 1;
  print(sum);
  print(product);
  print(shifted);
  return 0;
}
)");
  CHECK(!widths.diagnostics.hasErrors());
  std::ostringstream widthsOutput;
  coord::CppEmitter widthsEmitter(*widths.module, {}, widthsOutput);
  widthsEmitter.emit();
  const std::string widthsText = widthsOutput.str();
  CHECK(widthsText.find("coord_rt::Fix<2, 10, 16> sum") !=
        std::string::npos);
  CHECK(widthsText.find("coord_rt::Fix<4, 10, 32> product") !=
        std::string::npos);
  CHECK(widthsText.find("coord_rt::Fix<2, 10, 8> shifted") !=
        std::string::npos);

  Harness wideProduct(R"(
integer main() {
  @Power(10) fix a = 1.5;
  @Power(10) fix b = 1.5;
  @Power(10) fix product = a * b;
  print(product);
  return 0;
}
)");
  CHECK(!wideProduct.diagnostics.hasErrors());
  std::ostringstream wideOutput;
  coord::CppEmitter wideEmitter(*wideProduct.module, {}, wideOutput);
  wideEmitter.emit();
  CHECK(wideOutput.str().find("fixMultiplyTo<10, 10, 64>") !=
        std::string::npos);

  Harness operators(R"(
integer main() {
  @Power(2) fix a = 1.5;
  @Power(2) fix b = 0.4;
  print(a + b);
  print(a - b);
  print(a * b);
  print(a / b);
  print(a % b);
  print(-a);
  print(a & b);
  print(a | b);
  print(a ^ b);
  print(~a);
  print(a << 1);
  print(a >> 1);
  print(a++);
  print(++a);
  print(a--);
  print(--a);
  return 0;
}
)");
  CHECK(!operators.diagnostics.hasErrors());

  Harness negativePower(R"(
integer main() {
  @Power(-2) integer big = 12345;
  print(big);
  return 0;
}
)");
  CHECK(!negativePower.diagnostics.hasErrors());
  std::ostringstream negativeOutput;
  coord::CppEmitter negativeEmitter(*negativePower.module, {},
                                    negativeOutput);
  negativeEmitter.emit();
  CHECK(negativeOutput.str().find("coord_rt::Fix<-2, 10, 64>") !=
        std::string::npos);

  Harness radixThree(R"(
integer main() {
  @Power(2) @Radix(3) fix third = 0.5;
  print(third);
  @Radix(3) integer mask = 7;
  print(mask);
  return 0;
}
)");
  CHECK(!radixThree.diagnostics.hasErrors());
  std::ostringstream radixOutput;
  coord::CppEmitter radixEmitter(*radixThree.module, {}, radixOutput);
  radixEmitter.emit();
  CHECK(radixOutput.str().find("coord_rt::Fix<2, 3, 64> third") !=
        std::string::npos);

  Harness radixZero(R"(
integer main() {
  @Radix(0) integer value = 1;
  return 0;
}
)");
  CHECK(radixZero.countCode("E2100") >= 1);

  Harness radixOne(R"(
integer main() {
  @Radix(1) integer value = 1;
  return 0;
}
)");
  CHECK(radixOne.countCode("E2100") >= 1);

  Harness powerOnString(R"(
integer main() {
  @Power(2) string text = "x";
  return 0;
}
)");
  CHECK(powerOnString.countCode("E2101") >= 1);

  Harness fixLength(R"(
integer main() {
  @Length(128) @Power(2) fix wide = 1.5;
  print(wide);
  return 0;
}
)");
  CHECK(!fixLength.diagnostics.hasErrors());
  std::ostringstream fixLengthOutput;
  coord::CppEmitter fixLengthEmitter(*fixLength.module, {}, fixLengthOutput);
  fixLengthEmitter.emit();
  CHECK(fixLengthOutput.str().find("coord_rt::Fix<2, 10, 128> wide") !=
        std::string::npos);

  Harness scaleOverflow(R"(
integer main() {
  @Power(20) fix tooFine = 1.5;
  return 0;
}
)");
  CHECK(scaleOverflow.countCode("E2102") >= 1);

  Harness sameTypeCopy(R"(
integer main() {
  @Power(2) fix source = 1.5;
  @Power(2) fix copy = source;
  print(copy);
  return 0;
}
)");
  CHECK(!sameTypeCopy.diagnostics.hasErrors());

  Harness missingPower(R"(
integer main() {
  fix amount = 19.99;
  return 0;
}
)");
  CHECK(missingPower.countCode("E2103") >= 1);

  Harness integerTruncation(R"(
integer main() {
  integer value = 1.5;
  return 0;
}
)");
  CHECK(integerTruncation.countCode("E2105") >= 1);

  Harness missingTarget(R"(
integer main() {
  @Power(2) fix source = 1.5;
  fix target = source;
  return 0;
}
)");
  CHECK(missingTarget.countCode("E2109") >= 1);

  Harness mixedRadix(R"(
integer main() {
  @Power(2) @Radix(2) fix binary = 1.5;
  @Power(2) @Radix(10) fix decimal = 1.5;
  print(binary + decimal);
  return 0;
}
)");
  CHECK(mixedRadix.countCode("E2108") >= 1);

  Harness bitShape(R"(
integer main() {
  @Length(8) @Power(2) fix small = 1.0;
  @Length(16) @Power(2) fix wide = 1.0;
  print(small & wide);
  return 0;
}
)");
  CHECK(bitShape.countCode("E2111") >= 1);

  Harness fixFloatMix(R"(
integer main() {
  @Power(2) fix fixed = 1.5;
  float ratio = 2.0;
  print(fixed + ratio);
  return 0;
}
)");
  CHECK(fixFloatMix.countCode("E2005") >= 1);

  Harness implicitFloatToFix(R"(
integer main() {
  float ratio = 1.5;
  @Power(2) fix fixed = ratio;
  return 0;
}
)");
  CHECK(implicitFloatToFix.countCode("E2005") >= 1);

  Harness explicitConversions(R"(
integer main() {
  @Power(2) fix amount = 19.99;
  integer whole = amount.toInteger();
  float ratio = amount.toFloat();
  print(whole);
  print(ratio);
  return 0;
}
)");
  CHECK(!explicitConversions.diagnostics.hasErrors());
}

void testFixedArrays() {
  Harness harness(R"(
integer main() {
  integer[4] values;
  values[0] = 1;
  values[3] = 2;
  print(values[0] + values[3]);
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, {}, output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("std::array<std::int64_t, 4> values") !=
        std::string::npos);

  Harness badIndex(R"(
integer main() {
  integer[2] values;
  values["x"] = 1;
    return 0;
}
)");
  CHECK(badIndex.diagnostics.hasErrors());
}

void testAnnotationRegistry() {
  Harness harness(R"(
@Target(class, method)
@Repeatable
@Inherit
@Documented
@Retention(invocation)
annotation Marker { string value() const; }
)");
  CHECK(!harness.diagnostics.hasErrors());

  const coord::Decl* marker = nullptr;
  for (const auto& decl : harness.module->decls) {
    if (decl->kind == coord::DeclKind::Annotation &&
        decl->name == "Marker") {
      marker = decl.get();
      break;
    }
  }
  CHECK(marker != nullptr);
  if (marker == nullptr) {
    return;
  }

  coord::annotation::Registry registry;
  coord::DiagnosticEngine diagnostics;
  const coord::annotation::Descriptor* descriptor =
      registry.build(*marker, diagnostics);
  CHECK(descriptor != nullptr);
  if (descriptor == nullptr) {
    return;
  }
  CHECK(descriptor->kind == coord::annotation::Kind::Processor);
  CHECK(descriptor->repeatable);
  CHECK(descriptor->inherit);
  CHECK(descriptor->documented);
  CHECK(descriptor->retention == coord::annotation::Retention::Invocation);
  CHECK(descriptor->targets.count("class") == 1);
  CHECK(descriptor->targets.count("method") == 1);
  CHECK(!diagnostics.hasErrors());

  const coord::Decl* resultDecl = nullptr;
  for (const auto& decl : harness.module->decls) {
    if (decl->kind == coord::DeclKind::Annotation && decl->name == "Result") {
      resultDecl = decl.get();
      break;
    }
  }
  CHECK(resultDecl != nullptr);
  if (resultDecl != nullptr) {
    coord::annotation::Registry intrinsicRegistry;
    coord::DiagnosticEngine intrinsicDiagnostics;
    const coord::annotation::Descriptor* result =
        intrinsicRegistry.build(*resultDecl, intrinsicDiagnostics);
    CHECK(result != nullptr);
    if (result != nullptr) {
      CHECK(result->kind == coord::annotation::Kind::Intrinsic);
    }
  }
}

void testProcessorMethods() {
  Harness valid(R"(
@Target(class)
annotation Generate implements AnnotationProcessor {
  override void process(Description target) {
    addFunction("generated");
    addConstant("ANSWER", 42);
  }
}

@Generate
class Data {}
)");
  CHECK(!valid.diagnostics.hasErrors());

  const coord::Decl* generate = nullptr;
  for (const auto& decl : valid.module->decls) {
    if (decl->kind == coord::DeclKind::Annotation &&
        decl->name == "Generate") {
      generate = decl.get();
      break;
    }
  }
  CHECK(generate != nullptr);
  if (generate != nullptr) {
    CHECK(generate->processorMethod == "process");
    CHECK(generate->interfaces.size() == 1);
  }

  coord::processor::Executor executor(*valid.module, valid.diagnostics);
  executor.run();
  CHECK(!valid.diagnostics.hasErrors());
  CHECK(executor.invocations() == 1);
  CHECK(executor.takeGenerated().size() == 2);

  Harness classImpl(R"(
class Handler implements AnnotationProcessor {}
)");
  CHECK(classImpl.countCode("E2060") >= 1);

  Harness interfaceImpl(R"(
interface Handler implements AnnotationProcessor {}
)");
  CHECK(interfaceImpl.countCode("E2060") >= 1);

  Harness combined(R"(
@Target(class)
annotation Trace implements ProxyHandler, AnnotationProcessor {
  override void process(Description target) {
    addFunction("audited");
  }

  @Get
  void get(string field) {}
}

@Trace
@Proxy(Trace)
class Box {
public:
  integer value;
}
)");
  CHECK(!combined.diagnostics.hasErrors());

  const coord::Decl* trace = nullptr;
  for (const auto& decl : combined.module->decls) {
    if (decl->kind == coord::DeclKind::Annotation && decl->name == "Trace") {
      trace = decl.get();
      break;
    }
  }
  CHECK(trace != nullptr);
  if (trace != nullptr) {
    CHECK(trace->interfaces.size() == 2);
    CHECK(trace->processorMethod == "process");
  }
  const coord::Decl* box = combined.findClass("Box");
  CHECK(box != nullptr);
  if (box != nullptr) {
    CHECK(box->isProxy);
    CHECK(box->proxyHandler == "Trace");
  }
  coord::processor::Executor combinedExecutor(*combined.module,
                                               combined.diagnostics);
  combinedExecutor.run();
  CHECK(!combined.diagnostics.hasErrors());
  CHECK(combinedExecutor.invocations() == 1);

  Harness missing(R"(
annotation Missing implements AnnotationProcessor {}
)");
  CHECK(missing.countCode("E2060") >= 1);

  Harness duplicated(R"(
annotation Duplicate implements AnnotationProcessor {
  override void process(Description target) {}
  override void process(Description target) {}
}
)");
  CHECK(duplicated.countCode("E2060") >= 1);

  Harness badReturn(R"(
annotation BadReturn implements AnnotationProcessor {
  override integer process(Description target) { return 1; }
}
)");
  CHECK(badReturn.countCode("E2058") >= 1);

  Harness badParam(R"(
annotation BadParam implements AnnotationProcessor {
  override void process(string target) {}
}
)");
  CHECK(badParam.countCode("E2059") >= 1);

  Harness noOverride(R"(
annotation Implicit implements AnnotationProcessor {
  void process(Description target) {}
}
)");
  CHECK(noOverride.countCode("E2060") >= 1);

  Harness legacy(R"(
annotation Legacy {
  @Processor
  void generate(Description target) {}
}
)");
  CHECK(legacy.countCode("E2061") >= 1);
}

void testInterfaceWriter() {
  Harness harness(R"(
export class File {
public:
  File(string path) {}
  integer size() const { return 0; }
}

export integer add(integer left, integer right) {
  return left + right;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const std::string text = coord::InterfaceWriter::write(*harness.module);
  CHECK(text.find("export class File") != std::string::npos);
  CHECK(text.find("File(string path);") != std::string::npos);
  CHECK(text.find("integer size() const;") != std::string::npos);
  CHECK(text.find("export integer add(integer left, integer right);") !=
        std::string::npos);
}

void testProxyValidation() {
  Harness harness(R"(
@Target(method)
annotation Handler implements ProxyHandler {
  @Get
  void get() {}
}

@Proxy(Handler)
class Service {
public:
  void run() {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::Decl* service = harness.findClass("Service");
  CHECK(service != nullptr);
  if (service != nullptr) {
    CHECK(service->isProxy);
    CHECK(service->proxyHandler == "Handler");
  }

  Harness unknown(R"(
@Proxy(Missing)
class Service {}
)");
  CHECK(unknown.countCode("E2063") >= 1);

  Harness noTraps(R"(
@Target(method)
annotation Handler implements ProxyHandler {
  void get() {}
}

@Proxy(Handler)
class Service {}
)");
  CHECK(noTraps.countCode("E2063") >= 1);
}

void testBorrowChecks() {
  Harness referenceField(R"(
class Holder {
  integer& reference;
}
)");
  CHECK(referenceField.countCode("E2039") >= 1);

  Harness returnAddress(R"(
integer* bad() {
  integer value = 1;
  return &value;
}
)");
  CHECK(returnAddress.countCode("E2039") >= 1);

  Harness referenceParameter(R"(
integer read(integer& value) {
  return value;
}
)");
  CHECK(!referenceParameter.diagnostics.hasErrors());
}

void testDescriptionApi() {
  Harness harness(R"(
@Target(class)
annotation Reflectable implements AnnotationProcessor {
  override void process(Description target) {
    string name = target.name();
    string kind = target.kind();
    boolean has = target.hasAnnotation("Result");
    integer fields = target.fieldCount();
    integer methods = target.methodCount();
  }
}
)");
  CHECK(!harness.diagnostics.hasErrors());

  Harness badCall(R"(
annotation Bad implements AnnotationProcessor {
  override void process(Description target) {
    integer value = target.hasAnnotation(1);
  }
}
)");
  CHECK(badCall.countCode("E2005") >= 1);
}

void testOwnershipAnnotations() {
  Harness harness(R"(
class Payload {
public:
  integer value;
}

integer main() {
  @Unique Payload owned{};
  @Share Payload shared{};
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());

  Harness both(R"(
class Payload {}
integer main() {
  @Unique @Share Payload value{};
    return 0;
}
)");
  CHECK(both.countCode("E2065") >= 1);

  Harness primitive(R"(
integer main() {
  @Unique integer value = 1;
    return 0;
}
)");
  CHECK(primitive.countCode("E2065") >= 1);
}

void testProcessorExecution() {
  Harness harness(R"(
@Target(class)
annotation Inspect implements AnnotationProcessor {
  override void process(Description target) {
    string name = target.name();
    string kind = target.kind();
    integer fields = target.fieldCount();
    integer methods = target.methodCount();
    boolean has = target.hasAnnotation("Inspect");
    line("generated.coo", 42);
  }
}

@Inspect
class Data {
public:
  integer field;
  void run() {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  coord::processor::Executor executor(*harness.module, harness.diagnostics);
  executor.run();
  CHECK(!harness.diagnostics.hasErrors());
  CHECK(executor.invocations() == 1);
  CHECK(executor.lines().size() == 1);
  if (!executor.lines().empty()) {
    CHECK(executor.lines()[0].file == "generated.coo");
    CHECK(executor.lines()[0].line == 42);
  }

  Harness unsupported(R"(
@Target(class)
annotation Unsupported implements AnnotationProcessor {
  override void process(Description target) {
    print("not allowed");
  }
}

@Unsupported
class Target {}
)");
  CHECK(!unsupported.diagnostics.hasErrors());
  coord::processor::Executor badExecutor(*unsupported.module,
                                          unsupported.diagnostics);
  badExecutor.run();
  CHECK(unsupported.diagnostics.hasErrors());
  CHECK(unsupported.countCode("E2067") >= 1);

  Harness looping(R"(
@Target(function)
annotation Replicate implements AnnotationProcessor {
  override void process(Description target) {
    addAnnotatedFunction("copy", "Replicate");
  }
}

@Replicate
void seed() {}
)");
  CHECK(!looping.diagnostics.hasErrors());
  coord::processor::Executor loopExecutor(*looping.module,
                                           looping.diagnostics);
  loopExecutor.run(3);
  CHECK(looping.countCode("E2068") >= 1);
}

void testOwnershipMoves() {
  Harness copy(R"(
class Payload {
public:
  integer value;
}

integer main() {
  @Unique Payload first = Payload(1);
  Payload second = first;
    return 0;
}
)");
  CHECK(copy.countCode("E2045") >= 1);

  Harness useAfterMove(R"(
class Payload {
public:
  integer value;
}

integer main() {
  @Unique Payload first = Payload(1);
  @Unique Payload second = first;
  print(first.value);
    return 0;
}
)");
  CHECK(useAfterMove.countCode("E2045") >= 1);
}

void testReflectionRange() {
  Harness harness(R"(
class Data {
public:
  integer value;
}

integer main() {
  Reflector<Data> info;
  print(info.fieldName(5));
    return 0;
}
)");
  CHECK(harness.countCode("E2069") >= 1);

  Harness negative(R"(
class Data {
public:
  integer value;
}

integer main() {
  Reflector<Data> info;
  print(info.fieldType(99));
    return 0;
}
)");
  CHECK(negative.countCode("E2069") >= 1);
}

void testUnsupportedOwnershipPositions() {
  Harness field(R"(
class Payload {}
class Holder {
public:
  @Unique Payload payload;
}
)");
  CHECK(!field.diagnostics.hasErrors());

  Harness badType(R"(
class Holder {
public:
  @Unique integer value;
}
)");
  CHECK(badType.countCode("E2065") >= 1);

  Harness both(R"(
class Payload {}
class Holder {
public:
  @Unique @Share Payload payload;
}
)");
  CHECK(both.countCode("E2065") >= 1);

  Harness function(R"(
class Payload {}
@Unique
Payload make() {
  return Payload();
}
)");
  CHECK(!function.diagnostics.hasErrors());

  Harness badReturn(R"(
@Unique
integer make() {
  return 1;
}
)");
  CHECK(badReturn.countCode("E2065") >= 1);

  Harness badParam(R"(
class Sink {
public:
  void take(@Unique integer value) {}
}
)");
  CHECK(badParam.countCode("E2065") >= 1);
}

void testDescriptorConsistency() {
  Harness harness(R"(
@Target(class)
annotation Marker {}

@Marker
class Data {
public:
  integer first;
  string second;

  void run(integer count, string label) {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());

  const std::vector<coord::descriptor::Declaration> descriptors =
      coord::descriptor::Builder::build(*harness.module);
  const coord::descriptor::Declaration* data = nullptr;
  for (const auto& descriptor : descriptors) {
    if (descriptor.name == "Data") {
      data = &descriptor;
    }
  }
  CHECK(data != nullptr);
  if (data == nullptr) {
    return;
  }
  CHECK(data->kind == "class");
  CHECK(data->fields.size() == 2);
  CHECK(data->methods.size() == 1);
  CHECK(data->annotations.size() == 1);
  CHECK(data->fields[0].name == "first");
  CHECK(data->fields[0].type == "integer");
  CHECK(data->methods[0].parameterTypes.size() == 2);
  CHECK(data->methods[0].parameterTypes[0] == "integer");

  const coord::outline::Outline tree = coord::outline::Outline::build(*harness.module);
  const coord::outline::OutlineNode* node = nullptr;
  for (const auto& child : tree.root.children) {
    if (child.name == "Data") {
      node = &child;
    }
  }
  CHECK(node != nullptr);
  if (node != nullptr) {
    int fields = 0;
    int methods = 0;
    for (const auto& child : node->children) {
      if (child.kind == coord::outline::OutlineKind::Field) {
        ++fields;
      } else if (child.kind == coord::outline::OutlineKind::Method) {
        ++methods;
      }
    }
    CHECK(fields == static_cast<int>(data->fields.size()));
    CHECK(methods == static_cast<int>(data->methods.size()));
  }
}

void testDescriptorSchema() {
  Harness harness(R"(
interface Named {}

class Base {}

class Child extends Base implements Named {
public:
  integer value;
  void run() {}
}

enumerate Color {
  Red,
  Green
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const std::vector<coord::descriptor::Declaration> descriptors =
      coord::descriptor::Builder::build(*harness.module);
  const coord::descriptor::Declaration* child = nullptr;
  const coord::descriptor::Declaration* color = nullptr;
  for (const auto& descriptor : descriptors) {
    if (descriptor.name == "Child") {
      child = &descriptor;
    } else if (descriptor.name == "Color") {
      color = &descriptor;
    }
  }
  CHECK(child != nullptr);
  CHECK(color != nullptr);
  if (child != nullptr) {
    CHECK(child->base == "Base");
    CHECK(child->interfaces.size() == 1);
    CHECK(child->interfaces[0] == "Named");
    CHECK(child->span.begin < child->span.end);
    CHECK(child->fields.size() == 1);
    CHECK(child->fields[0].span.begin < child->fields[0].span.end);
    CHECK(child->fields[0].access == "public");
    CHECK(child->methods.size() == 1);
  }
  if (color != nullptr) {
    CHECK(color->variants.size() == 2);
    CHECK(color->variants[0].value == 0);
    CHECK(color->variants[1].value == 1);
  }
}

void testDescriptorMembers() {
  Harness harness(R"(
class Gadget {
public:
  Gadget(integer initial) {}
  ~Gadget() {}
  void run() {}
}

class Plain {
public:
  integer value;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const std::vector<coord::descriptor::Declaration> descriptors =
      coord::descriptor::Builder::build(*harness.module);
  const coord::descriptor::Declaration* gadget = nullptr;
  const coord::descriptor::Declaration* plain = nullptr;
  for (const auto& descriptor : descriptors) {
    if (descriptor.name == "Gadget") {
      gadget = &descriptor;
    } else if (descriptor.name == "Plain") {
      plain = &descriptor;
    }
  }
  CHECK(gadget != nullptr);
  CHECK(plain != nullptr);
  if (gadget != nullptr) {
    CHECK(gadget->constructors.size() == 1);
    CHECK(gadget->destructors.size() == 1);
    CHECK(gadget->methods.size() == 1);
    CHECK(gadget->constructors[0].parameterTypes.size() == 1);
  }
  if (plain != nullptr) {
    CHECK(plain->constructors.empty());
    CHECK(plain->destructors.empty());
    CHECK(plain->methods.empty());
  }
}

void testHintConsistency() {
  Harness harness(R"(
class Data {
public:
  integer value;
  string label;

  void run(integer count) {}
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::outline::Outline outline =
      coord::outline::Outline::build(*harness.module);
  coord::hint::HintEngine hints(outline);

  const std::vector<const coord::outline::OutlineNode*> leaves =
      outline.leaves();
  const std::vector<coord::hint::Hint> all = hints.all();
  CHECK(all.size() == leaves.size());
  for (const auto& leaf : leaves) {
    bool found = false;
    for (const auto& hint : all) {
      if (hint.label == leaf->name) {
        found = true;
      }
    }
    CHECK(found);
  }

  CHECK(!outline.root.children.empty());
  if (!outline.root.children.empty()) {
    const std::vector<coord::hint::Hint> atClass =
        hints.at(outline.root.children[0].span.begin);
    CHECK(!atClass.empty());
    bool hasField = false;
    bool hasMethod = false;
    for (const auto& hint : atClass) {
      if (hint.label == "value") {
        hasField = true;
      }
      if (hint.label == "run") {
        hasMethod = true;
      }
    }
    CHECK(hasField);
    CHECK(hasMethod);
  }
}

void testDescriptorPipelineConsistency() {
  Harness harness(R"(
@Target(class)
annotation Marker {}

@Marker
class Data {
public:
  integer first;
  string second;

  Data(integer initial) {}
  ~Data() {}
  void run(integer count) {}
}

enumerate Color {
  Red,
  Green
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const std::vector<coord::descriptor::Declaration> descriptors =
      coord::descriptor::Builder::build(*harness.module);
  const coord::descriptor::Declaration* data = nullptr;
  const coord::descriptor::Declaration* color = nullptr;
  for (const auto& descriptor : descriptors) {
    if (descriptor.name == "Data") {
      data = &descriptor;
    } else if (descriptor.name == "Color") {
      color = &descriptor;
    }
  }
  CHECK(data != nullptr);
  CHECK(color != nullptr);
  if (data != nullptr) {
    CHECK(data->fields.size() == 2);
    CHECK(data->constructors.size() == 1);
    CHECK(data->destructors.size() == 1);
    CHECK(data->methods.size() == 1);
    CHECK(data->annotations.size() == 1);
    CHECK(data->span.valid());
  }
  if (color != nullptr) {
    CHECK(color->variants.size() == 2);
  }

  const coord::outline::Outline outline =
      coord::outline::Outline::build(*harness.module);
  const coord::outline::OutlineNode* dataNode = nullptr;
  const coord::outline::OutlineNode* colorNode = nullptr;
  for (const auto& child : outline.root.children) {
    if (child.name == "Data") {
      dataNode = &child;
    } else if (child.name == "Color") {
      colorNode = &child;
    }
  }
  CHECK(dataNode != nullptr);
  CHECK(colorNode != nullptr);
  if (dataNode != nullptr && data != nullptr) {
    int fields = 0;
    int constructors = 0;
    int destructors = 0;
    int methods = 0;
    for (const auto& child : dataNode->children) {
      switch (child.kind) {
        case coord::outline::OutlineKind::Field: ++fields; break;
        case coord::outline::OutlineKind::Constructor: ++constructors; break;
        case coord::outline::OutlineKind::Destructor: ++destructors; break;
        case coord::outline::OutlineKind::Method: ++methods; break;
        default: break;
      }
    }
    CHECK(fields == static_cast<int>(data->fields.size()));
    CHECK(constructors == static_cast<int>(data->constructors.size()));
    CHECK(destructors == static_cast<int>(data->destructors.size()));
    CHECK(methods == static_cast<int>(data->methods.size()));
  }
  if (colorNode != nullptr && color != nullptr) {
    CHECK(colorNode->children.size() == color->variants.size());
  }

  coord::hint::HintEngine hints(outline);
  const auto all = hints.all();
  for (const auto* leaf : outline.leaves()) {
    bool found = false;
    for (const auto& hint : all) {
      if (hint.label == leaf->name) {
        found = true;
      }
    }
    CHECK(found);
  }
}

void testProxyExplicitRetention() {
  Harness missing(R"(
class Trace implements ProxyHandler {
public:
  @Call
  void onCall(string method) {}
}

@Proxy(Trace)
class Service {
public:
  void run() {}
}
)");
  CHECK(missing.countCode("E2063") >= 1);

  Harness staticRuntime(R"(
@Retention(runtime)
class Trace implements ProxyHandler {
public:
  @Call
  static void onCall(string method) {}
}

@Proxy(Trace)
class Service {
public:
  void run() {}
}
)");
  CHECK(staticRuntime.countCode("E2063") >= 1);

  Harness methodMissing(R"(
class Trace implements ProxyHandler {
public:
  @Call
  static void onCall(string method) {}
}

class Service {
public:
  @Proxy(Trace)
  void run() {}
}
)");
  CHECK(methodMissing.countCode("E2063") >= 1);
}

void testOwnershipBorrow() {
  Harness valid(R"(
class Payload {}
class Sink {
public:
  void take(@Unique Payload payload) {}
}
integer main() {
  Sink sink;
  @Unique Payload payload = Payload();
  sink.take(payload);
    return 0;
}
)");
  CHECK(!valid.diagnostics.hasErrors());

  Harness useAfterMove(R"(
class Payload {}
class Sink {
public:
  void take(@Unique Payload payload) {}
}
integer main() {
  Sink sink;
  @Unique Payload payload = Payload();
  sink.take(payload);
  print(payload.value);
    return 0;
}
)");
  CHECK(useAfterMove.countCode("E2045") >= 1);
}

void testInterfaceReader() {
  Harness harness(R"(
class Base {}
interface Named {}
export class File extends Base implements Named {
public:
  File(string path) {}
  integer size() const { return 0; }
  string name;
}
enumerate Color { Red = 0, Green = 1 }
export integer add(integer left, integer right) { return left + right; }
)");
  CHECK(!harness.diagnostics.hasErrors());
  const std::string text = coord::InterfaceWriter::write(*harness.module);
  coord::InterfaceModule module;
  std::string error;
  CHECK(coord::InterfaceReader::read(text, module, error));
  CHECK(error.empty());
  CHECK(module.declarations.size() >= 4);
  bool file = false;
  bool color = false;
  bool add = false;
  for (const auto& decl : module.declarations) {
    if (decl.name == "File") {
      file = true;
      CHECK(decl.kind == coord::InterfaceDecl::Kind::Class);
      CHECK(decl.base == "Base");
      CHECK(decl.interfaces.size() == 1);
      CHECK(decl.interfaces[0] == "Named");
      CHECK(decl.members.size() >= 3);
    } else if (decl.name == "Color") {
      color = true;
      CHECK(decl.variants.size() == 2);
      CHECK(decl.variants[1].first == "Green");
      CHECK(decl.variants[1].second == 1);
    } else if (decl.name == "add") {
      add = true;
      CHECK(decl.kind == coord::InterfaceDecl::Kind::Function);
      CHECK(decl.members.size() == 1);
      CHECK(decl.members[0].params.size() == 2);
    }
  }
  CHECK(file);
  CHECK(color);
  CHECK(add);

  coord::InterfaceModule broken;
  CHECK(!coord::InterfaceReader::read("module a\nclass X {\n", broken, error));
  CHECK(!error.empty());
}

void testMainSignature() {
  Harness voidMain(R"(
void main() {}
)");
  CHECK(voidMain.countCode("E2052") >= 1);

  Harness fixMain(R"(
fix main() {
    return 0;
}
)");
  CHECK(fixMain.countCode("E2052") >= 1);

  Harness valid(R"(
integer main() {
    return 0;
}
)");
  CHECK(!valid.diagnostics.hasErrors());

  Harness args(R"(
integer main(Array<string> args) {
    return 0;
}
)");
  CHECK(!args.diagnostics.hasErrors());

  Harness argc(R"(
integer main(integer argc, Array<string> args) {
    return 0;
}
)");
  CHECK(!argc.diagnostics.hasErrors());

  Harness badParams(R"(
integer main(integer value) {
    return 0;
}
)");
  CHECK(badParams.countCode("E2052") >= 1);
}

void testJsImportExport() {
  Harness harness(R"(
integer twice(integer value) { return value * 2; }
integer triple(integer value) { return value * 3; }
export { twice, triple };
export {};
)");
  CHECK(!harness.diagnostics.hasErrors());
  bool twiceExported = false;
  for (const auto& decl : harness.module->decls) {
    if (decl->name == "twice") {
      twiceExported = decl->exported;
    }
  }
  CHECK(twiceExported);
}

void testReferenceLocals() {
  Harness harness(R"(
integer main() {
    integer& mutable = 5;
    mutable++;
    const integer& value = 7;
    print(mutable);
    print(value);
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  coord::Interpreter interpreter(*harness.module, harness.diagnostics);
  interpreter.evaluate();
  std::ostringstream output;
  coord::CppEmitter emitter(*harness.module, interpreter.constValues(), output);
  emitter.emit();
  const std::string generated = output.str();
  CHECK(generated.find("std::int64_t& mutable_") != std::string::npos);
  CHECK(generated.find("const std::int64_t& value") != std::string::npos);
  CHECK(generated.find("checkedPostInc(mutable_") != std::string::npos);
}

void testBorrowSafety() {
  Harness cycle(R"(
class Node {
public:
    @Optional @Share Node next = null;
}

integer main() {
    Node node;
    node.next = node;
    return 0;
}
)");
  CHECK(cycle.countCode("W0005") >= 1);

  Harness recursiveType(R"(
class Node {
public:
    @Optional @Share Node next = null;
}

integer main() {
    Node node;
    return 0;
}
)");
  CHECK(recursiveType.countCode("W0005") >= 1);

  Harness container(R"(
integer main() {
    Array<integer&> values;
    return 0;
}
)");
  CHECK(container.countCode("E2039") >= 1);

  Harness selfMove(R"(
class Payload {
public:
    Payload() {}
}

integer main() {
    @Unique Payload payload = Payload();
    payload = payload;
    return 0;
}
)");
  CHECK(selfMove.countCode("E2045") >= 1);
}

void testNativeFFI() {
  // The new @Native(file:, abi:) form is accepted and defaults `symbol` to the
  // declaration name. Header/compiler checks happen in the driver, not Sema.
  Harness accepted(R"(
@Native(file: "native/include/foo.h", abi: "c")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(!accepted.diagnostics.hasErrors());
  const coord::Decl* nativePid = nullptr;
  for (const auto& decl : accepted.module->decls) {
    if (decl->kind == coord::DeclKind::Function && decl->name == "nativePid") {
      nativePid = decl.get();
    }
  }
  CHECK(nativePid != nullptr);
  if (nativePid != nullptr) {
    CHECK(nativePid->isNative);
    CHECK(nativePid->nativeFile == "native/include/foo.h");
    CHECK(nativePid->nativeAbi == "c");
    CHECK(nativePid->nativeSymbol == "nativePid");
  }

  Harness explicitSymbol(R"(
@Native(file: "native/include/foo.h", abi: "c", symbol: "getpid")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(!explicitSymbol.diagnostics.hasErrors());
  for (const auto& decl : explicitSymbol.module->decls) {
    if (decl->kind == coord::DeclKind::Function && decl->name == "nativePid") {
      CHECK(decl->nativeSymbol == "getpid");
    }
  }

  // The removed @Native("symbol") form must ask for a migration.
  Harness legacy(R"(
@Native("getpid")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(legacy.countCode("E2114") >= 1);

  Harness missingFile(R"(
@Native(abi: "c")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(missingFile.countCode("E2113") >= 1);

  Harness badAbi(R"(
@Native(file: "native/include/foo.h", abi: "rust")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(badAbi.countCode("E2113") >= 1);

  Harness unknownArgument(R"(
@Native(file: "native/include/foo.h", library: "m")
integer nativePid();
integer main() { return 0; }
)");
  CHECK(unknownArgument.countCode("E2113") >= 1);

  Harness body(R"(
@Native(file: "native/include/foo.h", abi: "c")
integer nativePid() {
    return 0;
}
integer main() { return 0; }
)");
  CHECK(body.countCode("E2078") >= 1);

  Harness unsupported(R"(
class Payload {
public:
    Payload() {}
}
@Native(file: "native/include/foo.h", abi: "c")
Payload nativePid();
integer main() { return 0; }
)");
  CHECK(unsupported.countCode("E2078") >= 1);

  // Native classes are opaque handles with body-less members and abi "c++".
  Harness nativeClass(R"(
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    Counter(integer initial);
    ~Counter();
    integer value();
    void increment();
}
integer main() { return 0; }
)");
  CHECK(!nativeClass.diagnostics.hasErrors());
  const coord::Decl* counter = nativeClass.findClass("Counter");
  CHECK(counter != nullptr);
  if (counter != nullptr) {
    CHECK(counter->isNative);
    CHECK(counter->nativeAbi == "c++");
    for (const auto& member : counter->members) {
      if (member->kind == coord::DeclKind::Constructor ||
          member->kind == coord::DeclKind::Destructor ||
          member->kind == coord::DeclKind::Method) {
        CHECK(member->isNative);
      }
    }
  }

  Harness classCAbi(R"(
@Native(file: "native/include/counter.h", abi: "c")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}
integer main() { return 0; }
)");
  CHECK(classCAbi.countCode("E2113") >= 1);

  Harness classField(R"(
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    integer value;
    Counter();
    ~Counter();
}
integer main() { return 0; }
)");
  CHECK(classField.countCode("E2113") >= 1);

  Harness classBody(R"(
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value() {
        return 0;
    }
}
integer main() { return 0; }
)");
  CHECK(classBody.countCode("E2113") >= 1);

  Harness copyNative(R"(
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}
@Unsafe
integer main() {
    Counter first = Counter();
    Counter second = first;
    return 0;
}
)");
  CHECK(copyNative.countCode("E2119") >= 1);

  Harness assignNative(R"(
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}
@Unsafe
integer main() {
    Counter first = Counter();
    Counter second = Counter();
    second = first;
    return 0;
}
)");
  CHECK(assignNative.countCode("E2119") >= 1);
}

void testDiagnosticLimit() {
  std::string source = "class Broken {\n";
  for (int i = 0; i < 2000; ++i) {
    source += "  ) broken member\n";
  }
  source += "}\n";
  Harness harness(source);
  CHECK(harness.diagnostics.hasErrors());
  CHECK(harness.diagnostics.diagnostics().size() <= 257);
}

void testSwitchStatement() {
  Harness harness(R"(
integer main() {
    integer value = 2;
    switch (value) {
        case (1) { print("one"); }
        case (2) { print("two"); }
        default() { print("other"); }
    }
    return 0;
}
)");
  CHECK(!harness.diagnostics.hasErrors());

  Harness badCase(R"(
integer main() {
    switch (1) {
        case ("one") { print("one"); }
    }
    return 0;
}
)");
  CHECK(badCase.countCode("E2005") >= 1);
}

void testHandlerInterfaces() {
  Harness missingProxy(R"(
class Trace {
public:
  @Call
  static void onCall(string method) {}
}

@Proxy(Trace)
class Service {
public:
  void run() {}
}
)");
  CHECK(missingProxy.countCode("E2074") >= 1);

  Harness validProxy(R"(
class Trace implements ProxyHandler {
public:
  @Call
  static void onCall(string method) {}
}

@Proxy(Trace)
class Service {
public:
  void run() {}
}
)");
  CHECK(!validProxy.diagnostics.hasErrors());

  Harness annotationProxy(R"(
annotation Trace implements ProxyHandler {
  @Call
  static void onCall(string method) {}
}

@Proxy(Trace)
class Service {
public:
  void run() {}
}
)");
  CHECK(!annotationProxy.diagnostics.hasErrors());

  Harness missingInvocation(R"(
class Trace {
public:
  void before() {}
  void after() {}
}

@Invocation(Trace)
integer work() {
  return 1;
}
)");
  CHECK(missingInvocation.countCode("E2075") >= 1);
}

void testMatch() {
  Harness missing(R"(
enumerate Color { Red, Green, Blue }
integer main() {
    Color color = Color.Red;
    match (color) {
        case Color.Red: { print("red"); }
        case Color.Green: { print("green"); }
    }
    return 0;
}
)");
  CHECK(missing.countCode("E2077") >= 1);

  Harness complete(R"(
enumerate Color { Red, Green }
integer main() {
    Color color = Color.Red;
    match (color) {
        case Color.Red: { print("red"); }
        case Color.Green: { print("green"); }
    }
    return 0;
}
)");
  CHECK(!complete.diagnostics.hasErrors());
}

void testDocAttachment() {
  Harness harness(R"(
/// class docs
class Point {
public:
    /// field docs
    integer x;
}
)");
  CHECK(!harness.diagnostics.hasErrors());
  const coord::Decl* point = harness.findClass("Point");
  CHECK(point != nullptr);
  if (point != nullptr) {
    CHECK(point->doc.find("class docs") != std::string::npos);
    bool fieldDoc = false;
    for (const auto& member : point->members) {
      if (member->name == "x" &&
          member->doc.find("field docs") != std::string::npos) {
        fieldDoc = true;
      }
    }
    CHECK(fieldDoc);
  }
}

void testVisibilityOwnership() {
  coord::VisibilitySet<int> parent;
  parent.insert("a", 1, "declA", "module");
  parent.insert("b", 2, "declB", "module");

  coord::VisibilitySet<int> child;
  child.insert("a", 3, "declAChild", "block");
  child.insert("c", 4, "declC", "block");

  CHECK(parent.ownerOf("a") == "module");
  CHECK(child.ownerOf("a") == "block");

  const auto intersection = parent.intersect(child);
  CHECK(intersection.contains("a") == false);
  CHECK(intersection.hasConflicts());

  const auto unionSet = parent.unite(child);
  CHECK(unionSet.contains("a"));
  CHECK(unionSet.contains("b"));
  CHECK(unionSet.contains("c"));
  CHECK(unionSet.hasConflicts());

  const auto complement = parent.complement(child);
  CHECK(!complement.contains("a"));
  CHECK(complement.contains("b"));
}

void testInvalidSyntax() {
  Harness harness("class {");
  CHECK(harness.diagnostics.hasErrors());
}

struct TemporaryDirectory {
  std::filesystem::path path;

  TemporaryDirectory() {
    static int counter = 0;
    ++counter;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code error;
    path = std::filesystem::temp_directory_path(error) /
           ("coord-test-" + std::to_string(stamp) + "-" +
            std::to_string(counter));
    if (error) {
      path = std::filesystem::path(".") /
             ("coord-test-" + std::to_string(counter));
    }
    std::filesystem::create_directories(path, error);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

void testPlatformIO() {
  using namespace coord::platform;
  CHECK(!operatingSystemName().empty());
  CHECK(!architectureName().empty());
  CHECK(targetTriple().find('-') != std::string::npos);
  const std::string unicodePath = "some/\xC3\xBCnicode path/file.txt";
  CHECK(pathToUtf8(pathFromUtf8(unicodePath)) == unicodePath);
  CHECK(normalizeSlashes("a\\b/c") == "a/b/c");
#if defined(_WIN32)
  CHECK(executableSuffix() == ".exe");
  CHECK(objectFileSuffix() == ".obj");
#else
  CHECK(executableSuffix().empty());
  CHECK(objectFileSuffix() == ".o");
#endif
  CHECK(!configDirectory("coord").empty());
  CHECK(!cacheDirectory("coord").empty());

  TemporaryDirectory temporary;
  const std::filesystem::path file = temporary.path / "nested" / "data.txt";
  std::string error;
  CHECK(writeFileAtomic(file, "hello", error));
  const std::optional<std::string> text = readFile(file, error);
  CHECK(text.has_value() && *text == "hello");
  CHECK(ensureDirectory(temporary.path / "a" / "b", error));
  CHECK(copyTree(temporary.path / "nested", temporary.path / "copy", error));
  const std::optional<std::string> copied =
      readFile(temporary.path / "copy" / "data.txt", error);
  CHECK(copied.has_value() && *copied == "hello");
#if defined(_WIN32)
  const ProcessResult echo = run({"cmd", "/d", "/c", "echo platform-ok"});
#else
  const ProcessResult echo = run({"sh", "-c", "echo platform-ok"});
#endif
  CHECK(echo.started);
  CHECK(!echo.timedOut);
  CHECK(echo.exitCode == 0);
  CHECK(echo.out.find("platform-ok") != std::string::npos);

  const ProcessResult missing = run({"coorc-definitely-missing-binary"});
  CHECK(!missing.started);
  CHECK(!missing.failure.empty());

#if defined(_WIN32)
  const ProcessResult failing = run({"cmd", "/d", "/c", "exit 7"});
  CHECK(failing.started && failing.exitCode == 7);
#else
  const ProcessResult failing = run({"sh", "-c", "exit 7"});
  CHECK(failing.started && failing.exitCode == 7);
#endif

  ProcessOptions timeoutOptions;
  timeoutOptions.timeout = std::chrono::milliseconds(250);
#if defined(_WIN32)
  const ProcessResult slow = run(
      {"powershell", "-NoProfile", "-Command", "Start-Sleep -Seconds 5"},
      timeoutOptions);
#else
  const ProcessResult slow = run({"sh", "-c", "sleep 5"}, timeoutOptions);
#endif
  CHECK(slow.timedOut);
  CHECK(slow.exitCode == -1);

  ProcessOptions inputOptions;
  inputOptions.input = "pipe-data\n";
#if defined(_WIN32)
  const ProcessResult reader = run({"cmd", "/d", "/c", "findstr ."},
                                   inputOptions);
#else
  const ProcessResult reader = run({"sh", "-c", "cat"}, inputOptions);
#endif
  CHECK(reader.started && reader.exitCode == 0);
  CHECK(reader.out.find("pipe-data") != std::string::npos);
}

void testChecksum() {
  CHECK(coord::checksum::sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(coord::checksum::sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  coord::checksum::Sha256 hasher;
  hasher.update("a");
  hasher.update(std::string_view("bc"));
  const std::array<std::uint8_t, 32> digest = hasher.digest();
  CHECK(coord::checksum::toHex(digest.data(), digest.size()) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(coord::checksum::crc32("123456789") == 0xCBF43926U);
}

void testSemver() {
  using namespace coord;
  const std::optional<semver::Version> version =
      semver::Version::parse("1.2.3-alpha.1+build.5");
  CHECK(version.has_value());
  CHECK(version->toString() == "1.2.3-alpha.1+build.5");
  CHECK(version->major == 1 && version->minor == 2 && version->patch == 3);
  CHECK(!semver::Version::parse("1.2.3.4").has_value());
  CHECK(!semver::Version::parse("x.y.z").has_value());

  CHECK(semver::compare(*semver::Version::parse("1.0.0-alpha"),
                        *semver::Version::parse("1.0.0")) < 0);
  CHECK(semver::compare(*semver::Version::parse("1.0.0-alpha.1"),
                        *semver::Version::parse("1.0.0-alpha.beta")) < 0);
  CHECK(semver::compare(*semver::Version::parse("1.0.0-alpha.2"),
                        *semver::Version::parse("1.0.0-alpha.10")) < 0);
  CHECK(semver::Version::parse("1.0.0") == semver::Version::parse("1.0.0+build"));

  const auto caret = semver::Requirement::parse("^1.2.3");
  CHECK(caret.has_value());
  CHECK(caret->matches(*semver::Version::parse("1.2.3")));
  CHECK(caret->matches(*semver::Version::parse("1.9.0")));
  CHECK(!caret->matches(*semver::Version::parse("2.0.0")));
  const auto zero = semver::Requirement::parse("^0.2.3");
  CHECK(zero.has_value());
  CHECK(zero->matches(*semver::Version::parse("0.2.9")));
  CHECK(!zero->matches(*semver::Version::parse("0.3.0")));
  const auto tilde = semver::Requirement::parse("~1.2.3");
  CHECK(tilde.has_value());
  CHECK(tilde->matches(*semver::Version::parse("1.2.9")));
  CHECK(!tilde->matches(*semver::Version::parse("1.3.0")));
  const auto range = semver::Requirement::parse(">=1.0, <2.0");
  CHECK(range.has_value());
  CHECK(range->matches(*semver::Version::parse("1.5.0")));
  CHECK(!range->matches(*semver::Version::parse("2.1.0")));
  const auto wildcard = semver::Requirement::parse("1.2.*");
  CHECK(wildcard.has_value());
  CHECK(wildcard->matches(*semver::Version::parse("1.2.99")));
  CHECK(!wildcard->matches(*semver::Version::parse("1.3.0")));

  CHECK(!caret->matches(*semver::Version::parse("1.3.0-beta")));
  const auto prerelease = semver::Requirement::parse(">=1.0.0-alpha");
  CHECK(prerelease.has_value());
  CHECK(prerelease->matches(*semver::Version::parse("1.0.0-beta")));
  CHECK(!semver::Requirement::parse("not a version").has_value());
}

void testTomlSubset() {
  const char* document = R"(
title = "demo"
[package]
name = "app"
version = "1.0.0"

[tools]
items = [1, 2, 3]

[[mirrors]]
name = "a"
url = "file:///registry"

[[mirrors]]
name = "b"
priority = 10
)";
  const coord::toml::ParseResult parsed = coord::toml::parse(document);
  CHECK(parsed.ok);
  CHECK(parsed.root.string("title").value_or("") == "demo");
  const coord::toml::Value* name = parsed.root.find("package.name");
  CHECK(name != nullptr && name->isString() && name->asString() == "app");
  const coord::toml::Value* mirrors = parsed.root.get("mirrors");
  CHECK(mirrors != nullptr && mirrors->isArray());
  CHECK(mirrors->elements().size() == 2);
  CHECK(mirrors->elements()[1].string("name").value_or("") == "b");
  CHECK(mirrors->elements()[1].integer("priority").value_or(0) == 10);

  const coord::toml::ParseResult multiline = coord::toml::parse(
      "text = \"\"\"\nline1\nline2\"\"\"\nnumber = 0x10\nflag = true\n");
  CHECK(multiline.ok);
  CHECK(multiline.root.string("text").value_or("") == "line1\nline2");
  CHECK(multiline.root.integer("number").value_or(0) == 16);
  CHECK(multiline.root.boolean("flag").value_or(false));

  const std::string written = coord::toml::write(parsed.root);
  const coord::toml::ParseResult reparsed = coord::toml::parse(written);
  CHECK(reparsed.ok);
  CHECK(coord::toml::write(reparsed.root) == written);

  const coord::toml::ParseResult invalid = coord::toml::parse("[package\n");
  CHECK(!invalid.ok);
  CHECK(invalid.error.line >= 1);
}

void testArchiveModule() {
  using namespace coord;
  const std::string text = "hello archive\n" + std::string(1000, 'x');
  const std::string gzip = archive::gzipStore(text);
  std::string restored;
  std::string error;
  CHECK(archive::gunzip(gzip, restored, error));
  CHECK(restored == text);
  std::string inflated;
  CHECK(archive::inflate(gzip.substr(10, gzip.size() - 18), inflated, error));
  CHECK(inflated == text);

  TemporaryDirectory temporary;
  std::vector<archive::ArchiveEntry> entries;
  entries.push_back({"math-1.2.0/coordination.toml",
                     "name math\nversion 1.2.0\n", 0644, false});
  entries.push_back({"math-1.2.0/src/math.coo",
                     "export integer twice(integer v) { return v * 2; }\n", 0644,
                     false});
  const std::string tarball = archive::createTarGz(entries, error);
  CHECK(!tarball.empty());
  const std::filesystem::path archivePath = temporary.path / "math.tar.gz";
  std::string writeError;
  CHECK(platform::writeFile(archivePath, tarball, writeError));
  const archive::ExtractResult result =
      archive::extract(archivePath, temporary.path / "out");
  CHECK(result.ok);
  CHECK(result.files == 2);
  std::string readError;
  const std::optional<std::string> source = platform::readFile(
      temporary.path / "out" / "src" / "math.coo", readError);
  CHECK(source.has_value() &&
        source->find("twice") != std::string::npos);

  std::vector<archive::ArchiveEntry> malicious;
  malicious.push_back({"../escape.txt", "nope", 0644, false});
  std::string maliciousError;
  CHECK(archive::createTarGz(malicious, maliciousError).empty());

  // A hand-built tar header with a traversal name must be rejected.
  std::string header(512, '\0');
  const std::string evil = "../evil.txt";
  std::memcpy(header.data(), evil.data(), evil.size());
  std::memcpy(header.data() + 100, "0000644\0", 8);
  std::memcpy(header.data() + 108, "0000000\0", 8);
  std::memcpy(header.data() + 116, "0000000\0", 8);
  std::memcpy(header.data() + 124, "00000000000\0", 12);
  std::memcpy(header.data() + 136, "00000000000\0", 12);
  std::memset(header.data() + 148, ' ', 8);
  header[156] = '0';
  std::memcpy(header.data() + 257, "ustar", 6);
  header[263] = '0';
  header[264] = '0';
  unsigned checksum = 0;
  for (const char character : header) {
    checksum += static_cast<unsigned char>(character);
  }
  char checksumField[8] = {};
  std::snprintf(checksumField, sizeof(checksumField), "%06o", checksum);
  std::memcpy(header.data() + 148, checksumField, 6);
  header[154] = '\0';
  header[155] = ' ';
  std::string evilTar = header;
  evilTar.append(1024, '\0');
  const archive::ExtractResult evilResult =
      archive::extractTar(evilTar, temporary.path / "evil");
  CHECK(!evilResult.ok);

  // Minimal stored ZIP archive (local header + central directory + EOCD).
  const auto appendU16 = [](std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
  };
  const auto appendU32 = [](std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
  };
  const std::string zipName = "math-1.2.0/coord.pkg";
  const std::string zipData = "name math\n";
  const std::uint32_t zipCrc = coord::checksum::crc32(zipData);
  std::string zip;
  appendU32(zip, 0x04034b50U);
  appendU16(zip, 20);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU32(zip, zipCrc);
  appendU32(zip, static_cast<std::uint32_t>(zipData.size()));
  appendU32(zip, static_cast<std::uint32_t>(zipData.size()));
  appendU16(zip, static_cast<std::uint16_t>(zipName.size()));
  appendU16(zip, 0);
  zip += zipName;
  zip += zipData;
  const std::uint32_t centralOffset =
      static_cast<std::uint32_t>(zip.size());
  appendU32(zip, 0x02014b50U);
  appendU16(zip, 20);
  appendU16(zip, 20);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU32(zip, zipCrc);
  appendU32(zip, static_cast<std::uint32_t>(zipData.size()));
  appendU32(zip, static_cast<std::uint32_t>(zipData.size()));
  appendU16(zip, static_cast<std::uint16_t>(zipName.size()));
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU32(zip, 0);
  appendU32(zip, 0);
  zip += zipName;
  const std::uint32_t centralSize =
      static_cast<std::uint32_t>(zip.size()) - centralOffset;
  appendU32(zip, 0x06054b50U);
  appendU16(zip, 0);
  appendU16(zip, 0);
  appendU16(zip, 1);
  appendU16(zip, 1);
  appendU32(zip, centralSize);
  appendU32(zip, centralOffset);
  appendU16(zip, 0);
  const archive::ExtractResult zipResult =
      archive::extractZip(zip, temporary.path / "zipout");
  CHECK(zipResult.ok);
  const std::optional<std::string> zipManifest =
      platform::readFile(temporary.path / "zipout" / "coord.pkg", readError);
  CHECK(zipManifest.has_value() && *zipManifest == zipData);
}

void testManifestModule() {
  using namespace coord;
  std::string error;
  const std::optional<package::Manifest> legacy = package::Manifest::parse(
      "name app\nversion 0.1.0\nsource src\ndependency ../lib\n"
      "dependency math ^1.2\ndev-dependency testkit 0.3\n",
      "D:/demo/coord.pkg", error);
  CHECK(legacy.has_value());
  if (legacy.has_value()) {
    CHECK(legacy->name == "app");
    CHECK(legacy->version.toString() == "0.1.0");
    CHECK(legacy->legacy);
    CHECK(legacy->dependencies.size() == 3);
    CHECK(legacy->dependencies[0].isPath());
    CHECK(legacy->dependencies[1].name == "math");
    CHECK(legacy->dependencies[1].requirement.matches(
        *semver::Version::parse("1.5.0")));
    CHECK(legacy->dependencies[2].kind == package::DependencyKind::Development);
  }

  const std::optional<package::Manifest> tomlManifest =
      package::Manifest::parse(
          "[package]\n"
          "name = \"demo\"\n"
          "version = \"0.3.1\"\n"
          "source = \"src\"\n"
          "\n"
          "[dependencies]\n"
          "math = \"^1.2\"\n"
          "local = { path = \"../lib\" }\n"
          "core = { version = \"~0.3\", platform = \"windows\" }\n"
          "\n"
          "[registries]\n"
          "official = \"file:///registry\"\n"
          "\n"
          "[trust]\n"
          "require_checksum = true\n"
          "allow_insecure_http = false\n"
          "\n"
          "[[mirrors]]\n"
          "name = \"local\"\n"
          "url = \"file:///registry\"\n"
          "priority = 5\n",
          "D:/demo/coordination.toml", error);
  CHECK(tomlManifest.has_value());
  if (tomlManifest.has_value()) {
    CHECK(tomlManifest->name == "demo");
    CHECK(tomlManifest->version.toString() == "0.3.1");
    CHECK(tomlManifest->dependencies.size() == 3);
    CHECK(tomlManifest->registries.size() == 1);
    CHECK(tomlManifest->mirrors.size() == 1);
    CHECK(tomlManifest->requireChecksum);
    CHECK(!tomlManifest->allowInsecureHttp);
    const package::DependencySpec* local = tomlManifest->dependency("local");
    CHECK(local != nullptr && local->isPath());
  }

  const bool windows =
      platform::operatingSystem() == platform::OperatingSystem::Windows;
  CHECK(package::platformMatches("windows") == windows);
  CHECK(package::platformMatches("!windows") == !windows);
  CHECK(package::platformMatches("unix") == !windows);
  CHECK(!package::isValidPackageName("../bad"));
  CHECK(package::isValidPackageName("math-utils"));
}

void testLockfileModule() {
  using namespace coord;
  package::Manifest manifest;
  manifest.name = "app";
  manifest.version = *semver::Version::parse("0.1.0");
  package::Resolution resolution;
  resolution.ok = true;
  package::ResolvedPackage math;
  math.name = "math";
  math.version = *semver::Version::parse("1.2.0");
  math.registry = "official";
  math.url = "file:///registry";
  math.checksum = "ABCDEF";
  math.dependencies = {"core"};
  resolution.packages.push_back(math);
  package::ResolvedPackage core;
  core.name = "core";
  core.version = *semver::Version::parse("0.3.1");
  core.registry = "official";
  resolution.packages.push_back(core);

  const package::Lockfile lock = package::lockfileFromResolution(manifest, resolution);
  CHECK(lock.packages.size() == 2);
  CHECK(!lock.manifestHash.empty());
  const std::string serialized = lock.serialize();
  std::string error;
  const std::optional<package::Lockfile> parsed = package::Lockfile::parse(serialized, error);
  CHECK(parsed.has_value());
  if (parsed.has_value()) {
    CHECK(parsed->fingerprint() == lock.fingerprint());
    const package::LockedPackage* mathEntry = parsed->find("math");
    CHECK(mathEntry != nullptr);
    if (mathEntry != nullptr) {
      CHECK(mathEntry->checksum == "sha256:abcdef");
      CHECK(mathEntry->dependencies.size() == 1);
    }
  }
  CHECK(!package::Lockfile::parse("version = 9\n", error).has_value());
}

void testResolverModule() {
  using namespace coord;
  std::map<std::string, package::IndexEntry> index;
  package::IndexEntry math;
  math.name = "math";
  package::IndexVersion math1;
  math1.version = *semver::Version::parse("1.0.0");
  math1.dependencies = {"core ^0.3"};
  package::IndexVersion math2;
  math2.version = *semver::Version::parse("1.2.0");
  math2.dependencies = {"core ^0.4"};
  math.versions = {math1, math2};
  index["math"] = math;

  package::IndexEntry core;
  core.name = "core";
  package::IndexVersion core1;
  core1.version = *semver::Version::parse("0.3.1");
  package::IndexVersion core2;
  core2.version = *semver::Version::parse("0.4.0");
  core.versions = {core1, core2};
  index["core"] = core;

  auto provider = [&index](const std::string&, const std::string& name,
                           const std::string&, std::string& error)
      -> std::optional<package::IndexEntry> {
    const auto found = index.find(name);
    if (found == index.end()) {
      error = "not found: " + name;
      return std::nullopt;
    }
    return found->second;
  };
  package::Resolver resolver(provider);
  package::Manifest root;
  root.name = "app";
  root.version = *semver::Version::parse("0.1.0");

  package::DependencySpec mathDependency;
  mathDependency.name = "math";
  mathDependency.rawRequirement = "^1.0";
  mathDependency.requirement = *semver::Requirement::parse("^1.0");
  root.dependencies.push_back(mathDependency);

  package::ResolveOptions options;
  package::Resolution resolution = resolver.resolve(root, nullptr, options);
  CHECK(resolution.ok);
  const package::ResolvedPackage* selected = resolution.find("math");
  CHECK(selected != nullptr);
  if (selected != nullptr) {
    CHECK(selected->version.toString() == "1.2.0");
    CHECK(selected->direct);
    CHECK(selected->dependencies.size() == 1);
  }

  // core ^0.3 from math 1.0.0 forces backtracking away from math 1.2.0.
  package::DependencySpec coreDependency;
  coreDependency.name = "core";
  coreDependency.rawRequirement = "^0.3";
  coreDependency.requirement = *semver::Requirement::parse("^0.3");
  root.dependencies.push_back(coreDependency);
  resolution = resolver.resolve(root, nullptr, options);
  CHECK(resolution.ok);
  {
    const package::ResolvedPackage* mathPackage = resolution.find("math");
    CHECK(mathPackage != nullptr);
    if (mathPackage != nullptr) {
      CHECK(mathPackage->version.toString() == "1.0.0");
    }
    const package::ResolvedPackage* corePackage = resolution.find("core");
    CHECK(corePackage != nullptr);
    if (corePackage != nullptr) {
      CHECK(corePackage->version.toString() == "0.3.1");
    }
  }

  // A locked version is preferred over newer candidates.
  package::Lockfile lock;
  package::LockedPackage lockedMath;
  lockedMath.name = "math";
  lockedMath.version = *semver::Version::parse("1.0.0");
  lockedMath.source = package::LockSource::Registry;
  lockedMath.registry = "official";
  lockedMath.dependencies = {"core 0.3.1"};
  lock.packages.push_back(lockedMath);
  resolution = resolver.resolve(root, &lock, options);
  CHECK(resolution.ok);
  {
    const package::ResolvedPackage* mathPackage = resolution.find("math");
    CHECK(mathPackage != nullptr);
    if (mathPackage != nullptr) {
      CHECK(mathPackage->version.toString() == "1.0.0");
    }
  }

  // Conflicting requirements are reported with the requiring origins.
  package::DependencySpec strictCore;
  strictCore.name = "core";
  strictCore.rawRequirement = ">=0.4";
  strictCore.requirement = *semver::Requirement::parse(">=0.4");
  root.dependencies.push_back(strictCore);
  resolution = resolver.resolve(root, nullptr, options);
  CHECK(!resolution.ok);
  CHECK(resolution.error.find("core") != std::string::npos);
}

void testMirrorModule() {
  using namespace coord;
  package::Manifest manifest;
  manifest.name = "app";
  manifest.registries.push_back(
      {"official", "file:///registry"});
  manifest.mirrors.push_back(
      {"local", "official", "file:///registry-local", 20, true, ""});
  manifest.mirrors.push_back(
      {"backup", "official", "file:///registry-backup", 5, true, ""});

  package::MirrorManager manager;
  std::string error;
  CHECK(manager.loadProjectConfig(manifest, error));
  const std::vector<package::MirrorEntry> candidates =
      manager.candidates("official");
  CHECK(candidates.size() >= 2);
  CHECK(candidates[0].name == "local");
  CHECK(candidates[1].name == "backup");
  CHECK(package::isSupportedRegistryUrl("file:///registry"));
  CHECK(package::isSupportedRegistryUrl("https://example.invalid/registry"));
  CHECK(!package::isSupportedRegistryUrl("ftp://example.invalid/registry"));
  CHECK(package::normalizeRegistryUrl("https://example/") ==
        "https://example");
  CHECK(manager.selectMirror("backup", error));
  CHECK(manager.config().defaultMirror == "backup");
}


}  // namespace

int main() {
  testLexer();
  testGenericDeclaration();
  testSynthesizedDestructor();
  testMissingDestructionWarning();
  testExplicitDestruction();
  testCompileTimeEvaluation();
  testCodegen();
  testInterface();
  testCallableInterfaceLambdaTarget();
  testEnum();
  testDuplicateEnumValue();
  testEnumCodegen();
  testPointerType();
  testScopeShadowing();
  testScopeRelations();
  testVisibilityOwnership();
  testSameScopeRedeclaration();
  testUnicodeAndBom();
  testModules();
  testAccessControl();
  testInheritanceRules();
  testAccessCodegen();
  testSuperAndInitializers();
  testMetaAnnotations();
  testAnnotationInheritance();
  testOptionalNull();
  testConversionConstructors();
  testResultCodegen();
  testOutlineAndHints();
  testSignednessAnnotations();
  testLengthAnnotations();
  testFixTypes();
  testFixedArrays();
  testExpectCodegen();
  testAnnotationRegistry();
  testProcessorMethods();
  testInterfaceWriter();
  testInterfaceReader();
  testProxyValidation();
  testBorrowChecks();
  testDescriptionApi();
  testOwnershipAnnotations();
  testProcessorExecution();
  testOwnershipMoves();
  testReflectionRange();
  testUnsupportedOwnershipPositions();
  testDescriptorConsistency();
  testDescriptorSchema();
  testDescriptorMembers();
  testHintConsistency();
  testDescriptorPipelineConsistency();
  testProxyExplicitRetention();
  testOwnershipBorrow();
  testMainSignature();
  testJsImportExport();
  testReferenceLocals();
  testBorrowSafety();
  testNativeFFI();
  testDiagnosticLimit();
  testSwitchStatement();
  testMatch();
  testDocAttachment();
  testHandlerInterfaces();
  testInvalidSyntax();
  testPlatformIO();
  testChecksum();
  testSemver();
  testTomlSubset();
  testArchiveModule();
  testManifestModule();
  testLockfileModule();
  testResolverModule();
  testMirrorModule();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "all coord tests passed\n";
  return 0;
}
