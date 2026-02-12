#!/bin/bash
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# --- CONFIGURATION (UPDATE THESE PATHS FOR YOUR SYSTEM) ---
SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"
OPTEE_CLIENT_PATH="$SYSROOT/usr"
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"
TA_DEV_KIT="/home/rajesh/latest_optee/optee_rpi3/optee_os/out/arm/export-ta_arm64"

# Get project root (one level up from scripts/)
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Create outputs directory
mkdir -p "$PROJECT_ROOT/outputs"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  C-FLAT Build Script for RPi3${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Project Root: $PROJECT_ROOT"
echo "TA Dev Kit:   $TA_DEV_KIT"
echo "Sysroot:      $SYSROOT"
echo "Cross CC:     $CROSS_CC"
echo ""

# --- STEP 1: BUILD TA ---
echo -e "${YELLOW}[1/5] Building C-FLAT Trusted Application...${NC}"
cd "$PROJECT_ROOT/secure-world/ta"

if [ ! -d "$TA_DEV_KIT" ]; then
    echo -e "${RED}ERROR: TA_DEV_KIT not found at $TA_DEV_KIT${NC}"
    exit 1
fi

export TA_DEV_KIT_DIR="$TA_DEV_KIT"
export CROSS_COMPILE="aarch64-linux-gnu-"
export CROSS_COMPILE_core="aarch64-linux-gnu-"
export CROSS_COMPILE_ta_arm64="aarch64-linux-gnu-"
make clean || true
make

if [ $? -eq 0 ]; then
    TA_FILE=$(ls *.ta 2>/dev/null | head -n 1)
    if [ -n "$TA_FILE" ]; then
        cp "$TA_FILE" "$PROJECT_ROOT/outputs/"
        echo -e "${GREEN}✓ TA built successfully: $TA_FILE${NC}"
    else
        echo -e "${RED}✗ TA build failed: .ta file not found${NC}"
        exit 1
    fi
else
    echo -e "${RED}✗ TA build failed${NC}"
    exit 1
fi

# --- STEP 2: BUILD LLVM PASS ---
echo -e "${YELLOW}[2/5] Building C-FLAT LLVM Pass...${NC}"
cd "$PROJECT_ROOT/instrumentation/llvm-pass"

if [ ! -f "CFlatPass.cpp" ]; then
    echo -e "${RED}ERROR: CFlatPass.cpp not found${NC}"
    exit 1
fi

mkdir -p build
cd build
cmake .. > /dev/null 2>&1
make

if [ $? -eq 0 ] && [ -f "CFlatPass.so" ]; then
    cp CFlatPass.so "$PROJECT_ROOT/outputs/"
    echo -e "${GREEN}✓ LLVM Pass built successfully: CFlatPass.so${NC}"
else
    echo -e "${RED}✗ LLVM Pass build failed${NC}"
    exit 1
fi

# --- STEP 3: BUILD RUNTIME LIBRARY ---
echo -e "${YELLOW}[3/5] Building libcflat runtime library...${NC}"
cd "$PROJECT_ROOT/instrumentation/runtime"

if [ ! -f "libcflat.c" ]; then
    echo -e "${RED}ERROR: libcflat.c not found${NC}"
    exit 1
fi

$CROSS_CC --sysroot=$SYSROOT -c libcflat.c -o libcflat.o \
    -I$OPTEE_CLIENT_PATH/include

if [ $? -eq 0 ] && [ -f "libcflat.o" ]; then
    echo -e "${GREEN}✓ Runtime library built successfully: libcflat.o${NC}"
else
    echo -e "${RED}✗ Runtime library build failed${NC}"
    exit 1
fi

# --- STEP 4: BUILD TEST APPLICATIONS ---
echo -e "${YELLOW}[4/5] Building test applications...${NC}"

# Function to build a single test app
build_test_app() {
    local TEST_APP=$1
    local TEST_DIR="$PROJECT_ROOT/test-applications/simple"
    
    echo "  Building: $TEST_APP"
    cd "$TEST_DIR"
    
    if [ ! -f "${TEST_APP}.c" ]; then
        echo -e "${RED}    ✗ Source file ${TEST_APP}.c not found${NC}"
        return 1
    fi
    
    # Step 4a: Generate LLVM IR
    clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
        --target=aarch64-linux-gnu \
        --sysroot=$SYSROOT \
        -I$OPTEE_CLIENT_PATH/include \
        -I$PROJECT_ROOT/instrumentation/runtime \
        ${TEST_APP}.c -o ${TEST_APP}.ll 2>&1 | grep -i error || true
    
    if [ ! -f "${TEST_APP}.ll" ]; then
        echo -e "${RED}    ✗ LLVM IR generation failed${NC}"
        return 1
    fi
    
    # Step 4b: Run C-FLAT Pass
    opt -load-pass-plugin="$PROJECT_ROOT/outputs/CFlatPass.so" \
        -passes=cflat-pass \
        ${TEST_APP}.ll -S -o ${TEST_APP}_instrumented.ll 2>&1
    
    if [ ! -f "${TEST_APP}_instrumented.ll" ]; then
        echo -e "${RED}    ✗ Instrumentation failed${NC}"
        return 1
    fi
    
    # Step 4c: Compile to object
    llc -march=aarch64 -filetype=obj ${TEST_APP}_instrumented.ll -o ${TEST_APP}.o 2>&1 | grep -i error || true
    
    if [ ! -f "${TEST_APP}.o" ]; then
        echo -e "${RED}    ✗ Object compilation failed${NC}"
        return 1
    fi
    
    # Step 4d: Link
    $CROSS_CC --sysroot=$SYSROOT \
        ${TEST_APP}.o \
        $PROJECT_ROOT/instrumentation/runtime/libcflat.o \
        -o ${TEST_APP}_app \
        -L$OPTEE_CLIENT_PATH/lib -lteec 2>&1 | grep -i error || true
    
    if [ $? -eq 0 ] && [ -f "${TEST_APP}_app" ]; then
        cp ${TEST_APP}_app "$PROJECT_ROOT/outputs/"
        echo -e "${GREEN}    ✓ Built: ${TEST_APP}_app${NC}"
        return 0
    else
        echo -e "${RED}    ✗ Linking failed${NC}"
        return 1
    fi
}

# Build all test applications
build_test_app "test_simple"
build_test_app "test_loop"

# --- STEP 5: GENERATE DEPLOYMENT INSTRUCTIONS ---
echo -e "${YELLOW}[5/5] Generating deployment files...${NC}"

cat > "$PROJECT_ROOT/outputs/DEPLOY.txt" << 'EOF'
╔════════════════════════════════════════════════════════════════╗
║              C-FLAT Deployment Instructions                    ║
╔════════════════════════════════════════════════════════════════╝

1. Copy files to Raspberry Pi 3:
   ─────────────────────────────────────────────────────────────
   On your development machine:
   
   $ scp outputs/*.ta root@<RPI_IP>:/tmp/
   $ scp outputs/*_app root@<RPI_IP>:/root/

2. Install Trusted Application:
   ─────────────────────────────────────────────────────────────
   On the Raspberry Pi (via SSH):
   
   $ sudo mkdir -p /lib/optee_armtz/
   $ sudo cp /tmp/*.ta /lib/optee_armtz/
   $ sudo chmod 444 /lib/optee_armtz/*.ta
   $ ls -l /lib/optee_armtz/

3. Run test applications:
   ─────────────────────────────────────────────────────────────
   $ cd /root
   $ chmod +x test_simple_app
   $ ./test_simple_app
   
   Expected output:
   [CFLAT] Initialized
   === C-FLAT Simple Test ===
   Result: 15
   === C-FLAT Attestation (XX bytes) ===
   Final Hash: <32-byte hex>
   ...

4. Run loop test:
   ─────────────────────────────────────────────────────────────
   $ ./test_loop_app
   
   Expected output should include loop iteration counts.

5. Troubleshooting:
   ─────────────────────────────────────────────────────────────
   If you get "failed to initialize context":
   - Check if tee-supplicant is running: ps aux | grep tee-supplicant
   - Start if needed: tee-supplicant -d
   
   If you get "failed to open session":
   - Verify TA is in /lib/optee_armtz/
   - Check TA permissions: ls -l /lib/optee_armtz/*.ta
   - Check dmesg for OP-TEE errors: dmesg | grep -i optee

6. Verify instrumentation:
   ─────────────────────────────────────────────────────────────
   Look at the .ll files to see inserted calls:
   $ grep "__cflat" test-applications/simple/*.ll

╚════════════════════════════════════════════════════════════════╝
EOF

echo -e "${GREEN}✓ Deployment instructions created: outputs/DEPLOY.txt${NC}"

# --- FINAL SUMMARY ---
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  BUILD COMPLETE!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Output files in: $PROJECT_ROOT/outputs/"
echo ""
ls -lh "$PROJECT_ROOT/outputs/" 2>/dev/null | grep -v "^total" | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Review outputs/DEPLOY.txt for deployment instructions"
echo "  2. Copy files to your Raspberry Pi 3"
echo "  3. Run test applications"
echo ""
echo -e "${GREEN}Happy Attesting! 🔒${NC}"
