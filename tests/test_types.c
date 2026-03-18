float add_floats(float a, float b) {
    return a + b;
}

int main() {
    float x = 1.5f;
    float y = 2.5f;
    float res = add_floats(x, y);
    if (res == 4.0f) {
        return 0;
    }
    return 1;
}
