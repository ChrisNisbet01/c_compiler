int main() {
    int x = 10;
    if (x > 5) {
        x = 0;
    } else {
        x = 1;
    }

    while (x < 10) {
        x = x + 1;
        if (x == 5) {
            continue;
        }
        if (x == 8) {
            break;
        }
    }

    for (int i = 0; i < 5; i = i + 1) {
        x = x + i;
    }

    do {
        x = x - 1;
    } while (x > 0);

    switch (x) {
        case 0:
            return 0;
        default:
            return 1;
    }

    goto end;
end:
    return 0;
}
