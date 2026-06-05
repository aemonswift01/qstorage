
#include "xlog.h"
#include "config.h"
#include <cstdio>

int add(int a, int b)
{
#ifdef DEBUG
    printf("DEBUG: %d + %d = %d\n", a, b, a + b);
#endif
    return a + b;
}
