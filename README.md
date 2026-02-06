# C-FLAT for Raspberry Pi 3

Control-Flow Attestation implementation using OP-TEE TrustZone on RPi3.

## Directory Structure
- `reference/c-flat/` - Original C-FLAT implementation (for reference)
- `instrumentation/` - LLVM pass and runtime support
- `secure-world/` - OP-TEE Trusted Application
- `test-applications/` - Test programs
- `tools/` - Analysis scripts
- `measurements/` - Expected hash database

## Building
See `scripts/build_all.sh`

## Testing
See `scripts/run_tests.sh`
