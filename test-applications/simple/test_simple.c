/* test-applications/simple/test_simple.c */
#include <stdio.h>
#include "../../instrumentation/runtime/libcflat.h"

int add(int a, int b) {
    return a + b;
}

int main() {
    printf("=== C-FLAT Simple Test ===\n");
    
    int x = 5;
    int y = 10;
    int result = add(x, y);
    
    printf("Result: %d\n", result);
    
    // Get attestation
    cflat_finalize_and_print();
    
    return 0;
}
