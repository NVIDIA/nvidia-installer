/*
 * Trivial __thread variable test.
 *
 * Gareth Hughes <gareth@nvidia.com>
 */

#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>

void seghandle(int bar);

int main(int argc, char *argv[])
{
    void *handle;
    int (*func)(void);
    
    signal(SIGSEGV, seghandle); 
    
    if (argc != 2) {
        exit(1);
    }
    
    handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) {
        exit(1);
    }

    func = dlsym(handle, "getTLSVar");
    if (!func) {
        exit(1);
    }

    func();

    if (dlclose(handle) != 0) {
        exit(1);
    }
    
    return 0;
}

void seghandle(int bar)
{
    exit(1);
}
