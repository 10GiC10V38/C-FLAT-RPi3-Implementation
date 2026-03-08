
---

# C-FLAT: Control-Flow Attestation for Embedded Systems on ARM TrustZone

A prototype implementation of **C-FLAT** (Control-FLow ATtestation) on Raspberry Pi 3B using **ARM TrustZone** (OP-TEE) and **LLVM** static instrumentation.

C-FLAT extends standard static attestation to verify the **runtime execution path** of a program — not just that benign code is loaded, but that it ran the expected way. This detects control-flow hijacking attacks (ROP, JOP) and non-control-data attacks that alter program behavior without changing code.

> Based on: *C-FLAT: Control-Flow Attestation for Embedded Systems Software* — Abera et al., CCS 2016
> Evaluated against: *BLAST: Practical Overhead for Control-Flow Attestation* — CCS 2023

**Detailed docs**: [docs/RESULTS.md](docs/RESULTS.md) · [docs/OBSERVATIONS.md](docs/OBSERVATIONS.md)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Normal World (Linux / RPi3)                   │
│                                                                   │
│  ┌─────────────────────┐        ┌──────────────────────────┐    │
│  │  Instrumented App   │        │     libcflat.c (runtime)  │    │
│  │  (e.g. syringe_app) │──────▶ │  __cflat_record_node()   │    │
│  │                     │        │  __cflat_call_enter()    │    │
│  │  [LLVM pass inserts │        │  __cflat_call_return()   │    │
│  │   hooks at compile  │        │  cflat_finalize_and_     │    │
│  │   time]             │        │  print()                 │    │
│  └─────────────────────┘        └────────────┬─────────────┘    │
│                                               │ TEEC_InvokeCommand│
│                                               │ (= world switch) │
└───────────────────────────────────────────────┼─────────────────┘
                                                │ SMC instruction
┌───────────────────────────────────────────────┼─────────────────┐
│                   Secure World (OP-TEE)        │                  │
│                                               ▼                  │
│                          ┌──────────────────────────────────┐   │
│                          │    cflat_ta.c (Trusted App)       │   │
│                          │                                    │   │
│                          │  SHA-256 hash chain:               │   │
│                          │  H_new = SHA256(H_prev || node_id) │   │
│                          │                                    │   │
│                          │  Loop records: id, iterations,     │   │
│                          │  invocations, pre/post hashes      │   │
│                          │                                    │   │
│                          │  Call stack: detects mismatched    │   │
│                          │  call/return edges (ROP)           │   │
│                          └──────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

Every `TEEC_InvokeCommand` call crosses the Normal World → Secure World boundary. The total count of these switches is the **world switch overhead**.

---

## Repository Structure

```
c-flat-rpi3/
├── instrumentation/
│   ├── llvm-pass/
│   │   └── CFlatPass.cpp       # LLVM pass: inserts hooks at compile time
│   └── runtime/
│       ├── libcflat.c          # Normal World runtime: wraps TEEC calls
│       └── libcflat.h          # API header (include in your program)
│
├── secure-world/
│   └── ta/
│       ├── cflat_ta.c          # Trusted Application: hash + loop tracking
│       └── cflat_ta.h          # TA command IDs and UUID
│
├── test-applications/
│   ├── simple/                 # Unit tests: loops, nested loops, conditionals
│   └── syringe/                # Syringe pump case study
│
├── scripts/
│   ├── build_cflat.sh          # Full build: TA + pass + runtime + tests
│   └── build_all.sh
│
├── outputs/                    # Pre-built binaries and instrumented IR
│   ├── a1b2c3d4-...-.ta        # Deploy to /lib/optee_armtz/ on RPi3
│   ├── CFlatPass.so            # LLVM pass plugin (used by opt)
│   └── libcflat.o              # Runtime object (link into your program)
│
└── reference/c-flat/           # Original C-FLAT reference (git submodule)
```

---

## Build Pipeline

Source code goes through four stages:

```
program.c
   │
   │  clang -emit-llvm -S -O2
   ▼
program.ll                    ← Human-readable LLVM Intermediate Representation
   │
   │  opt -load-pass-plugin CFlatPass.so -passes="cflat-pass"
   ▼
program_instrumented.ll       ← IR with __cflat_* calls inserted at every
   │                            branch, call site, and return
   │  llc -filetype=obj
   ▼
program.o                     ← AArch64 ELF object file
   │
   │  aarch64-gcc + libcflat.o + libteec
   ▼
program_app                   ← Final binary, ready to deploy to RPi3
```

---

## What the LLVM Pass Instruments

`CFlatPass.cpp` runs once per function at compile time and inserts three types of hooks:

| CFG Edge | Hook Inserted | Where |
|----------|--------------|-------|
| Conditional branch | `__cflat_record_node(bb_id)` | Before the `br` terminator |
| Unconditional branch | `__cflat_record_node(bb_id)` | Before the `br` terminator |
| Direct function call | `__cflat_call_enter(call_id, caller_id)` | Before the `call` instruction |
| Function return | `__cflat_call_return(bb_id)` | Before the `ret` terminator |

Loop back-edges are branches and are measured by `record_node`. This design matches the C-FLAT paper's domain-switch model and allows direct comparison with Table 2 of the BLAST paper.

**The pass skips**: declarations, indirect calls, compiler intrinsics, and external library functions (e.g., `printf`, `malloc`).

---

## What the TA Measures

The Trusted Application (`cflat_ta.c`) maintains:

1. **Rolling hash chain** — every `record_node` call updates:
   ```
   H_new = SHA256(H_prev || node_id)
   ```
   The final hash uniquely identifies the exact sequence of basic blocks executed.

2. **Loop records** — for each unique loop (identified by its header's address):
   - `iteration_count` — total loop iterations across all invocations
   - `invocation_count` — how many times the loop was entered
   - `pre_loop_hash` / `loop_hash` — hash snapshots for the sub-program

3. **Call stack** — tracks `call_enter`/`call_return` pairs to detect mismatched edges (the signature of a ROP attack).

**Attestation format** (returned by `cflat_finalize_and_print`):
```
[final_hash: 32 bytes]
[loop_count: 4 bytes]
[loop_record × N: 80 bytes each]
  └── loop_id(8) + pre_hash(32) + loop_hash(32) + iterations(4) + invocations(4)
```

---

## Setup & Build

### Prerequisites

- LLVM/Clang ≥ 14
- OP-TEE developer environment for AArch64
- AArch64 cross-compiler (`aarch64-none-linux-gnu-gcc`)
- Raspberry Pi 3B running OP-TEE

### Build Everything

```bash
# Full build: TA + LLVM pass + runtime + test apps
bash scripts/build_cflat.sh
```

### Deploy to RPi3

```bash
# One-time: copy the TA to the secure storage partition
scp outputs/a1b2c3d4-5678-9abc-def0-123456789abc.ta root@<RPI_IP>:/lib/optee_armtz/

# Copy and run test apps
scp outputs/test_loop_nested_app root@<RPI_IP>:/root/
scp test-applications/syringe/test_syringe_app root@<RPI_IP>:/root/
```

### Instrument an Arbitrary Program

```bash
PASS=outputs/CFlatPass.so
RT=outputs/libcflat.o
SYSROOT=<path-to-aarch64-sysroot>
CC=aarch64-none-linux-gnu-gcc

# 1. Emit LLVM IR
clang -emit-llvm -S -O2 -target aarch64-linux-gnu myprogram.c -o myprogram.ll

# 2. Instrument
opt -load-pass-plugin $PASS -passes="cflat-pass" -S myprogram.ll -o myprogram_inst.ll

# 3. Compile to object
llc -march=aarch64 -filetype=obj myprogram_inst.ll -o myprogram.o

# 4. Link
$CC myprogram.o $RT --sysroot=$SYSROOT -lteec -lm -o myprogram_app
```

Add one call to `cflat_finalize_and_print()` where you want the attestation output:

```c
#include "libcflat.h"

int main(void) {
    // ... your program ...
    cflat_finalize_and_print();
    return 0;
}
```

---

## Key Observations & Results

### Syringe Pump Case Study

The syringe pump (`test-applications/syringe/`) demonstrates verification of physical integrity.
The motor stepping loop runs exactly `mLBolus × ustepsPerML` iterations:

| Command | Volume | Expected iterations | Measured |
|---------|--------|-------------------|----------|
| `"10"` then `"+"` | 10 µL (0.010 mL) | 0.010 × 6826 = **68** | ✅ 68 |
| `"20"` then `"+"` | 20 µL (0.020 mL) | 0.020 × 6826 = **136** | ✅ 136 |
| `"100"` then `"+"` | 100 µL (0.100 mL) | 0.100 × 6826 = **682** | ✅ 682 |

A mismatch here indicates a non-control-data attack — an attacker altered the dispensed volume without changing code.

### Path Fingerprinting

Every distinct command sequence produces a unique Final Hash. Two runs of the same sequence produce the same hash (deterministic). A tampered run produces a different hash.

### World Switch Overhead

Each instrumented edge costs one Normal→Secure world switch:

| Benchmark / Command | World Switches | Dominant cost |
|--------------------|---------------|---------------|
| `test_simple` (1 call) | ~10 | call_enter + call_return |
| `test_loop` (5 iterations) | ~60 | record_node per branch |
| Syringe `"10"` (set volume) | ~247 | readSerial + toUInt branches |
| Syringe `"+"` (10 µL bolus) | ~1500 | 68 motor loop iterations × branches |
| Syringe `"+"` (20 µL bolus) | ~4055 | 136 motor loop iterations × branches |

The bolus command dominates because the inner motor loop executes 4 branches × N iterations, each generating a world switch.

---

## Development Notes: Bugs Found and Fixed

These issues were discovered during implementation and are documented here to help future contributors.

### Bug 1 — Nested loop headers not discovered
**File**: `CFlatPass.cpp`
**Symptom**: Inner loop headers were treated as regular basic blocks — no `loop_enter`/`loop_iteration` inserted.
**Root cause**: `for (Loop *L : LI)` only yields top-level loops. Subloops are not enumerated.
**Fix**: Replaced with a worklist that recursively visits `L->getSubLoops()`.

### Bug 2 — Spurious `loop_exit` inside inner loop
**File**: `CFlatPass.cpp`
**Symptom**: `Loop record not found` errors at depth=1 during nested loop execution.
**Root cause**: Exit detection used `SuccLoop != CurrentLoop`. When a block in the outer loop branched to the inner loop's header, `SuccLoop` (inner) ≠ `CurrentLoop` (outer) — but the destination is still *inside* the outer loop.
**Fix**: Changed to `!CurrentLoop->contains(Succ)`. The `contains()` method correctly returns `true` for subloop blocks.

### Bug 3 — TA attestation buffer overflow
**File**: `cflat_ta.c`
**Symptom**: Corrupted loop IDs in the last attestation record (e.g., `0x5def0000044c` instead of a valid pointer).
**Root cause**: `final_auth[1024]` was too small. With 14 loop records × 76 bytes + 36 header = 1100 bytes required.
**Fix**: Changed to `final_auth[36 + MAX_LOOP_RECORDS * 80]`.

### Bug 4 — Call stack underflow after finalize
**File**: `libcflat.c`
**Symptom**: `Call stack underflow` error and `Error: ffff0007` after each `cflat_finalize_and_print()` call.
**Root cause**: `CMD_INIT` (re-initialization) was called directly inside `cflat_finalize_and_print()`, resetting the TA call stack to depth 0. But `__cflat_call_return` for the enclosing function fired *after* finalize returned, finding an empty stack.
**Fix**: Deferred re-init using a `needs_reinit` flag. Re-init fires on the next *forward* instrumentation call (`record_node`, `loop_enter`, `call_enter`) — which is the first call of the next measurement cycle, after all pending `call_return`s have completed.

### Bug 5 — Loop record explosion on real benchmarks
**File**: `cflat_ta.c`
**Symptom**: `Too many loops` error on aha-mont64; the benchmark hit `MAX_LOOP_RECORDS=64` quickly.
**Root cause**: Every `loop_enter` call allocated a *new* `LoopRecord`. For a loop entered 1000 times (inner loop, outer loop runs 1000 iterations), 1000 records were created.
**Fix**: Changed `cmd_loop_enter` to reuse an existing *inactive* record with the same `loop_id`. Now each unique loop gets one record; `invocation_count` tracks how many times it was entered.

---

## Comparison with Reference C-FLAT

| Aspect | Reference C-FLAT | This Implementation |
|--------|-----------------|-------------------|
| Instrumentation | Binary-level (hookit tool) | LLVM IR (compile-time pass) |
| Event capture | SMC trap on every branch | Explicit calls inserted by pass |
| Loop detection | Inferred from branch table + loop table | Explicit `loop_enter`/`loop_exit` calls |
| Loop hashing | Per-iteration path hash | Cumulative hash over loop body |
| Nested loops | Loop stack with entry/exit pairs | `contains()` for subloop detection |
| Platform | ARMv7 (32-bit) | AArch64 (64-bit) |
| TEE interface | Direct SMC from hook trampolines | TEEC Client API (userspace) |

---

## Acknowledgments

- Original C-FLAT paper: *C-FLAT: Control-Flow Attestation for Embedded Systems Software* — Abera et al., CCS 2016
- BLAST overhead analysis: *BLAST: Practical Overhead for Control-Flow Attestation* — CCS 2023
- Reference implementation: [sss-lab/c-flat](https://github.com/sss-lab/c-flat)
- OP-TEE project: [OP-TEE OS](https://github.com/OP-TEE/optee_os)

---
