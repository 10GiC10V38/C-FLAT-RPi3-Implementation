# C-FLAT Results: Cubic, N-body & Syringe Pump

This document records results from running C-FLAT instrumented binaries on
**Raspberry Pi 3B** (AArch64, OP-TEE TrustZone).

**Benchmarks tested on RPi3**: `cubic`, `nbody` (Embench-IoT), and the syringe pump case study.
All 15 Embench-IoT benchmarks were built and instrumented; only these two were executed.

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

## Embench-IoT: Tested Benchmarks

Only **cubic** and **nbody** were run on RPi3. The remaining benchmarks were
built and instrumented (pass logs exist in `build/*_pass.log`) but not executed.

### Static instrumentation counts (from pass logs)

| Benchmark | Branches | Calls | Returns | **Total static hooks** |
|-----------|----------|-------|---------|------------------------|
| **cubic** | 29 | 11 | 7 | **47** |
| **nbody** | 46 | 8 | 8 | **62** |

**cubic** function breakdown:
- `SolveCubic`: 3 branches, 0 calls, 1 return
- `benchmark_body`: 20 branches, 5 calls, 1 return
- `verify_benchmark`: 6 branches, 0 calls, 1 return
- `embench_main`: 0 branches, 4 calls, 1 return

**nbody** function breakdown:
- `offset_momentum`: 8 branches, 0 calls, 1 return
- `bodies_energy`: 12 branches, 0 calls, 1 return
- `benchmark_body`: 8 branches, 2 calls, 1 return
- `verify_benchmark`: 18 branches, 0 calls, 1 return
- `embench_main`: 0 branches, 4 calls, 1 return

### RPi3 runtime results

| Benchmark | Static hooks | Dynamic world switches | Screenshots |
|-----------|-------------|----------------------|-------------|
| cubic | 47 | see screenshot | `screenshots/cflat_cubic.png` · `screenshots/cubic_after_correction.png` |
| nbody | 62 | see screenshot | `screenshots/cflat_nbody.png` · `screenshots/nbody_after_correction.png` |

> The `_after_correction` screenshots show clean runs after the loop-handling
> bugs (nested loop exit detection, loop record reuse) were fixed.
> See [OBSERVATIONS.md](OBSERVATIONS.md) for full details of those fixes.

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
