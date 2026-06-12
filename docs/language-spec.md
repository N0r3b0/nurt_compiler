# Nurt Language Specification

Status: Step 5 (full pipeline incl. codegen; `dopasuj` match statement and keyword logical operators `i`/`lub`). This document grows with each compiler stage.

## 1. Source files

- Extension: `.nrt`
- Encoding: ASCII (UTF-8 bytes are passed through inside string literals and comments).
- Newlines: `\n` (a preceding `\r` is treated as whitespace).

## 2. Lexical structure

### 2.1 Whitespace

Space, tab, carriage return, and newline separate tokens and are otherwise ignored.

### 2.2 Comments

A `#` that is **not** immediately followed by an identifier-start character
(`[A-Za-z_]`) begins a comment that extends to the end of the line.

```nurt
# this is a comment
10 -> #x   # '#x' is a sigil reference; this trailing text is a comment
```

Disambiguation rule: `#` immediately followed by a letter or underscore is the
**int sigil** (token `HashSigil`), and `#` immediately followed by `:` is the int
sigil of a return type annotation (`-> #:`); in every other case (`# ` with a
space, `#` at end of line, `#1`, `#!`, ...) it starts a comment.

### 2.3 Keywords

| Keyword | Token | Meaning |
| --- | --- | --- |
| `powolaj` | `KwPowolaj` | defines (summons) a function |
| `koniec` | `KwKoniec` | ends a block |
| `inaczej` | `KwInaczej` | default branch of `dopasuj` |
| `dopoki` | `KwDopoki` | while |
| `rob` | `KwRob` | do (opens a loop body) |
| `prawda` | `KwPrawda` | boolean literal true |
| `falsz` | `KwFalsz` | boolean literal false |
| `dopasuj` | `KwDopasuj` | multi-branch match statement |
| `i` | `KwI` | logical AND operator |
| `lub` | `KwLub` | logical OR operator |

Keywords are reserved and case-sensitive. In particular `i` and `lub` cannot be
used as identifiers (`#i` is a lexical/syntactic error); words merely starting
with a keyword (`igla`, `lubie`, `dopasujmy`) remain ordinary identifiers.

### 2.4 Type sigils

Every variable reference is prefixed by a sigil that fixes its static type:

| Sigil | Token | Type |
| --- | --- | --- |
| `#` | `HashSigil` | 64-bit signed integer |
| `$` | `DollarSigil` | string |
| `?` | `QuestionSigil` | boolean |

The sigil and the following identifier are two separate tokens; the parser combines
them into a typed variable reference.

### 2.5 Flow operators

| Operator | Token | Meaning |
| --- | --- | --- |
| `->` | `Arrow` | assignment: `10 -> #x` (value flows into the variable) |
| `<-` | `ReturnArrow` | return: `<- #wynik` |
| `=>` | `FatArrow` | introduces a branch body in a control flow query |

### 2.6 Identifiers

`[A-Za-z_][A-Za-z0-9_]*`, excluding keywords.

### 2.7 Integer literals

`[0-9]+`, value must fit in a signed 64-bit integer. A digit sequence immediately
followed by an identifier character (e.g. `12abc`) is a lexical error. Negative
numbers are formed with the unary `-` operator at the parser level.

### 2.8 String literals

Double-quoted, single-line. Supported escape sequences: `\n`, `\t`, `\"`, `\\`, `\0`.
An unknown escape or an unterminated string is a lexical error.

### 2.9 Operators and punctuation

| Category | Tokens |
| --- | --- |
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `!` and the keywords `i` (AND), `lub` (OR) |
| Punctuation | `(` `)` `,` `:` `\|` |

Note: `<` followed by `-` always lexes as `ReturnArrow` (`<-`). Write `a < -b` with a
space to express "less than negative b". A lone `=` or `&` is a lexical error.
A lone `\|` (token `Pipe`) introduces a branch in a control flow query or a
`dopasuj` case. The symbolic logical operators `&&` and `\|\|` were removed in
favour of `i`/`lub`; both now produce a dedicated lexical error pointing at the
keyword replacement.

## 3. Grammar

Nurt is newline-insensitive: statements are delimited purely by the grammar, not
by line breaks. The parser is a recursive descent parser with Pratt-style
precedence climbing for expressions (`src/parser/parser.cpp`).

### 3.1 EBNF

```ebnf
program        = { statement } ;

statement      = functionDef
               | whileLoop
               | matchStmt
               | returnStmt
               | exprLedStmt ;

functionDef    = "powolaj" IDENT "(" [ paramList ] ")"
                     [ "->" SIGIL ] [ ":" ]
                     { statement }
                 "koniec" ;
paramList      = SIGIL IDENT { "," SIGIL IDENT } ;

whileLoop      = "dopoki" expression "rob" [ ":" ] { statement } "koniec" ;

matchStmt      = "dopasuj" expression
                     { "|" caseLiteral "=>" ":" { statement } }
                     "|" "inaczej" "=>" ":" { statement }
                 "koniec" ;
caseLiteral    = INTEGER | "-" INTEGER | STRING | "prawda" | "falsz" ;

returnStmt     = "<-" [ expression ] ;

exprLedStmt    = expression ( "->" SIGIL IDENT      (* assignment *)
                            | "?" queryBranches     (* control flow query *)
                            |                       (* expression statement *)
                            ) ;

queryBranches  = branch { branch } "koniec" ;
branch         = "|" ( "prawda" | "falsz" ) "=>" ":" { statement } ;

expression     = orExpr ;
orExpr         = andExpr  { "lub" andExpr } ;
andExpr        = equality { "i" equality } ;
equality       = comparison { ( "==" | "!=" ) comparison } ;
comparison     = additive { ( "<" | "<=" | ">" | ">=" ) additive } ;
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary          = ( "-" | "!" ) unary | primary ;
primary        = INTEGER | STRING | "prawda" | "falsz"
               | SIGIL IDENT                          (* variable reference *)
               | IDENT "(" [ argList ] ")"            (* function call *)
               | "(" expression ")" ;
argList        = expression { "," expression } ;

SIGIL          = "#" | "$" | "?" ;
```

### 3.2 Statement forms

- **Assignment** is inverted: the value expression is parsed first, then `->`
  binds it to the sigil-typed target: `#a + #b -> #wynik`.
- **Control flow query**: an expression followed by a bare `?` opens a query.
  Each branch starts with `|`, a `prawda`/`falsz` label, and `=>:`. At least one
  branch is required; each label may appear at most once; either order is allowed.

  ```nurt
  ?gotowe ?
  | prawda =>:
      pisz("tak\n")
  | falsz =>:
      pisz("nie\n")
  koniec
  ```

  Disambiguation: `?` directly followed by an identifier is a bool variable
  reference; a `?` *not* followed by an identifier is the query marker.
- **Loop**: `dopoki [condition] rob: [statements] koniec`. The `:` after `rob`
  is optional.
- **Match**: `dopasuj` evaluates a target expression once and dispatches to the
  first case whose literal equals the target's value. Cases hold literals only
  (integers, optionally negative; strings; `prawda`/`falsz`). The `inaczej`
  branch is **mandatory** and must come **last**.

  ```nurt
  dopasuj #x
  | 1 =>:
      pisz("jeden\n")
  | 2 =>:
      pisz("dwa\n")
  | inaczej =>:
      pisz("inne\n")
  koniec
  ```
- **Function**: `powolaj name(params) -> #: [statements] koniec`. The return
  type annotation `-> #:` (or `$:` / `?:`) is omitted for void functions; the
  block-opening `:` is optional. The `#` sigil must be written glued to the `:`
  (`#:`), otherwise it starts a comment (see section 2.2).
- **Return**: `<- [expression]`, or a bare `<-` in void functions.
- **Bare identifiers** are only valid as function call names (`suma(1, 2)`);
  every variable reference requires a sigil.

### 3.3 Built-in functions

| Name | Meaning |
| --- | --- |
| `pisz(...)` | write the arguments to standard output |
| `bierz()` | read a value from standard input |

The parser maps calls to these names onto a dedicated `BuiltinKind` so later
stages can lower them directly to runtime calls. The names are Polish to match
the language identity; `out`/`in` are ordinary identifiers and calling them is
an "undefined function" error (with a hint pointing at the Polish name).

### 3.4 Operator precedence

From loosest to tightest; all binary operators are left-associative:

| Level | Operators |
| --- | --- |
| 1 | `lub` |
| 2 | `i` |
| 3 | `==` `!=` |
| 4 | `<` `<=` `>` `>=` |
| 5 | `+` `-` |
| 6 | `*` `/` `%` |
| 7 | unary `-` `!` |

## 4. Static semantics

Checked by the semantic analyzer (`src/semantic/analyzer.cpp`) after parsing.
Every violation is a compile error with a source location; the analyzer keeps
going after an error so all problems in a file are reported in one pass.

### 4.1 Variables and scopes

- A variable is **declared by its first assignment**: `10 -> #x` brings `#x`
  into existence. There is no separate declaration syntax.
- The sigil of that first assignment **fixes the type permanently**. Using or
  reassigning the same name with a different sigil (`$x` after `10 -> #x`) is
  an error.
- Reading a variable before any (lexically earlier) assignment is an error.
- **Function bodies are isolated scopes**: they see their parameters and their
  own locals, never global variables. Data enters a function through
  parameters and leaves through `<-`.
- Locals and parameters do not leak out of the function.

### 4.2 Functions

- `powolaj` is only allowed at the top level; nested definitions are errors.
- A function must be **defined before the first call site**. Direct recursion
  is allowed (the signature is registered before the body is checked).
- Function names must be unique; the built-ins `pisz`/`bierz` cannot be
  redefined. Parameter names must be unique within a function.
- Calls must pass exactly as many arguments as there are parameters, and each
  argument's type must match the parameter's sigil.
- A function with a return type annotation (`-> #:`) must return a value **on
  every control path**. A query (`?`) guarantees a return only when both
  branches are present and both return; a `dopoki` body may never execute, so
  loops guarantee nothing.
- `<-` must match the declared return type exactly; a bare `<-` is only valid
  in void functions, and `<- wyrazenie` is an error in void functions.
- `<-` outside a function is an error.

### 4.3 Expression typing

| Operator | Operands | Result |
| --- | --- | --- |
| `+` `-` `*` `/` `%` | int, int | int |
| `<` `<=` `>` `>=` | int, int | bool |
| `==` `!=` | both the same type | bool |
| `i` `lub` | bool, bool | bool |
| unary `-` | int | int |
| `!` | bool | bool |

There are **no implicit conversions**. The result of a void function call has
no value and cannot appear where a value is required.

### 4.4 Statement typing

- Assignment: the type of the expression must equal the target's sigil type.
- `dopoki` and `?` query conditions must be bool (any bool-typed expression is
  accepted, e.g. `#x > 0 ?`).
- An expression statement whose value is not void produces a *warning* (the
  result is silently discarded).

### 4.5 `dopasuj` matching

- The target must have a value (a void call cannot be matched).
- Every case literal must have **exactly** the target's type; there are no
  conversions (`dopasuj #x` with a case `"jeden"` is an error).
- **Duplicate case values** within one `dopasuj` are errors. Equality follows
  the value, not the spelling: two `7` cases collide, two `"x"` cases collide,
  and `prawda`/`falsz` may each appear at most once.
- Return path coverage: a `dopasuj` guarantees a return only when **every**
  case body *and* the `inaczej` body guarantee one. Cases are never assumed
  exhaustive — the mandatory `inaczej` covers all unmatched values.

### 4.6 Built-in functions

- `pisz(a, b, ...)` accepts **one or more** arguments of any value type
  (int, string, bool) and yields no value.
- `bierz()` takes **no arguments** and has no type of its own: it must flow
  directly into a variable (`bierz() -> #x` reads an int, `bierz() -> $s`
  reads a line). Reading into a bool, or using `bierz()` inside a larger
  expression, is an error.
