/* secure-world/host/cflat_host.c */
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <tee_client_api.h>
#include "cflat_host.h"
#include "../ta/cflat_ta.h"

static TEEC_Context ctx;
static TEEC_Session sess;
static bool initialized = false;

int cflat_init(void) {
    TEEC_Result res;
    TEEC_UUID uuid = TA_CFLAT_UUID;
    uint32_t err_origin;
    
    /* Initialize context */
    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS) {
        errx(1, "TEEC_InitializeContext failed: 0x%x", res);
    }
    
    /* Open session */
    res = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC,
                          NULL, NULL, &err_origin);
    if (res != TEEC_SUCCESS) {
        errx(1, "TEEC_OpenSession failed: 0x%x origin 0x%x", res, err_origin);
    }
    
    /* Send INIT command */
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, 
                                     TEEC_NONE, TEEC_NONE);
    
    res = TEEC_InvokeCommand(&sess, CMD_INIT, &op, &err_origin);
    if (res != TEEC_SUCCESS) {
        errx(1, "CMD_INIT failed: 0x%x", res);
    }
    
    initialized = true;
    printf("[C-FLAT] Initialized\n");
    return 0;
}

int cflat_record_node(uint64_t node_id) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(node_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(node_id >> 32);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_RECORD_NODE, &op, NULL);
    return (res == TEEC_SUCCESS) ? 0 : -1;
}

int cflat_loop_enter(uint64_t loop_header_id) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(loop_header_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(loop_header_id >> 32);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_LOOP_ENTER, &op, NULL);
    return (res == TEEC_SUCCESS) ? 0 : -1;
}

int cflat_loop_exit(uint64_t loop_header_id) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(loop_header_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(loop_header_id >> 32);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_LOOP_EXIT, &op, NULL);
    return (res == TEEC_SUCCESS) ? 0 : -1;
}

int cflat_call_enter(uint64_t call_site_id, uint64_t caller_node) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_VALUE_INPUT,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(call_site_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(call_site_id >> 32);
    op.params[1].value.a = (uint32_t)(caller_node & 0xFFFFFFFF);
    op.params[1].value.b = (uint32_t)(caller_node >> 32);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_CALL_ENTER, &op, NULL);
    return (res == TEEC_SUCCESS) ? 0 : -1;
}

int cflat_call_return(uint64_t call_site_id) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(call_site_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(call_site_id >> 32);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_CALL_RETURN, &op, NULL);
    if (res != TEEC_SUCCESS) {
        printf("[C-FLAT ERROR] Call-return mismatch detected!\n");
        return -1;
    }
    return 0;
}

int cflat_finalize(void) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_FINALIZE, &op, NULL);
    return (res == TEEC_SUCCESS) ? 0 : -1;
}

int cflat_get_auth(uint8_t *buffer, size_t *size) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = *size;
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_GET_AUTH, &op, NULL);
    if (res == TEEC_SUCCESS) {
        *size = op.params[0].tmpref.size;
        return 0;
    } else if (res == TEEC_ERROR_SHORT_BUFFER) {
        *size = op.params[0].tmpref.size;
        return -2; // Need larger buffer
    }
    return -1;
}

int cflat_get_log(uint8_t *buffer, size_t *size) {
    if (!initialized) return -1;
    
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = *size;
    
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_GET_LOG, &op, NULL);
    if (res == TEEC_SUCCESS) {
        *size = op.params[0].tmpref.size;
        return 0;
    }
    return -1;
}

void cflat_cleanup(void) {
    if (initialized) {
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        initialized = false;
        printf("[C-FLAT] Cleaned up\n");
    }
}
