# C-FLAT Results: Embench-IoT Benchmarks & Syringe Pump

This document records the measured world-switch counts from running the C-FLAT
instrumented binaries on **Raspberry Pi 3B** (AArch64, OP-TEE TrustZone), and
compares them with the expected values from the **BLAST paper (CCS 2023), Table 2**.

---

## How World Switches Are Counted

Each instrumented CFG edge causes one `TEEC_InvokeCommand` call, which is one
Normal World → Secure World transition. The pass inserts:

| Edge type | Hook | Fires |
|-----------|------|-------|
| Conditional branch | `record_node` | Once per BB execution reaching a br |
| Unconditional branch | `record_node` | Once per BB execution reaching a br |
| Direct function call | `call_enter` | Once per call site execution |
| Function return | `call_return` | Once per return |

**Static count** = total hooks inserted by the pass (from `_pass.log` files).
**Dynamic count** = world switches actually executed at runtime (reported by `cflat_finalize_and_print`).

---

## Embench-IoT: Static Instrumentation Counts

Counts from `build/*_pass.log` — these are the hooks inserted per benchmark:

| Benchmark | Branches | Calls | Returns | **Total hooks** |
|-----------|----------|-------|---------|-----------------|
| aha-mont64 | 22 | 22 | 10 | **54** |
| crc32 | 24 | 10 | 15 | **49** |
| cubic | 29 | 11 | 7 | **47** |
| edn | 60 | 14 | 14 | **88** |
| huffbench | 120 | 14 | 16 | **150** |
| matmult-int | 32 | 11 | 11 | **54** |
| minver | 94 | 10 | 9 | **113** |
| nbody | 46 | 8 | 8 | **62** |
| nettle-aes | 98 | 16 | 15 | **129** |
| nettle-sha256 | 101 | 13 | 12 | **126** |
| primecount | 24 | 7 | 7 | **38** |
| sglib-combined | 854 | 78 | 94 | **1026** |
| st | 23 | 18 | 13 | **54** |
| tarfind | 47 | 11 | 14 | **72** |
| ud | 54 | 7 | 7 | **68** |

> **Note**: Static counts are per-benchmark-function instrumentation.
> Dynamic world-switch counts depend on how many times each hook fires at runtime
> (loop iterations × static count within the loop body).

---

## Comparison with BLAST Paper (Table 2)

The BLAST paper measures world-switch overhead for C-FLAT on the same
Embench-IoT suite. Their metric is **domain switches per benchmark execution**.

The **static hook count** (from pass logs) is the number of unique instrumented
points in the code. The **dynamic world-switch count** is the actual runtime
count — static × number of times each hook fires (loop iterations matter heavily).

| Benchmark | Static hooks | BLAST Table 2 | Our dynamic count | Screenshots |
|-----------|-------------|---------------|-------------------|-------------|
| aha-mont64 | 54 | — | — | — |
| crc32 | 49 | — | — | — |
| cubic | 47 | — | — | `screenshots/cflat_cubic.png` |
| edn | 88 | — | — | — |
| huffbench | 150 | — | — | — |
| matmult-int | 54 | — | — | — |
| minver | 113 | — | — | — |
| nbody | 62 | — | — | `screenshots/cflat_nbody.png` |
| nettle-aes | 129 | — | — | — |
| nettle-sha256 | 126 | — | — | — |
| primecount | 38 | — | — | — |
| sglib-combined | 1026 | — | — | — |
| st | 54 | — | — | — |
| tarfind | 72 | — | — | — |
| ud | 68 | — | — | — |

> **TODO**: Fill `Our dynamic count` column with `World Switches:` value printed
> by each benchmark after running on RPi3. The BLAST paper values are from their
> Table 2 column "C-FLAT domain switches".

---

## Syringe Pump Results

### Physical Integrity Verification

The syringe pump motor stepping loop runs exactly `mLBolus × ustepsPerML` iterations.
`ustepsPerML = 16 × 200 × 80 / (30 × 1.25) = 6826.67 ≈ 6826`

| Command sequence | Volume | Formula | Expected iterations | **Measured** |
|-----------------|--------|---------|-------------------|--------------|
| `"10"` + `"+"` | 10 µL (0.010 mL) | 0.010 × 6826 | **68** | ✅ **68** |
| `"20"` + `"+"` | 20 µL (0.020 mL) | 0.020 × 6826 | **136** | ✅ **136** |
| `"100"` + `"+"` | 100 µL (0.100 mL) | 0.100 × 6826 | **682** | ✅ **682** |

### Attestation Hashes (per command)

Each distinct command sequence produces a unique, deterministic Final Hash:

| Command | Final Hash (SHA-256 chain) |
|---------|---------------------------|
| `"10"` (set 10µL) | `3053ddc3338e1b453b2362c836ce25507ea2297ef94c375495a2e8b1830bd799` |
| `"+"` (dispense 10µL) | `9aae5f6a5891927133...` |
| `"20"` (set 20µL) | `45cb366ea4e02df36b...` |
| `"+"` (dispense 20µL) | `45fbcc6581fe1c9d11...` |

### World Switch Overhead (Syringe Pump)

| Command | World Switches | Dominant source |
|---------|---------------|-----------------|
| `"10"` — set volume | ~247 | readSerial (3 chars) + toUInt branches |
| `"+"` — dispense 10µL | ~1499 | bolus: 68 iterations × ~22 switches/iter |
| `"20"` — set volume | ~1715 | cumulative from start |
| `"+"` — dispense 20µL | ~4055 | bolus: 136 iterations × ~22 switches/iter |

---

## Screenshots

Screenshots are stored in:
- `test-applications/embench-iot-benchmarks/screenshots/` — cubic, nbody results
- GitHub issue attachments — syringe pump attestation output

### Cubic benchmark
`screenshots/cflat_cubic.png` — raw output
`screenshots/cubic_after_correction.png` — after bug fixes

### N-body benchmark
`screenshots/cflat_nbody.png` — raw output
`screenshots/nbody_after_correction.png` — after bug fixes
