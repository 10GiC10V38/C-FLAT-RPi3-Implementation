#!/bin/bash
set -e

SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"
PROJECT_ROOT="$(cd ../.. && pwd)"

echo "Building all C-FLAT test applications..."
echo "=========================================="

build_test() {
    local TEST_NAME=$1
    echo ""
    echo "Building ${TEST_NAME}..."
    
    # Generate LLVM IR
    clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
        --target=aarch64-linux-gnu --sysroot=$SYSROOT \
        -I$SYSROOT/usr/include -I../../instrumentation/runtime \
        ${TEST_NAME}.c -o ${TEST_NAME}.ll
    
    # Instrument with C-FLAT pass
    opt -load-pass-plugin=$PROJECT_ROOT/outputs/CFlatPass.so \
        -passes=cflat-pass ${TEST_NAME}.ll -S -o ${TEST_NAME}_instrumented.ll
    
    # Compile to object
    llc -march=aarch64 -filetype=obj ${TEST_NAME}_instrumented.ll -o ${TEST_NAME}.o
    
    # Link
    $CROSS_CC --sysroot=$SYSROOT ${TEST_NAME}.o \
        ../../instrumentation/runtime/libcflat.o -o ${TEST_NAME}_app \
        -L$SYSROOT/usr/lib -lteec
    
    echo "✓ Built ${TEST_NAME}_app"
}

# Build all tests
build_test "test_simple"
build_test "test_loop"
build_test "test_loop_10"
build_test "test_loop_nested"
build_test "test_loop_cond"
build_test "test_loop_mult"

echo ""
echo "=========================================="
echo "All tests built successfully!"
echo ""
echo "Test binaries:"
ls -lh *_app

echo ""
echo "Copy to RPi3:"
echo "scp *_app root@<RPI_IP>:/root/"
