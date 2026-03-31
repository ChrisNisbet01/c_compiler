struct Inner { int x; int y; };
struct Outer { struct Inner inner; int z; };

int main() {
    struct Outer o;
    o.inner.x = 1;
    o.inner.y = 2;
    o.z = 3;
    return o.inner.x + o.inner.y + o.z - 6;
}
