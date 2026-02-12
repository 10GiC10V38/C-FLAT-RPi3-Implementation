/* test-applications/simple/test_loop.c */
#include <stdio.h>
#include "../../instrumentation/runtime/libcflat.h"

int main() {
    printf("=== C-FLAT Loop Test ===\n");
    

    
int sum1 = 0;
for (int i = 0; i < 3; i++) sum1 += i;

int sum2 = 0;
for (int j = 0; j < 5; j++) sum2 += j;
    
    printf("Sum:1 %d 2 %d\n", sum1,sum2);
    
    cflat_finalize_and_print();
    
    return 0;
}
