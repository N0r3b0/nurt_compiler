
# Specyfikacja Języka Programowania: Nurt

**Wersja:** 1.0 (Na potrzeby projektu kompilatora x86_64)

**Paradygmat:** Strukturalny, imperatywny, silnie i statycznie typowany

**Model przepływu:** Lewostronny (strzałkowy) z jawnym typowaniem symbolicznym (sigile)

---

## 1. Alfabet i Tokeny (Analiza Leksykalna)

### 1.1. Komentarze

Komentarze są jednoliniowe i zaczynają się od znaku `# ` (krzyżyk ze spacją). Każdy tekst po tym znaku do końca linii jest ignorowany przez kompilator.

```ruby
# To jest poprawny komentarz

```

### 1.2. Typy danych i Identyfikatory (Sigile)

W języku Velo typ zmiennej jest integralną częścią jej nazwy poprzez zastosowanie prefiksu (sigilu). Brak sigilu przed nazwą zmiennej jest błędem składniowym.

| Sigil | Typ danych | Odpowiednik C++ | Opis |
| --- | --- | --- | --- |
| `#` | `Int` | `int64_t` | 64-bitowa liczba całkowita ze znakiem |
| `$` | `String` | `std::string_view` / `char*` | Ciąg znaków ASCII (literał tekstowy) |
| `?` | `Bool` | `bool` | Wartość logiczna (`true` / `false`) |

**Zasada tworzenia identyfikatorów:** `[sigil][a-zA-Z][a-zA-Z0-9_]*`

*Przykłady:* `#wiek`, `$imie`, `?czy_dorosly`.

### 1.3. Słowa Kluczowe (Keywords)

Język wielkość liter (case-sensitive). Wszystkie słowa kluczowe pisane są małymi literami:
`fn`, `end`, `then`, `else`, `while`, `do`, `true`, `false`

### 1.4. Operatory i Znaki Specjalne

* **Przepływ (Przypisanie):** `->`
* **Zwrócenie wartości:** `<-`
* **Arytmetyczne:** `+`, `-`, `*`, `/`
* **Porównania:** `==`, `!=`, `<`, `>`, `<=`, `>=`
* **Strukturalne:** `:`, `(`, `)`, `,`, `?` (używane w strukturze warunkowej)

---

## 2. Gramatyka i Struktury Kontrolne (Parser)

### 2.1. Deklaracja i Przypisanie

Wartość (wyrażenie) znajduje się po lewej stronie, a cel (zmienna) po prawej stronie operatora `->`. Zmienna jest niejawnie deklarowana przy pierwszym przypisaniu na podstawie swojego sigilu.

```ruby
# Wyrażenie -> Cel
10 -> #x
"Witaj świecie" -> $komunikat
true -> ?stan

```

### 2.2. Wyrażenia Matematyczne

Priorytet operatorów jest standardowy (mnożenie i dzielenie przed dodawaniem i odejmowaniem). Nawiasy `()` wymuszają zmianę kolejności.

```ruby
(5 + 2) * #x -> #wynik

```

### 2.3. Instrukcja Warunkowa (Dopasowanie Stanu)

Zastępuje klasyczne `if/else`. Składa się z badanej zmiennej logicznej `?zmienna`, znaku zapytania `?`, gałęzi `| true =>` oraz `| false =>`, zakończonych słowem kluczowym `end`. Bloki instrukcji otwierane są znakiem dwukropka `:`.

```ruby
?czy_pali ?
| true  =>:
    10 -> #podatek
| false =>:
    0 -> #podatek
end

```

### 2.4. Pętla Warunkowa (`while`)

Wykonuje blok kodu tak długo, jak warunek logiczny jest spełniony. Struktura: `while [warunek] do:` ... `end`.

```ruby
while #licznik < 5 do:
    #licznik + 1 -> #licznik
end

```

---

## 3. Funkcje

### 3.1. Definicja Funkcji

Funkcje definiuje się za pomocą słowa kluczowego `powolaj`. Typ zwracany jest określany przez sam sigil po operatorze `->`. Jeśli funkcja nic nie zwraca (void), pomija się operator i sigil typu. Blok kończy się słowem `end`. Zwracanie wartości realizuje operator `<-`.

```ruby
# Funkcja przyjmująca dwa Inty i zwracająca Int
powolaj dodaj(#a, #b) -> #:
    #a + #b -> #suma
    <- #suma
end

# Funkcja bezwartościowa (void)
powolaj wypisz_blad($msg):
    # instrukcje wypisujące
end

```

### 3.2. Wywoływanie Funkcji

Funkcje wywołuje się standardowo, a ich wynik może być natychmiast przekazany do zmiennej.

```ruby
dodaj(5, #x) -> #wynik_koncowy

```

---

## 4. Wbudowane Funkcje Systemowe (I/O)

Kompilator musi zapewniać obsługę dwóch podstawowych funkcji wejścia/wyjścia, które pod spodem będą mapowane na linuxowe wywołania systemowe (syscalls) lub standardową bibliotekę C (`printf` / `scanf`):

* `out(...)` – Wypisuje zmienną lub literał na standardowe wyjście.
* `in(...)` – Pobiera wartość z klawiatury i zapisuje do zmiennej podanej jako argument.

```ruby
out($ "Podaj wiek: ")
in(#wiek)

```

---

## 5. Mapowanie na Architekturę docelową (NASM x86_64 / Linux)

Podczas generowania kodu, lewostronny przepływ danych ułatwia bezpośrednie mapowanie na składnię Intel w NASM:

```ruby
# Kod Nurt:
5 -> #x

; Wygenerowany NASM (Konwencja System V ABI):
mov qword [rbp-8], 5    ; zakładając, że #x jest na stosie pod adresem [rbp-8]

```

Wszystkie zmienne typu `#` (Int) muszą być traktowane jako 64-bitowe liczby całkowite (`qword` w NASM), co eliminuje problemy z dopasowaniem rozmiarów rejestrów (`rax`, `rbx` itd.).

