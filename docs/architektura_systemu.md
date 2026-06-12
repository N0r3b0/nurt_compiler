# Architektura systemu kompilatora języka Nurt

**Dokumentacja techniczna — specyfikacja architektury**

Status: wersja odpowiadająca etapowi 4 (pełny potok kompilacji do natywnego kodu
maszynowego x86_64). Dokument opisuje algorytmy architektoniczne, wzorce
generowanego kodu asemblerowego oraz decyzje inżynierskie; celowo nie zawiera
fragmentów kodu źródłowego kompilatora w C++ (ten jest udokumentowany w samych
plikach źródłowych oraz w `docs/language-spec.md`).

---

## 1. Wstęp i architektura ogólna (pipeline)

Kompilator `nurtc` jest klasycznym kompilatorem wielofazowym o architekturze
potokowej (ang. *pipeline*), w której każda faza konsumuje strukturę danych
wytworzoną przez fazę poprzednią i wytwarza reprezentację o wyższym poziomie
abstrakcji semantycznej — aż do finalnej translacji w dół, na poziom rozkazów
procesora:

```
plik .nrt
   │  (strumień bajtów)
   ▼
[1] Analizator leksykalny (Lexer)
   │  (strumień tokenów: słowa kluczowe, sygile, operatory, literały)
   ▼
[2] Analizator składniowy (Parser — rekurencyjny zstępujący + Pratt)
   │  (drzewo składniowe AST)
   ▼
[3] Analizator semantyczny (tablice symboli, kontrola typów)
   │  (zweryfikowane AST — niezmienione strukturalnie, potwierdzone semantycznie)
   ▼
[4] Generator kodu (NASM x86_64, System V AMD64)
   │  (tekst asemblera Intel syntax)
   ▼
nasm -f elf64  →  plik obiektowy ELF64  →  gcc -no-pie  →  natywny plik wykonywalny
```

Fazy [1]–[4] są zintegrowane w jednym pliku wykonywalnym `nurtc`; ostatnie dwa
ogniwa (asemblacja i konsolidacja) realizują narzędzia systemowe, spinane
skryptem automatyzującym `scripts/wypusc.sh`.

Wszystkie fazy raportują problemy do wspólnego **silnika diagnostycznego**,
który gromadzi błędy i ostrzeżenia wraz z dokładną lokalizacją źródłową
(wiersz, kolumna, przesunięcie bajtowe) i renderuje je z kontekstem — cytatem
wiersza źródłowego i wskaźnikiem `^`. Decyzją projektową jest **kontynuacja
analizy po błędzie** w obrębie każdej fazy (tzw. tryb odzyskiwania), tak aby
pojedyncze uruchomienie kompilatora ujawniało możliwie wiele usterek programu;
przejście do fazy następnej następuje jednak wyłącznie przy pustym rejestrze
błędów, co gwarantuje, że generator kodu operuje wyłącznie na drzewie w pełni
zweryfikowanym.

Charakterystyczną cechą leksykalną języka jest podwójna rola znaku `#`:
rozpoczyna on komentarz, *chyba że* bezpośrednio po nim następuje znak
rozpoczynający identyfikator (wówczas jest sygilem typu całkowitego, np. `#x`)
lub dwukropek (wówczas jest sygilem anotacji typu zwracanego, np. `-> #:`).
Jednoznaczność rozstrzyga pojedynczy znak podglądu (*lookahead*), dzięki czemu
lekser pozostaje skanerem jednoprzebiegowym o złożoności liniowej.

---

## 2. Specyfikacja gramatyki i parser (Pratt parsing)

### 2.1 Struktura parsera

Analizator składniowy jest **parserem rekurencyjnym zstępującym** (ang.
*recursive descent*): każdej produkcji gramatyki odpowiada procedura
rozbioru, a wzajemne wywołania tych procedur odwzorowują strukturę drzewa
wyprowadzenia. Gramatyka instrukcji (definicje funkcji `powolaj`, pętle
`dopoki`, powroty `<-`, zapytania `?`) jest rozstrzygalna pojedynczym tokenem
podglądu, co czyni ją gramatyką klasy LL(1) z dwoma punktowymi wyjątkami
opisanymi niżej (§2.3), wymagającymi podglądu LL(2).

### 2.2 Priorytety operatorów — wspinaczka po priorytetach

Wyrażenia rozbierane są techniką **Pratt parsingu** w wariancie *precedence
climbing* (wspinaczki po priorytetach): pojedyncza procedura parametryzowana
minimalnym akceptowanym priorytetem konsumuje operand jednoargumentowy, po
czym w pętli dowiązuje operatory dwuargumentowe o priorytecie nie niższym od
progu, wywołując się rekurencyjnie z progiem `priorytet + 1` dla prawego
operandu. Inkrementacja progu wymusza **lewostronną łączność** wszystkich
operatorów dwuargumentowych (`10 - 4 - 3` ≡ `(10 - 4) - 3`). Tabela
priorytetów (od najsłabiej do najsilniej wiążących):

| Poziom | Operatory | Wynik typowy |
| --- | --- | --- |
| 1 | `lub` | bool |
| 2 | `i` | bool |
| 3 | `==` `!=` | bool |
| 4 | `<` `<=` `>` `>=` | bool |
| 5 | `+` `-` | int |
| 6 | `*` `/` `%` | int |
| 7 | jednoargumentowe `-` `!` | int / bool |

Rozwiązanie to łączy zwięzłość parsera tabelarycznego (jeden punkt prawdy —
tabela priorytetów) z czytelnością rekurencji zstępującej i pozostaje w pełni
deterministyczne, bez nawrotów (*backtracking*).

Operatory logiczne są **słowami kluczowymi** języka: `i` (koniunkcja, wiąże
silniej) oraz `lub` (alternatywa, wiąże słabiej) — zgodnie z polską
tożsamością języka. Z perspektywy Pratt parsingu słowo kluczowe jest
pełnoprawnym tokenem infiksowym; tabela operatorów dwuargumentowych odwzorowuje
`KwLub` i `KwI` na poziomy 1 i 2 dokładnie tak samo, jak tokeny symboliczne.
Historyczne symbole `&&` i `\|\|` zostały usunięte ze zbioru tokenów: lekser
zgłasza dla nich dedykowany błąd leksykalny wskazujący zamiennik słownikowy,
co wyklucza ciche, częściowe rozpoznanie (np. `\|\|` jako dwa markery gałęzi).
Konsekwencją rezerwacji jest niedostępność `i` oraz `lub` jako identyfikatorów;
słowa jedynie rozpoczynające się od słowa kluczowego (`igla`, `lubie`,
`dopasujmy`) pozostają zwykłymi identyfikatorami dzięki regule najdłuższego
dopasowania w lekserze.

### 2.3 Przypisanie strzałkowe „od lewej do prawej"

Konstrukcją wyróżniającą Nurt jest inwersja klasycznego przypisania:

```
[wyrażenie] -> [zmienna docelowa]          np.   #a + #b * 2 -> #wynik
```

Wartość „przepływa" zgodnie z kierunkiem czytania — od źródła do celu. Z punktu
widzenia parsera oznacza to, że instrukcja nie może być rozpoznana po swoim
pierwszym tokenie: parser najpierw przeprowadza **pełny rozbiór wyrażenia**
(z zachowaniem wszystkich priorytetów), a dopiero token następujący po nim
klasyfikuje instrukcję. Jest to schemat „instrukcji prowadzonej wyrażeniem"
(*expression-led statement*):

- token `->` → instrukcja przypisania; parser oczekuje sygilu i identyfikatora
  celu, a wyrażenie zostaje dowiązane jako poddrzewo wartości węzła przypisania;
- token `?` → otwarcie zapytania przepływu sterowania (warunkiem jest właśnie
  rozebrane wyrażenie);
- brak obu → samodzielna instrukcja wyrażeniowa (np. `pisz(...)`).

Konsekwencją inwersji jest naturalna jednoprzebiegowość: nie istnieje problem
„celu poznanego przed wartością", a wyrażenie nigdy nie wymaga ponownego
rozbioru.

Dwa wspomniane wyjątki od LL(1) rozstrzygane są podglądem drugiego tokenu:

1. **Sygil `?` kontra marker zapytania.** Token `?`, po którym następuje
   identyfikator, jest referencją zmiennej logicznej (`?gotowe`); `?` bez
   identyfikatora po prawej stronie wyrażenia pełni rolę markera zapytania.
2. **`->` przypisania kontra `->` anotacji typu zwracanego** w nagłówku
   `powolaj f(...) -> #:` — rozstrzygane kontekstem produkcji nagłówka funkcji.

### 2.4 Instrukcja `dopasuj` — gramatyka i reprezentacja w AST

Dopasowanie wielogałęziowe ma postać:

```
dopasuj [wyrażenie]
| literał_1 =>:
    [instrukcje]
| literał_2 =>:
    [instrukcje]
| inaczej =>:
    [instrukcje]
koniec
```

Produkcja jest w pełni LL(1): słowo kluczowe `dopasuj` jednoznacznie otwiera
instrukcję, każdą gałąź otwiera token `|`, a rozróżnienie gałęzi literałowej
od domyślnej sprowadza się do testu pojedynczego tokenu (`inaczej` kontra
literał). Etykietą gałęzi może być **wyłącznie literał** — całkowity
(opcjonalnie poprzedzony znakiem `-`, składanym przez parser do ujemnej
stałej), napisowy lub logiczny (`prawda`/`falsz`); dowolne inne wyrażenie
w pozycji etykiety jest błędem składniowym. Celem dopasowania jest natomiast
pełnoprawne wyrażenie (np. `dopasuj #x % 2`), rozbierane standardową ścieżką
Pratt parsingu.

W AST instrukcję reprezentuje węzeł `MatchStatementNode` o trzech składowych:
poddrzewie wyrażenia celu, wektorze przypadków (każdy przypadek to para
*literał — blok instrukcji* wraz z lokalizacją źródłową dla diagnostyki) oraz
**wyodrębnionym bloku domyślnym** `inaczej`. Wyodrębnienie to nie jest
przypadkowe: gałąź `inaczej` jest **obowiązkowa** i musi być **ostatnia** —
parser egzekwuje oba warunki (brak `inaczej`, gałąź po `inaczej` oraz
zdublowane `inaczej` są osobnymi, precyzyjnymi błędami składniowymi), dzięki
czemu dalsze fazy mogą traktować pokrycie wartości niedopasowanych jako
inwariant strukturalny, a nie własność do każdorazowego dowodzenia.

### 2.5 Odzyskiwanie po błędach

Po wykryciu błędu składniowego parser przechodzi w tryb paniki i synchronizuje
się do najbliższego tokenu niezawodnie rozpoczynającego lub zamykającego
instrukcję (`powolaj`, `dopoki`, `dopasuj`, `<-`, `koniec`, `|`). Tokeny graniczne
napotkane w pozycji błędnej są konsumowane przez samą procedurę instrukcji, co
gwarantuje postęp i wyklucza zapętlenie; instrukcje rozebrane poprawnie po
miejscu błędu trafiają do AST, dzięki czemu dalsze fazy diagnostyki pozostają
użyteczne.

---

## 3. Analiza semantyczna (kontrola typów)

Analizator semantyczny wykonuje pojedynczy przebieg po AST, prowadząc dwie
tablice symboli: tablicę **funkcji** (sygnatury: lista typów parametrów, typ
zwracany, lokalizacja definicji) oraz tablicę **zmiennych** bieżącego zakresu.

### 3.1 Deklaracja przez pierwsze przypisanie i sygile

Nurt nie posiada odrębnej składni deklaracji. Zmienną **powołuje do życia
pierwsze przypisanie**, a sygil tego przypisania (`#` — 64-bitowa liczba
całkowita, `$` — napis, `?` — wartość logiczna) **ustala typ statyczny na całe
życie symbolu**. Model ten przenosi anotację typu z deklaracji do każdego
użycia: sygil jest powtarzany przy każdej referencji, dzięki czemu kontrola
spójności jest lokalna i natychmiastowa — odwołanie lub ponowne przypisanie
z innym sygilem (`$x` po wcześniejszym `10 -> #x`) jest błędem zgłaszanym wraz
z notą wskazującą miejsce pierwotnego ustalenia typu. Odczyt zmiennej przed
(leksykalnie wcześniejszym) przypisaniem jest błędem „użycia przed
zainicjowaniem".

### 3.2 Izolacja zakresów funkcji

Przyjęto rygorystyczny model **izolacji zakresów**: ciało funkcji widzi
wyłącznie własne parametry i zmienne lokalne — zmienne globalne (poziomu
najwyższego) nie są widoczne wewnątrz `powolaj`. Decyzja ta ma trzy
uzasadnienia inżynierskie: (a) eliminuje pytanie o semantykę uchwycenia stanu
globalnego w chwili wywołania, (b) czyni funkcje referencyjnie przejrzystymi
z dokładnością do operacji wejścia/wyjścia, (c) upraszcza generator kodu —
wszystkie zmienne, łącznie z „globalnymi", mogą być szczelinami ramki stosu
(zmienne poziomu najwyższego są lokalnymi funkcji `main`, §4.1). Diagnostyka
rozpoznaje próbę odwołania do istniejącej zmiennej globalnej z wnętrza funkcji
i podpowiada przekazanie jej jako parametru.

Funkcje muszą być **zdefiniowane przed pierwszym wywołaniem** (model
jednoprzebiegowy, analogiczny do C); sygnatura rejestrowana jest jednak przed
analizą ciała, co dopuszcza rekursję bezpośrednią. Definicje zagnieżdżone,
duplikaty nazw funkcji i parametrów oraz przesłanianie funkcji wbudowanych
(`pisz`, `bierz`) są odrzucane.

### 3.3 System typów wyrażeń

Kontrola typów jest **ścisła, bez jakichkolwiek konwersji niejawnych**:
arytmetyka i porządek wymagają operandów całkowitych, równość — operandów
jednego typu (dla napisów oznacza równość treści, §4.6), operatory logiczne —
operandów logicznych. Wynik wywołania funkcji bezzwrotnej (*void*) nie jest
wartością i nie może wystąpić w żadnej pozycji wartościującej. Mechanizm
tłumienia kaskad: wyrażenie, którego podwyrażenie zostało już zdiagnozowane,
otrzymuje typ „nieznany" i nie generuje wtórnych komunikatów.

### 3.4 Egzekwowanie typów zwracanych

Instrukcja `<- wyrażenie` jest weryfikowana względem zadeklarowanego (sygilem
w nagłówku) typu zwracanego funkcji bieżącej: niezgodność typu, `<-` bez
wartości w funkcji niebezzwrotnej, zwrot wartości w funkcji bezzwrotnej oraz
`<-` poza funkcją są błędami. Dodatkowo dla funkcji niebezzwrotnych
przeprowadzana jest **analiza pokrycia ścieżek powrotu** (analiza
konserwatywna, strukturalna): blok gwarantuje powrót, jeżeli zawiera
instrukcję gwarantującą powrót; zapytanie `?` gwarantuje powrót wyłącznie
wtedy, gdy obie gałęzie (`prawda` i `falsz`) są obecne i obie go gwarantują;
instrukcja `dopasuj` gwarantuje powrót wyłącznie wtedy, gdy gwarantuje go
**każdy** blok przypadku **oraz** obowiązkowy blok `inaczej` (§3.6);
pętla `dopoki` nie gwarantuje nigdy (ciało może nie wykonać się ani razu).
Funkcja niebezzwrotna, której ciało nie gwarantuje powrotu, jest odrzucana —
wyklucza to na etapie kompilacji niezdefiniowane zachowanie „spadnięcia
z końca funkcji" z przypadkową wartością w rejestrze wynikowym.

### 3.5 Typowanie kontekstowe funkcji `bierz()`

Funkcja wbudowana `bierz()` nie ma typu samoistnego — jej typ wyznacza sygil
celu przypisania (`bierz() -> #x` czyta liczbę, `bierz() -> $s` czyta słowo).
Analizator dopuszcza ją wyłącznie w pozycji bezpośredniej wartości
przypisania; użycie w głębi wyrażenia lub odczyt do zmiennej logicznej jest
błędem. To jedyne miejsce języka, w którym typowanie jest kontekstowe, i jest
ono celowo zawężone do wzorca trywialnie odwzorowywanego na wywołanie `scanf`
z adresem szczeliny docelowej (§4.7).

### 3.6 Ograniczenia semantyczne instrukcji `dopasuj`

Weryfikacja dopasowania wielogałęziowego obejmuje trzy klasy reguł:

1. **Zgodność typu etykiet z celem.** Wyrażenie celu musi posiadać wartość
   (wynik funkcji bezzwrotnej jest odrzucany), a jego typ statyczny — wyznaczony
   sygilem (`#`, `$`, `?`) zgodnie z regułami §3.3 — staje się typem odniesienia
   dla wszystkich etykiet: każdy literał przypadku musi mieć **dokładnie** ten
   typ, bez żadnych konwersji (`dopasuj #x` z gałęzią `"jeden"` jest błędem
   z komunikatem wskazującym oba typy).
2. **Zakaz duplikatów.** Wartości etykiet w obrębie jednego `dopasuj` muszą być
   parami różne; porównywana jest **wartość**, nie pisownia. Analizator prowadzi
   zbiory wartości widzianych osobno dla każdego typu (zbiór liczb, zbiór
   treści napisów, parę flag `prawda`/`falsz`), dzięki czemu wykrycie kolizji
   jest liniowe względem liczby gałęzi. Duplikat byłby kodem martwym: przy
   sekwencyjnym łańcuchu porównań (§4.5) druga gałąź o tej samej wartości jest
   nieosiągalna, więc język odrzuca ją statycznie.
3. **Pokrycie ścieżek powrotu.** W macierzy pokrycia (§3.4) `dopasuj` zachowuje
   się analogicznie do zapytania o obu gałęziach: ponieważ przypadki literałowe
   nigdy nie są traktowane jako wyczerpujące (dziedziny typów są praktycznie
   nieskończone), gwarancja powrotu wymaga, by powrót gwarantował każdy blok
   przypadku **i** blok `inaczej`. Obowiązkowość `inaczej`, wymuszona już
   składniowo (§2.4), czyni tę regułę konserwatywną, lecz zupełną: nie istnieje
   wartość celu, dla której sterowanie ominęłoby wszystkie bloki.

---

## 4. Niskopoziomowy generator kodu (NASM x86_64)

Generator emituje asembler w składni Intel dla NASM (`bits 64`,
`default rel`), przeznaczony dla systemu Linux x86_64 zgodnie z ABI **System V
AMD64**. Program konsoliduje się z biblioteką standardową C (symbole
zewnętrzne `printf`, `scanf`, `strcmp`), a punktem wejścia jest emitowany
symbol `main`, wołany przez kod rozruchowy libc. Model danych jest jednorodny:
**każda wartość Nurt zajmuje 64-bitowe słowo (qword)** — liczby jako i64,
wartości logiczne jako 0/1, napisy jako wskaźniki do bajtów zakończonych NUL
(literały internowane w sekcji `.rodata`, bufory odczytu w `.bss`). Nazwy
funkcji użytkownika otrzymują prefiks `nurt_`, co wyklucza kolizje z
symbolami libc (np. funkcja użytkownika o nazwie `abs`).

### 4.1 Zarządzanie ramką stosu

Każda funkcja (w tym `main`, którego ciałem jest kod poziomu najwyższego)
otwiera ramkę kanonicznym prologiem:

```nasm
push rbp            ; zachowanie ramki wołającego
mov  rbp, rsp       ; ustanowienie wskaźnika bazy bieżącej ramki
push rbx            ; rbx jest rejestrem zachowywanym przez wołanego (callee-saved)
sub  rsp, 8*N       ; rezerwacja N szczelin po 8 bajtów na zmienne lokalne
```

Rejestr `rbp` pełni rolę stałej bazy adresowania ramki: jego wartość nie
zmienia się przez cały czas życia funkcji, więc adresy zmiennych są
**deterministycznymi stałymi czasu kompilacji**. Przebieg wstępny generatora
(*pre-pass*) przechodzi ciało funkcji i przydziela szczelinę każdej zmiennej
w kolejności pierwszego wystąpienia: najpierw parametry, potem cele przypisań
(rekurencyjnie, łącznie z wnętrzami pętli i gałęzi zapytań). Układ ramki:

```
[rbp]        ← zachowane rbp wołającego
[rbp -  8]   ← zachowane rbx
[rbp - 16]   ← zmienna nr 0 (pierwszy parametr lub pierwsza zmienna)
[rbp - 24]   ← zmienna nr 1
...
[rbp - 16 - 8k] ← zmienna nr k
```

Przypisanie `wyrażenie -> #x` kompiluje się do dwóch kroków: wartościowania
wyrażenia do rejestru-akumulatora `rax`, po czym pojedynczego zapisu
`mov qword [rbp - offset_x], rax`. Epilog jest lustrem prologu:
`lea rsp, [rbp - 8]` / `pop rbx` / `pop rbp` / `ret` — wariant z `lea`
przywraca `rsp` niezależnie od chwilowych manipulacji wskaźnikiem stosu
wewnątrz ciała (operandy wyrażeń, wyrównanie dynamiczne), co czyni epilog
poprawnym w każdym punkcie powrotu.

Wartościowanie wyrażeń realizuje schemat akumulatorowo-stosowy: operatory
dwuargumentowe obliczają operand lewy, odkładają go (`push rax`), obliczają
operand prawy, po czym zdejmują lewy do `rbx` (`pop rbx`) i wykonują operację.
Schemat ten jest świadomie nieoptymalny (brak alokacji rejestrów), lecz
całkowicie kompozycyjny i poprawny dla dowolnie zagnieżdżonych wyrażeń —
właściwy kompromis dla kompilatora dydaktycznego.

### 4.2 Wyrównanie stosu do 16 bajtów — analiza pogłębiona

**Wymaganie ABI.** System V AMD64 stanowi, że w chwili wykonania rozkazu
`call` wskaźnik stosu musi spełniać `rsp ≡ 0 (mod 16)`; równoważnie — na
wejściu do funkcji wołanej, po zepchnięciu 8-bajtowego adresu powrotu,
zachodzi `rsp ≡ 8 (mod 16)`. Źródłem wymagania jest architektura SIMD:
kompilatory C (a więc i wnętrza `printf`/`scanf` z libc) generują dla operacji
na blokach 128-bitowych rozkazy SSE o **wyrównanych** operandach pamięciowych
(`movaps`, `movdqa`), adresując bufory tymczasowe względem własnego, świeżo
ustanowionego `rsp`/`rbp` przy założeniu, że wołający dotrzymał kontraktu.
Operand niewyrównany powoduje sprzętowy wyjątek ogólnej ochrony, objawiający
się natychmiastowym `SIGSEGV` w głębi libc — błąd szczególnie zdradliwy, bo
niedeterministyczny względem treści programu źródłowego: materializuje się
tylko wtedy, gdy łańcuch wywołań trafi na ścieżkę kodową używającą SSE, a
parzystość zepchnięć akurat złamie wyrównanie. Nowsze wersje glibc używają
ponadto rozszerzeń AVX (operandy 256-bitowe), co czyni kontrakt jeszcze
bardziej bezwzględnym.

**Problem w naszym modelu wartościowania.** Schemat akumulatorowo-stosowy
(§4.1) odkłada operandy rozkazem `push`, więc w chwili napotkania wywołania
(np. `pisz(...)` albo wywołania funkcji użytkownika *wewnątrz* większego
wyrażenia, jak w `#n * silnia(#n - 1)`) liczba nierozliczonych zepchnięć — a
zatem bieżąca reszta `rsp mod 16` — **zależy od głębokości i kształtu drzewa
wyrażenia**. Statyczne bilansowanie wyrównania wymagałoby śledzenia parzystości
zepchnięć na każdej ścieżce emisji i warunkowego wstawiania szczelin
wyrównujących; jest to wykonalne, lecz kruche.

**Rozwiązanie: dynamiczne wyrównanie wokół każdego wywołania.** Generator
otacza **każdy** rozkaz `call` czterorozkazową sekwencją:

```nasm
mov  rbx, rsp        ; zapamiętanie dokładnej, bieżącej wartości rsp
and  rsp, -16        ; wymuszenie rsp ≡ 0 (mod 16): zaokrąglenie W DÓŁ
xor  eax, eax        ; (tylko funkcje wariadyczne: al = 0 rejestrów wektorowych)
call <symbol>
mov  rsp, rbx        ; przywrócenie rsp sprzed wyrównania
```

Maska `-16` (czyli `0xFFFF…F0`) zeruje cztery najmłodsze bity `rsp`,
przesuwając wskaźnik stosu w dół o 0–15 bajtów — stos rośnie ku adresom
malejącym, więc operacja jedynie powiększa obszar martwy poniżej danych
żywych, niczego nie nadpisując. Odłożone wcześniej operandy wyrażeń leżą
**powyżej** starego `rsp` i pozostają nietknięte; po powrocie oryginalna
wartość `rsp` zostaje odtworzona, a oczekujące rozkazy `pop` widzą stos
dokładnie w stanie sprzed wywołania.

Kluczowy jest wybór schowka na stary `rsp`: musi to być rejestr, którego
funkcja wołana **nie zniszczy**. ABI dzieli rejestry na *caller-saved*
(`rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` — wolno je zniszczyć) i
*callee-saved* (`rbx`, `rbp`, `r12`–`r15` — wołany ma obowiązek je zachować).
Wybrano **`rbx`**: jest callee-saved, więc `printf`, `scanf`, `strcmp` i każda
funkcja zgodna z ABI zwrócą go nienaruszonym. Spójność domyka prolog: skoro
funkcje *generowane* same używają `rbx` (operandy dwuargumentowe, sekwencje
wyrównania), to wobec własnych wołających również muszą honorować jego
zachowanie — stąd `push rbx` w prologu i `pop rbx` w epilogu każdej funkcji
Nurt. Poprawność lokalna sekwencji jest niewrażliwa na użycie `rbx` przez
resztę ciała: między `mov rbx, rsp` a `mov rsp, rbx` nie występuje żaden inny
rozkaz generowany, a wartość `rbx` z wnętrza wartościowania operandów nigdy
nie jest żywa w poprzek wywołania (zdejmowana jest z `pop` dopiero po
domknięciu obu operandów).

Dla funkcji **wariadycznych** (`printf`, `scanf`) sekwencja zawiera dodatkowo
`xor eax, eax`: ABI wymaga, by przy wywołaniu funkcji o zmiennej liczbie
argumentów rejestr `al` zawierał górne ograniczenie liczby argumentów
przekazanych w rejestrach wektorowych (XMM). Nurt nie przekazuje wartości
zmiennoprzecinkowych, więc `al = 0`; pominięcie tego kroku skutkowałoby
odczytem śmieciowej wartości `al` i potencjalnym zrzutem ośmiu rejestrów XMM
do tzw. *register save area* — kolejnym źródłem trudnych błędów.

Koszt rozwiązania to trzy dodatkowe rozkazy całkowitoliczbowe na wywołanie —
pomijalny wobec kosztu samego `call` do libc — w zamian za **bezwarunkową,
strukturalną gwarancję** poprawności wyrównania, niezależną od kształtu
wyrażenia, w którym wywołanie występuje.

### 4.3 Obniżanie przepływu sterowania: pętla `dopoki`

Pętla `dopoki [warunek] rob: [ciało] koniec` obniżana jest do wzorca z
warunkiem na szczycie i dwiema etykietami lokalnymi o unikalnym numerze `N`
(licznik globalny generatora; etykiety z prefiksem `.` są w NASM lokalne
względem ostatniego symbolu globalnego, więc nie kolidują między funkcjami):

```nasm
.dopoki_N:
    <wartościowanie warunku do rax>     ; analizator gwarantuje typ bool: rax ∈ {0,1}
    cmp  rax, 0
    je   .wyjscie_N                     ; warunek fałszywy → wyjście z pętli
    <ciało pętli>
    jmp  .dopoki_N                      ; skok powrotny do ponownej oceny warunku
.wyjscie_N:
```

Warunek jest wartościowany w pełni przy każdej iteracji. Porównania wewnątrz
warunku same w sobie obniżane są do wzorca `cmp` + `setCC` + `movzx`
(materializacja predykatu do 0/1 w `rax`), po czym następuje test `cmp rax, 0`
— rozdzielenie produkcji wartości logicznej od skoku upraszcza generator
(wartość logiczna jest pełnoprawną wartością pierwszej kategorii, może zostać
przypisana do `?zmiennej`) kosztem jednej pary rozkazów możliwej do usunięcia
przez przyszłą optymalizację okienkową (*peephole*).

### 4.4 Obniżanie przepływu sterowania: zapytanie `?`

Zapytanie dwugałęziowe obniżane jest do klasycznego rombu sterowania z trzema
etykietami nazwanymi zgodnie ze składnią języka:

```nasm
    <wartościowanie warunku do rax>
    cmp  rax, 0
    je   .falsz_N          ; 0 → gałąź 'falsz'
.prawda_N:
    <instrukcje gałęzi prawda>
    jmp  .koniec_N         ; ominięcie gałęzi przeciwnej
.falsz_N:
    <instrukcje gałęzi falsz>
.koniec_N:
```

Gałąź nieobecna w źródle pozostawia pusty segment między etykietami — wzorzec
pozostaje jednolity, a `jmp .koniec_N` degeneruje się do skoku o zerowym
dystansie semantycznym. Zagnieżdżone zapytania i pętle otrzymują kolejne
numery `N`, więc etykiety nigdy nie kolidują. Operatory `i`/`lub` obniżane są
bitowo (`and`/`or` na wartościach 0/1) — bez krótkiego spięcia; jest to
udokumentowana decyzja semantyczna, bezpieczna, gdyż analizator dopuszcza
operandy wyłącznie logiczne, a jedynym efektem ubocznym wyrażeń mogłoby być
wejście/wyjście wewnątrz wołanej funkcji.

### 4.5 Obniżanie instrukcji `dopasuj`: sekwencyjny łańcuch rozdzielczy

Dopasowanie wielogałęziowe obniżane jest do **sekwencyjnego łańcucha
rozdzielczego** (*dispatch chain*): wyrażenie celu wartościowane jest do `rax`
**dokładnie raz**, po czym następuje seria porównań — po jednym na przypadek —
zakończona skokiem bezwarunkowym do bloku domyślnego. Każda instrukcja
otrzymuje unikatowy numer `N` z tego samego licznika, który numeruje zapytania
i pętle, a przypadki — indeksy porządkowe `k`, co gwarantuje globalną
unikatowość etykiet lokalnych również przy zagnieżdżaniu:

```nasm
    <wartościowanie celu do rax>      ; jednokrotne
    cmp  rax, 1
    je   .przypadek_N_0
    cmp  rax, 42
    je   .przypadek_N_1
    jmp  .inaczej_N                   ; żaden literał nie pasuje
.przypadek_N_0:
    <instrukcje przypadku 0>
    jmp  .koniec_dopasuj_N
.przypadek_N_1:
    <instrukcje przypadku 1>
    jmp  .koniec_dopasuj_N
.inaczej_N:
    <instrukcje bloku domyślnego>
.koniec_dopasuj_N:
```

Separacja strefy porównań od strefy bloków sprawia, że rejestr `rax` nie musi
przetrwać wykonania żadnego ciała gałęzi — łańcuch `cmp`/`je` jest zwarty,
a procesor wykonuje wyłącznie testy poprzedzające trafienie. Dla celów
całkowitych i logicznych literał trafia do porównania jako argument
natychmiastowy (`cmp rax, imm`); ponieważ koder x86_64 dopuszcza w tej formie
wyłącznie 32-bitowy argument rozszerzany znakowo, literały spoza zakresu
int32 są wstępnie ładowane do rejestru pomocniczego (`mov rcx, imm64`;
`cmp rax, rcx`). Wartości logiczne porównywane są z ich materializacją 0/1.

Cel napisowy wymaga porównania **treści**, spójnie z semantyką `==` (§4.6):
każdy przypadek obniżany jest do wywołania `strcmp` z pełną sekwencją
wyrównania stosu (§4.2). Ponieważ `strcmp` niszczy `rax`, wskaźnik celu jest
na czas łańcucha **parkowany na stosie** (`push rax`); każda iteracja odtwarza
go odczytem `mov rdi, [rsp]`, ładuje adres internowanego literału do `rsi`
i testuje zerowość wyniku. Każdy blok gałęzi (w tym `inaczej`) osiągany jest
przez dokładnie jedną etykietę, więc zdjęcie zaparkowanego słowa
(`add rsp, 8`) emitowane na początku każdego bloku wykonuje się dokładnie raz
na dowolnej ścieżce sterowania; powrót `<-` wewnątrz bloku pozostaje bezpieczny,
gdyż epilog odtwarza `rsp` bezwzględnie z `rbp`, a nie relatywnie.

### 4.6 Operacje na napisach

Równość `==`/`!=` operandów napisowych obniżana jest do wywołania `strcmp`
(z pełną sekwencją wyrównania §4.2) i materializacji predykatu
`sete`/`setne` na podstawie zerowości wyniku — porównywana jest zatem
**treść**, nie tożsamość wskaźników; rozróżnienie wymaga znajomości typów
statycznych operandów, którą generator odtwarza lokalnie z AST (lustrzana,
bezbłędowa wersja reguł typowania analizatora). Literały napisowe są
**internowane**: identyczne stałe (w tym łańcuchy formatu `%lld`, `%s`,
`%255s` oraz słowa `prawda`/`falsz`) współdzielą jedną definicję w `.rodata`.

### 4.7 Funkcje wbudowane `pisz` i `bierz`

`pisz(a, b, …)` jest obniżane do **sekwencji niezależnych wywołań `printf`**,
po jednym na argument, z łańcuchem formatu dobranym statycznie do typu
argumentu: `%lld` (int), `%s` (napis), zaś dla wartości logicznej — `%s` ze
wskaźnikiem wybranym bez rozgałęzienia, rozkazem warunkowego przeniesienia:

```nasm
    lea   rsi, [napis_prawda]
    lea   rcx, [napis_falsz]
    test  rax, rax
    cmove rsi, rcx           ; rax = 0 → wybierz "falsz"
```

`bierz()` wykorzystuje kontekst przypisania (§3.5): dla celu całkowitego
generator przekazuje `scanf` **adres szczeliny ramki** bezpośrednio —
`lea rsi, [rbp - offset]` z formatem `%lld`; dla celu napisowego rezerwuje
w `.bss` 256-bajtowy bufor statyczny przypisany do *miejsca wywołania*,
czyta formatem `%255s` (ogranicznik długości wyklucza przepełnienie bufora),
po czym zapisuje adres bufora do szczeliny zmiennej.

---

## 5. Konwencja wołania funkcji (odwzorowanie ABI)

Funkcje Nurt odwzorowane są wprost na konwencję wołania System V AMD64 dla
argumentów klasy INTEGER — wszystkie typy języka (i64, wskaźnik na napis,
0/1) należą do tej klasy, co czyni odwzorowanie jednorodnym:

| Pozycja parametru Nurt | Rejestr sprzętowy |
| --- | --- |
| 1 | `RDI` |
| 2 | `RSI` |
| 3 | `RDX` |
| 4 | `RCX` |
| 5 | `R8` |
| 6 | `R9` |
| wartość zwracana (`<-`) | `RAX` |

**Strona wołającego.** Argumenty wartościowane są od lewej do prawej, a każdy
wynik odkładany na stos (`push rax`); po zakończeniu ostatniego argumenty
zdejmowane są w kolejności odwrotnej (`pop`) wprost do rejestrów docelowych —
ostatnio odłożony trafia do rejestru pozycji najwyższej. Dwufazowość
(najpierw wszystkie wartości, potem ładowanie rejestrów) jest konieczna:
wartościowanie argumentu *k+1* może zawierać wywołania niszczące rejestry
caller-saved, w tym same rejestry argumentowe, więc wcześniejsze załadowanie
`RDI`–`R9` byłoby błędne. Następnie emitowana jest sekwencja wyrównania
i `call nurt_<nazwa>` (§4.2).

**Strona wołanej funkcji.** Prolog kopiuje parametry z rejestrów do ich
szczelin ramkowych (`mov [rbp - 16], rdi`, `mov [rbp - 24], rsi`, …), po czym
ciało traktuje parametry identycznie jak zmienne lokalne — jednorodność ta
upraszcza zarówno generator, jak i analizę programu wynikowego. Zrzut do
pamięci jest zachowawczy (parametr mógłby pozostać w rejestrze), lecz
konieczny w modelu bez alokatora rejestrów: rejestry argumentowe są
caller-saved i pierwsze wywołanie wewnętrzne by je zniszczyło.

**Wartość zwracana.** Instrukcja `<- wyrażenie` wartościuje wyrażenie do
`RAX` i wykonuje pełny epilog w miejscu — analiza pokrycia ścieżek (§3.4)
gwarantuje, że funkcja niebezzwrotna nie osiągnie końca ciała bez ustawienia
`RAX`. `main` kończy się jawnym `xor eax, eax` (kod wyjścia 0).

**Ograniczenie.** Backend wspiera do **sześciu** parametrów — dokładnie tyle,
ile rejestrów klasy INTEGER przewiduje ABI; przekroczenie limitu (wymagające
przekazywania argumentów na stosie, z odwróconą kolejnością i udziałem w
bilansie wyrównania) jest zgłaszane jako czytelny błąd diagnostyczny. Jest to
jedyne ograniczenie strukturalne backendu względem semantyki języka.

---

## Podsumowanie decyzji inżynierskich

| Decyzja | Uzasadnienie |
| --- | --- |
| Potok czteropoziomowy ze wspólną diagnostyką | rozdzielenie odpowiedzialności; wiele błędów w jednym przebiegu |
| Pratt / wspinaczka po priorytetach | jedna tabela priorytetów; lewostronna łączność przez `prio + 1` |
| Instrukcje prowadzone wyrażeniem | naturalna obsługa inwersji `wyrażenie -> cel` w jednym przebiegu |
| Deklaracja przez pierwsze przypisanie + sygile | typ widoczny w każdym użyciu; lokalna kontrola spójności |
| Izolacja zakresów funkcji | przejrzystość referencyjna; zmienne globalne = lokalne `main` |
| Analiza pokrycia ścieżek powrotu | eliminacja UB „spadnięcia z końca funkcji" w czasie kompilacji |
| Operatory słownikowe `i`/`lub` | spójność z polską tożsamością języka; `&&`/`\|\|` jako czytelne błędy |
| `dopasuj` z obowiązkowym `inaczej` | pokrycie wartości jako inwariant składniowy; brak duplikatów = brak kodu martwego |
| Łańcuch rozdzielczy `cmp`/`je` | jednokrotne wartościowanie celu; etykiety lokalne odporne na zagnieżdżenia |
| Jednorodny model qword | jeden rozmiar szczeliny; trywialny, deterministyczny przydział ramki |
| Dynamiczne wyrównanie `rsp` przez `rbx` | bezwarunkowa zgodność z ABI niezależnie od głębokości wyrażenia |
| Bool jako 0/1 + `setCC`/`cmove` | predykaty jako wartości pierwszej kategorii; wybór napisu bez skoków |
| `strcmp` dla równości napisów | semantyka treści, nie tożsamości wskaźnika |
| Prefiks `nurt_` symboli | brak kolizji z przestrzenią nazw libc |
| Limit 6 parametrów | granica rejestrów INTEGER ABI; jasny komunikat zamiast cichej degradacji |

---

## 6. Metodologia i Wyniki Badań Empirycznych

### 6.1 Cel i zakres badania

Celem części empirycznej jest ilościowa charakterystyka kodu wynikowego
generatora `nurtc` na tle referencyjnego kompilatora produkcyjnego. Jako
punkt odniesienia przyjęto **GCC 15.2.0 z wyłączonymi optymalizacjami**
(`-O0`), ponieważ dopiero ten poziom stanowi metodologicznie uczciwą bazę
porównawczą: `nurtc` nie zawiera żadnej fazy optymalizacji (ani na poziomie
reprezentacji pośredniej, ani podczas emisji), zatem zestawienie z `-O2`
mierzyłoby przede wszystkim dorobek kilkudziesięciu lat inżynierii
optymalizacyjnej GCC, a nie właściwości badanej architektury generatora.

Porównano pięć par programów o **behawioralnie identycznej logice** —
każdy algorytm zaimplementowano w Nurt (`.nrt`) i w C (`.c`), zachowując
tę samą strukturę funkcji, pętli i rozgałęzień; równoważność weryfikowana
jest automatycznie przez bajtowe porównanie strumieni wyjściowych obu
binariów (bramka `output mismatch` w skrypcie pomiarowym):

| Benchmark | Ćwiczone konstrukcje języka |
| --- | --- |
| `witaj` | bazowe I/O; odwzorowanie statycznego napisu w `.rodata` |
| `euklides` | pętla `dopoki`, arytmetyka modulo, głęboka rekurencja (liczby Fibonacciego — najgorszy przypadek algorytmu Euklidesa) |
| `kalkulator` | struktury warunkowe, porównania napisów, wielogałęziowa emisja `dopasuj` (łańcuch `strcmp` i łańcuch `cmp`/`je`) |
| `liczba_pierwsza` | intensywna iteracja matematyczna (dzielenie próbne `d·d ≤ n`), logika stanu boolowskiego |
| `rok_przestępny` | zagnieżdżone warunki złożone operatorami `i` / `lub` |

### 6.2 Konfiguracja stanowiska i metodologia pomiaru

Środowisko: WSL2 Ubuntu (jądro 6.6.114.1-microsoft-standard-WSL2),
CPU Intel Core i5-9600KF @ 3,70 GHz (6 rdzeni), GCC 15.2.0, NASM 3.01,
Python 3.14. Suite pomiarowy: `scripts/benchmark.py`; źródła i artefakty:
katalog `benchmarks/`.

Mierzone wielkości i procedury:

1. **Statyczna liczba instrukcji i jej struktura.** Parsowane są pliki
   asemblerowe obu kompilatorów (`.asm` — składnia Intel/NASM z `nurtc`;
   `.s` — składnia AT&T z `gcc -O0 -S`). Zliczane są wyłącznie rozkazy
   sprzętowe — etykiety, komentarze, dyrektywy asemblera i definicje
   danych (`db`, `.string`, …) są wykluczone. Obok liczby całkowitej
   raportowane są dwie klasy: **skoki/rozgałęzienia** (`jmp` oraz pełna
   rodzina skoków warunkowych `je`, `jne`, `jl`, …) i **operacje
   przesłań pamięciowo-rejestrowych** (`mov*`, `push`, `pop`).
2. **Zajętość pamięci (RAM).** Każde binarium uruchamiane jest
   **50-krotnie** pod kontrolą `/usr/bin/time -v`; raportowana jest
   średnia wartość pola *Maximum resident set size* (kB).
3. **Rozmiar pliku wykonywalnego.** Dokładny rozmiar w bajtach kopii
   binarium poddanej `strip` (eliminacja tablic symboli wyrównuje
   warunki: GCC domyślnie osadza więcej metadanych diagnostycznych).
4. **Szybkość kompilacji.** Średni czas ścieżki *źródło → asembler*
   (`nurtc plik.nrt -o plik.asm` vs `gcc -O0 -S`) z 30 powtórzeń po
   3 przebiegach rozgrzewkowych (amortyzacja zimnych pamięci podręcznych
   systemu plików). Porównywany jest ten właśnie etap, gdyż dalsze ogniwa
   (asemblacja `nasm`, konsolidacja `gcc`) są wspólne dla obu potoków.

### 6.3 Wyniki

Pomiary z dnia 12.06.2026 (pełne tabele jednostkowe generuje
`python3 scripts/benchmark.py`):

| Benchmark | Instr. Nurt | Instr. C | Skoki N/C | Przesłania N/C | RSS Nurt [kB] | RSS C [kB] | Rozmiar Nurt [B] | Rozmiar C [B] | Kompilacja Nurt [ms] | Kompilacja C [ms] |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `witaj` | 19 | 15 | 0 / 0 | 10 / 8 | 1536 | 1664 | 14 408 | 14 464 | 9,47 | 25,23 |
| `euklides` | 243 | 95 | 6 / 6 | 151 / 50 | 1664 | 1664 | 14 408 | 14 464 | 8,23 | 26,52 |
| `kalkulator` | 682 | 290 | 22 / 21 | 382 / 141 | 1664 | 1664 | 14 416 | 14 464 | 9,03 | 32,83 |
| `liczba_pierwsza` | 212 | 84 | 10 / 14 | 115 / 31 | 1664 | 1664 | 14 408 | 14 464 | 7,61 | 28,74 |
| `rok_przestepny` | 265 | 124 | 4 / 15 | 136 / 44 | 1664 | 1664 | 14 408 | 14 464 | 8,15 | 29,57 |

### 6.4 Analiza: koszty i zyski strategii stosowej

**Gęstość instrukcji (2,1–2,6× więcej rozkazów).** Nadwyżka jest
bezpośrednią, przewidywalną konsekwencją przyjętego w §4 modelu
**akumulatorowo-stosowego bez alokatora rejestrów**: każdy wynik pośredni
wyrażenia przechodzi cykl `push rax` / `pop rcx`, a każda zmienna żyje
wyłącznie w szczelinie ramki, skąd jest ładowana przy każdym użyciu.
Potwierdza to struktura nadwyżki — koncentruje się ona niemal w całości
w klasie przesłań (`mov`/`push`/`pop`: 3,0–3,7× więcej), podczas gdy
liczba skoków pozostaje porównywalna (1,0×), bo odwzorowanie struktur
sterujących na rozkazy `cmp`+`jcc` jest w obu kompilatorach analogiczne.
GCC nawet na `-O0` utrzymuje wyniki podwyrażeń w rejestrach w obrębie
jednej instrukcji języka, stąd różnica.

**Mniej rozgałęzień w logice boolowskiej.** W benchmarkach
`rok_przestepny` (4 vs 15) i `liczba_pierwsza` (10 vs 14) Nurt emituje
*mniej* skoków niż GCC. To mierzalny efekt decyzji z §3.3/§4: operatory
`i`/`lub` są **wartościowane materializująco** (predykaty `setCC`
łączone `and`/`or` jako wartości 0/1), podczas gdy C wymaga semantyki
skróconej (*short-circuit*), którą GCC na `-O0` realizuje kaskadą skoków
warunkowych. Kod Nurt ma w tych ścieżkach przepływ liniowy, przyjazny
predykcji skoków — koszt: brak skracania wartościowania (w Nurt operandy
logiczne są zawsze czyste, więc różnica nie jest obserwowalna
semantycznie).

**Pamięć i rozmiar binarium: parytet.** Średni szczytowy RSS (~1,6 MB)
jest w granicach ziarnistości pomiaru identyczny — zdominowany przez
mapowanie `libc` i stron startowych procesu, nie przez kod programu.
Rozmiary binariów po `strip` różnią się o ułamek procenta (14 408–14 416 B
vs 14 464 B): obie ścieżki linkują ten sam CRT i `libc` dynamicznie,
a sekcje kodu obu wariantów mieszczą się w tych samych wyrównanych
stronach ELF. Wniosek: nadwyżka instrukcji statycznych **nie propaguje
się** na zajętość zasobów w tej klasie programów.

**Szybkość kompilacji (3–4× szybciej).** Ścieżka `nurtc` źródło→asembler
trwa średnio 7,6–9,5 ms wobec 25,2–32,8 ms GCC. To strukturalna premia
prostoty: cztery fazy nad jednym AST, bez reprezentacji pośrednich
(GIMPLE/RTL), bez passów optymalizacyjnych i bez kosztu inicjalizacji
infrastruktury wielojęzykowego front-endu. Dla iteracyjnego cyklu
edycja–kompilacja–test, istotnego dydaktycznie dla języka edukacyjnego,
jest to właściwość pożądana.

**Bilans.** Strategia stosowa kupuje *prostotę i weryfikowalność
generatora* (deterministyczny przydział ramki §4.1, bezwarunkowa
poprawność wyrównania ABI §4.2, trywialne dowodzenie poprawności emisji
per-konstrukcja) płacąc gęstością kodu — kosztem statycznym, który w
badanej klasie programów nie przekłada się ani na zajętość pamięci, ani
na rozmiar artefaktu. Naturalnym kierunkiem dalszych prac jest liniowy
alokator rejestrów (*linear scan*) nad istniejącą emisją, który
zaadresowałby klasę przesłań — jedyną, w której kod Nurt ustępuje
referencji.
