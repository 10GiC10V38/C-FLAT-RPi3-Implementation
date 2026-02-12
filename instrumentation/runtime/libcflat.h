/* instrumentation/runtime/libcflat.h */
#ifndef LIBCFLAT_H
#define LIBCFLAT_H

#include <stdint.h>

/* These are called by instrumented code */
void __cflat_record_node(uint64_t node_id);
void __cflat_loop_enter(uint64_t loop_id);
void __cflat_loop_exit(uint64_t loop_id);
void __cflat_loop_iteration(uint64_t loop_id);  // NEW
void __cflat_call_enter(uint64_t call_id, uint64_t caller);
void __cflat_call_return(uint64_t call_id);

/* User can call this to print results */
void cflat_finalize_and_print(void);

#endif
