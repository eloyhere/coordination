# Coordination Framework Specification

> Coordination is a statically typed systems language with no macros: compile-time metaprogramming is driven by annotations, descriptors, reflection and proxies, while object lifetime reuses C++ RAII. This document is the single authoritative statement of the framework, language and runtime contract.

- Language: **Coordination**; source files `.coo`; module interfaces `.coox`.
- Compiler: `coorc`; package manifests `coordination.toml` / `coord.pkg`; lockfile `coord.lock`.
- Companion documents: [README (English)](readme.md) · [快速上手（中文）](readme.zh-CN.md) · [Framework (中文)](framework.zh-CN.md).
- Formal grammar: [docs/grammar.ebnf](docs/grammar.ebnf).
- Historical documents: [docs/archive/](docs/archive/) is archive-only and **subordinate to this document**.
- Status legend: ✅ implemented and tested · 🚧 partial/in progress · ⏳ specified or designed only.

## 1 Positioning and Principles

### 1.1 What Coordination Is

Coordination treats compile-time computation as a first-class capability rather than a macro add-on. The following are language constraints, not recommendations:

- **No macros**: no token substitution, string-built code, or textual expansion; annotations are the only entry point for compile-time metaprogramming.
- **Phase is determined by context**: there is no `comptime` / `constexpr` / `consteval` keyword. Annotation processors, annotation arguments, `const` initializers and generic arguments execute at compile time by syntactic position.
- **Zero runtime reflection by default**: `description` / `descriptor` / `reflection` / `reflector` / `proxy` exist only at compile time; only explicit `@Retention` materializes read-only side tables, and never changes object layout.
- **RAII reuses C++ semantics**: construction/destruction order, reverse destruction and no GC. Object lifetime is hard language semantics, not a library facility.

The first backend lowers checked programs to a single C++20 translation unit and hands it to a host C++ compiler. This lets M0 validate semantics with a real compiler (RAII order, ABI, debug info, optimization); the price is that language semantics must fit a subset C++ can express precisely.

### 1.2 Design Principles

| # | Principle | Meaning / cost |
| --- | --- | --- |
| P1 | One semantics, no macros | Compile-time code is ordinary declarations and expressions; there is no second grammar. |
| P2 | Context-determined, statically decidable phase | Compile-time context comes from syntax position; runtime values cannot leak into it. |
| P3 | Reflection is data, not strings | Queries yield `description` / `Reflector<T>`, not name-built strings. |
| P4 | Generation only appends | Processors can only add new declarations: incremental, predictable, cacheable. |
| P5 | RAII is C++ RAII | Construction: bases → members in declaration order → body. Destruction: body → members in reverse → bases. |
| P6 | Errors are values | Hidden `Result<T, Error>` bubbles up the call stack by default; no exceptions. |
| P7 | Safe by default, escape explicitly | Pointer arithmetic/dereference and `new`/`delete` require `unsafe`. ⏳ |
| P8 | Monomorphization first | Generics monomorphize by default, mapping naturally to C++ templates. |
| P9 | Diagnosable | Diagnostics carry source spans and stable codes; compile-time execution has fuel/memory limits. |
| P10 | Incremental backend | C++20 → `.coox` modules → LLVM/custom backend → self-hosting. |
| P11 | Every `{}` is a Scope | A Scope is simultaneously the visibility, shadowing and lifetime boundary. |

### 1.3 Key Decisions

| # | Decision | Rationale / cost |
| --- | --- | --- |
| D1 | 15 universe entries: 14 root types plus the independent runtime text type `string` | Every value/declaration type ultimately lands on a root; `string` is not part of the five metadata layers. |
| D2 | Five reflection layers: `description`, `descriptor`, `reflection`, `Reflector<T>`, `proxy` | Data and behavior separated; dynamic and static separated. |
| D3 | No macros; annotations are read-only and append-only; users can define annotations | Error location, hygiene, cacheability; generated entities must be standalone declarations. |
| D4 | Phase by context; no `comptime` keyword | Smaller grammar; phase errors are type-system diagnostics. |
| D5 | Only six built-in meta-annotations: `@Inherit`, `@Repeatable`, `@Target`, `@Meta`, `@Retention(stage)`, `@Documented` | Users cannot define meta-annotations or change semantics; `@Inherit` prefers direct annotations and fills gaps. |
| D6 | Hidden `Result<T, Error>` auto-bubbles by default; `@Result` exposes it, `@Expect` must be consumed; constructors cannot fail; no exceptions | Clean call sites; ABI needs a hidden return slot. ⏳ |
| D7 | One Scope per `{}`; duplicate declarations in one scope are errors; locals destruct in reverse | Visibility, shadowing and lifetime share one boundary. |
| D8 | Fixed RAII order; synthetic destructor when absent; user destructors must write `~this.member;` for every class-typed field (missing `W0001`, primitive `W0002`, duplicate `W0003`, last wins); the backend erases these calls | Zero-cost backend, no double destruction. |
| D9 | Single inheritance + multiple interfaces; class methods static by default, `virtual` explicit; `override` mandatory; `final` on class/method/field; `private` methods overridable but not callable directly; `super` reaches the base implementation; same-name fields hide; overload sets merge; covariant returns; base-first layout; automatic virtual destructor when virtual methods exist; C++ access sections default `private` | Simple layout, explicit overriding; slightly verbose declarations. |
| D10 | Postfix pointers `T*`; references `T&`; `const T*` means "pointer to const T" | Read declarations inside-out, aligned with C++. |
| D11 | `enumerate` declaration, `Enumeration` root; C-style integer backend; default 0/previous+1; unique names and values; annotatable variants; methods and `implements`; exhaustive `match`; no name table unless `@Retention(enum)`, which adds a side `(name,value,ordinal)` table | No class inheritance; stable ABI. |
| D12 | Sources: UTF-8/UTF-16LE/BE/UTF-32LE/BE, optional BOM stripped, internal UTF-8; identifiers allow Unicode letters/digits/underscore; `char` is a 32-bit scalar | Unicode-consistent; backend maps to `char32_t`. |
| D13 | One `.coo` = one module; `export` controls visibility; `.coox` is the interface/metadata; `coordination.toml`/`coord.pkg` plus `coord.lock` | Clear incremental granularity; manifests and lockfile implemented. |
| D14 | Lifecycle Load → Lex → Parse → @Processor → Sema → ConstEval → C++ lowering → host CC/link → run; `coorc check/build/run/--emit-cpp` | Every stage can stop separately for testing and diagnosis. |
| D15 | Compiler comprises 9 core stage headers plus 38 `modules/*` headers with one-way dependencies | Stages can migrate to separate libraries; I/O and platform modules use `.cpp`. |
| D16 | Stream contract: internal LF, CRLF/CR normalized on read, LF on write, 0-based lines, byte offsets are normalized UTF-8 offsets | Cross-platform line/offset consistency. |
| D17 | Cross-platform boundary in `modules/platform`: UTF-8 paths, directory conventions, executable/object/library naming, direct `argv` spawn (no shell), timeouts/output capture, Windows UTF-16 command lines | Compiler/package manager never write `#ifdef _WIN32`. |
| D18 | Packages: dual manifest formats; semver constraints + path-first + registry backtracking; `coord.lock` pins versions/SHA-256; vendor into `coord_modules/`; registry is a directory tree; mirrors fail over by priority and health | Reproducible, offline-capable, mirrorable. |
| D19 | Trust boundary: archive SHA-256 + HTTPS (system curl) + publish token; no package signatures/trust chain | No external crypto dependency; signatures deferred. |
| D20 | Every proxy object shares four `ProxyPolicy` strategies: `Error` (default), `Skip`, `Abort`, `Default(expr)`. `@Optional` defaults to `Error`; `@Proxy(handler)` defaults to `Skip`. Under `Error`, an unhandled direct proxy use is compile-time `E2048`. | One policy model covers Optional, `@Proxy`, construction and destructuring; no second null system. |
| D21 | Lowering generates an internal proxy layer; when the target is itself a `@Proxy`, the Optional layer is outer and the user handler inner. | Policy checks precede user traps; absent paths never invoke user handlers. |
| D22 | Generated programs are cross-platform: C++20 + conditional `<windows.h>`; Windows/MSVC uses `wmain`, converts UTF-16 argv to UTF-8 and sets a UTF-8 console; POSIX uses `main` | Source/path/console behavior is consistent; platform differences live only in the generated prelude. |
| D23 | Cross-compilation: `--target <triple>` (clang `--target`), `--sysroot <path>`, GCC cross toolchains via `--cxx`; MSVC cannot cross-compile; cross targets cannot `run` | Reuses host C++ toolchains instead of bundling a code generator. |
| D24 | `@Constructure` (`@Target(constructor)`) enables typed/named brace construction; `@Destructure` is both a `ProxyHandler` trap and a target-class method with reference outputs | Construction and destructuring stay user-defined; no implicit structural conversion. |
| D25 | Type annotations (`@Optional`, `@Unique`, `@Share`, `@Signed`, `@Unsigned`, `@Length`, `@Radix`, `@Weak`) target `type`; `@Constructure` targets `constructor`; `@Target` accepts both names | Annotations attach to types rather than declarations, so aliases and composed types share the same semantics. |

### 1.4 Replacing Macros

Four channels are all required: **annotations** attach intent to a target; **description** turns target properties into structured data; **descriptor / annotation processors** read description at compile time and generate new declarations; **proxy** wraps behavior around call sites at compile time. Generated entities can only be **new declarations**: they cannot rewrite existing declarations, splice tokens, emit arbitrary `#...` text, or modify the target's structure.

### 1.5 Documentation Map

| Document | Contents |
| --- | --- |
| `framework.md` / `framework.zh-CN.md` | Framework, language, reflection, compiler, standard-library summary, package protocol and backend mapping (this document). |
| `readme.md` / `readme.zh-CN.md` | QuickStart, build/test, CLI, concise package/mirror usage, cross-platform notes. |
| [docs/grammar.ebnf](docs/grammar.ebnf) | Formal grammar appendix; where the parser and this document disagree, code wins and this document is corrected. |
| [docs/archive/](docs/archive/) | Old architecture/spec/roadmap documents; historical reference only, unmaintained. |

## 2 Type Universe

### 2.1 Root Types (15 Entries)

Root types form the type universe: every value or declaration type ultimately lands on one of them, and concrete types are subtypes.

| Root type | Nature | Phase | Typical values / use | Runtime representation |
| --- | --- | --- | --- | --- |
| `integer` | Integer root/primitive | Runtime | 64-bit signed by default; `@Unsigned`, explicit `@Signed`, `@Length(N)` | Fixed-width integer |
| `float` | Binary floating-point root | Runtime | `float` (C++ `double`), planned `f32`/`f64` | IEEE-754 |
| `decimal` | Decimal fixed-point root | Runtime | `decimal` (value + scale), `@Length` storage narrowing | Fixed point |
| `character` | Unicode scalar root | Runtime | `char` (32-bit scalar) | UTF-32 code point |
| `boolean` | Boolean root | Runtime | `bool` | 1 byte |
| `Enumeration` | Enumeration root | Runtime | Concrete `enumerate` type | Integer + optional side name table |
| `class` | Nominal object root | Runtime | User `class`, `interface` | Struct + vtable |
| `annotation` | Annotation root | Compile time | User `annotation`, optionally with processors | None |
| `function` | Callable root | Two-phase | Functions, methods, operators, closures | Function pointer / closure object |
| `description` | Declaration properties | Compile time | Snapshot of class/function/method/variable properties | None |
| `descriptor` | Property-descriptor processor | Compile time | Takes `description`, emits declarations | None |
| `reflection` | Erased reflection cursor | Compile time | Dynamic kind/name/member queries | None |
| `reflector` | Typed reflection view | Compile time | `Reflector<T>` with statically checked members | None |
| `proxy` | Behavior proxy root | Compile-time expansion or runtime retention | `@Proxy(Handler)`; zero overhead by default | No object by default; wrapper in retained mode |
| `string` | UTF-8 text | Runtime | Independent text type, outside the five metadata layers | `std::string` |

Counting: 14 root types (`integer` … `proxy`) plus the independent runtime text type `string` gives 15 entries. `float` is the binary floating-point type (C++ `double`); `decimal` is fixed-point `value + scale`. `string` is **independent**: it is not an alias for `description`, does not participate in reflection or retention tables, and cannot carry compile-time metadata.

Auxiliary types: `void` (unit), `interface` (nominal interface covered by the `class` root), `Result<T, E>` and `Expect<T, E>`.

Common confusion: `description` is **data** (read-only snapshot); `descriptor` is **behavior** (processor). `reflection` is the erased layer, `Reflector<T>` the typed layer. `proxy` is not a new type but a call-site rewrite.

### 2.2 Built-in Concrete Types

- Integers: `integer` (64-bit signed by default); `@Unsigned` maps to unsigned, `@Signed` explicit signed; `@Length(8/16/32/64/128)` selects width. There are no `int`, `i8`, `usize` aliases.
- Floating point: `float` is binary floating point (C++ `double`); `decimal` is decimal fixed-point (value + scale); explicit `f32`/`f64` widths are planned.
- Text: `char` (32-bit Unicode scalar, not a byte), `string` (immutable UTF-8 byte sequence, runtime).
- Others: `bool`, `void`, `record`/`Record`; planned: `Result<T, Error>`, `Expect<T, Error>`, `Error`, `@Unique`/`@Share` ownership annotations, `T[N]`, `Array<T>` and views.
- Standard-library PascalCase names (`Integer`, `String`, `Character`, `Boolean`, `Float`, `Decimal`, `Record`) resolve to the same `Type*` as their lowercase keyword; diagnostics and reflection use the canonical name.

`decimal` fixed-point rules: the backend representation is `coord_rt::Decimal { value; scale; }`; `+`/`-`/`%` align scales first; `*` adds scales; `/` keeps the left operand's scale; bitwise operations align scales first; `<<`/`>>` shift only the value and keep the scale; `print` trims trailing zeros. `@Length(8/16/32/64)` narrows the value/scale fields to the given width, with arithmetic promoted to 64-bit and written back; `@Length(128)` decimal is not supported yet.

### 2.3 Subtyping and Assignability

- Nominal subtyping: `S <: T` iff `S == T`, `S`'s base chain contains `T`, `S` implements interface `T`, or `S` is a member of `T`'s union/intersection.
- Generics are invariant by default; `N extends integer` means every instance of `N` is a subtype of `integer`; the `class` root is the upper bound of nominal types.
- Unions `A | B` and intersections `A & B` apply to types and constraints; `&` means reference in parameter type position and intersection in constraint position (context disambiguates).
- Assignability: a subtype relationship is sufficient; additionally `integer` widens to `decimal` and numeric widening is allowed; pointers are covariant in pointee type (M0 behavior).
- Planned: `where` structural constraints, const generics (array lengths), generic interfaces, full union/intersection checks and `match` narrowing (⏳).

### 2.4 Nullability and ProxyPolicy

`@Optional` is neither a root type nor a `Nullable<T>` wrapper; it is a **type state** carried by a `@Target(type)` annotation:

- It applies to the types `T*`, `@Unique T`, `@Share T`. A plain `@Optional Class` is only meaningful in descriptor scenarios and **does not promise runtime nullability**.
- A non-optional `T*`/`@Unique`/`@Share` is non-null by default; assigning `null` reports `E2046`; applying `@Optional` to other types reports `E2047`.
- Nullable values: `null` may only be assigned to optionals. Under the default `Error` policy, optional-to-non-optional conversion is forbidden without narrowing or `orElse`/`get()`; under `Skip`/`Abort`/`Default(expr)`, materialization to a non-optional value follows the policy. Non-optional to optional is a safe widening. Some implicit-conversion boundaries are still 🚧.
- Element nullability cannot be expressed by a type annotation yet; v0 supports `@Optional` in type positions of fields/locals/parameters/returns, with type-level `Nullable<T>` deferred.
- **ProxyPolicy** is shared by every proxy object, not only Optional. Optional is one proxy kind; `@Proxy` objects, construction and destructuring use the same four policies. See §4.5.2.

## 3 Scope and Phases

### 3.1 Scope Kinds and the Scope Graph

The language exposes four scope boundaries:

| Scope | Introduced by | Contents | Lifetime endpoint |
| --- | --- | --- | --- |
| Module | `.coo` top level | imports, types, functions, constants | end of module checking |
| Type | `class` / `interface` / `annotation` / `enumerate` `{}` | fields, methods, constructors/destructors, nested types | end of type checking |
| Function | parameter list of function/method/constructor/destructor | parameters | function return |
| Block | statement block `{ ... }` | local variables, local constants | block exit (`}`, `return`, `break`, `continue`, error bubbling) |

The implementation models name resolution as a **Scope Graph**, with node kinds Module / Namespace (future) / Class / Function / Method / Block / TypeParam. Each node has `id`, `kind`, `parent`, `owner`, declarations, import edges, base edges and visibility rules.

Edge labels and resolution:

| Edge | Meaning | Participates in name resolution |
| --- | --- | --- |
| `LEX` | lexical parent scope | yes |
| `MEM` | class members / method's owning class | yes |
| `EXT` | base class / implemented interface | yes |
| `IMP` | module import | yes |
| `EXP` | re-export | yes |
| `TP` | type parameters | yes |
| `DEP` | call / type dependency | **no** |

Visibility formula:

```text
Visible(S) = Declarations(S, before use) ∪ Visible(parent(S))   // inner shadows outer
Visible(b) ∩ Visible(c) = declarations of common ancestors (after shadowing)
```

Dependency never enters the formula: **dependency grants no visibility**. `b` can call `a()` because `a` is already visible through a lexical parent, an import or explicit qualification — not because `a` is called.

Rules:

- Lookup proceeds inside-out and stops at the first match; inner scopes shadow outer ones; duplicate declarations in one scope are errors (`E2002`).
- Sibling scopes are fully isolated; textual adjacency or call relationships do not make them visible to each other; the dependency graph may contain cycles, but initialization order must detect them.
- Locals are visible only after their declaration point; types, functions and class members allow forward references (two-pass collection).
- A function body is a Block scope nested in the Function scope; a parameter may share a name with the outermost local of the body.
- A class body is a Type scope: fields do not participate in local lookup and must be named `this.field` or as a bare field inside a member function. Bare-name order: local Scope → current class fields → module constants.
- `this` is visible only inside a Type scope; using it outside a class/interface/annotation is `E2004`.

Resolution order (highest first):

1. locals/parameters declared before the use in the current block;
2. type parameters of the current function/method;
3. members of the current class;
4. base-class members (in inheritance order; ambiguity is an error);
5. module-level declarations;
6. explicit imports/aliases;
7. wildcard imports (lowest).

Multiple candidates at the same level must be an ambiguity error, never "first found wins". Private/protected members are resolved and then access-checked. Planned scope-specific codes: `E2080`–`E2086` (not visible, same-scope conflict, ambiguity, use before declaration, import conflict, private access, initialization cycle).

### 3.2 Scope Ownership and Initialization Order

- Every object has one **owner Scope**: locals belong to Block/Function, fields to their object, statics/globals to Module.
- When an owner Scope exits, it destroys owned objects in reverse declaration order; no detach, no extension, no counting.
- Class initialization: bases before subclasses; members in declaration order; static/module initialization in dependency topological order; initialization dependency cycles are compile-time errors where decidable.
- Name resolution and initialization order are separate concerns: dependency edges only drive initialization order, lifetime, diagnostics and outline.

### 3.3 Lifetimes

- Entering a Block constructs locals in declaration order; leaving destroys them in reverse. Early exits (`return`, `break`, `continue`, error bubbling) also trigger reverse destruction.
- A compiler-synthesized destructor destroys members in reverse declaration order and finally the base.
- Scope boundaries map one-to-one to C++ `{}`; the backend preserves braces, and the language plus backend jointly guarantee destruction order.
- Temporaries are destroyed at the end of the full expression; a moved-from source is still destroyed, so destructors must accept moved-from state.

### 3.4 Phase Model

There is no phase keyword; phase is determined by **syntax context**:

```text
Compile-time context: annotation processor body / annotation arguments / const initializers / generic arguments and constraints / .coox metadata construction (⏳)
Runtime context: ordinary functions / methods / constructors / destructor bodies
```

- Compile-time contexts may call ordinary pure functions; the interpreter executes by value and reports IO, time, randomness or mutable globals as errors.
- Runtime contexts cannot hold `annotation`/`description`/`descriptor`/`reflection`/`reflector`/`proxy` values (`E2009`).
- Compile-time execution is bounded by fuel and recursion depth: 2,000,000 evaluations / 512 call frames in M0; exceeding it is a compile error (`E3001`), never a hang.
- Same input must produce the same output; content-addressed result caching is the goal (⏳, none in M0). Compile-time errors are compile errors; compile-time control-flow exits also destroy in reverse order (the interpreter maintains a Scope stack and must pop on unwind).
- A `const` initializer that cannot be evaluated at compile time falls back to runtime (M0 behavior); evaluable ones enter the constant table and participate in later evaluation.

## 4 Language Specification

This section specifies **target semantics**. Status is summarized in §11; entries without ⏳ are either in effect or only lack negative tests. Per-item status is authoritative in §11.

### 4.1 Lexical Structure

- Sources: `.coo`; module interfaces `.coox`. Encoding: UTF-8, UTF-16LE/BE, UTF-32LE/BE; BOM optional and stripped on read; internal UTF-8. `Span` is a normalized UTF-8 byte offset (half-open); invalid code points become U+FFFD without aborting lexing.
- Comments: `//` line comments; `/* ... */` block comments, nestable; `///` and `/** */` documentation comments attach to the next declaration. Sema validates `@parameter`/`@param`, `@return`, `@template`; unknown or duplicate tags report `E2076`.
- Identifiers: start with a Unicode letter or `_`, continue with Unicode letters, digits or `_`; keywords are ASCII and reserved words cannot be identifiers. M0 is more permissive: any non-ASCII code point that is not Unicode whitespace/control is accepted; tightening to letters/digits is future work.
- Keywords: `module import export as class interface implements extends annotation function void const static virtual override final operator where if else switch while for in return break continue true false null this is match extern unsafe new delete integer decimal float record character boolean enumerate public private protected`. `descriptor`/`description`/`reflection`/`reflector`/`proxy` are **not** keywords. "Lexically reserved" does not mean implemented: `is`/`extern`/`unsafe`/`new`/`delete` are reserved and currently report `E1101` or lack semantics; `match` works for enumerations; `operator`/`where` parse but full semantics are ⏳.
- Literals: integers (decimal, `0x`/`0b`/`0o`, `_` separators); decimal fractions and exponents (`1e10`); characters `'a'`, `'\u{...}'`; strings `"..."`; `true`/`false`. Numeric suffixes are unsupported (`E1001`).
- Operators: `+ - * / % = += -= *= /= %= == != < <= > >= && || ! & | ^ ~ &= |= ^= << >> ++ -- -> => . .. ..= :: ? @ #`. `**` is not in the token table; `<<`/`>>` are formed at expression level from adjacent `<`/`>` so nested generics still parse; expression semantics follow the C++ same-precedence subset.
- `this` denotes the current instance; **`self` is not used**.

### 4.2 Declarations

#### 4.2.1 Classes

```coo
final class File {
public:
    string path;
    File(string p) { this.path = p; }
    ~File() {}
    integer size() const { return 0; }
}
```

- Access sections `public:` / `private:` / `protected:` are C++-style, **default `private`**; interface bodies default `public`. Single inheritance `extends Base`, multiple interfaces `implements I1, I2`.
- Construction: `: super(args)` calls the base constructor, `: field(expr)` initializes members; order is fixed: base → members in declaration order → constructor body.
- Destruction: `~Name()` takes no parameters, cannot be `static`/`const`, and cannot fail; the destructor is automatically virtual when the class has virtual methods (or implements an interface).
- Fields lay out in declaration order; `private` fields participate. A `final` class cannot be inherited (`E2031`); a `final` method cannot be overridden (`E2032`); a `final` field cannot be hidden (`E2032`).
- Constructors and destructors cannot fail; failure semantics require a `Result`-returning factory (⏳).
- Initialization forms: `A a{}` is per-field aggregate initialization (requires an aggregate class, else `E2049`); `A a()` calls a constructor; `A a = b` uses an accessible single-argument converting constructor; `A a = { ... }` with typed/named entries selects a `@Constructure` constructor (§4.3.1, §4.8.2).
- `@Constructure` is a `@Target(constructor)` annotation and may mark only a constructor of the target class. The marked constructor participates in typed/named brace construction; the constructor body remains user-defined and is responsible for the actual conversion. Ordinary positional `Type{1, 2}` construction remains available alongside it.
- `@Radix(2|8|10|16)` changes only the **spelling of generated C++ integer literals** for a declaration initializer, field default or `const` value: `@Radix(16) integer mask = 255;` emits `static_cast<std::int64_t>(0XFF)`, base 2 emits `0B...`, base 8 emits a C++ octal literal such as `0100`. It does not parse source literals differently and does not change runtime `print`; invalid bases or non-integer targets report `E2066`.

#### 4.2.2 Interfaces

```coo
interface Drawable {
    void draw() const;
    integer z() const { return 0; }   // default implementation
}

class Point extends Shape implements Drawable {
public:
    Point() {}
    override void draw() const { print("point"); }
}
```

- Interface methods are virtual by default and may have default implementations; interfaces cannot declare fields (`E2013`).
- Interfaces may extend other interfaces: `interface DestructureProxyHandler extends ProxyHandler {}`. `interface X extends ProxyHandler` is the supported form for proxy-handler subinterfaces; implementing a subinterface also satisfies the parent interface requirement.
- Class methods use static dispatch by default; `virtual` opts into dynamic dispatch. Overrides must be marked `override` (`E2035`), the parent method must be `virtual` (`E2033`), `final` methods cannot be overridden (`E2032`), and covariant returns are allowed (`E2034` on violation).
- A private method may be overridden and participate in dynamic dispatch, but a subclass cannot call the parent implementation by name (`E2030`); `super.method()` / `super.field` reach the base implementation. Conflicting same-name interface methods must be disambiguated by an explicit override in the implementing class.

#### 4.2.3 Enumerations

```coo
@Retention(enum)
enumerate Color {
    Red,
    Green = 3,
    Blue,      // 4
}
```

- `enumerate` is a declaration keyword; a concrete enum type is a subtype of the `Enumeration` root and is parallel to `class`, not part of class inheritance.
- C-style integer backend: the underlying type is a 64-bit integer; the first variant is 0 and later variants default to previous + 1; integer constant expressions may assign explicit values (`E2020`).
- Variant names are unique within an enum (`E2022`); explicit values must be unique (`E2023`); `Color.Red` has type `Color`; a missing variant is `E2024`; equality/comparison, `switch` and `print` work.
- Conversion: `Color.Red as integer` yields the underlying value; `n as Color` range-checks and panics when out of range (⏳).
- Enums may define methods and implement interfaces; they cannot declare instance fields or constructors. Both the enum type and individual variants are annotatable (targets `enumerate` / `enum_value`).
- `match` over an enum is exhaustive: all variants must be listed unless a wildcard/binding branch covers the rest; a missing variant is `E2077`.
- Runtime representation is the underlying integer; **no name table by default**. `@Retention(enum)` on the `enumerate` generates a read-only side `(name, value, ordinal)` table without changing layout/ABI (⏳ P2); other `@Retention` stages on an enum are `E2021`.

#### 4.2.4 Annotations

```coo
@Retention(invocation)
@Target(method)
annotation Audit(string reason);

class Service {
    @Audit("billing")
    integer charge(integer amount) { return amount; }
}
```

- Annotation declarations may take parameters and processor methods; processor methods run at compile time and receive the target `description`.
- Read-only, append-only (full rules in §4.7): application never overwrites/replaces/modifies earlier annotations and never modifies the target's structure; a processor may only emit new standalone declarations. By default the same annotation type appears once per target; `@Repeatable` permits repetition.
- Annotations exist only at compile time by default and cannot be runtime field/parameter types (`E2009`); only `@Retention` materializes a runtime read-only metadata table and generation logic. Processors may call the single directive primitive `line(file, line)` to set the generated C++ `#line` mapping.
- Built-in annotation names (recognized by the compiler; users cannot redefine them): `@Result`, `@Expect`, `@Invocation`, `@Proxy`, `@Get`, `@Set`, `@Destructure`, `@Construct`, `@Constructure`, `@Has`, `@Delete`, `@Keys`, `@Describe`, `@Call`, `@Copy`, `@Wrap`, `@Native`, `@FunctionalInterface`, `@Unique`, `@Share`, `@Optional`, `@Signed`, `@Unsigned`, `@Length`.
- **Target correction**: `@Optional`, `@Unique`, `@Share`, `@Signed`, `@Unsigned` and `@Length` are `@Target(type)` annotations; they attach to type expressions at declaration positions, not to the declaration itself. `@Constructure` is `@Target(constructor)`. All other annotations keep their declaration targets.
- **Layering**: only six meta-annotations are built into C++; all other annotations are ordinary Coo declarations in `lib/standard/annotations.coo`. The compiler retains only name-keyed semantic hooks (e.g. `@Result` return wrapping, `@Unsigned` storage mapping) and does not maintain a second built-in table. `Descriptor`/`Reflection` are ordinary classes in that file; `Description`/`Reflector<T>` are compiler-special.
- `@Native(header: string, library: string, symbol: string = "", abi: "c" | "c++" = "c")`: annotates a body-less class/function/method/variable; the compiler `#include`s the header in generated C++ and passes the library to the host linker. No runtime FFI/dlopen; the declaration introduces no Scope and does not participate in runtime reflection.

#### 4.2.5 Functions, Operators and Constants

```coo
integer add<N extends integer>(const N& a, const N& b);
Vec operator+(const Vec& lhs, const Vec& rhs);   // syntax ✅ / semantics ⏳
const integer FIB10 = fib(10);
const answer = 42;                               // inferred type
```

- Functions overload by name + parameter count (overload resolution by parameter type ⏳); default arguments parse (semantics ⏳); named arguments and closures (⏳).
- Generic parameters may carry an `extends` upper bound; `where` clauses express structural constraints (syntax ✅ / semantics ⏳); const generics (⏳).
- `operator` overloading: member or free functions, restricted to the lexical operator table (semantics ⏳).
- `const` initializers are evaluated at compile time; there is no `comptime` / `constexpr` keyword. `const` may omit the type and infer it from the initializer; local and top-level `const` follow the same compile-time rules.
- `print(x)` is a backend built-in (one line per call) until the standard library's `Println`/`Format` takes over.

### 4.3 Types, Statements and Expressions

Type forms (read inside-out, like C++):

| Form | Meaning | Status |
| --- | --- | --- |
| `T*` | Possibly-null pointer (postfix) | ✅ |
| `T&` | Non-null reference | ✅ |
| `const T` / `const T&` | Read-only value / reference | ✅ |
| `const T*` | Pointer to const `T` (the pointer itself is mutable) | ✅ |
| `A \| B` / `A & B` | Union / intersection (constraint position) | ✅ parse / ⏳ full checks |
| `(A, B) -> R` | Function type | ✅ parse / ⏳ semantics |
| `T[N]` | Fixed-size array, C++ `std::array<T,N>` | ✅ |
| `T?` | Explicit nullable marker | ⏳ (not parsed) |

- `if` / `match` are intended to be **expressions**; a block's value is its last expression (`if` expression ⏳, `match` expression ⏳).
- Statements: block, variable declaration, `if`/`else`, `while`, `for`, `for ... in`, `switch`, `match`, `return`, `break`, `continue`, `~this.member;`, expression statement.
- `++value`, `value++`, `--value`, `value--` accept integer/decimal lvalues only; prefix/postfix value semantics follow C++. `<<`/`>>` are formed at expression level from adjacent `<`/`>` tokens.
- Local `T&` / `const T&` are implemented; when bound to a temporary the backend creates a hidden temporary and binds the reference to it for the reference variable's lifetime.
- Operator precedence matches C++; `<` parses as generic arguments in type context and less-than in expression context. Literals and pure calls fold during ConstEval.

#### 4.3.1 Object Literals and Destructuring

```coo
class Point {
public:
    integer x;
    string y;
}

integer main() {
    Point point = {x: 1, y: "a"};
    {integer x, string y} = point;

    print(x); // 1
    print(y); // a
    return 0;
}
```

- `{x: expr}` names fields explicitly; `{x, y}` is shorthand for same-name variables; fields must exist and be accessible.
- Destructuring `{integer x, string y} = point;` declares new variables in the current scope, binding by field name.
- Proxy construction triggers `@Set` per field; destructuring triggers `@Get` per field. A target class may instead define an `@Destructure` method (reference outputs), and a proxy handler may define a `@Destructure` trap (§4.8.1).
- Destructuring an Optional proxy follows `ProxyPolicy` (§4.5.2/§4.5.3): the default `Error` policy reports `E2048` when not narrowed; `Skip` binds defaults without reading target fields; `Abort` terminates; `Default(expr)` destructures the fallback.
- `...rest` needs the future `Record` view and is not enabled; capture lists `[...]` are unsupported.

**Typed/named construction with `@Constructure`**:

```coo
class Point {
public:
    integer x;
    string y;

    @Constructure
    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }
}

integer main() {
    integer a = 3;
    string b = "hi";

    Point p = {a, b};                  // a and b are already declared
    Point q = {5, "ok"};               // positional values
    Point r = {y = "named", x = 9};    // named arguments
    Point s = {integer a, string b};   // explicit types only when disambiguation is needed

    print(p.x + q.x + r.x + s.x);
    return 0;
}
```

- Entry forms are `[A] value`, `[A] name` and `[A] name = expr`; the type label `A` is optional. Without `=`, a bare `name` is both the value expression and the match name; with `=`, the entry matches the constructor parameter by name. Entries are comma-separated and may be listed consecutively.
- If the variable/literal already has a type, write the expression directly (`{a, b}`, `{5, "ok"}`); add an explicit type label (`{integer a, string b}`, `{integer 5, string "ok"}`) only to disambiguate overloads.
- Matching considers only constructors marked `@Constructure`: entry count must match the parameter count; named entries match by parameter name; unnamed entries match by position and value type; explicit type labels also participate in assignability. No matching constructor or more than one viable candidate reports `E2076`.
- The user constructor body performs the actual conversion. Typed/named construction coexists with ordinary positional `Type{1, 2}` construction and with aggregate object literals (`{x: 1}`).
- If the target class is a `@Proxy`, the handler's `@Construct` hook runs before the constructor call; see §4.8.2.

#### 4.3.2 Lambda

Lambda **target types** follow Java (functional interface, function pointer, method reference), while **capture behavior** follows C++: an explicit capture list follows C++ rules; with an omitted capture list, referenced outer locals are copied by value and mutations affect the copy. Parameters and return types are explicit.

```coo
integer n = 1;

// Omitted capture list: n is copied; the mutation affects the copy only.
Function<integer, integer> copy =
    (const integer& x): integer -> { n = n + 1; return x + n; };

// Explicit by-reference capture: mutates the original.
Function<integer, integer> original =
    [&n](const integer& x): integer -> { n = n + 1; return x + n; };
```

Full form:

```text
[ capture-list ]? ( parameters ): return-type -> ( expression | { statements } )
```

| Rule | Semantics |
| --- | --- |
| Syntax | `[captures](const integer& x): integer -> { ... }`; the capture list is optional; the parameter list requires parentheses; the return type sits after `:` and before `->` |
| Omitted captures | Referenced outer locals are **copied by value**; the copy is mutable inside the lambda and the original is unchanged. `this` is copied by value (a pointer), so field mutations still affect the original object |
| Explicit captures | Same as C++: `[]`, `[=]`, `[&]`, `[x]`/`[&x]`, `[this]`, `[=, &x]`/`[&, x]` |
| Explicit by-value | Copies captured via `[=]`/`[x]` are read-only inside the lambda (assignment/`++`/`--` reports `E2088`); no `mutable` keyword in source (v0) |
| Explicit by-reference | Mutates the original; a by-reference lambda must not escape the Scope that declares it: returning it, storing it in a field/container literal, or assigning it to a variable in an outer scope reports `E2039` |
| Unlisted names | With an explicit capture list, unlisted outer names are invisible, as in C++ (`E2087`) |
| Capture validation | Unknown names, duplicate captures, a misplaced/repeated default capture and mixing `[=, x]`/`[&, &x]` report `E2087` |
| Target type | Functional interface (exactly one abstract method), function pointer `(A,B) -> R`, method reference, or an equivalent callable type |
| Functional-interface target | `@FunctionalInterface` interfaces with exactly one abstract method lower to the equivalent function type: `Function<A,R>` behaves as `(A) -> R`, `Consumer<T>` as `(T) -> void`, `Predicate<T>` as `(T) -> boolean`, `Supplier<T>` as `() -> T` and `BiFunction<A,B,R>` as `(A,B) -> R`. Lambdas and method references are checked against the abstract method, and `.apply`/`.accept`/`.test`/`.get` invoke the value. `lib/standard/utility.coo` declares these interfaces. Nominal interface values holding a class implementation are ⏳: a class cannot `implements` a functional interface in v0 (`E2004`), and wrong type-argument counts report `E2089` |
| Function-pointer target | Must capture nothing: `[]`, or an omitted capture list that references no outer names |
| Type checking | Parameter and return types must be compatible with the target; returns may be covariant; specific rules follow generic assignability |
| `this` | Copied by value when captures are omitted; with an explicit list, `[this]` is required to access members; capturing `this` prevents conversion to a function pointer |
| Shadowing | Lambda parameters **may** shadow outer locals/parameters; the compiler warns `W0004` (detection ⏳); the outer name is invisible inside the lambda |
| Return | Block bodies use `return`; expression bodies return automatically; both must be compatible with the declared return type |
| Generics | A lambda declares no type parameters of its own; the target type instantiation determines parameters/returns |
| Method references | `Type::staticMethod` produces a function value; `object::method` produces a bound function value that borrows the receiver and follows the same escape rules as a by-reference closure (`E2039`). Unbound `Type::instanceMethod` references are ⏳. |

A lambda "value" is usable only through a target type: assign, pass, return or store only after the target is fixed. A functional-interface target produces a closure object; a function-pointer target produces a capture-free thunk; method references behave the same way. `utility` provides `Function<T,R>`, `Consumer<T>`, `Supplier<T>`, `Predicate<T>`, `BiFunction<A,B,R>`. `@FunctionalInterface` is an optional marker checked for "exactly one abstract method." Lambdas compile to generated classes/closure objects with captures in fields; they monomorphize like ordinary interface implementations and add no runtime metadata. Status: 🚧. Implemented: function-type targets `(A) -> R`, expression and block bodies, implicit and explicit capture lists (`[]`, `[=]`, `[&]`, `[x]`, `[&x]`, `[this]`), omitted captures as mutable by-value copies, capture-list validation (`E2087`), read-only explicit by-value captures (`E2088`), by-reference escape checking for returns/fields/container literals/outer-scope variables (`E2039`), variable initialization/assignment, calls, lambda arguments and returns, static (`Type::method`) and bound (`object::method`) method references, calls through function-typed fields and chained calls on returned function values, and `@FunctionalInterface` targets lowered to function values with generic substitution and interface-method call forms (`Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction` from `lib/standard/utility.coo`). Not yet: nominal functional-interface values/class implementations, unbound instance method references, function-pointer thunk conversion, cross-thread closure escape, `this` lifetime enforcement.

#### 4.3.3 Control Flow and Pattern Matching

```coo
integer main() {
    integer total = 0;
    for (integer i = 0; i < 5; i = i + 1) {
        total = total + i;
    }

    Array<integer> queue = [1, 2, 3];
    for value in queue {
        total = total + value;
    }

    switch (total) {
        case 15: { print("fifteen"); }
        default: { print("other"); }
    }
    return 0;
}
```

- `switch` supports integer/string/character/boolean/enumerate; every branch has its own scope, there is no fallthrough, `default` must be last, the condition is evaluated once, and the backend emits an if/else chain.
- `if` expressions produce a value: `if (condition) a else b` with expression branches and chained `else if`; the condition must be `boolean` and both branches must be assignable to a common type. Block branches are ⏳; use an `if` statement for side effects.
- `match` supports enumerate variants (`Color.Red`), integer/string/character/boolean constants, the `_` wildcard, bindings (`x`), inclusive/exclusive ranges (`1..10`, `1..=10`), or-patterns (`1 | 2 | 3`) and guards (`x if x > 0`). Branches use `pattern => body` (block or expression); the legacy `case pattern:` form is accepted for compatibility.
- Exhaustiveness is checked: an enumerate must cover every variant unless a wildcard/binding is present; a `boolean` must cover `true` and `false` unless a wildcard is present; every other scalar match requires `_`. Missing coverage reports `E2077`, an unknown enumerate variant `E2024`, and a binding over a non-scalar value `E2077`. Bindings are copies visible in the guard and body. Type patterns and class destructuring patterns are ⏳.
- `for`, `for ... in`, `break`, `continue` and `return` are implemented; `for ... in` desugars to an `iterator()` + `next()` loop.
- `if let` has been removed; `Result`/`Expect` are handled explicitly with `isOk()` + `unwrap()` or `match`.

#### 4.3.4 Thread

`Thread` is a built-in move-only type lowered to `std::thread`:

```coo
Thread worker = Thread([](): void -> { print("worker"); });
print(worker.joinable());
worker.join();
print(hardwareConcurrency() > 0);
```

- Construction: `Thread(entry)` where `entry` has type `() -> void`. The entry may be a lambda, a capture-free function value or a method reference.
- Cross-thread captures: the entry closure must **own** everything it uses. `[]`, `[=]`, `[x]` and copies of `@Share` values are allowed; `[&]`, `[&x]`, `[this]` and implicit `this` use are rejected with `E2090`, as are function values whose lifetime is call-scoped (a function-typed parameter or a by-reference closure).
- Methods: `join()`, `detach()`, `joinable()`; `hardwareConcurrency()` returns an `integer` (the host's hardware thread count).
- Lifetime: `Thread` is move-only and cannot be copied or assigned (`E2090`). A joinable thread must be `join()`ed or `detach()`ed before it is destroyed; the generated `std::thread` follows the C++ rule and terminates the program otherwise. `Thread` is local-only in v0: fields, parameters and returns of type `Thread` are not supported yet.
- Safety boundary (v0): the compiler checks capture ownership, not shared memory. Sharing mutable state through pointers, `@Unique` handles or by-reference captures is not data-race safe; only immutable copies and `@Share` reference counting are promised. Synchronization primitives (`Mutex`/`Lock`, atomics) and scoped thread APIs are ⏳.

#### 4.3.5 Mutex

`Mutex` is a built-in non-copyable type lowered to `std::mutex`. v0 uses **manual** `lock()`/`unlock()`; there is no RAII guard yet:

```coo
class Counter {
public:
    Mutex mutex;
    integer value;

    void add(integer amount) {
        mutex.lock();
        value = value + amount;
        mutex.unlock();
    }
}
```

- Methods: `lock()`, `unlock()`, `tryLock()` returning `boolean` (lowered to `std::mutex::try_lock()`).
- `Mutex` values cannot be copied or assigned (`E2091`); declare a local or a field in place. A class with a `Mutex` field is non-copyable, so share the object through `@Share` instead of copying it by value.
- Boundary (v0): locking is manual; there is no `Lock` guard/RAII, no scoped-lock helper and **no static data-race analysis**. Forgetting `unlock()` blocks other threads, and locking the same mutex twice on one thread is undefined. `Mutex` supports local variables and class fields; parameters/returns are ⏳.
- Threads may capture `@Share` values by copy; the mutex then protects the fields that the locked methods touch. Only that manual discipline is promised.

#### 4.3.6 ScopedLock, Atomic and `@Lock`

Three complementary synchronization tools share the same v0 boundary: no data-race static analysis, fixed sequential consistency and no timeout/try-lock policies.

- **`ScopedLock`**: built-in RAII type lowered to `std::lock_guard<std::mutex>`, constructed from a named `Mutex` lvalue: `ScopedLock guard = ScopedLock(mutex);`. Entering the declaration locks, leaving the scope unlocks (including `return`/`break`/`continue`). It cannot be copied or assigned (`E2092`), must be initialized in place, and is local-only (fields/parameters/returns are rejected).
- **`Atomic`**: built-in `std::atomic<std::int64_t>` constructed from an initial `integer`: `Atomic count = Atomic(0);`. Supports `load()` returning `integer`, `store(integer)`, `fetchAdd(integer)`/`fetchSub(integer)` (return the value before the operation), `exchange(integer)` (returns the previous value) and `compareExchange(expected, desired)` returning `boolean` (true when the value equalled `expected` and was replaced by `desired`; `expected` is passed by value and is not updated). All operations are **seq_cst**; there are no memory-order parameters. `Atomic` is non-copyable (`E2092`) and may be a local or a class field; parameters/returns are ⏳. There is no generic `Atomic<T>`, no floating-point/pointer atomics and no `condition_variable` yet.
- **`@Lock(Exclusive, mutex, [captures]) { ... }`**: block annotation with a capture-capability list. The first argument is the literal mode `Exclusive` (other modes are ⏳), the second a named `Mutex` lvalue (`Name`/`Member`), and the optional third a capture list. Captures are **capability declarations**, not references:
  - `[x]` copies a value; only primitives, strings and enumerates are allowed. Class/container/pointer value captures are rejected (`E2094`) because a shallow copy would share `@Share`/pointer fields; there is no `@Copy` yet. Writes to `[x]` change the block-local copy only.
  - `[&x]` is a mutable borrow: field/index writes and mutating method calls are allowed.
  - `[const&x]` is a read-only borrow: writes report `E2094`, and only methods annotated `@ReadOnly` may be called on it; unannotated or mutating methods under a `const&` capture report `E2094`. Only direct calls are analyzed (no alias analysis); a method reached through another expression is not checked.
  - Writing an outer object that is not captured reports `E2094`. Locals declared **inside** the block are writable normally. A method call on an **uncaptured** class object is conservatively rejected unless the method is annotated `@ReadOnly`. Global side effects of methods called through `[&x]` are not analyzed in v0 (only direct writes/calls are checked).
  - Captures are scope-bound: returning a `[&x]`/`[const&x]` borrow, storing a capture into a field/container, or capturing it in a nested closure reports `E2039`. A `[x]` value capture is an owned copy and may be returned. Nested `@Lock` blocks are rejected (`E2094`) until lock modes define nesting semantics.
  - The legacy `@Lock(mutex)` syntax reports `E2094`; migrate to `@Lock(Exclusive, mutex, [captures])`.
  - Lowering: `{ std::lock_guard<std::mutex> __lock_N(mutex); <value-capture copies>; ... }`; the lock is released on every exit path. `@Lock` is only valid on statement blocks (`@Target(block)`). There is no timeout, try-lock, reentrancy or nested-lock detection: locking the same mutex twice deadlocks. The write-set checks are intentionally local and do not prove data-race freedom.

```coo
void add(integer amount) {
    @Lock(Exclusive, mutex, [&value]) {
        value = value + amount;
    }
}
```

#### 4.3.7 ScopedThread

`ScopedThread` is a built-in move-only type lowered to `std::jthread`; it uses the same cross-thread capture rules and diagnostics as `Thread`:

```coo
{
    ScopedThread worker = ScopedThread([counter](): void -> { counter.add(1); });
    print(worker.joinable());
}   // destructor joins automatically
```

- Construction: `ScopedThread(entry)` with `entry : () -> void`; lambdas, capture-free function values and method references are accepted.
- Captures: `[]`, `[=]`, `[x]` and `@Share` copies only; `[&]`, `[&x]`, `[this]` and implicit `this` are rejected with `E2090`.
- Methods: `join()`, `detach()`, `joinable()`. The destructor **joins automatically** when the thread is still joinable, so leaving the declaring block waits for the thread to finish.
- `ScopedThread` cannot be copied or assigned (`E2090`) and is local-only (fields/parameters/returns are not supported yet).
- Boundary (v0): there is no `stop_token`/`requestStop`, no cancellation and no exception propagation across threads; exceptions escaping a thread entry terminate the program. The same data-race caveats as `Thread` apply.

#### 4.3.8 `@Copy` and value copies

Classes containing `@Unique`, `@Share` or raw-pointer fields are **not implicitly copyable**; copying requires an explicit class-level `@Copy(Deep)` or `@Copy(Shallow)`, otherwise `E2095` reports at the copy site. Pure value classes keep implicit copies for compatibility.

- `@Copy(Deep)`: allowed fields are value fields and `@Unique` chains whose pointee is itself value-copyable or `@Copy(Deep)`. It rejects `@Share`, raw pointers/references, `@Copy(Shallow)` fields, thread/mutex/atomic fields and cycles. The backend generates a copy constructor and copy assignment that clone each `@Unique` pointee (`std::make_unique<T>(*other.field)`). Deep-copyability is checked recursively (with memo/cycle detection) across value fields, `@Unique` chains and base classes; a nested field that is not deep-copyable reports `E2095`.
- `@Copy(Shallow)`: member-wise copy; `@Unique` fields are rejected, `@Share` is copied by reference count and raw pointers are copied as pointers (dereferencing remains unsafe).
- A lock `[x]` value capture of a class requires `@Copy(Deep)`; `@Copy(Shallow)` and implicit classes are rejected because the snapshot would share state.
- `@Copy` only affects copying; it does not make a class usable across threads or provide `unsafe` capabilities.

#### 4.3.9 `@Safe` / `@Unsafe`

Code is safe by default. `@Unsafe` may annotate a module (file directive `@Unsafe;`), class, function, method, constructor or statement block, and propagates downwards; `@Safe` overrides it. `@Unsafe` only unlocks raw `T*`/`const T*` dereference, pointer arithmetic, `@Native`/FFI calls and unsafe casts — it does not disable type, borrow, bounds or lifetime checks. Calling an `@Unsafe`/`@Native` declaration (or `@Native` method) from a safe scope reports `E2096`; a `@Safe` function may open an explicit `@Unsafe { ... }` block as an audit boundary. v0 enforces raw-pointer dereference and FFI calls first; pointer arithmetic and unsafe casts are documented but not yet fully enforced.

### 4.4 Inheritance and Access

| Rule | Semantics | Status |
| --- | --- | --- |
| Number of bases | Single inheritance; multiple interfaces | ✅ |
| Access sections | `public:` / `private:` / `protected:`, default `private` (interfaces default `public`) | ✅ |
| Access checking | Out-of-bounds field/method/constructor access reports `E2030`; `protected` permits access within the base chain | ✅ |
| Same-name fields | Hiding: both copies exist and resolve by declaring Scope; a `final` field cannot be hidden (`E2032`) | ✅ |
| Private fields | Participate in layout; invisible to subclasses, which may declare a same-name private field | ✅ |
| Method overriding | Parent `virtual`, child `override`; missing either reports `E2033`/`E2035`; private methods can be overridden but not called directly | ✅ |
| `final` | Class cannot be inherited (`E2031`); method cannot be overridden (`E2032`) | ✅ |
| Overload sets | Merge across inheritance (resolved by name + parameter count) | ✅ |
| Return types | Covariant (return a subtype), otherwise `E2034` | ✅ |
| Layout | Base subobject first, then fields in declaration order; private fields included | ✅ |
| Destructor | Automatically virtual when virtual methods exist (or an interface is implemented) | ✅ |
| Explicit base access | `super.method()`, `super.field`, `: super(args)` in constructors | ✅ |
| Constructor initializer list | `: field(expr)` with initialization-order checks | ✅ |
| Virtual dispatch through base refs | End-to-end tests for `Base&`/`Base*` calling virtual methods | ✅ |
| Default ctor/dtor access checks | Default constructor `E2030`; default destructor checked | ✅ |

`this` has the static type of the current class; calls through `Base&`/`Base*` dispatch on the dynamic type. Interface implementation checks: every interface method must be implemented unless it has a default; a missing implementation reports `E2010`, and an implementation without `override` reports `E2035`.

### 4.5 Object Lifetime and RAII

These are hard language semantics that reuse C++ RAII.

- **Construction order** (fixed): base subobject → members in **declaration order** (using initializers when present) → constructor body. Constructors **cannot fail**; resource-acquisition failure uses a `Result`-returning factory (⏳).
- **Code generation**: aggregate initialization emits `A a{...}`; `A a()` emits `A a = A()` to avoid C++'s most vexing parse; `A a = b` delegates to a C++ converting constructor.
- **Destruction order** (fixed): user destructor body (if any) → members in **reverse declaration order** → base subobject. Each class has exactly one destructor: user-declared or compiler-synthesized.
- **Synthesized destructor**: when no destructor is declared, the compiler synthesizes one that destroys members in reverse declaration order and finally `destroy(base)`. The backend emits `~T() = default;` with a comment documenting the order; C++ guarantees the same order.

**User destructors and `~this.member;`**:

```coo
class Holder {
    A a;
    B b;
    ~Holder() {
        ~this.b;
        ~this.a;
    }
}
```

- For every user-defined class-typed field, `~this.member;` must be written; omitting one warns `W0001` (error under `-Werror`).
- Primitive fields (`integer`/`decimal`/`character`/`boolean`/`string`/pointers) must not be explicitly destroyed; doing so warns `W0002`.
- Repeating a member's destruction declaration is allowed but **only the last one takes effect** (exactly once), with warning `W0003`.
- `~this.member;` is a **destruction declaration**, not a second destruction: the C++ backend erases it (comment only), and C++ still destroys the member after the destructor body in reverse order, preventing double destruction.
- Base destruction is the compiler's job and is never written in a user destructor; `~this.member;` may appear only inside a destructor body and must name a field of the current class (otherwise `E2008`).

#### 4.5.1 Ownership, Scope Ownership, `@Unique` / `@Share`

Dangling fundamentally means a **reference's scope outlives the referenced object's owner Scope**. Coordination therefore uses static Scope containment plus two built-in ownership annotations instead of a reference stack:

- Every object has one **owner Scope**; every `&` has a **borrow Scope**; legality requires the borrow Scope to be nested inside the owner Scope.
- When the owner Scope exits, owned objects are destroyed in reverse declaration order; no detach, extension or counting.

1. **`@Unique` / `@Share` are built-in type annotations** (`@Target(type)`), written in type positions for locals, fields, parameters and returns. The static type remains `T`; the annotation determines storage/ownership/ABI. User annotations stay read-only and append-only; these built-ins, like `@Native`/`@Result`, may affect lowering.
2. **`@Unique T`**: unique ownership, heap storage, move-only. Released when the scope/field dies; non-null by default, nullable as `@Optional @Unique T`. Member access auto-dereferences; `@Unique T y = x` moves (x becomes invalid, `E2045`) and copying is forbidden. Provides Java-Optional-style `isPresent()` / `isEmpty()` / `get()`; `get()` panics when empty; `isPresent()` is false after a move.
3. **`@Share T`**: shared ownership, heap control block, atomic count, copyable. Copy +1, destroy -1; the last reference destroys the object; non-null by default, nullable as `@Optional @Share T`; may cross threads. Also provides `isPresent()` / `isEmpty()` / `get()`.
4. **Plain `T`**: value/stack semantics; fields die with their object; no control block.
5. **`&` borrow**: owns nothing, counts nothing, extends nothing; valid only within the current function/Block. `&` must not be returned, stored in fields/containers, passed across threads, or captured by a possibly-escaping closure; violation reports `E2039`. A `&` field or a container element type such as `Array<T&>` is a hard compile error. Currently `return &x`, `T&` fields, reference container elements and by-reference closure escapes (return/field/container literal/outer-scope variable) are checked; cross-thread checks remain. `@Unique x; x = x;` reports `E2045`; direct self-cycles through `@Share` fields emit `W0005`.
6. **Borrow sources**: `&` may borrow plain values, `@Unique` and `@Share`; borrowing does not change the owner; a moved `@Unique` cannot be borrowed (`E2045`).
7. **Parameters/returns**: a `@Unique` parameter transfers ownership by value; a `@Share` parameter increments the count; a `&` parameter only borrows and the callee must not store it. Returning `@Unique`/`@Share` transfers/shares ownership; returning `&` is forbidden.
8. **`@Unique` → `@Share`**: `@Share T r = box;` moves the `@Unique` into a shared control block and invalidates `box`; the reverse is forbidden.
9. **`const T&` to a temporary**: the temporary's lifetime extends to the end of the reference variable's Scope; owner rules are unchanged.
10. **Cycles**: only `@Share` can form cycles; reference counting cannot reclaim them. The type graph warns `W0005` for class-level `@Share` cycles (including self-recursive types), direct self-assignment warns `W0005`, and the runtime records `@Share` field assignments in a small object graph; at shutdown it DFS-traverses the live graph and reports a cycle (`W0005`) or, for leftover live objects without a detected cycle, a possible leak. Constructor bodies and initializer lists record edges; in-class field initializers and aliasing through arbitrary expressions may still be missing, so runtime traversal is partial. `&` cannot be stored and is inherently acyclic.
11. **Statics/globals**: lifetime is the process; they may be borrowed by `&`, but the borrow still must not escape.
12. **Conservative checking (v0)**: `&` is used only within the current function — not returned, stored or passed across threads; no full lifetime parameterization.
13. **`@Weak T`**: non-owning `std::weak_ptr<T>` view of an `@Share T` owner; default empty. It can be initialized/assigned from `@Share` or another `@Weak` value. `isPresent()`/`isEmpty()` test expiry; `lock()`/`upgrade()` return a nullable `@Share T` handle that supports `isPresent()/get()/orElse()`. Accessing `get()` after expiry aborts.

C++ backend mapping:

| Coordination | C++ backend |
| --- | --- |
| `@Unique T` | `std::unique_ptr<T>` (move-only, exclusive) |
| `@Share T` | `std::shared_ptr<T>` (copyable, atomic count) |
| `Weak<T>` | `std::weak_ptr<T>` (`isPresent()` = `!expired()`; `upgrade()` returns nullable `@Share`) |
| `@Optional @Unique T` | nullable `std::unique_ptr<T>` + Optional proxy/null checks |
| `@Optional @Share T` | nullable `std::shared_ptr<T>` + Optional proxy/null checks |
| `@Weak T` | `std::weak_ptr<T>`; `isPresent()/isEmpty()`, `lock()/upgrade()` returns nullable `@Share T` |
| `T` | `T` value |
| `T*` | raw pointer (`unsafe`) |

JPA cascade mapping:

| JPA cascade | Coordination |
| --- | --- |
| `persist` / `merge` | Copy/move the value into `@Share`; the owner becomes the shared control block |
| `detach` | The `&` borrow ends with its Scope (target lifetime unaffected) |
| `remove` / `orphanRemoval` | `@Unique` dies with the owner Scope; the last `@Share` release destroys the shared object |
| cascade to children | Fields die in reverse declaration order; `@Unique`/`@Share` fields recursively release |

Safe code cannot dangle (`&` cannot escape, owner covers borrow, `@Share` counts correctly); `T*`, FFI and pointer arithmetic remain unsafe and are the user's responsibility. Open: exact rules for `@Optional @Unique` / `@Optional @Share`, `@Unique` move details, merging `@Share` and `Shared<T>` APIs, cycle-detection algorithm/cost, and destructor-reentrancy diagnostics.

#### 4.5.2 ProxyPolicy

`ProxyPolicy` is shared by **every proxy object**; Optional is one proxy kind. It applies when a proxy value is used directly without narrowing or other handling: dereference, member access, implicit conversion to a non-proxy value, construction or destructuring. Policy names are case-insensitive.

| Policy | Direct/absent use | Diagnostic |
| --- | --- | --- |
| `Error` | Not allowed without narrowing/handling; no fallback path is generated | compile-time `E2048` |
| `Skip` | Continue with the type's default value/default bindings; skip the actual read or handler | none |
| `Abort` | Terminate at runtime, printing the source location | runtime panic |
| `Default(expr)` | Use `expr` as the fallback; it must be assignable to the target type/pointee | `E2005` on type mismatch |

- `@Optional` is a `@Target(type)` annotation applied to the types `T*`, `@Unique T` and `@Share T` (for example `@Optional integer*`, `@Optional @Unique Box`). In v0 a plain `@Optional Class` is for descriptor scenarios only and **does not promise runtime nullability**. Its default policy is `Error`.
- **v0 coverage**: policy execution is verified for **local variables** of type `T*`/`@Unique`/`@Share` (direct dereference, member access, implicit conversion to non-null, and local Optional destructuring), for **parameters and fields**, for narrowing, for `@Proxy(handler, policy)` `Skip`/`Abort`/`Error` destructuring paths, and for static/runtime `@Construct`/`@Destructure` hooks. `@Optional` on fields/parameters/returns is parsed and validated, but full policy execution is partial 🚧. `@Proxy` `Default(expr)` construction/destructuring, runtime-handler pre-construction hooks, and `Skip` defaults for arbitrary non-default-constructible classes also remain 🚧.
- `@Proxy(handler, policy)` configures the same four policies for a proxy object; without a policy argument the default is `Skip`. `@Proxy` policy also governs construction and destructuring paths (§4.8.1, §4.8.2).
- **Direct access** for an Optional means `*p`, `p.field`, `p.method()`, and assigning an optional to a non-optional variable/parameter (implicit conversion to non-null).
- **Narrowing**: inside the corresponding branch of `if (p != null)` or `if (p.isPresent())`, `p` is treated as narrowed. The **else branch** of `if (p == null)` / `if (p.isEmpty())` is narrowed likewise. Narrowed access is not restricted by the `Error` policy.
- `Skip`'s default value is constructed per target type: numeric 0, `boolean` false, empty `string`, `char` U+0000, a default-constructed user class. Unavailable default construction is a compile error. For destructuring, `Skip` binds default values without reading target fields; for construction it skips the absent hook.
- `Default(expr)` evaluates `expr` only on the absent/null path; the present path never reads it.
- `.get()` is the explicit panic exit for Optional and is unaffected by policy: on null it terminates and prints `optional value is null` plus the source location.
- `.orElse(x)` is always safe: null returns `x`, present returns the target; `x` must be assignable to the target/pointee type.
- Non-optional → optional is a safe widening. Under `Error`, optional → non-optional is forbidden without narrowing, `orElse` or explicit `get()`; under `Skip`/`Abort`/`Default(expr)`, implicit materialization to a non-optional value applies the policy (`Skip` uses the default value, `Abort` checks at runtime, `Default` uses the fallback). Some implicit-conversion boundaries remain 🚧 (`coord_e2e_optional_implicit` covers the basic Skip/Default cases).
- `@Optional` does not solve dangling: `@Optional T*` lifetime remains unsafe; it expresses null state only.

#### 4.5.3 Optional Proxies, `@Destructure` and Destructuring

Lowering generates an internal **proxy layer** for `@Optional`: member access, calls, construction and destructuring pass through it before reaching the target. The Optional proxy is not a user-visible type and does not change target layout or ABI.

- v0 mainly covers **local variables**. Fields/parameters/returns whose type carries `@Optional` are parsed and validated, but full policy/proxy execution is partial 🚧; expressions returning Optional are treated as proxies where implemented. If the target itself is a `@Proxy`, **Optional is outer and the user handler inner**: absent paths never invoke user handlers; only present paths expand `@Call`/`@Get`/`@Set`/`@Construct`/`@Destructure`.
- `ProxyPolicy` (§4.5.2) takes effect at the Optional proxy layer **before** any user `@Destructure` method or trap.
- Destructuring (`{T a, U b} = value;`) supports Optional proxies; on null:
  - `Error`: compile-time `E2048` when not narrowed;
  - `Skip`: binds defaults, reads no target fields and calls no user traps;
  - `Abort`: terminates at runtime and prints the source location;
  - `Default(expr)`: destructures `expr`.
- When the target class defines an `@Destructure` method, destructuring first declares default-initialized bindings and then calls that method with references to them; the method writes the outputs. Otherwise each binding is read from the corresponding field (through the proxy `@Destructure` trap when present). See §4.8.1.
- When present, the Optional proxy forwards to the target; field reads/method calls follow normal rules. Narrowing affects static checking only and adds no runtime branch.

### 4.6 Error Model

The `@Result<T>` C++ `Result<T>` wrapper, success wrapping, statement-level automatic bubbling, `error("...")`, `@Result main` and error propagation are implemented. `@Expect` has an independent `Expect<T>` type identity, `isOk/isErr/unwrap/expect`, assignment/printing/discard checks, and explicit consumption with `isOk()` + `unwrap()`. `if let` has been removed. `match` as a future control-flow enhancement (⏳) no longer blocks P3.

- Every function/method's declared return type `T` is the **success type**; the internal effective return type is `Result<T, Error>`, hidden from developers. When a caller does not handle a `Result`-returning call, the error **bubbles automatically** and the caller's effective return type also carries `Error`.
- There is no `?`, `try`, `throw` or `catch`, and no runtime exceptions; bubbling is an early return, and locals destruct in reverse on Scope exit.
- `@Result<T>` and `@Expect<T>` attach to functions/methods, **must carry the success type `T`**, and `T` must equal the declared return type (`E2073` otherwise); the annotation does not change the declared return type.

| Call site | Sees | Auto-bubble | Consumption |
| --- | --- | --- | --- |
| Unannotated fallible function | plain `T` (hidden `Result`) | yes | no |
| `@Result<T>` function | `Result<T, Error>` | no; caller handles explicitly | no; may be passed/discarded |
| `@Expect<T>` function | `Expect<T, Error>` | no | must be consumed in this Scope |

- `@Result<T>` disables auto-bubbling at that call site: the caller must use `match`/`isOk()` + `unwrap()`/`return`, or explicitly convert to `Result`.
- `@Expect<T>` yields an independent must-use type `Expect<T, Error>` (convertible to `Result`); it cannot be stored in fields/containers or consumed lazily across functions; leaving the Scope unconsumed is `E2055`.
- Consumption: `match`, `isOk()` + `unwrap()`, `expect`, or `return`.
- `Error` is a global interface: `code`, `message`, `source`, `backtrace`; errors can wrap lower-level errors; the default error type is `Error`.
- Entry points: `integer main()`, `integer main(Array<string>)` or `integer main(integer argc, Array<string> args)`; `void main` and other parameter forms report `E2052`. Unhandled errors are printed by the runtime with a nonzero exit code.

### 4.7 Annotations, Meta-Annotations and Retention

Annotation targets: `class`, `interface`, `annotation`, `function`, `method`, `field`, `variable`, `parameter`, `module`, `enumerate`, `enum_value`, `type`, `constructor`, `block`. Applications run in source order; processors for one target run in annotation application order.

- `type` targets a type expression: `@Optional`, `@Unique`, `@Share`, `@Signed`, `@Unsigned`, `@Length`, `@Radix` and `@Weak` are type annotations, so the annotation is attached to the type at a local/field/parameter/return position, not to the declaration target names above.
- `constructor` targets a constructor declaration: `@Constructure` marks a constructor of the target class for typed/named brace construction.
- `block` targets a statement-level `{ ... }` block for `@Retention(Compile)` / `@Retention(Runtime)` execution control.
- The other names keep their declaration semantics (`@Result`/`@Expect` on functions/methods, `@Destructure` on methods, `@Retention` on annotations/enums, and so on). `@Target` validation accepts `type`, `constructor` and `block`.

**Read-only, append-only**:

- Application never overwrites, replaces or modifies earlier annotations; multiple annotations coexist.
- A processor cannot read, modify or overwrite other annotations on the target; it only sees a `description` snapshot and emits new declarations.
- A processor cannot modify the target's structure (fields, methods, layout, visibility); generating `toJson`-style code means generating standalone functions/classes, never adding members to the target.
- Generated entities enter normal Sema; names are compiler-assigned (predictable, unique) and references resolve by description identity rather than string concatenation. Generated declarations may be seen by later processor rounds (until a fixpoint, with a round limit).

**Single directive primitive**:

```coo
line(file: string, line: integer);
```

- Available only in annotation processors (compile-time contexts); calling it in an ordinary function/method body is a compile error.
- Semantics bind to the **annotated declaration/block**: the processor sets the generated C++ `#line` mapping for that target and restores the previous mapping on exit.
- Multiple `line` calls for one target: the last wins; different targets never overwrite each other.
- Only this primitive exists; no `#warning`, `#error`, `#pragma`, no new directive names, and no arbitrary `#...` text.
- Diagnostic coordinates still use the original `.coo` Span; `#line` only affects generated-C++ debug/diagnostic locations.

**Meta-annotations** (built-in only; users cannot define new ones or change semantics):

| Meta-annotation | Purpose | Violation |
| --- | --- | --- |
| `@Inherit` | Propagate along class → subclass, interface → implementor, virtual method → override | Applied to a non-annotation: error |
| `@Repeatable` | Allow repeated application on one target; queries return a list | Repetition without it: error |
| `@Target(...)` | Restrict applicable targets (the set above) | Mismatched target: error |
| `@Meta` | Marks "this is a meta-annotation"; built-ins only | User application: error |
| `@Retention(stage)` | Retention stage: bare/`runtime`/`invocation` on annotations; `enum` on enumerations | Target/stage mismatch: error |
| `@Documented` | Include the annotation in docs/IDE hints (no compile semantics) | Applied to a non-annotation: error |

#### 4.7.1 Meta-Annotations and Processor Architecture

- **description** is the language-level, user-visible read-only snapshot (name, kind, annotation values, members). **descriptor** is the compiler's internal semantic object (targets/repeatable/inherit/retention/processor/intrinsic), frozen after finalization and never serialized.
- Meta-annotations bootstrap into `AnnotationDescriptor` at C++ startup; MetaProcessor mutates only `DescriptorBuilder`, never description.
- `AnnotationKind`: `Meta` (6), `Processor` (can generate declarations via a `@Processor` method), `Intrinsic` (lowering hooks such as `@Result`/`@Unsigned`/`@Native`).
- Ordinary processors attach with a `@Processor` method inside the annotation declaration:

```coo
annotation Serializable {
    @Processor
    void generate(Description target) {
        addFunction("serialize" + target.name);
    }
}
```

- Fixed five phases: Meta finalization → application validation → `@Inherit` expansion → ordinary processors in application order → Retention materialization. Ordinary processors only receive description and cannot modify target structure; `@Processor` and the description API are P4 (parsing and basic execution implemented).
- Processors run before Sema, bind description, support locals/if/description queries/`line`, and generate declarations via `addFunction`/`addFunctionReturning`/`addConstant`/`addAnnotatedFunction`; generated declarations participate in the next round with an 8-round default limit and `E2068` on overflow.
- **Intrinsic visibility**: users may **apply** standard-library Intrinsics (`@Result`, `@Expect`, `@Unsigned`, `@Signed`, `@Length`, `@Native`, `@Unique`, `@Share`, `@Optional`, `@Proxy`, `@Invocation`, etc.) and are subject to target/repeatability/Retention validation; users **cannot define** new Intrinsics or give their own annotations lowering/ABI hooks. The intrinsic-name-to-C++-hook table is closed.

`@Inherit` resolution (consistent with "annotations cannot override"): collect **direct** annotations on the target in source order; for annotation types carrying `@Inherit`, also collect along inheritance (class → base chain → interfaces; interface → parent interfaces; method → overridden virtual chain). **Direct annotations win**; an inherited copy is not added when the same type is already directly present. Different types coexist; non-`@Repeatable` types keep only the nearest copy (ties by declaration order). `effective_annotations()` returns a read-only direct+inherited view marking `is_inherited`; the direct annotation list on the source/`description` is never modified.

`@Retention` is the **retention stage** meta-annotation:

| Form | Applies to | Stage | Meaning |
| --- | --- | --- | --- |
| omitted | — | Compile | Compile-time only, zero runtime footprint |
| `@Retention` / `@Retention(runtime)` | `annotation` | Runtime | Read-only side table + generated access logic |
| `@Retention(invocation)` | `annotation` | Invocation | Everything in Runtime + per-call value interception |
| `@Retention(enum)` | `enumerate` | Enum | Runtime + enum variant `(name, value, ordinal)` table |
| `@Retention(Compile)` | `block` | Compile | Block is executed by the interpreter at compile time; the C++ backend omits it |
| `@Retention(Runtime)` | `block` | Runtime | Block is not executed at compile time; it is generated and runs normally |

**Block retention**: a statement-level block may be annotated as `@Retention(Compile) { ... }` or `@Retention(Runtime) { ... }`.

- `@Retention(Compile)` blocks execute during compile-time evaluation. Their `print` output goes to the compile-time output (inspect it with `coorc --print-consts`) and never enters the generated program; the C++ backend omits the block.
- `@Retention(Runtime)` blocks are not executed at compile time; they are emitted and executed normally at runtime.
- Compile-time blocks must not depend on runtime local state; only constants and pure functions are recommended. `@Retention(Invocation)` or `@Retention(enum)` on a block reports `E2044`.
- `@Target` accepts `block` as a target name.
- Stages progress Compile → Runtime → Invocation; Invocation implies Runtime, and stages never regress. Enum is the enum-specific branch of Runtime and applies only to `enumerate`.
- Only declarations, annotations/enums or code blocks carrying `@Retention` receive the corresponding treatment; unmarked ones stay compile-time with zero runtime footprint. Runtime/Enum/Invocation materialization produces **side read-only data + generation logic** and never changes layout, field order or ABI; Compile blocks execute at compile time and are omitted from generated C++.
- Retained annotations may only annotate declarations with stable identity: class/interface/annotation/function/method/field/module/enum; `@Retention` on locals/parameters is an error (statement-level blocks are the explicit exception). Effective annotations from `@Inherit` are computed at compile time and materialized together.
- Runtime exposes only a narrow "query retained annotation/enum variants by declaration id" interface, never arbitrary member enumeration or string-based invocation.

### 4.8 Proxy

`@Proxy(Handler)` or `@Proxy(Handler, Policy)` binds a handler's traps to a target. The handler is an annotation or class and must implement the standard marker interface `ProxyHandler` or one of its subinterfaces (missing reports `E2074`); unimplemented traps fall back to direct forwarding.

| Trap | Coordination annotation | Trigger | Default |
| --- | --- | --- | --- |
| `get` | `@Get` | Read a field/property | direct read |
| `set` | `@Set` | Write a field/property | direct write |
| `has` | `@Has` | Member existence query | static member check |
| `deleteProperty` | `@Delete` | Delete/invalidate | compile error or no-op |
| `ownKeys` | `@Keys` | Enumerate members | declared members |
| `getOwnPropertyDescriptor` | `@Describe` | Member descriptor | returns `description` |
| `apply` | `@Call` | Call a function/method | direct call |
| `construct` | `@Construct` | Before constructing an object | direct construction |
| `destructure` | `@Destructure` | Before destructuring a proxy object | ordinary per-field reads |

`@Constructure` is not a trap: it is a `@Target(constructor)` annotation that enables typed/named brace construction (§4.8.2).

```coo
@Target(method)
annotation LogHandler implements ProxyHandler {
    @Get
    void get(string name) { print(name); }
}

@Proxy(LogHandler)
class Service { ... }
```

- In a static language property names are known at compile time, so trap parameters are `description` or an optional `string` name, never runtime string lookup.
- A proxy changes neither the target's type, member set, layout nor ABI; `@Proxy` is a call-site rewrite, not a new type or structural mutation.
- Expansion targets: `class` (construct wraps `@Construct`, member access wraps `@Get`/`@Set`, method calls wrap `@Call`); `function` (every call site wraps `@Call`); `method` (entry/exit traps, virtual calls expand by dynamic type); `variable` (each read/write becomes `get`/`set`).
- Virtual dispatch and interface calls are covered; traps expand where the static type is known. Failed proxy resolution (signature mismatch, unproxyable target) is a compile error. Proxies stack; expansion order is annotation inside-out with explicit `@Proxy` first.
- Three modes: handler without `@Retention` → compile-time expansion, zero overhead, no proxy object; `@Retention` → runtime proxy wrapper with runtime trap dispatch; `@Retention(invocation)` → runtime proxy whose traps may modify arguments/returns/fields.
- `@Proxy(handler)` uses `ProxyPolicy.Skip` by default; `@Proxy(handler, Error|Skip|Abort|Default(expr))` selects the policy for absent construction/destructuring paths. The same four policies are documented in §4.5.2.
- `ProxyHandler` is the marker base; `MethodProxyHandler`, `DestructureProxyHandler`, `ConstructureProxyHandler`, `ClassProxyHandler`, `ObjectProxyHandler` and `FunctionProxyHandler` extend it. `interface X extends ProxyHandler` is supported, and implementing any subinterface satisfies the `@Proxy` handler requirement.
- `@Construct` and `@Destructure` are **handler hooks**: they run before the default construction/destructuring behavior, which still happens. Hooks may validate, record, or modify values where the signature permits.
- Current progress: `@Proxy(Handler[, Policy])` validation, class-handler static `@Call/@Get/@Set`, annotation/runtime retention modes, `@Construct` before `@Constructure` construction, per-binding `@Destructure`, and the policy default `Skip` are implemented at a basic level. `@Has/@Delete/@Keys/@Describe` remain declared but not expanded; runtime-handler construct hooks and some implicit optional→non-optional boundaries are 🚧.

#### 4.8.1 `@Destructure` and Destructuring

`@Destructure` has two forms:

1. **ProxyHandler trap**: `@Target(method)` on a handler method. Destructuring a proxy object calls the handler once per bound field; static/runtime rules are the same as `@Get`/`@Set`.
2. **Target-class method**: `@Destructure void unpack(T& outA, U& outB)` on the class being destructured. The method receives one reference parameter per binding and writes the outputs.

```coo
class Point {
public:
    integer x;
    string y;

    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }

    @Destructure
    void unpack(integer& outX, string& outY) {
        outX = x + 1;
        outY = y + "!";
    }
}

integer main() {
    Point point(1, "a");
    {integer a, string b} = point;   // a == 2, b == "a!"
    return 0;
}
```

- For `{T a, U b} = obj;`: if the target class has a matching `@Destructure` method, the compiler first declares default-initialized bindings, calls the handler `@Destructure` hook per binding when the object is a proxy, and then calls the target method with references to the bindings; the user method writes the values. Otherwise each binding is filled from the corresponding field (through the proxy trap when present).
- A handler trap method takes a field name (`string`), which may be omitted; a runtime handler may additionally receive the target/context following the `@Get` runtime rules.
- The target method's parameter count must equal the binding count and its parameter types must accept the binding types; a non-static method is used. Missing/incompatible methods fall back to field reads.
- `ProxyPolicy` takes effect **before** user `@Destructure`: absent Optional paths call no handler (`Error` reports `E2048`, `Skip` binds defaults, `Abort` terminates, `Default` destructures the fallback).
- Destructuring reads only bound fields, never unbound ones; a missing/inaccessible field follows ordinary destructuring errors.
- `@Destructure` changes neither layout nor ABI and introduces no general runtime reflection.

#### 4.8.2 `@Constructure`

`@Constructure` is `@Target(constructor)` and may mark only a constructor of the target class. It enables typed/named brace construction; the constructor body remains user-defined and performs the actual conversion.

```coo
class Point {
public:
    integer x;
    string y;

    @Constructure
    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }
}

integer main() {
    integer a = 3;
    string b = "hi";

    Point p = {a, b};                  // already-typed variables
    Point q = {5, "ok"};               // positional literals
    Point r = {y = "named", x = 9};    // named arguments
    Point s = {integer a, string b};   // explicit labels for disambiguation
    return 0;
}
```

- Entry syntax is `[A] value`, `[A] name` or `[A] name = expr`; the type label `A` is optional. Without `=`, a bare `name` is both the value expression and the match name; with `=`, the entry matches a constructor parameter by name. Entries are comma-separated.
- Type labels are only required for disambiguation. Already-typed variables and literals are written directly (`{a, b}`, `{5, "ok"}`); explicit labels (`{integer a, string b}`) select among otherwise ambiguous constructors.
- Matching considers only constructors marked `@Constructure`: entry count must equal the parameter count; named entries match parameter names; unnamed entries match by position and value type; explicit type labels participate in assignability. No match or more than one viable candidate reports `E2076`.
- Typed/named construction coexists with ordinary positional `Type{1, 2}` construction and aggregate object literals (`{x: 1}`).
- When the target class is a `@Proxy`, the handler's `@Construct` hook runs before the constructor call; construction then proceeds with the user constructor (§4.8).
- `@Constructure` is declaration syntax only; it introduces no runtime reflection and does not change layout/ABI.

#### 4.8.3 `@Invocation`

`@Invocation` marks a **function, method or other callable** and wraps each call with before/after hooks:

```coo
class Audit implements InvocationHandler {
public:
    void before() { print("before"); }
    void after() { print("after"); }
}

@Invocation(Audit)
integer work() {
    print("work");
    return 1;
}
```

- The handler must implement the standard marker interface `InvocationHandler` (missing reports `E2075`).
- Handler shape: `before(...)` and/or `after(...)`; parameters are exposed by writable reference and the return value through a result slot.
- Compile-time expansion with no runtime object by default; when the handler or target carries `@Retention(invocation)`, runtime hooks are generated and may modify arguments/returns.
- `@Invocation` does not change the error model; it composes with `@Result`/`@Expect`, and errors still flow through Result/Expect.
- Multiple hooks run in annotation application order: before in order, after in reverse; hooks may not add/remove members or change signatures.
- Signature mismatch or a non-callable target is a compile error; hooks follow virtual dispatch when a base reference calls an override.

### 4.9 Modules and Packages

- One `.coo` file is one module; the path is the module name. Without a `module` declaration, the module name derives from the file path. Private by default; `export` exports declarations (types, functions, constants).
- `.coox` is the compiled module interface/metadata (public interface + reflection metadata) for incremental `import`; private declarations are not exported. Module initialization follows dependency topological order and forbids cycles.
- `coordination.toml`/`coord.pkg` describe the package, targets and dependencies; `coord.lock` pins resolution. The full protocol is §8.

```coo
module app.main;                        // optional
import {User, Order} from app.model;    // selective import
import app.model as m;                  // aliased import
export { User };                        // export list
export class File { ... }
```

- Supported: `import {a,b} from xx;`, `import xx as alias;` and `export {a,b};`; the old `import xx {a};` is removed. Single-file mode supports relative imports (resolved from the importing file's directory, each file loaded once, missing file `E2070`).
- `coorc build <dir>` enters project/package mode: `coord.pkg`'s `name/source/dependency` recursively collects dependency sources and reports cycle errors; without a manifest it recursively collects `.coo`. `coorc deps <package-dir>` prints dependency-first order. `.coox` is readable/writable; `--incremental` skips unchanged builds using `.coo`/root `coord.pkg`/`.coox` stamps.
- Limitation: aliased local declarations `l.Payload p = ...` are syntactically ambiguous today; parameter/return type positions are fully covered.

### 4.10 Memory, Concurrency and FFI

- Value semantics + references + RAII; no GC. `@Unique`, `@Share` atomic reference counting, and `new`/`delete` only inside `unsafe` blocks (⏳).
- Pointer dereference/arithmetic requires `unsafe`; references are non-null (check ⏳). The standard library provides threads, channels and atomics; the language has no built-in async/await (⏳).
- `extern "C++"` blocks declare headers, types and symbols; the compiler generates C++ boundary wrappers. The C ABI is a narrower subset (⏳ P5).
- Zero runtime reflection by default: no RTTI, no annotation table, no proxy objects; explicit `@Retention` materializes only retained annotation tables and generated logic.

## 5 Reflection Layers and the Class Model

### 5.1 Model

```text
source declarations
   │  compile-time snapshot
   ▼
description ──(descriptor processor)──► new declarations (appended to the module)
   │                    │
   │ erased query       │ typed query
   ▼                    ▼
reflection          Reflector<T>
```

| Layer | Definition | When it exists | Typical operations |
| --- | --- | --- | --- |
| `description` | Property descriptor for class/interface/enumerate/annotation/function/method/field/variable: kind, name, type, members, annotations, visibility, span, flags | compile time | read fields, walk members, compare, processor input |
| `descriptor` | Processor of property descriptors: takes one or more `description`s and emits declarations | compile time | `process(target)`, `generate()`, annotation processors |
| `reflection` | Erased reflection cursor: dynamic kind/name/member queries | compile time | `kind()`, `name()`, `members()`, `get("x")` |
| `Reflector<T>` | Typed reflection view with static checks for a known `T` | compile time | `Reflector<File>.fields`, `.methods`, `.annotations` |
| `proxy` | Behavior proxy: a trap set | compile time by default; runtime in retained mode | `get`, `set`, `has`, `call`, `construct`, `destructure` |

### 5.2 `description` and `descriptor`

```coo
description d = description_of(User);
d.kind; d.name; d.qualifiedName; d.type;
d.members; d.annotations; d.visibility; d.flags; d.span;

annotation Json {
    descriptor generate(Description target) { return declaration { ... }; }
}
```

- `description` is an immutable value: comparable, hashable and serializable into `.coox`; `description_of(x)` accepts a type, declaration or value, and runtime values are allowed only in compile-time contexts.
- A processor is a **compile-time function/method** taking `description` and producing new declarations; it may only **append** to the end of the current module, never replace/delete/rewrite existing declarations or mutate the target's structure.
- Processors run after the target passes Sema and before whole-module lowering; processors for one target run in annotation application order; rounds repeat to a fixpoint with a limit.
- Processors must not read IO/time/randomness; external input requires an explicit capability annotation and must enter the cache key; fuel/memory limits apply.

### 5.3 `reflection` and `Reflector<T>`

```coo
reflection r = reflect(value);                 // erased, compile time
Reflector<User> ru = Reflector<User>();        // typed, compile time
for (description field : ru.fields) { print(field.name); }
```

- General algorithms (serialization, ORM, test discovery, dependency injection) use `reflection`; business code and IDE-completion scenarios use `Reflector<T>`.
- `reflection` member access is dynamically checked at compile time (missing member is a compile error); `Reflector<T>` checks members statically, so a typo is a compile error.
- `Reflector<T>` queries fold to C++ constants with no runtime RTTI; an out-of-range index reports `E2069`; data comes from descriptor and is shared with outline/hint/runtime tables.

### 5.4 Retention Materialization

**Runtime**: the compiler emits a read-only side table (`declaration id -> annotation record`, in a read-only section) and narrow accessors for retained annotations; `@Inherit` expands to an effective list at compile time, so runtime never walks the inheritance chain; object layout, field order and vtable are unchanged; across modules `.coox` carries only retained metadata for public declarations.

**Enum**: `@Retention(enum)` emits an `enum type id -> [(name, value, ordinal)]` side table and narrow accessors by name/value/iteration; enum values remain the underlying integer and layout/ABI are unchanged.

**Invocation**: value-level per-call hooks on top of Runtime.

| Hook | Trigger | Mutable values |
| --- | --- | --- |
| before call | function/method entry | argument values |
| after call | function/method return | return value |
| field read | `get` | the read value |
| field write | `set` | the value about to be written |
| after construct | constructor return | fields of the constructed object |
| proxy trap | `@Proxy` call site | arguments/returns/field values |

- **Value changes only**: a new value must be assignable to the original type (subtypes allowed); the stored static type never changes. **No type/structure changes**: signatures, generic arguments, member sets, field order, vtables and ABI are fixed.
- A handler may keep runtime state (counters, caches) in side storage, never in target layout; hooks introduce no recoverable errors and can only panic.
- Hook chains run in annotation application order, each seeing the previous value; `@Repeatable` handlers run in order; under `@Inherit`, direct annotations' handlers precede inherited ones; annotations without `@Retention(invocation)` produce no hooks.

### 5.5 Code Generation Rules and Determinism

- The only generation entry is annotation processors; **there is no `emit` keyword**. Generated entities are new declarations only: no modification of existing declarations or target structure, no token injection.
- Multiple annotations never override each other: every processor works from the same `description` snapshot and emits its own declarations; "later annotation replaces earlier" does not exist.
- Generated names must be predictable (for debugging), but references resolve by description identity rather than textual concatenation; generated declarations participate in Sema, overload resolution and visibility.
- Processor execution forbids IO/time/randomness; capability annotations are the only exception and must enter the cache key. Results are content-addressed (⏳) for reproducible incremental builds.
- Reflection values cannot escape to runtime; `.coox` serializes descriptor data, never compile-time handles.

### 5.6 Class / Member / Field / Function / Method / Constructor

Reflection base types: `Class`, `Member`, `Field`, `Function`, `Method`, `Constructor`, `Modifier`, `Description`, `Descriptor`.

```text
Member
  ├─ Field
  └─ Function
       ├─ Method
       └─ Constructor

Description                 // read-only declaration view
  └─ Descriptor             // runtime layout + query tables

Class                       // type handle pointing at a Descriptor
Modifier                    // bit-flag enum
```

- `Member` unifies `name/owner/modifiers`; `Field` adds `type/index`; `Function` unifies `returnType/parameterTypes/generics/invoke`; `Method` adds `owner`/`this`; `Constructor` is a special `Function` with no return type.
- `Description` is the read-only base view; `Descriptor` extends it with layout; `Class` holds only `const Descriptor*`.
- `Modifier` is a bit flag enum: `None`, `Public`, `Private`, `Protected`, `Static`, `Final`, `Virtual`, `Override`, `Abstract`, `Const`, `Optional`, `Unique`, `Share`; only `modifiers.has(Modifier.X)` plus a few conveniences are exposed.

```coo
class Class {
public:
    string name;
    string qualifiedName;
    integer id;

    Class getSuperClass();
    Array<Class> getInterfaces();
    Description getDescription();
    Descriptor getDescriptor();

    boolean isPrimitive();
    boolean isEnum();
    boolean isRecord();
    boolean isInterface();
    boolean isProxy();
    boolean isAssignableFrom(Class other);
}
```

- A primitive's `getSuperClass()` returns Object's Class; `Object.getSuperClass()` returns `null`; `Class` equality is by id; each type has one Class instance; canonical names are lowercase, and aliases such as `Integer` share the same id.
- `Descriptor` provides `fields`/`functions`/`constructors`, `size`/`alignment`, `getFunctions`/`getField`/`getFunction`/`getConstructor`/`hasAnnotation`; member lookup uses name + parameter types.
- `Value` supports null, primitives, string and object references; `Field.get/set` and `Function.invoke` return `Result<Value>`; object references use `Result<T> cast<T>()` after checking Class.

### 5.7 Runtime Descriptor Tables and Access Control

```cpp
struct ClassDescriptor {
  const char* name;
  const char* qualifiedName;
  std::uint32_t id;
  const ClassDescriptor* superClass;
  const ClassDescriptor* const* interfaces;
  const FieldDescriptor* fields;
  const MethodDescriptor* methods;
  const ConstructorDescriptor* constructors;
  const AnnotationEntry* annotations;
  std::size_t size;
  std::size_t alignment;
  std::uint32_t modifiers;
};
```

- Each type emits one `inline constexpr` table; `Class` stores only `const ClassDescriptor*`; a `Member` handle stores a table pointer + index; ids are deterministically assigned within a package (sorted by package name + qualified name) and written into `.coox`; modules merge descriptor tables to avoid ODR conflicts; tables live in read-only sections.
- Only public members are exposed by default; `@Reflect` is required to query/call private/protected members; private members are excluded from the public Descriptor; `getDeclared*` opens under `@Reflect`.
- A proxy's `getClass()` returns the target class, and `Descriptor.isProxy()` marks proxies.
- Any value can call `getClass()` to obtain a `Class`; a primitive's `getSuperClass()` returns Object's `Class`, and `Object.getSuperClass()` returns `null`. Runtime retention exposes narrow queries (such as `hasRuntimeAnnotation`/`runtimeAnnotationValue` and `runtimeFieldCount`/`runtimeFieldName`/`runtimeMethodCount`/`runtimeBase`), never general runtime reflection.
- `Module`/`Reflection` are the entry points for free functions and cross-package discovery; `Class.of<T>()` accepts only concrete types, with explicit arguments for generics; `Module` carries package name, version and exported types/functions; `TypeParameter` stores unerased parameters and bounds.

### 5.8 Documentation

`lib/standard/documentation.coo` provides ordinary `Document` (summary, structured tags, raw text) and `DocumentTag` (name, subject, text). Documents attach to `Description`/`Member`/`FunctionMember`/`Module`; the reflection model class is `FunctionMember` so that `Function` can name the functional interface; the runtime Descriptor keeps a read-only Document reference; the compiler only extracts `///` and `/** */` and builds the Document, while parsing/rendering stays in the standard library; the existing `@parameter/@return/@template` validation remains.

### 5.9 Confirmed Decisions and Open Questions

Confirmed: `Description` keeps its name and becomes an ordinary standard-library base class with processor signature compatibility; `Value` object references may cross packages but must carry `Class` and package id; `Field.set` rejects `@Unique`/`@Share` fields; `Method.invoke`'s outer `Result<Value>` represents reflection failure only, with callee errors inside `Value`; `Class.getDescription()` is runtime-available; generic `Class` carries `Array<Class>` arguments plus `TypeParameter`; annotations retain typed values; static Class access uses `Class.of<T>()`; descriptors default to public only.

Open: compatibility layer for the `Description` migration; cross-package lifetime of `Value`; error channel for reflective calls; exact availability of `Class.getDescription()`; `@Reflect` granularity; cross-module descriptor-table merging.

## 6 Compiler Lifecycle and Engineering

### 6.1 Pipeline

| Stage | Input | Output | Key invariant |
| --- | --- | --- | --- |
| ① Load | file path | `SourceFile` (owns buffer) | `string_view` always points into stable memory |
| ② Lex | source text | `Token[]` | keywords, literals, comments and Spans complete |
| ③ Parse | token stream | `ModuleAst` | syntax error recovery; every node has a Span |
| ③.5 @Processor | AST + description snapshots | generated declarations | append-only; 8 rounds; generated code enters Sema |
| ④ Sema | AST | types/symbols/diagnostics | types resolvable; Scope stack balanced; RAII checks |
| ⑤ ConstEval | checked AST | constant table, compile-time output | pure functions, fuel limits, determinism |
| ⑥ Lower | AST + constants | C++20 text | restricted C++ subset only |
| ⑦ Host CC | `.cpp` | object files + executable | `--cxx`/`$CXX`/`cl`/`c++` replaceable |
| ⑧ Run | executable | exit code | `coorc run` only |

`check` stops at ⑤ (including ConstEval); `--emit-cpp` stops at ⑥; `build` reaches ⑦; `run` reaches ⑧. Options: `-o`, `--emit-cpp`, `--cxx`, `--keep-cpp`, `--dump-tokens`, `--dump-ast`, `--print-consts`, `-Werror`, `--version`. Intermediate C++ is deleted by default in `build`; `--keep-cpp` keeps it. The host compiler defaults to `cl` on Windows and `c++` elsewhere, overridable by `--cxx` or `CXX`.

### 6.2 Repository Layout

```text
libs/coord/
  include/coord/   support / syntax / ast / parser / sema / interp / codegen
                   driver / standard_library
                   modules/  identifier / access / modifier / operator / scope / symbols
                             unicode / character / charset / stream / file / thread / atomic
                             annotation_registry / descriptor / outline / hint / interface / processor
                             module / package / project / dependency / compiler
                             abstract_syntax_tree
                             platform / semver / toml / checksum / archive
                             manifest / lockfile / net / registry / mirror / resolver
                             package_manager
  src/             implementations (C++ backend emitter in generate.cpp)
tools/coorc/       CLI entry point
tests/             unit tests (unit/main.cpp) + e2e.cmake + package_manager.cmake
examples/          .coo examples
lib/standard/      standard library sources
```

CMake targets: `coorc_core` (static library for all compiler stages), `coorc` (CLI), `coord_tests` (unit tests). The root `coordination.cpp/.h` is a leftover Visual Studio template, not part of the toolchain (`COORD_BUILD_LEGACY_DEMO` is off by default).

### 6.3 Compiler Modules

| Module | Files | Responsibility | Status |
| --- | --- | --- | --- |
| `Identifier` | `modules/identifier.h` | Identifier interning (id ↔ text) | ✅ |
| `Syntax` / `Lexer` | `syntax.h` / `src/lexer.cpp` | `TokKind`, `Token`; Unicode source → tokens | ✅ |
| `AbstractSyntaxTree` | `ast.h` / `modules/abstract_syntax_tree.h` | AST nodes, `dumpAst` | ✅ |
| `Parser` | `parser.h` | Recursive descent + Pratt expressions | ✅ |
| `AnnotationRegistry` | `modules/annotation_registry.h` | Annotation descriptors, MetaProcessor, registration/lookup | ✅ |
| `Descriptor` | `modules/descriptor.h` | `Field`/`Method`/`Variant`/`Declaration` snapshots and Builder | ✅ |
| `Outline` / `Hint` | `modules/outline.h`, `hint.h` | Declaration outline tree and completion/hints | ✅ / ⏳ CLI |
| `Interface` | `modules/interface.h` | `.coox` declaration-level writer/reader | ✅ basic |
| `Processor` | `modules/processor.h` | Description snapshots, 8-round limit, generated declarations | ✅ basic |
| `Sema` | `sema.h` / `modules/compiler.h` | Names/types/inheritance/Scope/RAII checks | ✅ |
| `Operator` / `Scope` / `Access` / `Modifier` | `modules/operator.h`, `scope.h`, `access.h`, `modifier.h` | Operator tables, Scope stack, access, modifiers | ✅ |
| `Platform` | `modules/platform.h` / `src/platform.cpp` | OS/arch/target triple, UTF-8 paths, `argv` spawn, console encoding | ✅ |
| `Semver` / `Toml` / `Checksum` / `Archive` | `modules/semver.h`, `toml.h`, `checksum.h`, `archive.h` | Versions, TOML, SHA-256/CRC, archives | ✅ |
| `Manifest` / `Lockfile` | `modules/manifest.h`, `lockfile.h` | Manifests, lockfile read/write and fingerprints | ✅ |
| `Net` / `Registry` / `Mirror` / `Resolver` | `modules/net.h`, `registry.h`, `mirror.h`, `resolver.h` | Downloads, indexes, mirror health, backtracking | ✅ |
| `PackageManager` | `modules/package_manager.h` | install/update/add/remove/list/tree/clean/pack/publish/init/search | ✅ |
| `Module` / `Project` / `Package` / `Dependency` | `modules/module.h`, `project.h`, `package.h`, `dependency.h` | Discovery, project root, dependency graph | ✅ / ⏳ CLI |
| `Unicode` / `Character` / `Charset` | same-named headers | Code points, UTF-8 codecs, BOM | ✅ |
| `Stream` / `File` | `modules/stream.h`, `file.h` | Cross-platform text streams and UTF-8 files | ✅ |
| `Thread` / `Atomic` | `modules/thread.h`, `atomic.h` | RAII thread boundary, `Atomic<T>` | ✅ |
| `Compiler` / `Evaluator` / `Linker` | `modules/compiler.h` | Driver facade, interpreter wrapper, host-link boundary | ✅ facade |
| `Driver` | `driver.h` / `src/driver.cpp` | Lifecycle, artifacts, `--emit-cpp`/`build`/`run` | ✅ |

Dependencies are strictly one-way:

```text
Identifier / Access / Modifier / Operator / Scope / Unicode
        ↓
Syntax → Lexer → AST → Parser → AnnotationRegistry / Processor
        ↓
Descriptor → Outline → Hint ; Sema → Evaluator → Compiler → Linker
        ↓
Module / Package / Project / Dependency (independent toolchain layer using File/Scope)
        ↓
coorc CLI
```

The front end does not depend on package management; scanners do not depend on the type checker; `codegen` depends only on `sema`'s types and `interp`'s constants, never on driver. Pure data/algorithm modules under `modules/*` are header-only by default; platform/IO modules (platform, toml, archive, manifest, lockfile, net, registry, mirror, resolver, package_manager) use `.cpp` to limit link units.

### 6.4 Support Layer Contracts

- **SourceManager / Span**: each file has one `std::unique_ptr<SourceFile>` with a stable address so `string_view` stays safe; `lineStarts` is precomputed and `lineCol(Span)` uses `upper_bound`. `Span { file, begin, end }` flows through lexer/parser/sema/diagnostics; offsets are normalized UTF-8 bytes.
- **DiagnosticEngine**: a diagnostic is `{severity, code, message, span}`; rendering is `path:line:col: error[E2030]: ...` plus the source line and `^~~~` (1-based line/column). `-Werror` promotes warnings; warnings keep codes (`W0001` etc.) for suppression.
- **Interpreter**: values are `Void / Bool / Int / Decimal / Char / String`; the Scope stack mirrors Sema (a call pushes a Function Scope, the body pushes a Block); `return`/`break`/`continue` use internal signals and must pop Scopes on unwind.
- **Unicode/encoding**: sources may be UTF-8, UTF-16LE/BE or UTF-32LE/BE; BOM detection strips it (UTF-32LE BOM before UTF-16LE); internal UTF-8; invalid code points become U+FFFD. `char` is a 32-bit scalar (backend `char32_t`). `Character` provides `isAscii`, `isDigit`, `isLetter`, `isWhitespace`, `isControl`, `toUtf8`, `decode`.
- **Stream / File**: internal LF; CRLF/CR normalized on read; always LF on write, so lines/offsets are cross-platform. **Stream line indexes are 0-based** (diagnostics display 1-based), `readByLine` returns content without the newline, and `lineCount` counts LF. `readByStream`/`writeByStream` handle whole text; `readByByteOffset(offset)` returns text to the end; `writeByByteOffset(offset, content)` overwrites from `offset`, appending past the end; `writeByLine(index, content)` replaces an existing line and rejects embedded newlines. `File` exposes `readAll`, `writeAll`, `exists` plus Stream methods.
- **Concurrency**: `Thread` is an RAII thread boundary; `Atomic<T>` provides lock-free atomics; the toolchain avoids mutable global state so parallel tests/builds stay reproducible.
- **Platform**: all cross-platform differences live here: UTF-8 paths, directory conventions, executable/object/library naming, direct `argv` spawn (no shell), timeouts/output capture, Windows UTF-16 command lines, console encoding.

### 6.5 Cross-Platform Toolchain

| Host | Target triple | Default host C++ compiler |
| --- | --- | --- |
| Windows x64/arm64 | `x86_64-pc-windows-msvc` / `aarch64-pc-windows-msvc` | `cl` |
| Linux x64/arm64 | `x86_64-unknown-linux-gnu` / `aarch64-unknown-linux-gnu` | `c++` (g++/clang++) |
| macOS x64/arm64 | `x86_64-apple-darwin` / `aarch64-apple-darwin` | `c++` (Apple clang) |
| FreeBSD | `*-unknown-freebsd` | `c++` |

- Host compiler discovery order: `--cxx` → `COORD_CXX` → `CXX` → platform candidates (Windows `cl`/`clang++`/`g++`; POSIX `c++`/`clang++`/`g++`); `--cxx-flavor msvc|gnu` forces the argument style.
- The toolchain spawns subprocesses with `platform::run(argv)` directly, **never through a shell**; Windows uses `CreateProcessW` (UTF-16 command line and environment block), Unix uses `fork`/`execvp` + process group; timeouts, output capture, stdin pipes and cancellation are supported.
- All cross-module paths are UTF-8; Windows converts to UTF-16 file APIs; sources are LF internally and normalized on read/write; config dirs are `%APPDATA%\coord`, `~/Library/Application Support/coord`, `${XDG_CONFIG_HOME:-~/.config}/coord`; caches are `%LOCALAPPDATA%\coord\cache`, `~/Library/Caches/coord`, `${XDG_CACHE_HOME:-~/.cache}/coord`, overridable with `COORD_CONFIG`/`COORD_CACHE_DIR`.
- CMake uses `Threads::Threads` and platform presets; Windows uses Ninja + cl, Linux/macOS use `linux-release`/`macos-release`/`linux-debug`. Linux/macOS have been verified locally with g++; Windows is the primary development platform.
- Cross-compilation: `coorc --target <triple>` passes `--target` to clang; GCC cross toolchains are supplied via `--cxx`; `--sysroot <path>` passes a sysroot; MSVC cannot cross-compile; a cross target cannot be executed with `coorc run`.

### 6.6 Test Status

`ctest` currently registers **190 tests**: `coord_unit` plus 189 end-to-end tests. `coord_unit` covers lexing, parsing, generics, Scope, RAII, compile-time evaluation, codegen, interfaces, enums, pointers, Unicode/BOM, module scanning, access/inheritance, `super`, meta-annotations, Optional/null, converting constructors, Result/Expect, for-in, `@Signed/@Unsigned/@Length`, fixed arrays, array literals, ArrayView, OrderedMap, OrderedSet, UnorderedMap, UnorderedSet, 128-bit integers, Invocation/multi-handler/virtual dispatch, Processor-generated declarations, Proxy class/annotation/runtime call/field trap/state, reflector, runtime retention/value/member/type, descriptor pipeline, ownership/optional/fields/params/returns/initializer, main signatures, `.coox` reader, package graph, outline/hint and `@Test`. The e2e suite covers examples and fixtures (compile/run, negative cases, determinism, incremental builds, dependency cycles, two-package builds, and the full package-manager/mirror/registry flow); the newly landed coverage is `coord_e2e_optional_policies`, `coord_e2e_optional_error`, `coord_e2e_optional_implicit`, `coord_e2e_constructure`, `coord_e2e_constructure_error`, `coord_e2e_destructure_annotation`, `coord_e2e_proxy_construct_destructure`, `coord_e2e_platform` and `coord_e2e_retention_block`, `coord_e2e_radix`, `coord_e2e_radix_error`, `coord_e2e_default_init`, `coord_e2e_weak`, `coord_e2e_bounds`, `coord_e2e_optional_params`, `coord_e2e_optional_fields`, `coord_e2e_optional_return`, `coord_e2e_division`, `coord_e2e_overflow`, `coord_e2e_compound`, `coord_e2e_shift`, `coord_e2e_share_cycle`, `coord_e2e_share_cycle_ctor`, and the lambda closure slice `coord_e2e_lambda_capture`/`_escape`/`_escape_scope`/`_unlisted`/`_readonly`/`_this`, and the control-flow slice `coord_e2e_match_patterns`, `coord_e2e_if_expression`, `coord_e2e_match_missing`, `coord_e2e_match_no_wildcard`, `coord_e2e_match_bad_pattern`, `coord_e2e_if_expression_type_error`, and the function-value slice `coord_e2e_method_ref`, `coord_e2e_method_ref_escape`, `coord_e2e_method_ref_unbound`, and the functional-interface slice `coord_e2e_functional_interface`, `coord_e2e_functional_interface_error`, `coord_e2e_functional_interface_arity_error`, `coord_e2e_functional_interface_methods_error`, and the thread slice `coord_e2e_thread`, `coord_e2e_thread_capture_error`, `coord_e2e_thread_this_error`, `coord_e2e_thread_copy_error`, and the Mutex slice `coord_e2e_mutex`, `coord_e2e_mutex_copy_error`, `coord_e2e_mutex_assign_error`, and the ScopedLock/Atomic/@Lock slice `coord_e2e_atomic`, `coord_e2e_scopedlock_copy_error`, `coord_e2e_atomic_copy_error`, `coord_e2e_atomic_assign_error`, `coord_e2e_lock_annotation`, `coord_e2e_lock_annotation_error`, and the ScopedThread/Atomic-ops slice `coord_e2e_scoped_thread`, `coord_e2e_atomic_ops`, `coord_e2e_scopedthread_copy_error`, `coord_e2e_scopedthread_assign_error`, `coord_e2e_scopedthread_capture_error`, `coord_e2e_scopedthread_this_error`, and the @Lock capability slice `coord_e2e_lock_capture`, `coord_e2e_lock_capture_uncaptured_write`, `coord_e2e_lock_capture_const_write`, `coord_e2e_lock_capture_readonly_method`, `coord_e2e_lock_capture_value_class`, `coord_e2e_lock_capture_escape`, `coord_e2e_lock_legacy_error`, `coord_e2e_lock_nested_error`, `coord_e2e_lock_capture_uncaptured_call`, `coord_e2e_lock_capture_value_return`, and the @Copy slice `coord_e2e_copy_deep`, `coord_e2e_copy_deep_unique`, `coord_e2e_copy_shallow`, `coord_e2e_copy_deep_share_error`, `coord_e2e_copy_shallow_unique_error`, `coord_e2e_copy_explicit_required_error`, `coord_e2e_copy_bad_mode_error`, `coord_e2e_lock_capture_deep`, `coord_e2e_lock_capture_shallow_error`, `coord_e2e_copy_deep_unique_chain`, `coord_e2e_copy_deep_nested_error`, `coord_e2e_copy_explicit_shell_error`, and the Safe/Unsafe slice `coord_e2e_unsafe_ffi_error`, `coord_e2e_unsafe_ffi_block`, `coord_e2e_unsafe_pointer_error`, `coord_e2e_unsafe_safe_override_error`. The complete list is in [`tests/CMakeLists.txt`](tests/CMakeLists.txt). `COORD_SANITIZE=ON` builds the compiler with ASan/UBSan (including the `linux-sanitize` preset); `COORD_FUZZ=ON` builds Clang/libFuzzer parser and compiler harnesses under [`tests/fuzz`](tests/fuzz); the compiler harness keeps `Checker` alive through emission because `TypeExpr::resolved` points into checker-owned types. Diagnostics are capped at 256 stored entries, and the parser advances after member-recovery errors to avoid unbounded diagnostic growth. `coord_e2e_packages` needs neither a host C++ compiler nor network; `coord_e2e_packages_build` requires a working host C++ toolchain.

## 7 Standard Library Design

> The standard library ships with the compiler. Public APIs are written in Coordination; native code appears only behind private `@Native` boundaries linked at compile time. Where this section conflicts with the implementation, language semantics defer to this specification and standard-library APIs defer to §7.

### 7.1 Goals and Constraints

- **No macros**: formatting, test discovery and collection literals rely on grammar and annotations, never text macros.
- **Public API is pure Coo**: C/C++ appears only at private `@Native` boundaries; users see Coordination types/functions/methods.
- **Cross-platform consistency**: paths, newlines, encodings, time and randomness are normalized by the library, not leaked to users.
- **Predictable ownership**: move by default; copying needs `@Copy`; no GC.
- **Versioned with the compiler**: no independent ABI promise; `.coox` carries a version and breaking changes are allowed before 1.0.
- **Synchronous first**: Thread/Mutex/Channel/Atomic are provided; async/await is not built in.

### 7.2 Module Map

Top-level modules are peers without a `std.` prefix:

| Module | Contents |
| --- | --- |
| `utility` | `Iterable`, `Iterator`, `Hash`, `Eq`, `Ord`, `Move`, `Forward`, `Swap`, `Exchange`, `Min`, `Max`, `Clamp`, `Abs`, `Defer`, `ScopeGuard`, `BitCast`, endianness, `Format`, `Println` |
| `memory` | `Shared<T>`, `Weak<T>`, `Allocator`, `GlobalAllocator`; `@Unique`/`@Share` are built-in ownership annotations |
| `collection` | `Array<T>`, `ArrayView<T>`, `OrderedMap<K,V>`, `UnorderedMap<K,V>`, `OrderedSet<T>`, `UnorderedSet<T>` |
| `string` | `string` extensions, `StringBuilder`, `StringView`, Unicode iterators, encoding conversion |
| `io` | `Reader`, `Writer`, `Stream`, `Console`, `BufferedReader`, `BufferedWriter`, `EndOfStream` |
| `file` | `File`, `Path`, `Directory`, `FileStream` |
| `atomic` | `Atomic<T>`, `MemoryOrder` |
| `thread` | `Thread`, `Mutex<T>`, `MutexGuard<T>`, `Channel<T>`, `ChannelClosed` |
| `reflection` | `Reflection`, `Reflector<T>`, `Proxy<T,H>`, `ProxyHandler` |
| `time` | `Instant`, `Duration`, `Clock` |
| `mathematics` | `Vector2/3/4`, `Matrix`, scalar math functions |
| `random` | `Random`, `SplitMix64`, `Xoshiro256`, `UniformInt`, `UniformFloat` |
| `test` | `@Test`, assertions, runtime support for `coorc test` |

Import rules:

```coo
import collection;          // unqualified: Array<T>, OrderedMap<K,V>
import collection as c;     // qualified: c.Array<T>
import string;              // string extensions and StringBuilder
```

A module may span files internally, but `import collection;` sees the module's exported set. Standard library v0 implements each top-level module as one `.coo` (or one export facade), split later.

### 7.3 Prelude and Naming

Names automatically available in every module (everything else needs an explicit import): `Result`, `Expect`, `Error`, `Shared`, `Array`, `Iterator`, `Iterable`. Built-in primitives remain lowercase: `integer`, `string`, `bool`, `char`, `void`, `pointer`, etc. Library and user classes use PascalCase:

| Category | Rule | Examples |
| --- | --- | --- |
| Modules | lowercase | `collection`, `atomic`, `thread` |
| Types/protocols | PascalCase | `Array`, `OrderedMap`, `MutexGuard` |
| Functions/methods | lowerCamel | `push`, `readLine`, `trySend` |
| Annotations | PascalCase | `@Test`, `@Native` |
| Enum variants | PascalCase | `Red`, `Green` |
| Built-in types | lowercase | `integer`, `string`, `bool` |

### 7.4 `@Native` FFI (v1 subset)

```coo
@Native("getpid")
integer nativePid();

integer main() {
    print(nativePid());
    return 0;
}
```

- Current implementation accepts `@Native("c_symbol")` on a **body-less function**; the symbol must be a C identifier.
- Supported signatures: `integer`, `boolean`, `float`, `string`, `void` parameters and return values. A `string` parameter is passed as `.c_str()`; a string return converts a nullable `char*`, mapping `nullptr` to the empty string. No classes, pointers, callbacks or opaque handles yet.
- The emitter produces a Coord-visible wrapper that calls `::c_symbol(...)`; the C symbol must already be declared by the generated translation unit's platform headers (`windows.h` on Windows, `unistd.h` on POSIX).
- `@Native` cannot be combined with `@Result`, `@Expect`, `@Test`, `@Unique`, `@Share`, `@Weak`, `@Optional` or generics (`E2078`).
- This is an **unsafe boundary**: the programmer must ensure the C ABI and signature match. There is no runtime `dlopen`, dynamic lookup or FFI metadata.
- Full design — `header`/`library` arguments, opaque `@Native class`, native methods, C++ ABI selection, automatic library linking — remains ⏳.

### 7.5 Core Protocols (utility)

```coo
interface Iterator<T> {
    T? next();
}

interface Iterable<T> {
    Iterator<T> iterator();
}

interface Hash {
    integer hash() const;
}

interface Eq<T> {
    bool equals(const T& other) const;
}

interface Ord<T> {
    int compare(const T& other) const;   // <0, 0, >0
}

interface Function<T, R> { R apply(T value); }
interface Consumer<T> { void accept(T value); }
interface Supplier<T> { T get(); }
interface Predicate<T> { bool test(T value); }
interface BiFunction<A, B, R> { R apply(A first, B second); }
```

- `Function`/`Consumer`/`Supplier`/`Predicate`/`BiFunction` are functional interfaces and lambda targets; `@FunctionalInterface` optionally checks "exactly one abstract method."
- `T?` means `T | void`; `next()` returning `void` signals the end.
- `for x in xs` expands to `iterator()` + `next()`, without requiring an index.
- `Hash`/`Eq` serve `UnorderedMap`/`UnorderedSet`; `Ord` serves `OrderedMap`/`OrderedSet` and sorting. User classes implement these explicitly in v0; annotation derivation comes later.

### 7.6 Collections (collection)

```coo
Array<integer> xs = [1, 2, 3];
xs.pushBack(4);
xs.pushFront(0);
integer first = xs.popFront();
for x in xs { Println(x); }

OrderedMap<string, integer> ages;
ages.set("alice", 30);
integer? alice = ages.get("alice");
```

- `Array<T>` → `std::deque<T>`; head/tail operations are O(1). Implemented: type, default construction, `[1, 2, 3]` literals, `pushBack`/`pushFront`/`popFront`/`popBack`, `length`/`isEmpty`/`clear`, indexed reads/writes, `for ... in`, `view()` → `ArrayView<T>`; `ArrayView<T>` supports `length`/`isEmpty`/`view[i]`; `get`/`at` return `@Optional T*`.
- Fixed arrays `T[N]` → `std::array<T,N>`; length is part of the type.
- `OrderedMap<K,V>` → `std::map<K,V>`; `UnorderedMap<K,V>` → `std::unordered_map<K,V>`; methods `set(k,v)`, `get(k)` (null when missing), `contains(k)`, `remove(k)`, `length`, `isEmpty`. `OrderedSet<T>` → `std::set<T>`; `UnorderedSet<T>` → `std::unordered_set<T>`; methods `add/contains/remove/length/isEmpty`.
- Common API: `insert`, `remove`, `get`, `contains`, `length`, `clear`, `keys()`, `values()`, `entries()`, `iterator()`; `get` returns `V?`, `operator[]` only for existing keys (insert with `insert`).
- Constraints: `OrderedMap`/`OrderedSet` keys implement `Ord` and iterate ascending; `UnorderedMap`/`UnorderedSet` require `Hash` + `Eq` and iterate in unspecified order. Literals, iteration and custom constraints for all four containers remain future work.
- Move by default; `@Copy` produces a deep copy (elements must also be `@Copy`); empty literals need an explicit type annotation.

### 7.7 Memory and Ownership (memory)

```coo
@Unique File file = File.open("a.txt");
@Share Config config = Config();
Shared<Config> shared = Shared.make(Config());
Weak<Config> weak = shared.weak();
if (weak.isPresent()) {
    @Optional @Share Config stillAlive = weak.upgrade();
}

@Optional @Unique File maybe = null;
if (maybe != null) {
    maybe.get().readAll();
}
File fallback = maybe.orElse(File.open("default.txt"));
```

- `&` is a non-escaping borrow: no fields/containers/returns; escaping requires `@Unique`/`@Share`.
- `@Unique`: unique ownership, move-only; a `@Target(type)` annotation; `isPresent()`, `isEmpty()`, `get()` (panics when empty), `orElse(default)`.
- `@Share`: shared ownership, atomic count, copyable; a `@Target(type)` annotation with the same query API.
- `@Optional`: nullable **type** for `T*`, `@Unique`, `@Share`; a `@Target(type)` annotation; `ProxyPolicy` is documented in §4.5.2; non-optional is non-null by default, and assigning `null` reports `E2046`.
- `Weak<T>`: maps to `std::weak_ptr<T>`; `isPresent()` = not expired, `isEmpty()` = expired, `upgrade() -> @Optional @Share T`.
- `Shared<T>`: explicit heap-allocated owning pointer; `Shared.make(args...)` constructs on the heap; `Weak<T>` does not increment the count.
- `Allocator` interface `allocate/deallocate/reallocate` (all `unsafe`); `GlobalAllocator` is the default; v0 `@Unique`/`@Share`/`Shared`/`Array` use it exclusively, with custom allocators deferred.
- `new`/`delete` are allowed only inside `unsafe` blocks; ordinary code uses `@Unique`/`@Share`/`Shared`.
- C++ backend: `@Unique T` → `std::unique_ptr<T>`, `@Share T` → `std::shared_ptr<T>`, `Weak<T>` → `std::weak_ptr<T>`.

### 7.8 Strings (string)

- Built-in `string` is an **immutable UTF-8 byte sequence**; `length` returns bytes; `s[i]` returns a `u8` byte.
- Unicode is exposed through iterators:

```coo
for c in s.chars() { Println(c); }        // Unicode scalars
for g in s.graphemes() { ... }            // grapheme clusters (future)
string sub = s[1..3];                     // byte slice, returns StringView
```

- `StringBuilder` handles incremental concatenation; `StringView` is a non-owning read-only slice.
- `f"..."` is compile-time interpolation (see §7.14); `Format`/`Println` live in `utility`.
- Encoding conversion: `Charset`/`Unicode` modules (UTF-8/16/32 + BOM), shared with the compiler.

### 7.9 IO and Files (io / file)

```coo
interface Reader {
    Result<integer, Error> read(Span<u8> buffer);
    Result<string, Error> readLine();
}

interface Writer {
    Result<integer, Error> write(Span<const u8> buffer);
    Result<void, Error> flush();
}
```

- All fallible IO returns `Result<T, Error>`; end-of-input returns an `EndOfStream` error.
- `Span<T>` is a non-owning view; `Span<const u8>` is a read-only byte range.
- `Stream` provides whole-text/line/byte-offset access; line indexes 0-based, internal LF, CRLF/CR normalized on read, LF on write; consistent across platforms.
- `File` provides `open`, `readAll`, `writeAll`, `openStream` and Stream mode; `Path`/`Directory` handle paths; `Console` provides stdin/stdout/stderr; `BufferedReader`/`BufferedWriter` are optional buffering layers.
- Built-in `readFile`/`writeFile`/`fileExists`/`deleteFile`/`timeMillis`/`sleepMillis` are compiler-prelude conveniences; they remain source-compatible when the standard library takes over.

### 7.10 Concurrency (atomic / thread)

```coo
Atomic<integer> counter = Atomic<integer>(0);
counter.fetchAdd(1);

Mutex<Array<integer>> queue = Mutex(Array<integer>());
{
    MutexGuard<Array<integer>> guard = queue.lock();
    guard.get().pushBack(1);
}   // guard destructor unlocks

Channel<integer> channel = Channel<integer>(16);   // bounded; Channel<integer>() unbounded
channel.send(1);
integer value = channel.receive()?;                // failure is ChannelClosed
channel.close();
```

- `Atomic<T>`: `load`/`store`/`exchange`/`fetchAdd`/`compareExchange`; `MemoryOrder` specifies ordering.
- `Thread`: `Thread.spawn(function)`, `join`, `detach`, `hardwareConcurrency`.
- `Mutex<T>`: `lock() -> MutexGuard<T>` (RAII), `tryLock() -> MutexGuard<T>?`.
- `Channel<T>`: `send`/`receive` return `Result`; `trySend`/`tryReceive`; after `close()`, receive returns `ChannelClosed`; bounded/unbounded constructors.
- No async/await; event loops belong in user libraries.

### 7.11 Time, Mathematics, Random (time / mathematics / random)

- `Instant` (monotonic time point), `Duration` (nanoseconds), `Clock` (system/monotonic).
- `mathematics`: `Vector2/3/4`, `Matrix`, `Sqrt`, `Sin`, `Cos`, `Pow`, etc.; types use PascalCase.
- `random`: `Random` interface with `SplitMix64`/`Xoshiro256`; `UniformInt`/`UniformFloat` distributions; explicit seed. No automatic seeding by default, so tests are reproducible.
- Time and randomness never participate in compile-time evaluation (IO/time/randomness are forbidden there).

### 7.12 Testing (test + coorc test)

```coo
import test;

@Test
void additionWorks() {
    assertEq(1 + 1, 2);
    assertTrue(2 > 1);
    assertFalse(1 > 2);
}
```

- `@Test` marks zero-argument functions returning `void`; assertions: `assert`, `assertEq`, `assertTrue`, `assertFalse`.
- `coorc test <file>` compiles and runs collected `@Test` functions **serially**; `--filter <substring>` runs only tests whose names contain the substring; failure exits nonzero and prints file/line.
- Tests may use `defer`, RAII and ordinary library APIs; parallel execution, fixtures and benchmarks are future work.

### 7.13 Reflection and Proxies (reflection)

| Library type | Wraps / role |
| --- | --- |
| `Reflection` | Static reflection entry |
| `Reflector<T>` | Typed reflection view over the built-in `reflector<T>` |
| `Proxy<T,H>` | Proxy wrapper over the built-in `proxy`; `H` implements `ProxyHandler` |
| `ProxyHandler` | Trap interface; runtime proxy behavior |

```coo
description d = Reflection.describe<User>();
Array<description> members = Reflection.membersOf<User>();
description? name = Reflection.annotationOf<Serializable, User>();
integer size = Reflection.sizeOf<User>();

Reflector<User> r = Reflection.reflect<User>();
for field in r.fields() { Println(field.name()); }
```

- `describe<T>()`, `membersOf<T>()`, `annotationsOf<T>()`, `annotationOf<A,T>()`, `sizeOf<T>()`, `alignOf<T>()`; compile-time only — `Reflection` cannot be a runtime field/parameter type (`E2009`).
- `Reflector<T>`: `name()`, `kind()`, `fields()`, `methods()`, `annotations()`, `size()`, `align()`, `construct(args...)`, `invoke(method, args...)`; member access is statically checked, so typos are compile errors; `construct`/`invoke` respect access control.
- `Proxy<T,H>` is a **separate wrapper type** that changes neither layout nor ABI of `T`; constructed with `Proxy.make(value, handler)`; Java-Optional-style `isPresent()`/`isEmpty()`/`get()`/`unwrap()`; not a subtype of `T`.
- `ProxyHandler` declares `get/set/has/deleteProperty/keys/call/construct/destructure`; unimplemented methods default to direct forwarding. `@Proxy` + `@Get/@Set/...` and the interface are two spellings of the same traps: annotations for compile time, the interface for runtime/`@Retention` proxies. Handler state lives in side storage, never in target layout.
- `ProxyHandler` subinterfaces: `MethodProxyHandler`, `DestructureProxyHandler`, `ConstructureProxyHandler`, `ClassProxyHandler`, `ObjectProxyHandler`, `FunctionProxyHandler`. Each extends `ProxyHandler`; `interface X extends ProxyHandler` is supported, and implementing any subinterface satisfies the `@Proxy` handler requirement.

### 7.14 Syntax Sugar

| Sugar | Desugaring / semantics |
| --- | --- |
| `for x in xs { ... }` | `Iterator<T> it = xs.iterator(); while (it.next()) { ... }` |
| `[1, 2, 3]` | `Array<T>` construction + `pushBack`; element type inferred from context |
| `[k: v, ...]` | `OrderedMap`/`UnorderedMap` construction + `insert` |
| `a..b` / `a..=b` | Half-open/closed range value; `xs[a..b]` returns `ArrayView<T>` with bounds checks |
| `{a, b}` / `{y = "named", x = 9}` | `@Constructure` typed/named construction; type labels optional; matches an annotated constructor |
| `{x: 1}` | Aggregate object literal (field initializer), distinct from `@Constructure` entries |
| `[captures](const integer& x): integer -> expr` | Lambda (Java target types; C++ explicit captures; omitted captures copy by value and the copy is mutable) |
| `Type::method` / `object::method` | Method reference over a function-type target; static refs lower to function pointers, bound refs capture the receiver by reference and follow closure escape rules; unbound instance refs ⏳ |
| `if let` / `match` | Destructure `Result`/`Expect`/unions; `match` is exhaustive over enums (`if let` removed) |
| `defer expr;` | Runs at Scope exit in reverse declaration order, sharing the local-destruction stack |
| `f"{name}: {count}"` | Compile-time placeholder parsing with type checks; `{{`/`}}` escape |
| `f(name: value)` | Named arguments; positional arguments precede named ones |

- No `?` error propagation: errors bubble by default; explicit handling uses `isOk`/`unwrap`/`match`.
- Collection literals fail when the element type cannot be inferred; an empty literal must use `Array<T>()`.

### 7.15 Error Type

```coo
interface Error {
    integer code() const;
    string message() const;
    string source() const;
    Backtrace backtrace() const;
}
```

- Subtypes: `IoError`, `EndOfStream`, `ParseError`, `AllocError`, `ChannelClosed`.
- `Result<T, Error>` is a prelude type; `Expect<T, Error>` must be consumed explicitly.
- Library functions do not throw or panic (except unrecoverable allocation failure); recoverable errors always return `Result`.

### 7.16 Implementation and Release

- Sources: `lib/utility/`, `lib/memory/`, `lib/collection/`, `lib/string/`, `lib/io/`, `lib/file/`, `lib/atomic/`, `lib/thread/`, `lib/reflection/`, `lib/time/`, `lib/mathematics/`, `lib/random/`, `lib/test/`.
- Public layers are pure Coo; platform-specific pieces declare `@Native` in `lib/*/internal/*.coo`.
- The compiler resolves modules from the toolchain `lib/` and project dependencies according to `import`, generating/caching `.coox`.
- The library shares the compiler version; `.coox` records version and public API digest; library tests live in `lib/*/tests/*.coo` using `@Test` and `coorc test`.
- Releases contain sources and precompiled `.coox`; no independent package-manager version.

### 7.17 Milestones and Open Questions

**P5 first acceptance**: `Array`/`OrderedMap`/`UnorderedMap` + iteration + literals/slicing/for-in; `@Unique`/`@Share`/`@Optional` + `Shared` + `Allocator`; `string` byte indexing + `chars()` + `f"..."` + `StringBuilder`; Reader/Writer/Stream/File/Path with LF normalization; Thread/Mutex/Channel/Atomic; `Reflection`/`Reflector<T>`/`Proxy<T,H>`/`ProxyHandler`; `@Test` + `coorc test`; `Instant`/`Duration`, basic math, `Random`.

**Open**: method-reference syntax and overload resolution; `OrderedMap` backing store (B-tree vs sorted array); whether `Span<T>`/`ArrayView<T>`/`StringView` merge; `Format`/`f"..."` placeholder grammar and localization; test parallelism/fixtures/benchmarks; whether custom allocators enter container generics; the stable range of the `@Native` C++ ABI and platform shim organization.

## 8 Packages, Workspaces, Locks, Registries and Mirrors

> The package manager is implemented (`coorc 0.2`). This section specifies `coordination.toml` / `coord.pkg`, `coord.lock`, the registry protocol, mirror selection and offline behavior.

### 8.1 Design Goals

- **Zero external dependencies**: a registry is a local directory or HTTP(S) root; checksums, gzip/tar and TOML parsing are implemented by the toolchain. HTTP uses system `curl`; no TLS library is bundled.
- **Reproducible**: `coord.lock` pins versions, sources and SHA-256; `--locked` / `--frozen` fail when the lockfile is stale.
- **Offline-capable**: `--offline` reads only the cache and `coord_modules/`; `file://` registries need no network.
- **Mirrorable**: registry and mirror are separate concepts; failover follows priority and health.
- **Cross-platform**: Windows / macOS / Linux / FreeBSD share commands and directory layout; paths, encodings and subprocess spawning are normalized by `modules/platform.h`.

### 8.2 Manifests

A package is any directory containing a manifest:

| File | Description |
| --- | --- |
| `coordination.toml` | Full format, recommended |
| `coord.pkg` | Line-oriented M0 format, backward compatible |

When both exist, `coordination.toml` wins. Scanning ignores `out/`, `coord_modules/`, `.git/`, `.vs/`, `build/`, `cmake-build-*/`.

`coord.pkg`:

```text
name math
version 1.2.0
edition 2026
source src
description math helpers

dependency ../lib              # path dependency
dependency core ^0.3           # registry dependency
dependency util@~1.4           # @ form is equivalent
dev-dependency testkit 0.1
build-dependency codegen 0.2
registry official              # default registry for this package
mirror cn                      # default mirror
```

Fields: `name` (required), `version`, `edition`, `source`, `description`, `license`, `repository`, `homepage`, `registry`, `mirror`, `dependency`, `dev-dependency`, `build-dependency`; unknown fields are errors. Path detection: starts with `.`, `/`, `\`, or contains `/`/`\`, or has the `path:` prefix; everything else is `name [requirement]`.

`coordination.toml`:

```toml
[package]
name = "app"
version = "0.1.0"
edition = "2026"
source = "src"
description = "demo"
license = "MIT"
registry = "official"     # default registry for dependencies
mirror = "cn"             # default mirror

[dependencies]
math = "^1.2"
local = { path = "../lib" }
core = { version = "~0.3", registry = "official", platform = "windows" }
optional = { version = "^0.1", optional = true }

[dev-dependencies]
testkit = "0.1"

[build-dependencies]
codegen = "0.2"

[registries]
official = "https://registry.coordination-lang.org"
internal = "file:///srv/coord-registry"

[[mirrors]]
name = "cn"
registry = "official"
url = "https://cn.registry.coordination-lang.org"
priority = 10
enabled = true
description = "Asia-Pacific"

[workspace]
members = ["lib", "tools"]   # reserved: parsed only, not installed recursively
```

Dependencies may also use subtables:

```toml
[dependencies.math]
version = "^1.2"
mirror = "cn"
```

Dependency fields: `version`, `path`, `registry`, `mirror`, `platform`, `optional`; `requires` is only a sorting hint for `coorc deps` and is ignored by the resolver. `platform` filters `windows`, `linux`, `macos`, `unix`, architecture names (`x86_64`/`aarch64`/...), `os-arch`, with `,`/space separators and `!` negation; non-matching dependencies are not resolved; `optional = true` resolves only with `--all-optional`.

### 8.3 Versions and Constraints

Semantic versions `major.minor.patch[-prerelease][+build]`:

| Form | Range |
| --- | --- |
| `*` / empty | any stable version |
| `1.2.3` / `^1.2.3` | `>=1.2.3 <2.0.0` |
| `^0.2.3` | `>=0.2.3 <0.3.0` |
| `^0.0.3` | `>=0.0.3 <0.0.4` |
| `~1.2.3` | `>=1.2.3 <1.3.0` |
| `~1.2` | `>=1.2.0 <1.3.0` |
| `1.2.*` | `>=1.2.0 <1.3.0` |
| `>=1.0, <2.0` | intersection |
| `=1.2.3` | exact |

Prereleases match only when their `major.minor.patch` is explicitly constrained (`>=1.0.0-alpha` may match `1.0.0-beta`; `^1.0.0` does not match `1.1.0-beta`).

### 8.4 Resolution

1. Resolve path dependencies first (recursively reading their manifests); versions come from those manifests. Constraint violations or the same name from two paths are errors; path cycles report `cyclic path dependency`.
2. Registry dependencies use backtracking: pick an undecided package, try "locked version first, then newest to oldest", add the version's dependency constraints from the index, and backtrack on conflict.
3. The same package cannot come from two registries; registry packages cannot depend on path packages.
4. Results are sorted by package name for determinism.
5. When a lockfile exists and `update` was not requested, pinned versions win; `coorc update [name...]` ignores pins for the named packages.

Failure messages include each constraint's origin and the actually available versions.

### 8.5 `coord.lock`

```toml
# Generated by coorc. Do not edit manually.
version = 1
manifest-hash = "sha256:..."

[[package]]
name = "math"
version = "1.2.0"
source = "registry"          # registry | path
registry = "official"
url = "https://..."
checksum = "sha256:..."
dependencies = ["core 0.3.1"]

[[package]]
name = "lib"
version = "0.1.0"
source = "path"
path = "../lib"
```

- Written atomically by `coorc install` / `update` / `add` / `remove`.
- `manifest-hash` is the SHA-256 of the resolution-affecting manifest fields.
- `--locked`: the resolution must match the lockfile or fail; `--frozen`: same as `--locked` plus never writing the lockfile.
- Commit `coord.lock` (optional for libraries, recommended for executables); `coord_modules/` usually goes into `.gitignore`.

### 8.6 Registry Protocol

A registry is a directory tree (local directory or HTTP(S) root):

```text
index.toml                        # optional; used by coorc search
index/<name>.toml                 # per-package version index
packages/<name>/<version>.tar.gz  # package archive (also .tar / .zip)
health                            # optional; used by mirror probes
```

`index/<name>.toml`:

```toml
name = "math"
description = "math helpers"

[[version]]
version = "1.2.0"
checksum = "sha256:..."
dependencies = ["core ^0.3"]
yanked = false
archive = "packages/math/1.2.0.tar.gz"   # optional override of the default path
```

- Archive root is `<name>-<version>/` (a single top-level directory is stripped on extract) and must contain the manifest; the manifest's `name` must match the index and its `version` must match (`coord.pkg` may omit version, defaulting to `0.0.0`).
- Checksums are verified before extraction; when the index has no checksum, the first download's actual SHA-256 is used with a warning.
- `coorc pack` emits a deterministic `.tar.gz`: sorted entries, mtime 0, gzip with stored blocks.
- `coorc publish` writes archives directly for local registries and updates `index/<name>.toml` plus `index.toml`; for remote registries it uses `PUT <base>/api/v1/packages/<name>/<version>` with credentials from `COORD_TOKEN`.

### 8.7 Mirror Manager

- **registry**: a package repository endpoint with a name (`official`) and URL.
- **mirror**: another endpoint for the same registry, carrying `priority` (higher tried first).

Configuration layers (later overrides earlier):

1. Built-in catalog: `official` (enabled), `cn` / `eu` (disabled by default; `mirror use` enables).
2. User config `config.toml` (written by `coorc mirror add/use`).
3. Project manifest `[registries]`, `[[mirrors]]`, `[package].registry/mirror`.
4. Environment: `COORD_REGISTRY`, `COORD_MIRROR`, `COORD_OFFLINE`, `COORD_TIMEOUT_MS`, `COORD_CONFIG`, `COORD_CACHE_DIR`, `COORD_CURL`.
5. Command line: `--registry`, `--mirror`.

Request order: explicit mirror → enabled mirrors of that registry by descending priority → the registry URL itself; URLs are deduplicated. Failures are recorded in `<cache>/mirror-health.toml`; after 2 consecutive failures the mirror is skipped for 5 minutes (explicitly selected mirrors excepted).

```bat
coorc mirror list
coorc mirror add cn https://cn.example/coord --registry official --priority 10
coorc mirror use cn
coorc mirror test cn
coorc mirror auto
coorc config set mirror.timeout-ms 5000
```

When `health` is absent, `index.toml` is probed; `mirror test` prints reachability and latency; `mirror auto` picks the fastest reachable mirror. The built-in `cn`/`eu` URLs are placeholders; override them with `mirror add` or the project manifest.

### 8.8 Cache and Vendor

```text
<cache>/coord/
  registries/<hash>/index/...      # index cache (remote TTL 1h; local read-through)
  downloads/<name>-<version>.archive
  sources/<name>-<version>-<hash>/ # extracted package with .coord-package.toml
  tmp/                             # extraction temp dir
  mirror-health.toml

<project>/
  coord.lock
  coord_modules/<name>/            # vendor, with .coord-package.toml
```

- `<cache>`: Windows `%LOCALAPPDATA%\coord\cache`, macOS `~/Library/Caches/coord`, Linux `${XDG_CACHE_HOME:-~/.cache}/coord`; override with `COORD_CACHE_DIR`.
- `coorc clean` removes `coord_modules/`; `coorc clean --cache` also clears the cache.
- `coorc build` merges `coord_modules/*`, manifest path dependencies (recursively) and the current package's `source` directory, so installing packages requires no source changes. The incremental stamp covers the root manifest, `coord.lock`, vendor manifests, file counts and newest mtimes, plus the host target triple and compiler.

### 8.9 Offline and Reproducibility

- `coorc install --offline`: indexes/archives come from the cache only; a complete vendor requires no network or cache access.
- `--locked`: recommended for CI; changes to the manifest or resolution fail immediately with a hint to run `coorc update`.
- `--frozen`: `--locked` without writing the lockfile, suitable for read-only workspaces.
- Corrupt cache/index: delete `<cache>/coord` or run `coorc clean --cache` and retry.

### 8.10 Security

- Archive member names are validated **before** stripping the top-level directory: absolute paths, drive letters, `..`, Windows reserved names and illegal characters are rejected.
- Symlinks are rejected by default; when enabled, targets must be relative and must not escape.
- Extraction is bounded by total bytes (512 MiB) and entry count; the archive itself is capped at 1 GiB.
- Archive SHA-256 is verified before extraction; the lockfile pins checksums. `[trust] require_checksum = true` refuses registry entries without a checksum instead of merely warning.
- `[trust] allow_insecure_http = false` (default) refuses non-loopback `http://` registry URLs; `file://`, `https://`, `localhost`, `127.0.0.1` and `[::1]` are allowed.
- HTTP is executed by system `curl`; failures report every mirror attempt. Private registries may receive a Bearer token via `COORD_TOKEN` (`publish`). Package signatures/trust chains remain future work.

### 8.11 CLI

```bat
coorc init [dir] [--name <name>] [--force]
coorc install [--locked|--frozen] [--offline] [--no-dev] [--all-optional]
coorc update [package...]
coorc add <name[@req] | path> [--dev] [--path]
coorc remove <name>
coorc list
coorc tree
coorc clean [--cache]
coorc pack [-o archive.tar.gz]
coorc publish [--dry-run]
coorc search <term>
coorc mirror list|show|add|remove|use|test|auto
coorc config list|get|set
```

Package commands share: `--manifest <path>`, `--mirror <name|url>`, `--registry <name|url>`, `--verbose`. Manifests are found upward from the current directory. `add`/`remove` edit the manifest before installing; a failed install rolls the manifest back.

### 8.12 Limits and Future Work

- Workspace `members` are parsed but not installed recursively;
- no features/optional-dependency combinations (`--all-optional` is an all-on switch);
- no git dependencies and no package signatures/trust chain (SHA-256 + TLS + token is the current boundary);
- registry `search` relies on `index.toml` (local registries scan `index/`);
- cross-compilation supports `--target`/`--sysroot`/`--cxx`, but an MSVC host cannot cross-compile and cross targets cannot `run`.

## 9 C++ Backend Mapping

### 9.1 Type Mapping

| Coordination | C++20 |
| --- | --- |
| `void` / `bool` | `void` / `bool` |
| `integer` (default) | `std::int64_t` |
| `@Unsigned integer` | `std::uint64_t` |
| `@Signed integer` | `std::int64_t` |
| `@Length(8/16/32/64)` | `std::uint8_t`…`std::uint64_t` / `std::int8_t`…`std::int64_t` |
| `@Length(128)` | `coord_rt::UInt128` / `Int128` |
| `float` | `double` |
| `decimal` | `coord_rt::Decimal` (value + scale fixed point) |
| `record` / `Record` | `coord_rt::Record` (root type) |
| `char` / `string` | `char32_t` / `std::string` |
| `Enumeration` / concrete `enumerate` | `enum class` (underlying `std::int64_t`); same-layout wrapper when it has methods/interfaces |
| `T&` / `const T&` / `T*` / `const T*` | the same C++ forms |
| `T[N]` | `std::array<T, N>` |
| `Array<T>` | `std::deque<T>` |
| `ArrayView<T>` | `coord_rt::ArrayView<T>` |
| `OrderedMap<K,V>` | `std::map<K,V>` |
| `UnorderedMap<K,V>` | `std::unordered_map<K,V>` |
| `OrderedSet<T>` | `std::set<T>` |
| `UnorderedSet<T>` | `std::unordered_set<T>` |
| `@Unique T` | `std::unique_ptr<T>` |
| `@Share T` | `std::shared_ptr<T>` |
| `Weak<T>` | `std::weak_ptr<T>` |
| `@Result<T>` | `coord_rt::Result<T>` |
| `@Expect<T>` | `coord_rt::Expect<T>` |
| user `class` | same-named `class`; suffix `_` on C++ keyword conflicts |
| `Reflector<T>` / `Proxy` / `Description` and other metatypes | no runtime semantics; `Description` currently keeps only a `std::string` placeholder |

### 9.2 Classes, Interfaces and Inheritance

- Emit `class C : public Base, public I1, public I2 { ... }`; a `final` class emits `class C final`; access sections emit `public:` / `private:` / `protected:` labels, omitting the label when the first member is `private` (the C++ default).
- Interfaces emit abstract classes: `virtual Ret m(params) = 0;` with `virtual ~I() = default;`; default methods carry bodies. Methods emit `static`/`virtual`/`override`/`final` as declared, with `= 0;` when bodyless.
- Fields emit in declaration order, private ones included; constructor bodies emit as written (`: super(args)` / `: field(expr)` become C++ initializer lists); destructors: user bodies emit as written, synthesized ones emit `~T() = default;` with a comment documenting "members in reverse, then base", prefixed with `virtual` when the class has virtual methods (or implements an interface).
- `~this.member;` emits no code, only a comment, preventing double destruction.

### 9.3 Enumerations

- A plain enum lowers to `enum class Color : std::int64_t { Red, Green = 3, Blue };`; an enum with methods or `implements` emits a same-layout wrapper (one underlying integer field + `static constexpr` variant constants + member functions/interface vtable), still one integer in size (⏳).
- `Color.Red` → `Color::Red`; `print` outputs the underlying integer; exhaustive `match` lowers to `switch` with one branch per variant and no `default`; `e as integer` yields the value and `n as Color` range-checks (⏳); `@Retention(enum)` adds a side table without changing layout (⏳).

### 9.4 RAII, Generics and Entry Points

- `~this.member;` emits no code; member destruction is entirely C++'s job: after the destructor body, C++ destroys members in reverse declaration order and then the base. Locals are plain C++ locals; `{}` maps one-to-one to Scope and braces are preserved. Error bubbling lowers to an early `return`, still triggering C++ destruction.
- Generics monomorphize to C++ templates: `integer add<N extends integer>(const N& a, const N& b);` emits `template <typename N> requires (std::is_integral_v<N>) std::int64_t add(const N& a, const N& b);`. `extends` bounds become `requires` clauses; unbounded parameters emit unconstrained templates; `where` structural constraints also map to `requires` (⏳).
- `@Constructure` lowers to `Type(arg1, arg2, ...)` in the matched constructor's parameter order; if the target is a `@Proxy`, the handler's `@Construct` call is emitted first with the same values. Destructuring with a target `@Destructure` method emits one default-initialized binding per name, optional per-binding `@Destructure` handler calls, then `obj.method(binding1, binding2, ...)`; field destructuring emits the per-field reads instead.
- `@Retention(Compile)` blocks are executed by the interpreter at compile time and omitted from generated C++; their `print` output goes to compile-time output (`coorc --print-consts`) and never enters the program. `@Retention(Runtime)` blocks are emitted and executed normally.
- `integer main()` / `integer main(Array<string>)` / `integer main(integer, Array<string>)` emit `coord_program::coord_main(...)`; the platform entry function calls it and returns its exit code; `void main` reports `E2052`.
- `print(x)` comes from the generated prelude (`coord_rt::print` overloads for `std::string`, `bool`, `char32_t`, arithmetic types and enums), one line per call; the library's `Println` takes over in P5. Annotation declarations emit only a "compile-time only, no runtime code" comment.

### 9.5 Cross-Platform Contract for Generated Programs

Generated C++ uses standard C++20 plus a conditional `<windows.h>`; platform differences live only in the generated prelude and built-in implementations, so user programs need no platform branches.

| Concern | Windows / MSVC | POSIX (Linux/macOS/FreeBSD) |
| --- | --- | --- |
| Entry | `wmain`, converts UTF-16 `argv` to UTF-8, then calls `coord_main` | `main`, `argv` are byte strings interpreted as UTF-8 |
| Console | calls `SetConsoleOutputCP(CP_UTF8)` / `SetConsoleInputCP(CP_UTF8)` at startup | inherits the terminal encoding; no switch |
| Strings | wide-character entry + internal UTF-8 | internal UTF-8 |
| Paths | UTF-8 → `std::filesystem::path` (constructed with `u8string`) | UTF-8 → `std::filesystem::path` |

- Path/file APIs consistently use UTF-8 → `std::filesystem::path`; on Windows the conversion goes through `u8string` so paths are never interpreted in the ANSI code page.
- Built-in file/time functions: `readFile`, `writeFile`, `fileExists`, `deleteFile`, `timeMillis`, `sleepMillis`.
- New platform built-ins available to programs: `operatingSystem()`, `architecture()`, `targetTriple()`, `pathSeparator()`, `lineSeparator()`, `executableSuffix()`, `currentDirectory()`, `temporaryDirectory()`, `homeDirectory()`, `environment(name) -> Result<string>`, `setEnvironment`, `pathJoin`, `pathFileName`, `pathParent`, `pathExtension`, `pathIsAbsolute`, `pathNormalize`, `pathExists`, `createDirectory`, `createDirectories`, `deleteDirectory`, `listDirectory(path) -> Result<Array<string>>`.
- Output stays LF; diagnostics/source positions use the original `.coo` Span and never change with platform newlines.

### 9.6 Cross-Compilation

- `coorc --target <triple>`: a clang host receives `--target <triple>`; `--sysroot <path>` passes a sysroot.
- GCC cross toolchains are selected with `--cxx <cross-g++>`; the toolchain organizes arguments per `--cxx-flavor gnu`.
- MSVC cannot cross-compile; an MSVC host can only build Windows targets.
- A cross target cannot execute `coorc run` (running fails); only `check`/`build`/`--emit-cpp` are available.
- `coorc platform` prints the host and target triple plus executable/object/library naming; `coorc doctor` additionally checks compiler candidates, `curl`, config/cache directories and the manifest.

### 9.7 Retention, `#line` and Backend Constraints

- Runtime/Enum/Invocation materialization: side read-only tables live in the `coord_meta` namespace keyed by a stable `DeclarationId`; each retained annotation type gets narrow accessors; `@Inherit` effective lists are expanded at compile time; target layout, field order and vtable are unchanged (⏳ P2).
- Invocation hook emission points: before call `handler.before(args...)` (arguments by reference), after call `handler.after(result)`, field reads become `handler.on_get(field)`, field writes become `handler.on_set(field, value)`, after construct `handler.after_construct(this)`, and runtime proxy trap dispatch; value rewriting uses references/return slots without new types or object headers.
- `line(file, line)`: the backend maintains a source-mapping stack, pushing on entry to the annotated declaration/block and popping on exit; it emits `#line <line> "<file>"` before target code and restores the previous mapping afterwards. File strings escape `\` and `"`; lines must be ≥ 1; diagnostic coordinates still use the original `.coo` Span.
- The backend uses a restricted C++ subset only: C++20, no exceptions, no RTTI, no user-visible template metaprogramming; ABI, debug info and optimization belong to the host compiler. Host compiler defaults to `cl` on Windows and `c++` on POSIX, overridable via `--cxx`/`COORD_CXX`/`CXX`; flags are `/std:c++20 /EHsc /utf-8` (MSVC) or `-std=c++20 -O2 -pthread` (POSIX). Generated files are deleted by default, kept with `--keep-cpp`/`--emit-cpp`, and `--emit-cpp -` writes to stdout; every user identifier passes the C++ keyword check (suffix `_` on conflict), and string/character literals are escaped before emission.

## 10 Diagnostics

### 10.1 Error Codes

| Code | Meaning |
| --- | --- |
| `E1001` | Lexical error (unterminated string/comment, illegal character, numeric overflow, unsupported suffix) |
| `E1101` | Syntax error |
| `E2001` | Duplicate type declaration / duplicate constant |
| `E2002` | Duplicate declaration in one Scope / duplicate member / declaration not allowed at module level |
| `E2003` | Unknown type |
| `E2004` | Unknown identifier / illegal base class / illegal member access / `this` out of context |
| `E2005` | Type mismatch (assignment, argument, operator, const assignment, non-assignable Optional fallback) |
| `E2006` | Constructor name error |
| `E2007` | Destructor signature error |
| `E2008` | Illegal `~this.member;` (outside a destructor, unnamed field, missing field) |
| `E2009` | Metatype used in a runtime declaration |
| `E2010` | Interface method not implemented |
| `E2011` | `main` signature error (multiple definitions, wrong parameters, non-void/integer return) |
| `E2012` | Feature not implemented yet (arrays, `match`, etc.) |
| `E2013` | Interfaces cannot declare fields |
| `E2014` | Annotation bodies may only contain methods |
| `E2020` | Enum variant value is not an integer constant expression |
| `E2021` | `@Retention` on an enum must be `@Retention(enum)` |
| `E2022` | Duplicate enum variant name |
| `E2023` | Duplicate enum value |
| `E2024` | Enum has no such variant |
| `E2030` | Access out of bounds (field/method/constructor) |
| `E2031` | Inheriting a `final` class |
| `E2032` | Overriding a `final` method / hiding a `final` field |
| `E2033` | `override` without a virtual base method |
| `E2034` | Non-covariant return type |
| `E2035` | Overriding a virtual method without `override` (including interface implementations) |
| `E2039` | Non-escaping `&` returned, stored in a field/container, passed across threads, or captured by an escaping closure — also a by-reference closure returned, stored in a field/container literal, or stored in an outer-scope variable |
| `E2040` | Annotation target mismatch (`@Target`) |
| `E2044` | Illegal `@Retention` stage on a code block (only `Compile`/`Runtime`; `Invocation`/`enum` report this) |
| `E2045` | `@Unique` copied, used after move, or borrowing a moved `@Unique` |
| `E2046` | Assigning null to a non-`@Optional` `T*`/`@Unique`/`@Share` |
| `E2047` | `@Optional` applied to a type other than `T*`/`@Unique`/`@Share` |
| `E2048` | Direct use of an unhandled proxy under `ProxyPolicy.Error`: un-narrowed Optional access, or proxy construction/destructuring without a handling path |
| `E2049` | `A a{...}` on a non-aggregate, or too many initializers |
| `E2050` | Calling a `@Result` function from a non-`@Result`/`@Expect` function |
| `E2051` | One function marked both `@Result` and `@Expect` |
| `E2052` | `error(...)` outside a `@Result`/`@Expect` return position, or bad `@Result main` return type |
| `E2053` | One type marked both `@Signed` and `@Unsigned` |
| `E2054` | `@Signed`/`@Unsigned` on a non-`integer` type |
| `E2055` | `@Expect` result not consumed (discarded as a statement) |
| `E2056` | `@Length` is not 8/16/32/64/128 |
| `E2057` | `@Length` on a non-basic numeric type, or `@Length(128)` on a non-integer |
| `E2058` | `@Processor` method does not return void |
| `E2059` | `@Processor` method does not take a single `description` |
| `E2060` | An annotation declares multiple `@Processor` methods |
| `E2061` | `@Processor` outside an annotation body |
| `E2063` | `@Proxy` handler/trap invalid, missing trap, or illegal policy; illegal `@Destructure`/`@Construct` hook signature |
| `E2064` | `@Test` is not a zero-argument void function, or bad test/assertion arguments |
| `E2065` | `@Unique`/`@Share` used together or on a non-class/pointer type |
| `E2066` | `@Invocation` handler missing/not a class/missing public void before()/after() |
| `E2067` | Processor body uses an unsupported statement/expression/description method |
| `E2068` | Processor generation exceeded the round limit (default 8) |
| `E2069` | `Reflector<T>` index out of bounds |
| `E2070` | Unresolvable import |
| `E2071` | Standard annotation library cannot be read |
| `E2073` | `@Result/@Expect` missing its expected type or mismatching the return type |
| `E2074` | `@Proxy` handler does not implement `ProxyHandler` or a subinterface |
| `E2075` | `@Invocation` handler does not implement `InvocationHandler` |
| `E2076` | No matching or ambiguous `@Constructure` constructor for a typed/named literal; also used for illegal/duplicate documentation tags |
| `E2077` | `match` is not exhaustive (missing enum variant/boolean value, or no `_` for another scalar), the value type is unsupported, a pattern does not fit the value, a range is not on integer/character, or a binding/or-pattern is illegal |
| `E2087` | Invalid lambda capture: unknown identifier, duplicate capture, misplaced/repeated default capture, mixed default mode, unlisted outer name with an explicit list, implicit `this` without `[this]` |
| `E2088` | Assignment or `++`/`--` to an explicit by-value lambda capture (read-only in v0) |
| `E2089` | Invalid `@FunctionalInterface`: not exactly one abstract method, or a functional-interface type reference with the wrong number of type arguments |
| `E2090` | Thread/ScopedThread rules: bad entry call/signature, by-reference/`this` capture, call-scoped function value, copying/assigning a thread value, or `hardwareConcurrency()` with arguments |
| `E2091` | Mutex values cannot be copied or assigned |
| `E2092` | `ScopedLock`/`Atomic` values cannot be copied or assigned, or a `ScopedLock` is not initialized with `ScopedLock(mutex)` |
| `E2096` | `@Unsafe` violation: raw pointer dereference, or calling an `@Unsafe`/`@Native` declaration/method, from a safe scope |\n| `E2095` | `@Copy` errors: missing/invalid mode, `@Copy(Deep)` with `@Share`/pointer/thread/mutex fields or shallow fields, `@Copy(Shallow)` with `@Unique`, copying a class with owning fields without an explicit `@Copy` |
| `E2094` | `@Lock` capability errors: legacy syntax, non-Mutex/unnamed mutex argument, writing an uncaptured or `const&` object, calling a non-`@ReadOnly` method through `const&`, and value captures of class/container/pointer types |
| `E2080`–`E2086` | Planned Scope-specific diagnostics (not visible, same-scope conflict, ambiguity, use before declaration, import conflict, private access, initialization cycle) |
| `E3001` | Compile-time evaluation error (fuel exhausted, recursion limit, division by zero, etc.) |

### 10.2 Warning Codes

| Code | Meaning |
| --- | --- |
| `W0001` | User destructor omits the destruction declaration for a class-typed member |
| `W0002` | Destruction declaration for a non-class member |
| `W0003` | Duplicate destruction declaration (last wins) |
| `W0004` | Lambda parameter shadows an outer local/parameter |
| `W0005` | `@Share` reference cycle prevents release (leak warning) |

### 10.3 Diagnostic Rendering

Diagnostic format: `path:line:col: error[E2030]: message`, followed by the source line and a `^~~~` span indicator; lines and columns are 1-based. `-Werror` promotes warnings while keeping their codes for suppression. `Span` always points at the original `.coo`; generated `#line` directives only affect downstream C++ debug/diagnostic locations.

## 11 Implementation Status and Roadmap

### 11.1 Status Overview

| Capability | Status | Notes |
| --- | --- | --- |
| Scope / shadowing / same-scope redeclaration / reverse local destruction | ✅ | One Scope per `{}`; `E2002`; interpreter mirrors it |
| Classes, constructors/destructors, methods, `this`, single inheritance, `implements` | ✅ | M0 vertical slice |
| Synthetic RAII destructors, `W0001/W0002/W0003`, `~this.member;` erasure | ✅ | Backend `~T() = default;` + reverse-order comment |
| `enumerate`: variant values, `Color.Red`, methods, `implements`, `print`, exhaustive `match` | ✅ | Variant `match` works; full pattern matching ⏳ |
| Generics `<N extends integer>` monomorphized to C++ templates | ✅ | `requires std::is_integral_v<N>` |
| `const` compile-time evaluation (no `comptime`) | ✅ | Fuel 2,000,000 / recursion 512 |
| Unicode/UTF-16/32/BOM and Unicode identifiers | ✅ | Internal UTF-8; `char` is 32-bit |
| `T*` (including `const T*`, `T&`) | ✅ | Postfix syntax + backend mapping |
| C++ backend `build` produces an executable | ✅ | Invokes host `cl`/`c++` |
| Module/package/dependency headers (Module/Package/Project/Dependency) | ✅/⏳ | `Package`/`Dependency` wired into `coorc deps` and project mode; `Module`/`Project` remain headers/tests |
| AnnotationRegistry / Descriptor / Outline / Hint / Interface / Processor | ✅/🚧 | Processor-generated declarations, outline, `.coox` wired into driver; Hint lacks CLI |
| Stream / File | ✅ | LF normalization, line/byte-offset access |
| Access sections + default `private` + access checks | ✅ | `E2030` |
| `final` classes/methods/fields + same-name field hiding | ✅ | `E2031`/`E2032` |
| `virtual`/`override` enforcement + covariant returns | ✅ | `E2033`/`E2035`/`E2034` |
| `super.method()` / `super.field` / `: super(args)` / `: field(expr)` | ✅ | Covered by e2e |
| Enum methods / `implements` | ✅ | Wrapper class + underlying integer |
| Virtual dispatch through base references/pointers | ✅ | `examples/polymorphism.coo` |
| `@Share` cycle detection | ✅/⏳ | Type-graph + direct self-assignment `W0005`; runtime records assignment edges (including constructor bodies/initializer lists) and DFS-detects cycles at shutdown; in-class field initializers/aliases partial; `coord_e2e_share_cycle`, `coord_e2e_share_cycle_ctor` |
| `&` borrow / `@Unique`/`@Share` escape (JPA cascade) | 🚧 | `E2039` return/field/container references plus by-reference closure escapes, `E2045` self-move/use-after-move, `W0005` direct `@Share` self-cycle; full borrow/lifetime analysis and cross-thread checks ⏳ |
| Meta-annotations `@Inherit/@Repeatable/@Target/@Meta/@Retention/@Documented` | ✅ | Validation + `@Inherit` effective lists |
| Runtime/Enum retention tables / annotation comments | ✅ | `kRetainedAnnotations` / `k<Enum>Variants`; Invocation hooks P4 |
| `integer` `@Length` 8/16/32/64/128 + `@Signed/@Unsigned` | ✅ | 128-bit via `coord_rt::UInt128` |
| `Result` / `Expect` | ✅ | Type-argument validation, distinct type, consumption checks |
| Annotation processors, `line`, proxy, five reflection layers | ✅/🚧 | Processor basics, `line`, proxy class/annotation/runtime, `@Invocation`, `Reflector` work; full descriptor/runtime reflection continues |
| `.coox`, multi-module linking, standard library | 🚧 | Relative imports, project dirs, `.coox`, outline/hint, `@Test`, `coorc deps`, containers work; remaining library APIs pending |
| Package manager and mirror manager | ✅ | Manifests, backtracking, lockfile, vendor, file/HTTP registries, SHA-256, `[trust]` checksum/HTTPS policy, offline, mirror failover, pack/publish |
| Cross-platform toolchain (Windows/macOS/Linux/FreeBSD) | ✅ | Host discovery, argv spawn, UTF-8 paths, CMake presets; Linux/macOS verified with g++ |
| Cross-platform generated programs (wmain/UTF-8 console/platform builtins) | ✅ | `coord_e2e_platform` covers OS/path/file/env builtins |
| `ProxyPolicy` on local `T*`/`@Unique`/`@Share` | ✅ | `Error`/`Skip`/`Abort`/`Default` for direct dereference, member access, implicit non-null conversion and local Optional destructuring; narrowing; `coord_e2e_optional_policies` / `_error` / `_implicit` |
| `@Proxy(handler, policy)` destructuring + `@Construct`/`@Destructure` hooks | ✅ | `Skip`/`Abort`/`Error` destructuring; static `@Construct` and static/runtime `@Destructure` hooks; `coord_e2e_proxy_construct_destructure` |
| `ProxyHandler` subinterfaces (`MethodProxyHandler`, `DestructureProxyHandler`, `ConstructureProxyHandler`, `ClassProxyHandler`, `ObjectProxyHandler`, `FunctionProxyHandler`) | ✅ | `interface X extends ProxyHandler` implemented; implementing a subinterface satisfies `@Proxy` |
| `@Constructure` typed/named literals | ✅ | `examples/constructure.coo`; negative `constructure_error` (`E2076`); `@Proxy` classes accept typed/named construction by default |
| `@Weak` C++ mapping + weak API | ✅ | `std::weak_ptr<T>`; local/field/parameter/return positions; `isPresent/isEmpty/lock/upgrade`; `coord_e2e_weak` |
| Default initialization | ✅ | Locals, fields, arrays and object-literal temporaries emit `{}`; `coord_e2e_default_init` |
| Checked integer arithmetic | ✅ | `+ - * / %`, `++/--`, `+= -= *= /= %=` use overflow/div-zero helpers on 64-bit and 128-bit paths; `<<`/`>>` use range/overflow helpers for 8/16/32/64/128-bit; `coord_e2e_division`, `coord_e2e_overflow`, `coord_e2e_compound`, `coord_e2e_shift`, `coord_e2e_share_cycle`, `coord_e2e_share_cycle_ctor` |
| Bounds-checked `[]` | ✅ | `Array`/fixed array and `ArrayView` indexing use `checkedIndex`/`checkedIndexView` and abort out of range; raw pointer indexing stays unsafe; `coord_e2e_bounds` |
| `@Destructure` target method + ProxyHandler trap | ✅ | `coord_e2e_destructure_annotation`; per-binding hook |
| Block retention (`@Retention(Compile/Runtime)` blocks) | ✅ | `examples/retention_block.coo` and `coord_e2e_retention_block`, `coord_e2e_radix`, `coord_e2e_radix_error`, `coord_e2e_default_init`, `coord_e2e_weak`, `coord_e2e_bounds`, `coord_e2e_optional_params`, `coord_e2e_optional_fields`, `coord_e2e_optional_return`, `coord_e2e_division`, `coord_e2e_overflow`, `coord_e2e_compound`, `coord_e2e_shift`, `coord_e2e_share_cycle`, `coord_e2e_share_cycle_ctor`; `Invocation`/`enum` on a block reports `E2044` |
| `@Optional` params, fields and returns (policy execution) | ✅ | Parameters/fields/returns support `Error`/`Skip`/`Abort`/`Default`, `.isPresent()/.get()/.orElse()`; `coord_e2e_optional_params`, `coord_e2e_optional_fields`, `coord_e2e_optional_return` |
| `@Optional` on returns; `@Proxy` `Default(expr)` construction/destructuring; runtime pre-construction hook; `Skip` defaults for arbitrary non-default-constructible classes | 🚧 | Return-value policy execution and some construction/destructuring fallbacks remain partial |
| `@Has/@Delete/@Keys/@Describe` traps | ⏳ | Declared, not expanded |
| ASan/UBSan + parser/compiler fuzz | ✅ | `COORD_SANITIZE`, `COORD_FUZZ`, `linux-sanitize`; parser + compiler harnesses; diagnostic cap 256 |
| Cross-compilation `--target`/`--sysroot` | 🚧 | Design locked, implementation in progress |
| workspace / features / signatures / git dependencies | ⏳ | Workspace `members` parsed only |
| Thread model | 🚧 | Built-in move-only `Thread` from `() -> void` with `join()`/`detach()`/`joinable()`/`hardwareConcurrency()` and cross-thread capture checks (`E2090`); built-in non-copyable `Mutex` with manual `lock()`/`unlock()`/`tryLock()` (`E2091`, `coord_e2e_mutex`), RAII `ScopedLock` (`E2092`), `ScopedThread`/`std::jthread` auto-join (`coord_e2e_scoped_thread`), seq_cst `Atomic` with `load`/`store`/`fetchAdd`/`fetchSub`/`exchange`/`compareExchange` (`coord_e2e_atomic`, `coord_e2e_atomic_ops`), and `@Lock(Exclusive, mutex, [captures])` block annotations with value/mutable/const captures, `@ReadOnly` method effects and write-set checks (`E2094`, `coord_e2e_lock_capture`), explicit `@Copy(Deep|Shallow)` with generated deep copies for `@Unique` trees (`E2095`, `coord_e2e_copy_deep`); data-race safety, atomics of other types, condition variables, scoped threads and non-local positions ⏳ |
| lambda / `if` expressions / full pattern matching | 🚧 | Lambda subset: function-type target, expression/block body, implicit/explicit captures (`E2087` capture validation, `E2088` read-only by-value), by-reference escape checks (`E2039`), assignment/argument/return, static/bound method references and chained calls; `@FunctionalInterface` targets lower to function values with generic substitution (`E2089`, `Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction` in `lib/standard/utility.coo`, tests `coord_e2e_functional_interface*`); nominal functional-interface values/class implementations, unbound instance refs, function-pointer thunks and cross-thread escape ⏳; tests `coord_e2e_lambda*`, `coord_e2e_method_ref*`. `if` expressions with expression branches work (`coord_e2e_if_expression`; block branches ⏳). `match` supports constants/or/ranges/bindings/guards with enum/boolean/scalar exhaustiveness (`coord_e2e_match_patterns`, negatives `_missing`/`_no_wildcard`/`_bad_pattern`); type patterns and class destructuring ⏳ |
| Safe/Unsafe | 🚧 | `@Safe`/`@Unsafe` scopes with downward propagation and block override (`E2096`); v0 enforces raw-pointer dereference and FFI/`@Native` calls, pointer arithmetic/unsafe casts documented but not yet fully enforced |\n| `@Native` / FFI | 🚧 | Body-less primitive/string functions call a C symbol through a generated wrapper; `coord_e2e_native_ffi`, `coord_e2e_native_ffi_string`; classes/pointers/headers/libraries ⏳ |
| `@Copy` / `@Wrap` / `@FunctionalInterface` semantics | ⏳ | Annotations declared |
| `Weak<T>` / shared-cycle detection | ⏳ | Design and mapping finalized |
| `is` / `new` / `delete` / `unsafe` / `extern` | ⏳ | Reserved, not implemented |
| `operator` / `where` | 🚧 | Parseable; full semantics pending |
| `T?` | ⏳ | Not parsed |

### 11.2 P1 Detail

Completed: access-section parsing with default `private`; field/method/constructor access checks (`E2030`); `final` classes/methods/fields (`E2031`/`E2032`); mandatory `virtual` + `override` (`E2033`/`E2035`); covariant returns (`E2034`); same-name field hiding and `final` field hiding prohibition; C++ backend access labels, `class X final`, `virtual`/`override`/`final`, virtual destructors; tests `testAccessControl`, `testInheritanceRules`, `testAccessCodegen`.

### 11.3 Long-Term Goals and Execution Plan (M3)

**Goal**: complete the M3 minimum closed loop on the 190-test baseline established after descriptor unification, the package manager and the lambda/match/thread/synchronization/copy/safety slices.

**Baseline (2026-11-28)**: `ctest` registers **190 tests**, with **190/190 passing on Windows**; on Linux, `coord_unit` + `coord_e2e_packages` and the newly added examples pass with g++. P0–P3, descriptor unification, collections/test, relative imports, project directories, `.coox` read/write, outline/hint, basic proxy/reflection/ownership, `ProxyPolicy`, `@Constructure`/`@Destructure`, block retention, main parameter forms, plus the cross-platform driver, package manager, mirror manager and registry are in place.

**Final acceptance**:

1. A workspace with two dependent packages builds;
2. runtime proxy/reflection can be demonstrated;
3. `@Unique/@Share` fields/parameters are usable;
4. the minimum standard-library API passes e2e;
5. every stage has a clean build and a fully green `ctest`.

**Stage plan**:

| Stage | Rounds | Deliverables | Acceptance |
| --- | --- | --- | --- |
| A P4 runtime | R1–R6 | Explicit `@Retention(runtime)`; method/field per-object handlers; destructor release; runtime member/inheritance/type reflection; runtime-table vs reflector consistency e2e | Runtime proxy positive/negative cases; member/inheritance query e2e |
| B Ownership/Invocation | R7–R12 | Ownership for fields/parameters/returns/initializer lists; `E2045` after move; multi-handler functions/methods; Invocation follows virtual dispatch | Ownership e2e; move negatives; multi-handler/virtual e2e |
| C Modules/packages | R13–R20 | Two-package builds/`coorc deps`/cycle e2e; `.coox` read/write; `--incremental`; selective imports; aliased functions and parameter types | Two-package dependency builds; `.coox` change triggers incrementality; cycles error |
| D Standard library v0 | R21–R28 | Standard annotations read from `lib/standard/annotations.coo`; explicit imports, minimal memory/io/fs/sync APIs, `@Test` integration | No C++-injected standard annotations; File/Stream/Sync e2e |
| E lambda/match | R29–R33 | Java-semantics lambdas; exhaustive enum/match | Lambda capture/call e2e; missing-variant error |
| F Wrap-up | R34–R36 | Integration, negatives, determinism, docs, performance | Clean build; full ctest; documentation calibration |

**Stage gates**: every stage ends with a `--clean-first` build and a fully green `ctest`; every feature needs parser/Sema/backend or module tests plus at least one negative case; partial work is never reported as complete; a cross-stage change that breaks passing tests is reverted first.

**Execution rules**: each batch adds tests/spec first, then Parser → Sema → Codegen; every feature needs a positive case, at least one negative case, a generated-C++ inspection and e2e where relevant; only the four main documents are maintained; meta-annotations live in C++ and all other annotations in the Coo standard library; generated declarations must re-enter Sema, and textual splicing is forbidden.

### 11.4 Next Milestone (M4)

M4 is reserved for backend work and self-hosting: LLVM or a custom backend, layout/vtable/error ABI/debug info, and self-hosting. M4 is outside this goal.

### 11.5 Cross-Cutting Risks and Open Questions

| Risk | Symptom | Mitigation |
| --- | --- | --- |
| Compile-time blow-up | Slow interpreter, high memory | Fuel + memory limits + content cache (⏳); lazy instantiation |
| Hidden Result ABI | Unstable across C++/module boundaries | Define the Coo ABI first; explicit wrapping at boundaries |
| Over-powerful annotations/proxies | Become de facto macros | New declarations only; no rewriting/text splicing |
| Unstable reflection API | Brittle user code | Five-layer evolution; `.coox` versioning |
| C++ backend leakage | Layout/exception semantic mismatch | Restricted subset in generated code; observable e2e tests |
| Diagnostic quality | Generated-code errors are hard to locate | `#line` mapping + source Spans on generated nodes |
| `ProxyPolicy` divergence | `Skip`/`Abort`/`Default` semantics drift across Optional, `@Proxy`, construction and destructuring | This spec locks four policies and narrowing; e2e per policy |
| Cross-platform entry differences | argv/console/path encoding mismatch | Generated prelude normalizes wmain/main, UTF-8 console and filesystem |

Open questions: ① final `description`/`descriptor` API names and members; processor return syntax (`declaration { ... }` vs `description[]`). ② exact proxy trap signatures and `default_*` set; generation round limit and conflict resolution. ③ concrete lowering of `Result`/`Expect` in virtual/interface calls and `main`; the `@Expect` consumption boundary. ④ `.coox` binary format and version compatibility; workspace recursive install and feature combinations. ⑤ standard-library namespace and `string` API surface; whether concurrency needs `async`; registry signatures/trust chain and private-source authentication. ⑥ the minimum viable self-hosting subset. ⑦ `ProxyPolicy(Skip)`/`Default(expr)` behavior when default construction is unavailable; implicit optional→non-optional boundaries; runtime-handler construct hooks.

## 12 Appendices

### 12.1 Grammar Summary

Below is the currently implemented subset (ISO/IEC 14977 EBNF style); the full grammar (including planned productions) is in [docs/grammar.ebnf](docs/grammar.ebnf). Planned productions are not implemented; the parser reports `E1101` or Sema reports `E2012`.

```ebnf
compilation-unit = [ "module" path ";" ], { import-decl }, { top-level-decl } ;
import-decl      = "import", path, [ "as", identifier ],
                   [ ".", "{", identifier, { ",", identifier }, "}" ], ";" ;
top-level-decl   = { annotation-use }, [ "export" ],
                   ( class-decl | interface-decl | annotation-decl
                   | enum-decl | function-decl | const-decl ) ;

class-decl       = [ "final" ], "class", identifier, [ generic-params ],
                   [ "extends" type ], [ "implements" type, { ",", type } ],
                   class-body, [ ";" ] ;
class-body       = "{", { access-section | class-member }, "}" ;
access-section   = ( "public" | "private" | "protected" ), ":" ;
class-member     = { annotation-use },
                   ( field-decl | method-decl | constructor-decl | destructor-decl ) ;
field-decl       = [ "final" ], type, identifier, [ "=", expression ], ";" ;
method-decl      = { "virtual" | "override" | "final" | "static" },
                   type, identifier, [ generic-params ], "(", params, ")",
                   [ "const" ], ( block | ";" ) ;
constructor-decl = identifier, "(", params, ")",
                   [ ":", initializer, { ",", initializer } ], block ;
destructor-decl  = "~", identifier, "(", ")", block ;

enum-decl        = "enumerate", identifier, [ "implements", type, { ",", type } ],
                   "{", [ enum-variant, { ",", enum-variant }, [ "," ] ],
                   [ ";", { method-decl } ], "}", [ ";" ] ;
enum-variant     = { annotation-use }, identifier, [ "=", expression ] ;
interface-decl   = "interface", identifier, [ generic-params ], "{", { method-decl }, "}" ;
annotation-decl  = "annotation", identifier, [ generic-params ], [ "(", params, ")" ],
                   ( ";" | "{", { method-decl }, "}" ) ;
function-decl    = type, ( identifier | operator-name ), [ generic-params ],
                   "(", params, ")", [ "where", constraint, { ",", constraint } ],
                   ( block | ";" ) ;
const-decl       = "const", [ type ], identifier, "=", expression, ";" ;

generic-params   = "<", identifier, [ "extends", type ],
                   { ",", identifier, [ "extends", type ] }, ">" ;
params           = [ param, { ",", param } ] ;
param            = type, identifier, [ "=", expression ] ;
type             = [ "const" ], path, [ "<", type, { ",", type }, ">" ],
                   { "&" | "*" | "[", expression, "]" } ;              (* T?: planned *)

block            = "{", { statement }, "}" ;
statement        = block | var-decl
                 | "if", "(", expression, ")", statement, [ "else", statement ]
                 | "while", "(", expression, ")", statement
                 | "return", [ expression ], ";" | "break", ";" | "continue", ";"
                 | "~", postfix-expression, ";"                        (* destruction declaration *)
                 | expression, ";" ;
var-decl         = [ "const" ], type, identifier, [ "=", expression ], ";" ;
expression       = assignment | logical-or | ... ;                    (* C++ same-precedence subset *)

annotation-use   = "@", path, [ "(", arguments, ")" ] ;
(* planned: for / in, unsafe, match and full patterns (_, literal, path(...), p | p, p if e) *)
```

### 12.2 Glossary

| Term | Meaning |
| --- | --- |
| Root type | One of the 14 top-level categories of the type universe; every type ultimately falls into one, plus `string` for 15 entries |
| Metatype | `annotation`/`Description`/`Descriptor`/`Reflection`/`Reflector`/`Proxy`; compile-time only (`E2009`) |
| Scope | The visibility and lifetime boundary introduced by each `{}`; Module/Type/Function/Block |
| Scope Graph | Name-resolution graph of scopes/declarations/references and LEX/MEM/EXT/IMP/EXP/TP edges |
| Phase | Compile time or runtime; determined by syntax context, with no keyword |
| `const` initializer | A constant expression interpreted during ConstEval |
| description / descriptor | Read-only snapshot of declaration properties (data) / a processor of those properties (behavior) |
| reflection / `Reflector<T>` | Erased reflection cursor / typed reflection view with static member checks |
| proxy / trap | JS-style behavior proxy / one behavior hook (`get`, `set`, `call`, `construct`, `destructure`, etc.) |
| Retention | Retention stage: Compile (default)/Runtime/Invocation; Enum for enumerations |
| Side table | Read-only metadata materialized by retention, never part of object layout |
| Synthetic destructor | Compiler-generated destructor used when the user declares none; reverse declaration order |
| Monomorphization | Generating a concrete instantiation per generic argument, mapped to C++ templates |
| Directive primitive | The only compiler directive a processor may call: `line(file, line)` |
| Span | `{file, begin, end}`, a half-open range of normalized UTF-8 byte offsets |
| Optional proxy | Internal lowering proxy that carries `ProxyPolicy` and sits outside a user `@Proxy` |
| `ProxyPolicy` | Shared policy for all proxy objects: `Error` (default for `@Optional`), `Skip`, `Abort`, `Default(expr)`; `@Proxy` defaults to `Skip` |
| `@Constructure` | `@Target(constructor)` marker enabling typed/named brace construction; type labels are optional |
| `@Destructure` | Dual-form annotation: a `ProxyHandler` trap or a target-class method with reference outputs |
| Block retention | `@Retention(Compile)` executes at compile time and is omitted by the backend; `@Retention(Runtime)` is emitted and runs normally |

### 12.3 Documentation Map and Archive Policy

- English framework: this document; Chinese framework: [framework.zh-CN.md](framework.zh-CN.md).
- English QuickStart: [readme.md](readme.md); Chinese QuickStart: [readme.zh-CN.md](readme.zh-CN.md).
- Formal grammar: [docs/grammar.ebnf](docs/grammar.ebnf).
- Old architecture, spec, reflection, compiler, roadmap and implementation-plan documents under [docs/archive/](docs/archive/) are **historical archive only** and are no longer updated; this document wins on conflict. `docs/archive/index.md` is itself archived, and its links are only guaranteed within the archive.
- Maintenance rule: language/framework/backend/package-protocol changes touch only the four main documents; `docs/grammar.ebnf` tracks the parser; the archive is never modified.



