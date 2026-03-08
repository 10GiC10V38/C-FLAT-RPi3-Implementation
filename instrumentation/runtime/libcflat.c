/* instrumentation/runtime/libcflat.c */
#include <stdio.h>
#include <string.h>
#include <tee_client_api.h>

/* Match TA UUID */
#define TA_CFLAT_UUID \
    { 0xa1b2c3d4, 0x5678, 0x9abc, \
        { 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc} }

/* Match TA Commands */
#define CMD_INIT           0
#define CMD_RECORD_NODE    1
#define CMD_LOOP_ENTER     2
#define CMD_LOOP_EXIT      3
#define CMD_LOOP_ITERATION 4
#define CMD_CALL_ENTER     5
#define CMD_CALL_RETURN    6
#define CMD_FINALIZE       7
#define CMD_GET_AUTH       8


static TEEC_Context ctx;
static TEEC_Session sess;
static int initialized = 0;
static int needs_reinit = 0;
static uint32_t world_switch_count = 0;

/* Auto-initialize on first use */
static void ensure_init(void) {
    if (initialized) return;

    TEEC_UUID uuid = TA_CFLAT_UUID;
    TEEC_Result res;
    uint32_t err_origin;

    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS) {
        printf("[CFLAT] Context init failed: 0x%x\n", res);
        return;
    }

    res = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC,
                          NULL, NULL, &err_origin);
    if (res != TEEC_SUCCESS) {
        printf("[CFLAT] Session open failed: 0x%x\n", res);
        return;
    }

    /* Send INIT command */
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    res = TEEC_InvokeCommand(&sess, CMD_INIT, &op, NULL);
    world_switch_count++;
    if (res != TEEC_SUCCESS) {
        printf("[CFLAT] INIT failed: 0x%x\n", res);
        return;
    }

    initialized = 1;
}

/* Deferred re-init: called from "forward" instrumentation functions
 * (record_node, loop_enter, call_enter) to reset TA state for the
 * next measurement cycle. This fires AFTER all pending call_returns
 * from the previous cycle have completed. */
static void maybe_reinit(void) {
    if (!needs_reinit) return;

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    TEEC_InvokeCommand(&sess, CMD_INIT, &op, NULL);
    world_switch_count++;
    needs_reinit = 0;
}

/* Called by instrumented code - Record a node */
void __cflat_record_node(uint64_t node_id) {
    ensure_init();
    if (!initialized) return;
    maybe_reinit();

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(node_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(node_id >> 32);

    TEEC_InvokeCommand(&sess, CMD_RECORD_NODE, &op, NULL);
    world_switch_count++;
}

/* Called by instrumented code - Loop enter */
void __cflat_loop_enter(uint64_t loop_id) {
    ensure_init();
    if (!initialized) return;
    maybe_reinit();

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(loop_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(loop_id >> 32);

    TEEC_InvokeCommand(&sess, CMD_LOOP_ENTER, &op, NULL);
    world_switch_count++;
}

/* Called by instrumented code - Loop exit */
void __cflat_loop_exit(uint64_t loop_id) {
    ensure_init();
    if (!initialized) return;

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(loop_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(loop_id >> 32);

    TEEC_InvokeCommand(&sess, CMD_LOOP_EXIT, &op, NULL);
    world_switch_count++;
}


/* Called by instrumented code - Loop iteration (on header re-entry) */
void __cflat_loop_iteration(uint64_t loop_id) {
    ensure_init();
    if (!initialized) return;

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(loop_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(loop_id >> 32);

    TEEC_InvokeCommand(&sess, CMD_LOOP_ITERATION, &op, NULL);
    world_switch_count++;
}


/* Called by instrumented code - Function call */
void __cflat_call_enter(uint64_t call_id, uint64_t caller) {
    ensure_init();
    if (!initialized) return;
    maybe_reinit();

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_VALUE_INPUT,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(call_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(call_id >> 32);
    op.params[1].value.a = (uint32_t)(caller & 0xFFFFFFFF);
    op.params[1].value.b = (uint32_t)(caller >> 32);

    TEEC_InvokeCommand(&sess, CMD_CALL_ENTER, &op, NULL);
    world_switch_count++;
}

/* Called by instrumented code - Function return */
void __cflat_call_return(uint64_t call_id) {
    ensure_init();
    if (!initialized) return;

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = (uint32_t)(call_id & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(call_id >> 32);

    TEEC_InvokeCommand(&sess, CMD_CALL_RETURN, &op, NULL);
    world_switch_count++;
}

/* User callable - Finalize and get attestation */
void cflat_finalize_and_print(void) {
    if (!initialized) {
        printf("[CFLAT] Not initialized\n");
        return;
    }

    /* Finalize */
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_FINALIZE, &op, NULL);
    world_switch_count++;
    if (res != TEEC_SUCCESS) {
        printf("[CFLAT] Finalize failed: 0x%x\n", res);
        return;
    }

    /* Get attestation */
    uint8_t auth[8192];
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
                                     TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = auth;
    op.params[0].tmpref.size = sizeof(auth);

    res = TEEC_InvokeCommand(&sess, CMD_GET_AUTH, &op, NULL);
    world_switch_count++;
    if (res != TEEC_SUCCESS) {
        printf("[CFLAT] Get auth failed: 0x%x\n", res);
        return;
    }

    /* Defer re-init to the next "forward" instrumentation call.
     * This allows pending call_returns to complete against the
     * current TA state before resetting. */
    needs_reinit = 1;

    size_t auth_size = op.params[0].tmpref.size;
    printf("\n=== C-FLAT Attestation (%zu bytes) ===\n", auth_size);

    /* Print final hash (first 32 bytes) */
    printf("Final Hash: ");
    for (int i = 0; i < 32 && i < (int)auth_size; i++) {
        printf("%02x", auth[i]);
    }
    printf("\n");

    /* Parse loop records */
    if (auth_size > 36) {
        uint32_t loop_count = *(uint32_t*)&auth[32];
        printf("Loop Count: %u\n", loop_count);

        size_t offset = 36;
        for (uint32_t i = 0; i < loop_count && offset + 80 <= auth_size; i++) {
            uint64_t loop_id = *(uint64_t*)&auth[offset];
            uint32_t iterations = *(uint32_t*)&auth[offset + 72];
            uint32_t invocations = *(uint32_t*)&auth[offset + 76];

            printf("  Loop 0x%lx: %u iterations (%u invocations)\n",
                   loop_id, iterations, invocations);
            offset += 80; // 8 + 32 + 32 + 4 + 4
        }
    }

    printf("World Switches: %u\n", world_switch_count);
    printf("=== End Attestation ===\n\n");
}

/* Cleanup (optional, call at program exit) */
void __attribute__((destructor)) cflat_cleanup(void) {
    if (initialized) {
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
    }
}
