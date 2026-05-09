int
main()
{
    char * multipart_string = "This is a "
                              "multipart string";
    char * one_line_string = "This is a multipart string";

    for (int i = 0; one_line_string[i] != 0; i++)
    {
        if (one_line_string[i] != multipart_string[i])
        {
            return i + 1;
        }
    }
    return 0;
}
