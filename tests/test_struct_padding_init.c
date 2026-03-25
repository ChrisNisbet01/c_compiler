#include <stdio.h>

struct Padded {
    char a;
    int b;
    char c;
};

int main() {
    struct Padded p = {0};
    printf("p.a = %d, p.b = %d, p.c = %d\n", p.a, p.b, p.c);
    if (p.a != 0 || p.b != 0 || p.c != 0) {
        return 1;
    }

    struct Padded q = {1, 2, 3};
    printf("q.a = %d, q.b = %d, q.c = %d\n", q.a, q.b, q.c);
    if (q.a != 1 || q.b != 2 || q.c != 3) {
        return 2;
    }

    struct Padded r;
    r.a = 5;
    r.b = 10;
    r.c = 15;
    printf("r.a = %d, r.b = %d, r.c = %d\n", r.a, r.b, r.c);
    if (r.a != 5 || r.b != 10 || r.c != 15) {
        return 3;
    }

    return 0;
}
