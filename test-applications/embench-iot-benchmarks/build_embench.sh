#!/bin/bash
# test-applications/embench-iot-benchmarks/build_embench.sh
#
# Build all Embench-IOT benchmarks with C-FLAT instrumentation.
# Binary names: cflat_<benchmark-name>
#
# Paper reference: Table 2 in BLAST (CCS 2023)
# All benchmarks run with CPU_MHZ=1000 (as in the paper).
#
# Usage:
#   ./build_embench.sh            # build all
#   ./build_embench.sh aha-mont64 # build single benchmark

set -e

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

SYSROOT="/home/rajesh/latest_optee/optee_rpi3/out-br/host/aarch64-buildroot-linux-gnu/sysroot"
OPTEE_CLIENT_PATH="$SYSROOT/usr"
CROSS_CC="/home/rajesh/latest_optee/optee_rpi3/toolchains/aarch64/bin/aarch64-none-linux-gnu-gcc"

PASS_SO="$PROJECT_ROOT/outputs/CFlatPass.so"
LIBCFLAT_OBJ="$PROJECT_ROOT/instrumentation/runtime/libcflat.o"
RUNTIME_INC="$PROJECT_ROOT/instrumentation/runtime"
WRAPPER="$SCRIPT_DIR/embench_wrapper.c"

BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

CPU_MHZ=1000   # matches the paper's setting

# ── Benchmark table: <source-file> <benchmark-name> <extra-link-flags> ───────
# source file       benchmark name     extra libs
BENCHMARKS=(
  "basicmath_small.c  cubic            -lm"
  "crc_32.c           crc32            "
  "libedn.c           edn              "
  "libhuffbench.c     huffbench        "
  "libminver.c        minver           -lm"
  "libst.c            st               -lm"
  "libud.c            ud               "
  "matmult-int.c      matmult-int      "
  "mont64.c           aha-mont64       "
  "nbody.c            nbody            -lm"
  "nettle-aes.c       nettle-aes       "
  "nettle-sha256.c    nettle-sha256    "
  "primecount.c       primecount       "
  "sglib-combined.c   sglib-combined   "
  "tarfind.c          tarfind          "
)

# ── Sanity checks ─────────────────────────────────────────────────────────────
check_tools() {
  for tool in clang opt llc llvm-link; do
    if ! command -v "$tool" &>/dev/null; then
      echo -e "${RED}ERROR: '$tool' not found in PATH${NC}"
      exit 1
    fi
  done
  if [ ! -f "$PASS_SO" ]; then
    echo -e "${RED}ERROR: CFlatPass.so not found at $PASS_SO${NC}"
    echo "  Run scripts/build_cflat.sh first."
    exit 1
  fi
  if [ ! -f "$LIBCFLAT_OBJ" ]; then
    echo -e "${RED}ERROR: libcflat.o not found at $LIBCFLAT_OBJ${NC}"
    echo "  Run scripts/build_cflat.sh first."
    exit 1
  fi
  if [ ! -f "$CROSS_CC" ]; then
    echo -e "${RED}ERROR: Cross-compiler not found: $CROSS_CC${NC}"
    exit 1
  fi
}

# ── Build one benchmark ───────────────────────────────────────────────────────
build_benchmark() {
  local SRC="$1"       # e.g. mont64.c
  local NAME="$2"      # e.g. aha-mont64
  local EXTRA="$3"     # e.g. -lm

  local SRC_PATH="$SCRIPT_DIR/$SRC"
  local BINARY="cflat_${NAME}"
  local OUT="$BUILD_DIR/$BINARY"

  # IR / object intermediates (kept for inspection)
  local BENCH_LL="$BUILD_DIR/${NAME}.ll"
  local BENCH_INST_LL="$BUILD_DIR/${NAME}_instrumented.ll"
  local BENCH_OBJ="$BUILD_DIR/${NAME}.o"
  local WRAP_LL="$BUILD_DIR/${NAME}_wrapper.ll"
  local WRAP_INST_LL="$BUILD_DIR/${NAME}_wrapper_instrumented.ll"
  local WRAP_OBJ="$BUILD_DIR/${NAME}_wrapper.o"

  echo -e "${CYAN}── Building cflat_${NAME} ──────────────────────────────${NC}"

  # Step 1: benchmark → LLVM IR
  # -Dmain=embench_main  : rename benchmark's main so we can supply our own
  # -DCPU_MHZ=$CPU_MHZ   : sets number of runs (1000 as in the paper)
  echo "  [1/6] Compiling to LLVM IR..."
  clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
    --target=aarch64-linux-gnu \
    --sysroot="$SYSROOT" \
    -I"$OPTEE_CLIENT_PATH/include" \
    -I"$RUNTIME_INC" \
    -I"$SCRIPT_DIR" \
    -DCPU_MHZ=$CPU_MHZ \
    -Dmain=embench_main \
    "$SRC_PATH" -o "$BENCH_LL" 2>&1 | grep -i "error\|warning" | head -20 || true

  if [ ! -f "$BENCH_LL" ]; then
    echo -e "${RED}  ✗ LLVM IR generation failed for $SRC${NC}"
    return 1
  fi

  # Step 2: apply C-FLAT instrumentation pass to benchmark IR
  # Pass diagnostics go to a log file (stderr piped to head -N can SIGPIPE opt)
  echo "  [2/6] Applying C-FLAT pass..."
  local PASS_LOG="$BUILD_DIR/${NAME}_pass.log"
  opt -load-pass-plugin="$PASS_SO" \
      -passes=cflat-pass \
      "$BENCH_LL" -S -o "$BENCH_INST_LL" 2>"$PASS_LOG" || {
    echo -e "${RED}  ✗ opt returned non-zero for $NAME${NC}"
    tail -5 "$PASS_LOG"
  }

  if [ ! -f "$BENCH_INST_LL" ]; then
    echo -e "${RED}  ✗ C-FLAT pass failed for $NAME${NC}"
    return 1
  fi

  # Step 3: compile instrumented benchmark IR → object
  echo "  [3/6] Compiling instrumented IR to object..."
  llc -march=aarch64 -filetype=obj "$BENCH_INST_LL" -o "$BENCH_OBJ"

  if [ ! -f "$BENCH_OBJ" ]; then
    echo -e "${RED}  ✗ Object compilation failed for $NAME${NC}"
    return 1
  fi

  # Step 4: wrapper → LLVM IR (no -Dmain=embench_main here)
  echo "  [4/6] Compiling wrapper to LLVM IR..."
  clang -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
    --target=aarch64-linux-gnu \
    --sysroot="$SYSROOT" \
    -I"$OPTEE_CLIENT_PATH/include" \
    -I"$RUNTIME_INC" \
    "$WRAPPER" -o "$WRAP_LL" 2>&1 | grep -i "error" | head -10 || true

  if [ ! -f "$WRAP_LL" ]; then
    echo -e "${RED}  ✗ Wrapper IR generation failed${NC}"
    return 1
  fi

  # Step 5: apply C-FLAT pass to wrapper (captures the outer main's nodes)
  echo "  [5/6] Applying C-FLAT pass to wrapper..."
  opt -load-pass-plugin="$PASS_SO" \
      -passes=cflat-pass \
      "$WRAP_LL" -S -o "$WRAP_INST_LL" 2>/dev/null
  llc -march=aarch64 -filetype=obj "$WRAP_INST_LL" -o "$WRAP_OBJ"

  if [ ! -f "$WRAP_OBJ" ]; then
    echo -e "${RED}  ✗ Wrapper object compilation failed${NC}"
    return 1
  fi

  # Step 6: link → cflat_<name>
  echo "  [6/6] Linking..."
  "$CROSS_CC" \
    --sysroot="$SYSROOT" \
    "$BENCH_OBJ" "$WRAP_OBJ" "$LIBCFLAT_OBJ" \
    -o "$OUT" \
    -L"$OPTEE_CLIENT_PATH/lib" -lteec -lm $EXTRA \
    2>&1 | grep -i "error" | head -20 || true

  if [ -f "$OUT" ]; then
    local SIZE
    SIZE=$(stat -c%s "$OUT" 2>/dev/null || echo "?")
    echo -e "${GREEN}  ✓ cflat_${NAME}  (${SIZE} bytes)${NC}"
    return 0
  else
    echo -e "${RED}  ✗ Link failed for $NAME${NC}"
    return 1
  fi
}

# ── Main ──────────────────────────────────────────────────────────────────────
echo -e "${GREEN}════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  C-FLAT Embench-IOT Build  (CPU_MHZ=${CPU_MHZ})${NC}"
echo -e "${GREEN}════════════════════════════════════════════════════════${NC}"
echo ""

check_tools

FILTER="$1"   # optional: only build this benchmark

PASSED=0; FAILED=0; FAILED_LIST=""

for entry in "${BENCHMARKS[@]}"; do
  read -r SRC NAME EXTRA <<< "$entry"

  # If a filter was given, skip non-matching benchmarks
  if [ -n "$FILTER" ] && [ "$NAME" != "$FILTER" ]; then
    continue
  fi

  if build_benchmark "$SRC" "$NAME" "$EXTRA"; then
    PASSED=$((PASSED + 1))
  else
    FAILED=$((FAILED + 1))
    FAILED_LIST="$FAILED_LIST $NAME"
  fi
  echo ""
done

# ── Summary ───────────────────────────────────────────────────────────────────
echo -e "${GREEN}════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Build Summary${NC}"
echo -e "${GREEN}════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  ${GREEN}Passed : $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
  echo -e "  ${RED}Failed : $FAILED ($FAILED_LIST )${NC}"
fi
echo ""
echo "  Binaries in: $BUILD_DIR/"
echo ""
ls -lh "$BUILD_DIR"/cflat_* 2>/dev/null | awk '{printf "    %-28s %s\n", $9, $5}' || true
echo ""

if [ $FAILED -gt 0 ]; then
  exit 1
fi
