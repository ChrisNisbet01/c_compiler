#include <stdio.h>

int
main()
{
    char * file_name = __FILE__;
    printf("File name: %s\n", file_name);
    return file_name == NULL;
}
