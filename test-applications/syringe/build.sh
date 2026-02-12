#!/bin/bash
set -e

SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"
PROJECT_ROOT="$(cd ../.. && pwd)"

echo "Building C-FLAT Syringe Pump Benchmark"
echo "======================================="

# Step 1: Generate LLVM IR
echo "  [1/4] Generating LLVM IR..."
clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
    --target=aarch64-linux-gnu --sysroot=$SYSROOT \
    -I$SYSROOT/usr/include -I../../instrumentation/runtime \
    test_syringe.c -o test_syringe.ll

# Step 2: Run C-FLAT instrumentation pass
echo "  [2/4] Instrumenting with C-FLAT pass..."
opt -load-pass-plugin=$PROJECT_ROOT/outputs/CFlatPass.so \
    -passes=cflat-pass test_syringe.ll -S -o test_syringe_instrumented.ll 2>&1

# Step 3: Compile to object
echo "  [3/4] Compiling to object..."
llc -march=aarch64 -filetype=obj test_syringe_instrumented.ll -o test_syringe.o

# Step 4: Link
echo "  [4/4] Linking..."
$CROSS_CC --sysroot=$SYSROOT test_syringe.o \
    ../../instrumentation/runtime/libcflat.o -o test_syringe_app \
    -L$SYSROOT/usr/lib -lteec -lm

echo ""
echo "Done! Built test_syringe_app"
ls -lh test_syringe_app
echo ""
echo "Deploy: scp test_syringe_app root@<RPI_IP>:/root/"
