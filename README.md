# Nurt

**Nurt** (Polish: *current*, *stream*) is a pedagogical programming language developed for a
Master's Thesis. Its compiler, `nurtc`, is written in modern C++20 and emits pure Linux
x86_64 NASM assembly.

The central metaphor of the language is *flow*: values flow with the current (nurt) into
variables via the left-to-right arrow `->`, and flow back out of functions via `<-`.

## Language at a glance

```nurt
# Sum of two integers - a first Nurt program

powolaj suma(#a, #b)
    #a + #b -> #wynik
    <- #wynik
koniec

10 -> #x
32 -> #y
suma(#x, #y) -> #z
```

| Element | Syntax | Meaning |
| --- | --- | --- |
| Function definition | `powolaj nazwa(...)` ... `koniec` | "summon" a function |
| Block terminator | `koniec` | ends functions and control blocks |
| Assignment | `wartosc -> #x` | value flows left-to-right into a variable |
| Return | `<- wyrazenie` | value flows back out of the function |
| Int sigil | `#x` | 64-bit signed integer |
| String sigil | `$s` | string |
| Bool sigil | `?b` | boolean (`prawda` / `falsz`) |
| While loop | `dopoki warunek rob` ... `koniec` | loop while condition holds |
| Else branch | `inaczej` | alternative branch |
| Comment | `# tekst` | `#` not followed by an identifier starts a line comment |

File extension: **`.nrt`**

See [docs/language-spec.md](docs/language-spec.md) for the full specification.

## Repository layout

```
├── CMakeLists.txt        # Top-level build (C++20)
├── docs/                 # Language specification
├── src/
│   ├── main.cpp          # nurtc driver
│   ├── common/           # Source locations, diagnostics
│   ├── lexer/            # Tokenizer (Step 1)
│   ├── parser/           # Recursive-descent parser (Step 2)
│   ├── ast/              # Abstract syntax tree (Step 2)
│   ├── semantic/         # Sigil-driven type checking (Step 3)
│   └── codegen/          # x86_64 NASM emitter (Step 4)
├── runtime/              # NASM runtime stubs (print, exit)
├── examples/             # Sample .nrt programs
├── scripts/              # nasm + ld helper scripts
└── tests/                # GoogleTest suites
```

## Building

Requires CMake >= 3.20 and a C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Usage

```bash
nurtc examples/hello.nrt    # currently dumps the token stream (Step 1)
```
