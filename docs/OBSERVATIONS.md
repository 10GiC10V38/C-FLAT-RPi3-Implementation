# Implementation Observations & Bug Fixes

This document records every significant issue found during development of the
C-FLAT implementation on RPi3, with root cause analysis and the fix applied.
It is intended to help future contributors understand the non-obvious design
decisions and avoid the same pitfalls.

---

## Bug 1 — Nested Loop Headers Not Discovered

**File**: `instrumentation/llvm-pass/CFlatPass.cpp`
**Symptom**: Nested loop test (`test_loop_nested`) ran without errors but the
inner loop received no `loop_enter`/`loop_iteration` instrumentation — the
inner header was treated as a plain basic block.

**Root cause**:
```cpp
// WRONG — only top-level loops
for (Loop *L : LI) {
    loopHeaders.insert(L->getHeader());
}
```
`LoopInfo::begin()/end()` only yields *top-level* loops. Subloops are not
enumerated; they are only accessible via `L->getSubLoops()`.

**Fix**: Replaced with a worklist that recursively visits all subloops:
```cpp
SmallVector<Loop*, 8> worklist(LI.begin(), LI.end());
while (!worklist.empty()) {
    Loop *L = worklist.pop_back_val();
    loopHeaders.insert(L->getHeader());
    worklist.append(L->getSubLoops().begin(), L->getSubLoops().end());
}
```

**Lesson**: Always use the worklist pattern when walking nested LLVM loop trees.
`LI.begin()/end()` is for top-level loops only.

---

## Bug 2 — Spurious `loop_exit` Inside Inner Loop

**File**: `instrumentation/llvm-pass/CFlatPass.cpp`
**Symptom**: After deploying the fix for Bug 1, nested loop test showed:
```
Loop record not found for loop_id=OUTER_ID at depth=1
```
9 errors total (3 per inner loop execution × 3 outer iterations).

**Root cause**:
Exit detection used:
```cpp
if (SuccLoop != CurrentLoop) {   // WRONG
```
When a block in the **outer** loop branches to the **inner** loop's header,
`LI.getLoopFor(inner_header) = inner_loop` ≠ `CurrentLoop = outer_loop`.
The condition was `true`, so `loop_exit(outer_id)` was inserted at the
inner loop's header — which is still *inside* the outer loop.

This was visible in the instrumented IR:
```llvm
; Block %10 = inner loop header (WRONG: has loop_exit for outer loop)
call void @__cflat_loop_exit(i64 <outer_id>)   ; ← spurious
call void @__cflat_record_node(i64 <inner_id>)
```

**Fix**:
```cpp
if (!CurrentLoop->contains(Succ)) {   // CORRECT
```
`Loop::contains(BasicBlock*)` returns `true` for any block in the loop *or*
any of its subloops. This correctly handles the case where the successor is
the entry of a subloop.

**Lesson**: Never use `getLoopFor(BB) != CurrentLoop` to detect loop exits.
Use `!CurrentLoop->contains(BB)` instead.

---

## Bug 3 — TA Attestation Buffer Overflow

**File**: `secure-world/ta/cflat_ta.c`
**Symptom**: The last loop record in the attestation had a corrupted `loop_id`
(e.g., `0x5def0000044c` instead of `0x5deff9e85830`) when the program had
≥ 14 loop records.

**Root cause**:
```c
uint8_t final_auth[1024];   // WRONG — too small
```
Each loop record serializes to 80 bytes (8+32+32+4+4). With a 36-byte header,
14 records need 36 + 14×80 = 1156 bytes. The 14th record started writing at
offset 1024, exactly at the buffer boundary, overflowing into adjacent memory.

**Fix**:
```c
uint8_t final_auth[36 + MAX_LOOP_RECORDS * 80];  // Always fits all records
```
Also added a bounds check inside the serialization loop:
```c
if (offset + 80 > sizeof(ctx->final_auth)) {
    EMSG("Auth buffer full at loop %d/%d", i, ctx->loop_count);
    break;
}
```

**Lesson**: Size fixed buffers against `MAX_*` constants, not estimated sizes.

---

## Bug 4 — Call Stack Underflow After Finalize

**File**: `instrumentation/runtime/libcflat.c`
**Symptom**: After each `cflat_finalize_and_print()` call:
```
E/TA: cmd_call_return:299 Call stack underflow
D/TC: Error: ffff0007 of 4
```
The error fired for every `call_return` from the function that called
`cflat_finalize_and_print()`.

**Root cause**:
`cflat_finalize_and_print()` called `CMD_INIT` immediately after `CMD_GET_AUTH`
to reset the TA for the next measurement cycle. `CMD_INIT` zeroed
`ctx->call_depth = 0`. But the LLVM pass had already inserted
`__cflat_call_return` *after* `cflat_finalize_and_print()` returns (at the
caller's call site), which fired against the now-empty call stack.

Execution order:
```
call_enter(process_command)   → call_depth = 1
  ... work ...
  cflat_finalize_and_print()
    CMD_FINALIZE
    CMD_GET_AUTH
    CMD_INIT  ← resets call_depth = 0  ← PROBLEM
  return from cflat_finalize_and_print
call_return(process_command)  → call_depth = 0 - 1 = underflow!
```

**Fix**: Deferred re-initialization using a `needs_reinit` flag:
```c
// In cflat_finalize_and_print():
needs_reinit = 1;   // Don't call CMD_INIT here

// In record_node, loop_enter, call_enter:
static void maybe_reinit(void) {
    if (!needs_reinit) return;
    TEEC_InvokeCommand(&sess, CMD_INIT, ...);
    needs_reinit = 0;
}
```
Re-init now fires on the first *forward* instrumented call of the next
measurement cycle — after all pending `call_return`s from the previous
cycle have completed.

**Lesson**: Never reset shared TA state from inside an instrumented call tree.
Defer state resets to the next entry point boundary.

---

## Bug 5 — Loop Record Explosion on Real Benchmarks

**File**: `secure-world/ta/cflat_ta.c`
**Symptom**: `aha-mont64` hit `Too many loops` immediately after starting,
followed by cascading `Loop iteration for inactive/unknown loop` errors.

**Root cause**:
```c
// WRONG — creates a new record every time a loop is entered
LoopRecord *rec = &ctx->loops[ctx->loop_count++];
```
For a nested loop where the inner loop runs N times per outer iteration,
this allocated N `LoopRecord` entries for the same loop. With
`MAX_LOOP_RECORDS = 64`, benchmarks with deeply iterated inner loops
overflowed within a few outer iterations (aha-mont64: outer loop 10,
inner loop 64 = 640 records needed).

**Fix**: Reuse the existing *inactive* record for the same `loop_id`:
```c
// Look for an existing inactive record first
LoopRecord *rec = NULL;
for (int i = 0; i < ctx->loop_count; i++) {
    if (ctx->loops[i].loop_id == loop_header_id && !ctx->loops[i].is_active) {
        rec = &ctx->loops[i];
        break;
    }
}
if (!rec) {
    // Allocate new only if no match found
    rec = &ctx->loops[ctx->loop_count++];
    ...
}
rec->invocation_count++;   // Track how many times this loop was entered
```
Now each *unique* loop_id gets exactly one record. `iteration_count`
accumulates across all invocations; `invocation_count` tracks entries.

**Lesson**: In a long-running TEE session, per-invocation allocation grows
without bound. Index records by identity (loop_id), not by occurrence.

---

## Design Decision: BLAST-Compatible Pass (v0.2)

**Context**: The original pass (v0.1) inserted `loop_enter`, `loop_exit`, and
`loop_iteration` calls in addition to `record_node`/`call_enter`/`call_return`.
This provides richer loop data (iteration counts per loop) but increases
world-switch counts beyond what the C-FLAT paper specifies.

**Decision**: Simplified to v0.2 which only instruments the three CFG edge
types from the paper:
- Branches → `record_node`
- Calls → `call_enter`
- Returns → `call_return`

Loop back-edges are branches and are counted by `record_node`. This matches
the BLAST paper's Table 2 domain-switch model, enabling direct comparison
of world-switch overhead.

**Trade-off**: Loop iteration counts are no longer separately tracked in
attestation output. The hash chain still captures loop behavior implicitly
(different iteration counts produce different hashes), but the per-loop
`iteration_count` and `invocation_count` fields will be zero.

The v0.1 pass with full loop tracking is preserved in git history and can
be restored if per-loop metrics are needed.

---

## Observation: World Switch Cost Breakdown

On RPi3, each world switch (Normal → Secure via `TEEC_InvokeCommand`) has
measurable overhead. The syringe pump case study shows how this scales:

- **Simple command** (`"10"` sets a variable): ~247 switches
  — driven by string parsing branches in `readSerial` + `toUInt`

- **Motor command** (`"+"` 10µL): ~1499 switches
  — dominated by the bolus loop: 68 iterations × ~22 switches per iteration
  (each iteration executes branches for digitalWrite + delayMicroseconds checks)

- **Motor command** (`"+"` 20µL): ~4055 switches
  — 136 iterations × ~22 = ~3000 for the bolus alone

This confirms the BLAST paper's finding that loops with many iterations are
the dominant source of CFA overhead, not one-time initialization code.
