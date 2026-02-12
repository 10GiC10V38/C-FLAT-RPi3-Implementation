#!/bin/bash
set -e

echo "=== Building C-FLAT TA ==="
cd secure-world/ta
export TA_DEV_KIT_DIR=/path/to/optee_os/out/arm/export-ta_arm64
make

echo ""
echo "=== Building Test Application ==="
cd ../../test-applications/simple
export TEEC_EXPORT=/path/to/optee_client/out/export
make

echo ""
echo "=== Build Complete ==="
ls -lh *.ta 2>/dev/null || true
ls -lh test_simple 2>/dev/null || true
