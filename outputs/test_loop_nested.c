/* test-applications/simple/test_loop.c */
#include <stdio.h>
#include "../../instrumentation/runtime/libcflat.h"

int main() {
    printf("=== C-FLAT Loop Test ===\n");
    
    int sum = 0;
for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
        sum += i * j;
    }
}
    
    printf("Sum: %d\n", sum);
    
    cflat_finalize_and_print();
    
    return 0;
}
