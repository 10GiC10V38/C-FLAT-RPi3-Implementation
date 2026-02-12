/* secure-world/host/cflat_host.h */
#ifndef CFLAT_HOST_H
#define CFLAT_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Initialize C-FLAT session */
int cflat_init(void);

/* Record a control-flow node */
int cflat_record_node(uint64_t node_id);

/* Loop handling */
int cflat_loop_enter(uint64_t loop_header_id);
int cflat_loop_exit(uint64_t loop_header_id);

/* Call-return matching */
int cflat_call_enter(uint64_t call_site_id, uint64_t caller_node);
int cflat_call_return(uint64_t call_site_id);

/* Finalize and get attestation */
int cflat_finalize(void);
int cflat_get_auth(uint8_t *buffer, size_t *size);

/* Debug: Get execution log */
int cflat_get_log(uint8_t *buffer, size_t *size);

/* Cleanup */
void cflat_cleanup(void);

#endif
