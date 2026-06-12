/* rok_przestepny.c - benchmark 5: rok przestepny (odpowiednik
 * rok_przestepny.nrt). Identyczna logika: zagniezdzony warunek
 * (rok % 4 == 0 && rok % 100 != 0) || rok % 400 == 0 w petli zliczajacej. */
#include <stdio.h>

int czy_przestepny(long long rok) {
    return (rok % 4 == 0 && rok % 100 != 0) || rok % 400 == 0;
}

int main(void) {
    long long licznik = 0;
    long long rok = 1;
    while (rok <= 400000) {
        if (czy_przestepny(rok)) {
            licznik = licznik + 1;
        }
        rok = rok + 1;
    }
    printf("%s%lld%s", "lata przestepne w zakresie 1..400000: ", licznik, "\n");

    printf("%s%s%s", "rok 1900: ", czy_przestepny(1900) ? "prawda" : "falsz", "\n");
    printf("%s%s%s", "rok 2000: ", czy_przestepny(2000) ? "prawda" : "falsz", "\n");
    printf("%s%s%s", "rok 2024: ", czy_przestepny(2024) ? "prawda" : "falsz", "\n");
    printf("%s%s%s", "rok 2026: ", czy_przestepny(2026) ? "prawda" : "falsz", "\n");
    return 0;
}
