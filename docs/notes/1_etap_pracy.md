To jest ten moment w pisaniu pracy magisterskiej, w którym zamykasz fazę koncepcyjną i otwierasz rozdział zatytułowany **„Architektura i Implementacja Systemu”**.


---

# RAPORT ARCHITEKTONICZNO-TEORETYCZNY: FAZA I

**Projekt:** Kompilator języka strukturalnego *Nurt* (`.nrt`)

**Stan projektu:** Ukończona i przetestowana analiza leksykalna (Lexer)

**Środowisko docelowe:** Linux x86_64 (WSL Ubuntu)

---

## 1. Co zbudowaliśmy pod kątem teoretycznym?

W klasycznej teorii kompilatorów (według tzw. *Książki ze Smokiem* – Aho, Lam, Sethi, Ullman), proces tłumaczenia języka wysokiego poziomu na kod maszynowy dzieli się na fazy. Do tej pory w pełni zaimplementowaliśmy i zamknęliśmy **Fazę Front-endu: Analizę Leksykalną (Scanning / Lexing)**.

```
+------------------+     Strumień     +-------------------+     Strumień     +----------------+
| Plik źródłowy    | ---------------> |       LEXER       | ---------------> |     PARSER     |
| (tekst/kod .nrt) |  znaków (char)   | (Zbudowany w 100%)|     tokenów      |  (Kolejny etap)|
+------------------+                  +-------------------+                  +----------------+

```

### Anatomia Lexera języka Nurt

Z punktu widzenia systemu operacyjnego, plik z kodem źródłowym (np. `program.nrt`) to po prostu gigantyczny, jednowymiarowy ciąg bajtów (znaków ASCII/UTF-8). Komputer nie wie, czym jest `powolaj` ani `->`.

Zadaniem zbudowanego przez nas **Lexera** jest przetworzenie tego bezwładnego ciągu znaków na strumień **Tokenów** – czyli najmniejszych, niepodzielnych jednostek składniowych o znaczeniu semantycznym.

Pod kątem teoretycznym zaimplementowaliśmy w C++20 **Deterministyczny Automat Skończony (DFA)**, który czyta plik znak po znaku i wykonuje następujące operacje:

1. **Odsiewanie szumu:** Automatycznie ignoruje białe znaki (spacje, tabulacje, znaki nowej linii) oraz komentarze (wszystko od znaku `# ` do końca linii).
2. **Kategoryzacja leksykalna:** Grupuje znaki w tzw. *leksemy* i przypisuje im stałe typy tokenów. Na przykład ciąg znaków `p-o-w-o-l-a-j` jest mapowany na token `TOKEN_KW_POWOLAJ`.
3. **Ekstrakcja Sigili (Unikalna cecha Nurtu):** Lexer został nauczony, że znaki `#`, `$` oraz `?` nie są zwykłymi operatorami. Jeśli po `#` następuje litera (np. `#wiek`), automat traktuje to jako nierozerwalną całość: identyfikator zmiennej typu całkowitego.
4. **Rozróżnianie kontekstu operatorów:** To kluczowy element. Automat musi odróżnić pojedynczy znak minus `-` (operator arytmetyczny) od sekwencji `->` (strzałka przypisania) oraz `<-` (strzałka zwrotu). Zaimplementowany mechanizm potrafi "spojrzeć o jeden znak w przód" (ang. *lookahead*), aby podjąć trafną decyzję.

---

## 2. Co dokładnie wywołałeś w terminalu Ubuntu?

Komendy, które uruchomiłeś w systemie WSL Ubuntu, to standardowy łańcuch narzędziowy (toolchain) w profesjonalnym świecie inżynierii oprogramowania C++. Rozbijmy je na czynniki pierwsze:

### Komenda 1: `cmake -S . -B build`

* **Co to robi?** Inicjalizuje system generowania plików budowy. Flaga `-S .` (Source) mówi: "tu, w tym katalogu, jest główny kod źródłowy i plik konfiguracji `CMakeLists.txt`". Flaga `-B build` (Build) mówi: "załóż folder o nazwie `build` i tam wrzuć wszystkie techniczne pliki konfiguracyjne, skrypty Make/Ninja oraz pliki obiektowe".
* **Efekt teoretyczny:** CMake przeczytał konfigurację, sprawdził, czy Twój kompilator w Ubuntu (GCC/Clang) obsługuje standard C++20, oraz automatycznie pobrał z internetu bibliotekę testową GoogleTest (v1.15.2).

### Komenda 2: `cmake --build build`

* **Co to robi?** To jest właściwy proces kompilacji Twojego projektu. CMake uruchamia pod spodem natywny kompilator systemu Linux (np. `make` lub `ninja`), który bierze pliki `.cpp` i zamienia je na pliki binarne.
* **Efekt teoretyczny:** W logach widzieliśmy m.in.:
* `Building CXX object .../lexer.cpp.o` – skompilowanie kodu źródłowego leksera do kodu obiektowego.
* `Linking CXX static library libnurt_core.a` – spakowanie logiki rdzenia języka do biblioteki statycznej.
* `Linking CXX executable nurtc` – utworzenie głównego pliku wykonywalnego naszego kompilatora (`nurtc`).
* `Linking CXX executable lexer_tests` – utworzenie osobnego programu binarnego, którego jedynym zadaniem jest przetestowanie leksera.



### Komenda 3: `ctest --test-dir build --output-on-failure`

* **Co to robi?** Uruchamia program narzędziowy `CTest` (część ekosystemu CMake), który szuka w folderze `build` zarejestrowanych testów jednostkowych i wykonuje je jeden po drugim. Flaga `--output-on-failure` nakazuje wypisać szczegółowy błąd w terminalu tylko wtedy, gdy któryś test upadnie.
* **Efekt teoretyczny:** Program wykonał **29 scenariuszy testowych**. Zasymulował podanie błędnego kodu (np. przepełnienie liczby `int64`, złe znaki ucieczki w stringu) oraz poprawnego kodu i sprawdził, czy Lexer zareagował w 100% zgodnie ze specyfikacją języka Nurt. Wszystkie 29 testów przeszło pomyślnie.

---

## 3. Rola CMake – po co nam to narzędzie?

Mógłbyś zapytać: *„Skoro mam pliki `.cpp`, czemu nie mogę po prostu wpisać `g++ main.cpp lexer.cpp -o nurtc`?”*.

Przy małym projekcie składającym się z jednego pliku jest to możliwe. Jednak Twój projekt kompilatora jest zaawansowany akademicko. Zawiera strukturę podkatalogów (`src/lexer`, `src/parser`, `tests/`), a także zewnętrzną zależność (GoogleTest).

**CMake (Cross-Platform Make) to system automatyzacji budowania projektów.** Jego zadania to:

* **Abstrakcja nad systemem:** Ty pracujesz na WSL Ubuntu, ale gdyby Twój promotor pobrał projekt na czysty system macOS lub Windows, CMake sam wygeneruje pliki budowy dostosowane do jego kompilatora (Xcode, Visual Studio czy Make).
* **Zarządzanie zależnościami:** Dzięki funkcji `FetchContent`, CMake sam pobiera, kompiluje i linkuje GoogleTest z Twoim kodem testowym. Nie musisz niczego instalować ręcznie w systemie operacyjnym za pomocą `apt-get`.
* **Przyrostowa kompilacja:** Jeśli zmienisz jedną linię w `lexer.cpp`, CMake wie, że nie musi ponownie kompilować całego projektu i bibliotek testowych – przebuduje tylko ten jeden zmieniony plik, oszczędzając czas (i tokeny w Cursorze!).

---

## 4. Co musisz wiedzieć na temat kompilatorów na tym etapie? (Kompendium Wiedzy)

Przed wejściem w fazę Parsowania (Etap 2), musisz opanować fundamentalną terminologię i koncepcje, które będą grillowane podczas Twojej obrony magisterskiej.

### A. Lokacja w kodzie (Source Locations)

Nasz Lexer nie tylko wyciąga tekst tokenu (np. identyfikator `#x`), ale dla każdego tokenu zapisuje strukturę `Location { line, column }`.

* **Dlaczego to ważne?** Kompilator nie może po prostu powiedzieć "błąd składni". Musi precyzyjnie wskazać użytkownikowi: `Błąd w linii 14, kolumna 5: oczekiwano 'koniec', znaleziono 'falsz'`. Nasz kod już teraz to potrafi i śledzi te współrzędne podczas czytania pliku.

### B. Zarządzanie pamięcią i typy danych

Zauważ, że w logach testowych pojawił się test `LexerIntegers.MaxInt64Fits`. Ponieważ Twój język docelowo kompiluje się do architektury x86_64, podjęliśmy genialną w swojej prostocie decyzję: **wszystkie liczby całkowite (`#`) są traktowane jako 64-bitowe liczby ze znakiem (`int64_t` w C++, `qword` w assemblerze NASM)**.
Dzięki temu Twój lexer już na etapie czytania tekstu sprawdza, czy użytkownik nie wpisał liczby większej niż $9\,223\,372\,036\,854\,775\,807$ lub mniejszej niż $-9\,223\,372\,036\,854\,775\,808$. Jeśli przekroczy ten zakres – lexer wyrzuci błąd parsowania liczby (overflow error).

### C. Case-Sensitivity (Wrażliwość na wielkość liter)

Twój język restrykcyjnie podchodzi do wielkości liter. Słowo `powolaj` to klucz definicji funkcji, ale `Powolaj` lub `POWOLAJ` zostanie przez nasz Lexer potraktowane jako błąd składniowy lub zwykły identyfikator. To standardowe zachowanie w nowoczesnych językach (C++, Rust, Python), które udowadnia dojrzałość Twojego projektu.

### D. Czym różni się Lexer od Parseru? (Przejście do Etapu 2)

To jest najważniejsze pytanie teoretyczne.

* **Lexer** (to co mamy): Działa "bezktextowo". Widzi pojedyncze klocki. Wie, że `10` to liczba, `->` to strzałka, a `#x` to zmienna. Nie ma jednak pojęcia, czy zestawienie ich w ciąg `10 #x ->` ma jakikolwiek sens. Dla Lexera to po prostu trzy poprawne klocki leżące obok siebie.
* **Parser** (to co zbudujemy teraz): Działa w oparciu o **Gramatykę Języka**. Weźmie strumień klocków z Lexera i sprawdzi, czy tworzą one poprawne "zdania". To Parser wykryje, że `10 #x ->` jest błędem gramatycznym, a poprawna struktura to `10 -> #x`. Wynikiem działania parsera będzie **AST (Abstract Syntax Tree)** – czyli wielopoziomowe drzewo logiczne reprezentujące strukturę programu.
