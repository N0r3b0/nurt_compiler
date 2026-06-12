/* euklides.c - benchmark 2: NWD (odpowiednik euklides.nrt)
 * Identyczna logika: wariant petlowy (modulo) i wariant rekurencyjny
 * (liczby Fibonacciego = najgorszy przypadek), plus petla sumy kontrolnej. */
#include <stdio.h>

long long nwd_petla(long long a, long long b) {
    while (b != 0) {
        long long reszta = a % b;
        a = b;
        b = reszta;
    }
    return a;
}

long long nwd_rekurencja(long long a, long long b) {
    if (b == 0) {
        return a;
    } else {
        return nwd_rekurencja(b, a % b);
    }
}

int main(void) {
    printf("%s%lld%s", "nwd_petla(1071, 462) = ", nwd_petla(1071, 462), "\n");
    printf("%s%lld%s", "nwd_rekurencja(1134903170, 701408733) = ",
           nwd_rekurencja(1134903170LL, 701408733LL), "\n");

    long long idx = 0;
    long long suma = 0;
    while (idx < 1000) {
        suma = suma + nwd_petla(idx * 1071, 462);
        suma = suma + nwd_rekurencja(idx * 1071, 462);
        idx = idx + 1;
    }
    printf("%s%lld%s", "suma kontrolna = ", suma, "\n");
    return 0;
}
