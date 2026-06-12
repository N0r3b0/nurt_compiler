/* kalkulator.c - benchmark 3: kalkulator (odpowiednik kalkulator.nrt)
 * Identyczna logika: dyspozycja po operatorze napisowym (lancuch strcmp,
 * jak emisja 'dopasuj' na napisie), dyspozycja po liczbie oraz petla sumy. */
#include <stdio.h>
#include <string.h>

long long oblicz(long long a, const char *op, long long b) {
    if (strcmp(op, "+") == 0) {
        return a + b;
    } else if (strcmp(op, "-") == 0) {
        return a - b;
    } else if (strcmp(op, "*") == 0) {
        return a * b;
    } else if (strcmp(op, "/") == 0) {
        if (b == 0) {
            printf("%s", "blad: dzielenie przez zero\n");
            return 0;
        } else {
            return a / b;
        }
    } else if (strcmp(op, "%") == 0) {
        return a % b;
    } else {
        printf("%s%s%s", "nieznany operator: ", op, "\n");
        return 0;
    }
}

void opisz(long long wynik) {
    if (wynik == 0) {
        printf("%s", "wynik zerowy\n");
    } else if (wynik == 42) {
        printf("%s", "odpowiedz na wszystko!\n");
    } else {
        if (wynik > 0 && wynik % 2 == 0) {
            printf("%s", "wynik dodatni i parzysty\n");
        } else {
            printf("%s", "wynik ujemny lub nieparzysty\n");
        }
    }
}

int main(void) {
    printf("%s%lld%s", "84 + 42 = ", oblicz(84, "+", 42), "\n");
    printf("%s%lld%s", "84 - 42 = ", oblicz(84, "-", 42), "\n");
    printf("%s%lld%s", "84 * 42 = ", oblicz(84, "*", 42), "\n");
    printf("%s%lld%s", "84 / 42 = ", oblicz(84, "/", 42), "\n");
    printf("%s%lld%s", "84 % 42 = ", oblicz(84, "%", 42), "\n");
    /* pisz() w Nurt wypisuje argumenty w miare ich wartosciowania, wiec
     * komunikaty uboczne oblicz() przeplataja sie z tekstem - odwzorowujemy
     * te kolejnosc rozbijajac wywolanie na sekwencje printf. */
    printf("%s", "84 / 0 = ");
    printf("%lld", oblicz(84, "/", 0));
    printf("%s", "\n");
    printf("%s", "84 ^ 42 = ");
    printf("%lld", oblicz(84, "^", 42));
    printf("%s", "\n");

    opisz(oblicz(84, "-", 42));
    opisz(oblicz(84, "%", 42));
    opisz(oblicz(84, "+", 42));
    opisz(oblicz(42, "-", 84));

    long long idx = 0;
    long long suma = 0;
    while (idx < 1000) {
        suma = suma + oblicz(idx, "+", 7);
        suma = suma + oblicz(idx, "*", 3);
        suma = suma + oblicz(idx, "%", 13);
        idx = idx + 1;
    }
    printf("%s%lld%s", "suma kontrolna = ", suma, "\n");
    return 0;
}
