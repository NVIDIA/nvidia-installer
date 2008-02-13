static __thread int foo;

int getTLSVar(void)
{
    foo = 0;
    return foo;
}
