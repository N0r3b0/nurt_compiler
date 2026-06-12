# Nurt

[English](README.md) | **Polski**

**Nurt** to pedagogiczny język programowania stworzony na potrzeby pracy magisterskiej.
Kompilator `nurtc` jest napisany w nowoczesnym C++20 i generuje czysty asembler NASM
dla Linux x86_64 (składnia Intel, ABI System V AMD64).

Centralna metafora języka to *przepływ* (nurt): wartości płyną nurtem do zmiennych
poprzez strzałkę od lewej do prawej `->`, a z funkcji wracają przez `<-`.

## Język w skrócie

```nurt
# Suma dwóch liczb całkowitych

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

| Element | Składnia | Znaczenie |
| --- | --- | --- |
| Definicja funkcji | `powolaj nazwa(...)` ... `koniec` | „przywołanie" funkcji |
| Typ zwracany | `-> #:` / `-> $:` / `-> ?:` | opcjonalna adnotacja w nagłówku funkcji |
| Koniec bloku | `koniec` | kończy funkcje, pętle, zapytania i dopasowania |
| Przypisanie | `wartosc -> #x` | wartość płynie od lewej do prawej do zmiennej |
| Zwrócenie | `<- wyrazenie` | wartość wraca z funkcji |
| Sygiel int | `#x` | 64-bitowa liczba całkowita ze znakiem |
| Sygiel string | `$s` | łańcuch znaków |
| Sygiel bool | `?b` | wartość logiczna (`prawda` / `falsz`) |
| Pętla | `dopoki warunek rob:` ... `koniec` | pętla dopóki warunek jest prawdziwy |
| Zapytanie warunkowe | `?warunek ?` ... `koniec` | rozgałęzienie na `prawda`/`falsz` (`\| prawda =>:` / `\| falsz =>:`) |
| Dopasowanie | `dopasuj expr` ... `\| inaczej =>:` ... `koniec` | wielościeżkowe dopasowanie wartości |
| Logiczne I / LUB | `i` / `lub` | polskie operatory (nie `&&` / `\|\|`) |
| Funkcje wbudowane | `pisz(...)`, `bierz()` | zapis na stdout / odczyt ze stdin |
| Komentarz | `# tekst` | `#` niepoprzedzone identyfikatorem ani `:` rozpoczyna komentarz do końca linii |

Rozszerzenie pliku: **`.nrt`**

Pełna specyfikacja: [docs/language-spec.md](docs/language-spec.md).
Architektura kompilatora: [docs/architektura_systemu.md](docs/architektura_systemu.md).

## Potok kompilacji

```
.nrt  --(nurtc)-->  .asm  --(nasm -f elf64)-->  .o  --(gcc -no-pie)-->  plik wykonywalny
```

`nurtc` wykonuje cztery fazy wewnętrzne:

1. **Analizator leksykalny** — strumień tokenów (słowa kluczowe, sygile, operatory, literały)
2. **Analizator składniowy** — rekurencyjny zstępujący + wspinaczka priorytetów (Pratt) → AST
3. **Analizator semantyczny** — kontrola typów oparta na sygile, tablice symboli
4. **Generator kodu** — wynik w asemblerze NASM x86_64

Każda faza raportuje diagnostykę z kontekstem źródłowym; przy błędach kompilacja
zatrzymuje się przed przejściem do następnej fazy.

## Układ repozytorium

```
├── CMakeLists.txt              # Główny plik budowania (C++20)
├── README.md / README.pl.md    # Przegląd projektu (EN / PL)
├── docs/
│   ├── language-spec.md        # Specyfikacja języka
│   └── architektura_systemu.md # Architektura kompilatora
├── src/
│   ├── main.cpp                # Sterownik nurtc
│   ├── common/                 # Lokalizacje źródłowe, diagnostyka
│   ├── lexer/                  # Analizator leksykalny
│   ├── parser/                 # AST + parser rekurencyjny zstępujący
│   ├── semantic/               # Kontrola typów oparta na sygile
│   └── codegen/                # Generator asemblera x86_64
├── examples/                   # Przykładowe programy .nrt
├── scripts/
│   └── wypusc.sh               # Pełny potok: kompilacja → uruchomienie (Linux)
└── tests/                      # Zestawy GoogleTest (lexer, parser, semantic, dopasuj)
```

## Budowanie

Wymagane: CMake >= 3.20 oraz kompilator C++20 (GCC 11+, Clang 14+, MSVC 19.30+).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Na Windows z MSVC podaj konfigurację jawnie:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Pierwsza konfiguracja pobiera GoogleTest przez CMake `FetchContent` (wymaga sieci).

## Użycie

Skompiluj plik `.nrt` do asemblera NASM (domyślnie: `<wejscie>.asm`):

```bash
./build/nurtc examples/hello.nrt
./build/nurtc examples/hello.nrt -o build/wyjscie/hello.asm
```

Flagi diagnostyczne zatrzymują potok wcześniej:

```bash
./build/nurtc examples/hello.nrt --tokens   # wypisz strumień tokenów
./build/nurtc examples/hello.nrt --ast      # wypisz drzewo składniowe (AST)
```

Na Linuksie pełny potok (kompilacja → asemblacja → linkowanie → uruchomienie):

```bash
scripts/wypusc.sh examples/hello.nrt
```

Artefakty trafiają do `build/wyjscie/`.

## Przykłady

| Plik | Prezentuje |
| --- | --- |
| [examples/hello.nrt](examples/hello.nrt) | Funkcje, pętle, zapytania, funkcje wbudowane |
| [examples/kalkulator.nrt](examples/kalkulator.nrt) | Interaktywny kalkulator |
| [examples/warunek.nrt](examples/warunek.nrt) | Zapytania warunkowe |
| [examples/nwd.nrt](examples/nwd.nrt) | NWD algorytmem Euklidesa |
| [examples/logowanie.nrt](examples/logowanie.nrt) | Prosty prompt logowania |

## Testy

Cztery pliki wykonywalne GoogleTest pokrywają etapy kompilatora:

- `lexer_tests` — tokenizacja, rozróżnianie sygila/komentarza, literały
- `parser_tests` — gramatyka, kształt AST, priorytety operatorów
- `semantic_tests` — kontrola typów, rozwiązywanie symboli
- `dopasuj_tests` — semantyka instrukcji `dopasuj`

Uruchom wszystkie testy: `ctest --test-dir build` (lub `-C Debug` na MSVC).
