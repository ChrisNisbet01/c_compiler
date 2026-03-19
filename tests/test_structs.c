struct Point {
    int x;
    int y;
};

struct Rectangle {
    struct Point top_left;
    int width;
    int height;
};

int main() {
    struct Point p;
    p.x = 10;
    p.y = 20;
    
    struct Rectangle r;
    r.top_left.x = 0;
    r.top_left.y = 0;
    r.width = 100;
    r.height = 50;
    
    return p.x + p.y + r.width + r.height;
}
