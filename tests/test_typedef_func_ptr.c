typedef void (*void_func_t)(void);
typedef int (*int_func_t)(int);

struct callbacks
{
    void_func_t on_entry;
    int_func_t on_exit;
};

struct callbacks cb;

int main(void)
{
    (void)cb;
    return 0;
}
