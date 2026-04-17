char * lib_paths[64];

int
main(void)
{
    lib_paths[0] = "abc";

    return lib_paths[0] == 0;
}
