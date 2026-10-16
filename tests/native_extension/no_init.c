/* A shared library with no MM_InitWrapper, which Phoenix should skip. */

#ifdef _WIN32
__declspec(dllexport)
#endif
int not_an_extension(void)
{
    return 42;
}
