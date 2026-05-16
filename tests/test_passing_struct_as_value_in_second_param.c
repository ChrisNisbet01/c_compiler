#include <stdio.h>

struct epc_parse_result_t
{
    _Bool is_error;
    union
    {
        void * success;
        void * error;
    } data;
};

struct epc_parse_session_t
{
    struct epc_parse_result_t FIRST;
    void * SHOULD_HAVE_OFFSET_16;
};

int
main()
{
    struct epc_parse_session_t s = {0};

    unsigned long offset_to_parse_ctx = ((unsigned long)&((struct epc_parse_session_t *)0)->SHOULD_HAVE_OFFSET_16);
    printf("test: offset is: %lu\n", offset_to_parse_ctx);
    return offset_to_parse_ctx - 16;
}
