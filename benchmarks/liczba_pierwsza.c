/* liczba_pierwsza.c - benchmark 4: test pierwszosci (odpowiednik
 * liczba_pierwsza.nrt). Identyczna logika: dzielenie probne d*d <= n
 * z flaga logiczna, zliczanie pierwszych ponizej 10000 i test 2^31-1. */
#include <stdio.h>

int czy_pierwsza(long long n) {
    if (n < 2) {
        return 0;
    } else {
        int wynik = 1;
        long long d = 2;
        while (d * d <= n && wynik) {
            if (n % d == 0) {
                wynik = 0;
            } else {
                d = d + 1;
            }
        }
        return wynik;
    }
}

int main(void) {
    long long licznik = 0;
    long long n = 2;
    while (n < 10000) {
        if (czy_pierwsza(n)) {
            licznik = licznik + 1;
        }
        n = n + 1;
    }
    printf("%s%lld%s", "liczb pierwszych ponizej 10000: ", licznik, "\n");

    printf("%s%s%s", "czy_pierwsza(2147483647) = ",
           czy_pierwsza(2147483647LL) ? "prawda" : "falsz", "\n");
    printf("%s%s%s", "czy_pierwsza(2147483646) = ",
           czy_pierwsza(2147483646LL) ? "prawda" : "falsz", "\n");
    return 0;
}
