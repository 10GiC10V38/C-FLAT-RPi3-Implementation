/* test-applications/embench-iot-benchmarks/embench_wrapper.c
 *
 * Wrapper main() for C-FLAT-instrumented Embench-IOT benchmarks.
 *
 * Each benchmark's main() is renamed to embench_main() at compile time
 * via -Dmain=embench_main. This file provides the real main() that:
 *   1. Calls the benchmark
 *   2. Calls cflat_finalize_and_print() to report TEE domain switches
 */

#include "../../instrumentation/runtime/libcflat.h"

/* Forward declaration: the benchmark's original main(), renamed by the
 * preprocessor via -Dmain=embench_main */
extern int embench_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    int ret = embench_main(argc, argv);
    cflat_finalize_and_print();
    return ret;
}
