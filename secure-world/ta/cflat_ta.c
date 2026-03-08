/* secure-world/ta/cflat_ta.c */
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include "cflat_ta.h"

/* Configuration */
#define MAX_LOOP_DEPTH      16
#define MAX_LOOP_RECORDS    64
#define MAX_CALL_DEPTH      32
#define MAX_LOG_SIZE        16384  // 16KB for debugging

/* Loop Measurement Storage */
typedef struct {
    uint64_t loop_id;           // Loop header node ID
    uint8_t  pre_loop_hash[32]; // Hash before loop (reference point)
    uint8_t  loop_hash[32];     // Cumulative hash within loop
    uint32_t iteration_count;   // Total iterations across all invocations
    uint32_t invocation_count;  // How many times this loop was entered
    bool     is_active;         // Currently executing?
    int      depth_level;       // Which nesting level (0-based)
} LoopRecord;

/* Call-Return Matching */
typedef struct {
    uint64_t call_site_id;      // Call site identifier
    uint64_t caller_node;       // Node that made the call
} CallRecord;

/* Session Context */
typedef struct {
    /* Current measurement state */
    uint8_t current_hash[32];   // SHA-256: 32 bytes
    TEE_OperationHandle hash_op;
    bool is_initialized;
    
    /* Loop tracking */
    LoopRecord loops[MAX_LOOP_RECORDS];
    int loop_count;
    int loop_depth;             // Current nesting level
    
    /* Call stack (for return matching) */
    CallRecord call_stack[MAX_CALL_DEPTH];
    int call_depth;
    
    /* Final attestation: 36 header + MAX_LOOP_RECORDS * 80 bytes each */
    uint8_t final_auth[36 + MAX_LOOP_RECORDS * 80];
    uint32_t auth_size;
    
    /* Debug log */
    uint8_t debug_log[MAX_LOG_SIZE];
    uint32_t log_idx;
    
} cflat_session_ctx;

/* ========== Entry Points ========== */

TEE_Result TA_CreateEntryPoint(void) {
    DMSG("C-FLAT TA Create");
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
    DMSG("C-FLAT TA Destroy");
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types, 
                                     TEE_Param params[4], 
                                     void **sess_ctx) {
    (void)param_types;
    (void)params;
    
    cflat_session_ctx *ctx = TEE_Malloc(sizeof(cflat_session_ctx), 0);
    if (!ctx) return TEE_ERROR_OUT_OF_MEMORY;
    
    TEE_MemFill(ctx, 0, sizeof(cflat_session_ctx));
    ctx->hash_op = TEE_HANDLE_NULL;
    
    *sess_ctx = ctx;
    DMSG("C-FLAT Session Opened");
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx) {
    cflat_session_ctx *ctx = (cflat_session_ctx *)sess_ctx;
    
    if (ctx->hash_op != TEE_HANDLE_NULL) {
        TEE_FreeOperation(ctx->hash_op);
    }
    
    TEE_Free(ctx);
    DMSG("C-FLAT Session Closed");
}

/* ========== Helper Functions ========== */

/* Append to debug log */
static void append_log(cflat_session_ctx *ctx, uint8_t tag, 
                      const void *data, uint32_t size) {
    if (ctx->log_idx + 1 + size > MAX_LOG_SIZE) {
        EMSG("Debug log overflow");
        return;
    }
    
    ctx->debug_log[ctx->log_idx++] = tag;
    if (size > 0 && data) {
        TEE_MemMove(&ctx->debug_log[ctx->log_idx], data, size);
        ctx->log_idx += size;
    }
}

/* Update cumulative hash: H_new = SHA256(H_prev || node_id) */
static TEE_Result update_hash(cflat_session_ctx *ctx, uint64_t node_id) {
    if (!ctx->is_initialized) {
        return TEE_ERROR_BAD_STATE;
    }
    
    uint8_t temp_hash[32];
    uint32_t temp_size = 32;
    
    /* Compute: hash(current_hash || node_id) */
    TEE_ResetOperation(ctx->hash_op);
    TEE_DigestUpdate(ctx->hash_op, ctx->current_hash, 32);
    TEE_DigestUpdate(ctx->hash_op, &node_id, 8);
    TEE_DigestDoFinal(ctx->hash_op, NULL, 0, temp_hash, &temp_size);
    
    /* Update current hash */
    TEE_MemMove(ctx->current_hash, temp_hash, 32);
    
    return TEE_SUCCESS;
}

/* ========== Command Handlers ========== */

static TEE_Result cmd_init(cflat_session_ctx *ctx) {
    /* Reset all state */
    TEE_MemFill(ctx->current_hash, 0, 32);
    ctx->loop_count = 0;
    ctx->loop_depth = 0;
    ctx->call_depth = 0;
    ctx->auth_size = 0;
    ctx->log_idx = 0;
    
    /* Initialize SHA-256 */
    if (ctx->hash_op != TEE_HANDLE_NULL) {
        TEE_FreeOperation(ctx->hash_op);
    }
    
    TEE_Result res = TEE_AllocateOperation(&ctx->hash_op, 
                                           TEE_ALG_SHA256,
                                           TEE_MODE_DIGEST, 
                                           0);
    if (res != TEE_SUCCESS) {
        EMSG("Failed to allocate hash operation: 0x%x", res);
        return res;
    }
    
    ctx->is_initialized = true;
    
 //   append_log(ctx, TAG_NODE, NULL, 0); // Log init
    DMSG("C-FLAT initialized");
    return TEE_SUCCESS;
}

static TEE_Result cmd_record_node(cflat_session_ctx *ctx, 
                                   uint64_t node_id) {
    TEE_Result res = update_hash(ctx, node_id);
    if (res != TEE_SUCCESS) return res;
    
 //   append_log(ctx, TAG_NODE, &node_id, 8);
    
    return TEE_SUCCESS;
}

static TEE_Result cmd_loop_enter(cflat_session_ctx *ctx,
                                  uint64_t loop_header_id) {
    if (ctx->loop_depth >= MAX_LOOP_DEPTH) {
        EMSG("Loop nesting too deep");
        return TEE_ERROR_OVERFLOW;
    }

    /* Try to find an existing INACTIVE record for this loop_id.
     * This handles the common case of an inner loop being entered
     * once per outer loop iteration — we reuse the same record
     * and accumulate iterations, rather than allocating a new one. */
    LoopRecord *rec = NULL;
    int rec_idx = -1;
    for (int i = 0; i < ctx->loop_count; i++) {
        if (ctx->loops[i].loop_id == loop_header_id &&
            !ctx->loops[i].is_active) {
            rec = &ctx->loops[i];
            rec_idx = i;
            break;
        }
    }

    if (!rec) {
        /* No existing record — allocate a new one */
        if (ctx->loop_count >= MAX_LOOP_RECORDS) {
            EMSG("Too many unique loops (max %d)", MAX_LOOP_RECORDS);
            return TEE_ERROR_OVERFLOW;
        }
        rec_idx = ctx->loop_count++;
        rec = &ctx->loops[rec_idx];
        rec->loop_id = loop_header_id;
        rec->iteration_count = 0;
        rec->invocation_count = 0;
        rec->depth_level = ctx->loop_depth;
    }

    /* Save pre-loop hash BEFORE resetting */
    TEE_MemMove(rec->pre_loop_hash, ctx->current_hash, 32);

    /* Reset hash for loop sub-program */
    TEE_MemFill(ctx->current_hash, 0, 32);

    rec->is_active = true;
    rec->invocation_count++;
    ctx->loop_depth++;

    /* Update hash with loop header */
    update_hash(ctx, loop_header_id);

    return TEE_SUCCESS;
}
static TEE_Result cmd_loop_exit(cflat_session_ctx *ctx, 
                                 uint64_t loop_header_id) {
    if (ctx->loop_depth <= 0) {
        EMSG("Loop stack underflow");
        return TEE_ERROR_BAD_STATE;
    }
    
    /* Find the active record for this loop_id */
    LoopRecord *rec = NULL;

    for (int i = 0; i < ctx->loop_count; i++) {
        if (ctx->loops[i].loop_id == loop_header_id &&
            ctx->loops[i].is_active) {
            rec = &ctx->loops[i];
            break;
        }
    }
    
    if (!rec) {
        EMSG("Loop record not found for loop_id=0x%lx", loop_header_id);
        return TEE_ERROR_ITEM_NOT_FOUND;
    }
    
    /* Save loop hash (iterations already counted by loop_iteration calls) */
    TEE_MemMove(rec->loop_hash, ctx->current_hash, 32);
    
    /* Restore pre-loop hash */
    TEE_MemMove(ctx->current_hash, rec->pre_loop_hash, 32);
    
    rec->is_active = false;
    ctx->loop_depth--;

    return TEE_SUCCESS;
}
static TEE_Result cmd_loop_iteration(cflat_session_ctx *ctx, 
                                      uint64_t loop_header_id) {
    /* Find the MOST RECENT active loop with this ID */
    LoopRecord *rec = NULL;
    
    for (int i = ctx->loop_count - 1; i >= 0; i--) {  // Search backwards
        if (ctx->loops[i].loop_id == loop_header_id && 
            ctx->loops[i].is_active) {
            rec = &ctx->loops[i];
            break;
        }
    }
    
    if (!rec) {
        /* Loop not active - might be called outside a loop context */
        DMSG("Loop iteration for inactive/unknown loop 0x%lx", loop_header_id);
        return TEE_SUCCESS;
    }
    
    /* Increment iteration count */
    rec->iteration_count++;
    
    return TEE_SUCCESS;
}
static TEE_Result cmd_call_enter(cflat_session_ctx *ctx,
                                  uint64_t call_site_id,
                                  uint64_t caller_node) {
    if (ctx->call_depth >= MAX_CALL_DEPTH) {
        EMSG("Call stack overflow");
        return TEE_ERROR_OVERFLOW;
    }
    
    CallRecord *rec = &ctx->call_stack[ctx->call_depth++];
    rec->call_site_id = call_site_id;
    rec->caller_node = caller_node;
    
  //  append_log(ctx, TAG_CALL, &call_site_id, 8);
    
    return TEE_SUCCESS;
}

static TEE_Result cmd_call_return(cflat_session_ctx *ctx,
                                   uint64_t call_site_id) {
    /* call_return is now inserted at the callee's ret site (before ret),
     * so the ID is the callee BB id — not the original call_site_id.
     * We simply track depth; no ID matching needed. */
    (void)call_site_id;
    if (ctx->call_depth > 0)
        ctx->call_depth--;
    return TEE_SUCCESS;
}

static TEE_Result cmd_finalize(cflat_session_ctx *ctx) {
    /* Check if there are any still-active loops (shouldn't be) */
    for (int i = 0; i < ctx->loop_count; i++) {
        if (ctx->loops[i].is_active) {
            EMSG("Finalizing with active loop 0x%lx", ctx->loops[i].loop_id);
            /* Force close it */
            ctx->loops[i].is_active = false;
        }
    }
    
    /* Serialize attestation:
     * Format: [final_hash(32)] [loop_count(4)] [loop_records...]
     */
    
    uint32_t offset = 0;
    
    /* 1. Final hash */
    TEE_MemMove(&ctx->final_auth[offset], ctx->current_hash, 32);
    offset += 32;
    
    /* 2. Loop count */
    TEE_MemMove(&ctx->final_auth[offset], &ctx->loop_count, 4);
    offset += 4;
    
    /* 3. Each loop record */
    for (int i = 0; i < ctx->loop_count; i++) {
        LoopRecord *rec = &ctx->loops[i];

        /* loop_id(8) + pre_hash(32) + loop_hash(32) + iterations(4) + invocations(4) = 80 */
        if (offset + 80 > sizeof(ctx->final_auth)) {
            EMSG("Auth buffer full at loop %d/%d", i, ctx->loop_count);
            break;
        }

        TEE_MemMove(&ctx->final_auth[offset], &rec->loop_id, 8);
        offset += 8;

        TEE_MemMove(&ctx->final_auth[offset], rec->pre_loop_hash, 32);
        offset += 32;

        TEE_MemMove(&ctx->final_auth[offset], rec->loop_hash, 32);
        offset += 32;

        TEE_MemMove(&ctx->final_auth[offset], &rec->iteration_count, 4);
        offset += 4;

        TEE_MemMove(&ctx->final_auth[offset], &rec->invocation_count, 4);
        offset += 4;
    }
    
    ctx->auth_size = offset;
    
    DMSG("Finalized. Auth size: %u bytes, Loops: %d", 
         ctx->auth_size, ctx->loop_count);
    
    return TEE_SUCCESS;
}

static TEE_Result cmd_get_auth(cflat_session_ctx *ctx, TEE_Param *params) {
    if (ctx->auth_size == 0) {
        EMSG("Attestation not finalized");
        return TEE_ERROR_BAD_STATE;
    }
    
    if (params[0].memref.size < ctx->auth_size) {
        params[0].memref.size = ctx->auth_size;
        return TEE_ERROR_SHORT_BUFFER;
    }
    
    TEE_MemMove(params[0].memref.buffer, ctx->final_auth, ctx->auth_size);
    params[0].memref.size = ctx->auth_size;
    
    return TEE_SUCCESS;
}

static TEE_Result cmd_get_log(cflat_session_ctx *ctx, TEE_Param *params) {
    if (params[0].memref.size < ctx->log_idx) {
        params[0].memref.size = ctx->log_idx;
        return TEE_ERROR_SHORT_BUFFER;
    }
    
    TEE_MemMove(params[0].memref.buffer, ctx->debug_log, ctx->log_idx);
    params[0].memref.size = ctx->log_idx;
    
    return TEE_SUCCESS;
}

/* ========== Main Command Dispatcher ========== */

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx, 
                                       uint32_t cmd_id,
                                       uint32_t param_types, 
                                       TEE_Param params[4]) {
    cflat_session_ctx *ctx = (cflat_session_ctx *)sess_ctx;
    uint64_t node_id, loop_id, call_id, caller;
    
    switch (cmd_id) {
        case CMD_INIT:
            return cmd_init(ctx);
            
        case CMD_RECORD_NODE:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            node_id = params[0].value.a;
            node_id |= ((uint64_t)params[0].value.b << 32);
            
            return cmd_record_node(ctx, node_id);
            
        case CMD_LOOP_ENTER:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            loop_id = params[0].value.a;
            loop_id |= ((uint64_t)params[0].value.b << 32);
            
            return cmd_loop_enter(ctx, loop_id);
            
        case CMD_LOOP_EXIT:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            loop_id = params[0].value.a;
            loop_id |= ((uint64_t)params[0].value.b << 32);
            
            return cmd_loop_exit(ctx, loop_id);
          
        case CMD_LOOP_ITERATION:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
    
            loop_id = params[0].value.a;
            loop_id |= ((uint64_t)params[0].value.b << 32);
    
            return cmd_loop_iteration(ctx, loop_id);
            
        case CMD_CALL_ENTER:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT ||
                TEE_PARAM_TYPE_GET(param_types, 1) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            call_id = params[0].value.a;
            call_id |= ((uint64_t)params[0].value.b << 32);
            
            caller = params[1].value.a;
            caller |= ((uint64_t)params[1].value.b << 32);
            
            return cmd_call_enter(ctx, call_id, caller);
            
        case CMD_CALL_RETURN:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            call_id = params[0].value.a;
            call_id |= ((uint64_t)params[0].value.b << 32);
            
            return cmd_call_return(ctx, call_id);
            
        case CMD_FINALIZE:
            return cmd_finalize(ctx);
            
        case CMD_GET_AUTH:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            return cmd_get_auth(ctx, params);
            
        case CMD_GET_LOG:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            return cmd_get_log(ctx, params);
            
        default:
            EMSG("Unknown command: %u", cmd_id);
            return TEE_ERROR_BAD_PARAMETERS;
    }
}
