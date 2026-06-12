# Nurt

**English** | [Polski](README.pl.md)

**Nurt** (Polish: *current*, *stream*) is a pedagogical programming language developed for a
Master's Thesis. Its compiler, `nurtc`, is written in modern C++20 and emits pure Linux
x86_64 NASM assembly (Intel syntax, System V AMD64 ABI).

The central metaphor of the language is *flow*: values flow with the current (nurt) into
variables via the left-to-right arrow `->`, and flow back out of functions via `<-`.

## Language at a glance

```nurt
# Sum of two integers

powolaj suma(#a, #b) -> #:
    #a + #b -> #wynik
    <- #wynik
koniec

10 -> #x
32 -> #y
suma(#x, #y) -> #z

dopoki #z > 0 rob:
    #z - 1 -> #z
koniec

prawda -> ?gotowe
"Witaj, Nurt!\n" -> $powitanie

?gotowe ?
| prawda =>:
    pisz($powitanie)
| falsz =>:
    pisz("jeszcze nie...\n")
koniec
```

| Element | Syntax | Meaning |
| --- | --- | --- |
| Function definition | `powolaj nazwa(...)` ... `koniec` | "summon" a function |
| Return type | `-> #:` / `-> $:` / `-> ?:` | optional annotation on function header |
| Block terminator | `koniec` | ends functions, loops, queries, and match blocks |
| Assignment | `wartosc -> #x` | value flows left-to-right into a variable |
| Return | `<- wyrazenie` | value flows back out of the function |
| Int sigil | `#x` | 64-bit signed integer |
| String sigil | `$s` | string |
| Bool sigil | `?b` | boolean (`prawda` / `falsz`) |
| While loop | `dopoki warunek rob:` ... `koniec` | loop while condition holds |
| Control flow query | `?warunek ?` ... `koniec` | branch on a boolean (`\| prawda =>:` / `\| falsz =>:`) |
| Match | `dopasuj expr` ... `\| inaczej =>:` ... `koniec` | multi-way dispatch on a value |
| Logical AND / OR | `i` / `lub` | Polish keyword operators (not `&&` / `\|\|`) |
| Built-ins | `pisz(...)`, `bierz()` | write to stdout / read from stdin |
| Comment | `# tekst` | `#` not followed by an identifier or `:` starts a line comment |

File extension: **`.nrt`**

See [docs/language-spec.md](docs/language-spec.md) for the full specification and
[docs/architektura_systemu.md](docs/architektura_systemu.md) for the compiler architecture
(Polish).

## Compilation pipeline

```
.nrt  --(nurtc)-->  .asm  --(nasm -f elf64)-->  .o  --(gcc -no-pie)-->  executable
```

`nurtc` runs four internal phases in order:

1. **Lexer** — token stream (keywords, sigils, operators, literals)
2. **Parser** — recursive descent + Pratt precedence climbing → AST
3. **Semantic analyzer** — sigil-driven type checking, symbol tables
4. **Code generator** — NASM x86_64 output

Each phase reports diagnostics with source context; compilation stops before the next
phase if errors are present.

## Repository layout

```
├── CMakeLists.txt              # Top-level build (C++20)
├── README.md / README.pl.md    # Project overview (EN / PL)
├── docs/
│   ├── language-spec.md        # Language specification
│   └── architektura_systemu.md # Compiler architecture (PL)
├── src/
│   ├── main.cpp                # nurtc driver
│   ├── common/                 # Source locations, diagnostics
│   ├── lexer/                  # Lexical analyzer
│   ├── parser/                 # AST + recursive-descent parser
│   ├── semantic/               # Sigil-driven type checking
│   └── codegen/                # x86_64 NASM emitter
├── examples/                   # Sample .nrt programs
├── scripts/
│   └── wypusc.sh               # Full build-and-run pipeline (Linux)
└── tests/                      # GoogleTest suites (lexer, parser, semantic, dopasuj)
```

## Building

Requires CMake >= 3.20 and a C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with MSVC, pass the configuration explicitly:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

The first configure downloads GoogleTest via CMake `FetchContent` (network required once).

## Usage

Compile a `.nrt` file to NASM assembly (default output: `<input>.asm`):

```bash
./build/nurtc examples/hello.nrt
./build/nurtc examples/hello.nrt -o build/wyjscie/hello.asm
```

Debug flags stop the pipeline early:

```bash
./build/nurtc examples/hello.nrt --tokens   # dump token stream
./build/nurtc examples/hello.nrt --ast      # dump abstract syntax tree
```

On Linux, run the full pipeline (compile → assemble → link → execute) with:

```bash
scripts/wypusc.sh examples/hello.nrt
```

Artifacts land in `build/wyjscie/`.

## Examples

| File | Demonstrates |
| --- | --- |
| [examples/hello.nrt](examples/hello.nrt) | Functions, loops, queries, built-ins |
| [examples/kalkulator.nrt](examples/kalkulator.nrt) | Interactive calculator |
| [examples/warunek.nrt](examples/warunek.nrt) | Control flow queries |
| [examples/nwd.nrt](examples/nwd.nrt) | GCD via Euclid's algorithm |
| [examples/logowanie.nrt](examples/logowanie.nrt) | Simple login prompt |

## Tests

Four GoogleTest executables cover the compiler stages:

- `lexer_tests` — tokenization, sigil/comment disambiguation, literals
- `parser_tests` — grammar, AST shape, precedence
- `semantic_tests` — type checking, symbol resolution
- `dopasuj_tests` — match statement semantics

Run all tests with `ctest --test-dir build` (or `-C Debug` on MSVC).
