/* witaj.c - benchmark 1: Hello World (odpowiednik witaj.nrt)
 * Identyczna logika: statyczny napis -> zmienna -> wypisanie przez printf. */
#include <stdio.h>

int main(void) {
    const char *powitanie = "Witaj, swiecie!\n";
    printf("%s", powitanie);
    return 0;
}
