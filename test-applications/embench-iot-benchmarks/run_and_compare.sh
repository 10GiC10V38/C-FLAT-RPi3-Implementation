#!/bin/bash
# test-applications/embench-iot-benchmarks/run_and_compare.sh
#
# Run each cflat_<benchmark> binary and compare TEE domain-switch counts
# against Table 2 of the BLAST paper (CCS 2023).
#
# Run this ON THE RASPBERRY PI 3 after deploying the binaries and TA.
#
# Usage:
#   ./run_and_compare.sh [binary-dir]
#
# Default binary-dir is the same directory as this script.

set -e

# ── Reference values from Table 2 (CFLAT column) ─────────────────────────────
# Format:  benchmark-name  expected-switch-count
declare -A TABLE2_CFLAT=(
  ["aha-mont64"]="857844016"
  ["crc32"]="871930016"
  ["cubic"]="2030022"
  ["edn"]="1106118020"
  ["huffbench"]="984236016"
  ["matmult-int"]="1201018222"
  ["minver"]="277500079"
  ["nbody"]="17279126"
  ["nettle-aes"]="227449298"
  ["nettle-sha256"]="272250050"
  ["primecount"]="1607180016"
  ["sglib-combined"]="1461660932"
  ["st"]="43329019"
  ["tarfind"]="267607860"
  ["ud"]="1031644018"
)

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

BIN_DIR="${1:-$(dirname "$0")/build}"

if [ ! -d "$BIN_DIR" ]; then
  echo -e "${RED}ERROR: Binary directory not found: $BIN_DIR${NC}"
  echo "  Build first with:  ./build_embench.sh"
  exit 1
fi

RESULTS_FILE="$BIN_DIR/results_$(date +%Y%m%d_%H%M%S).txt"

# ── Header ────────────────────────────────────────────────────────────────────
echo -e "${BOLD}${GREEN}"
echo "════════════════════════════════════════════════════════════════════════"
echo "  C-FLAT Embench-IOT  —  TEE Domain Switch Count Verification"
echo "  Reference: Table 2 of BLAST (CCS 2023)"
echo "════════════════════════════════════════════════════════════════════════"
echo -e "${NC}"

printf "%-20s %15s %15s %10s %s\n" "Benchmark" "Got" "Expected" "Delta%" "Status"
printf "%-20s %15s %15s %10s %s\n" "─────────────────────" "───────────────" "───────────────" "─────────" "──────"

MATCH=0; CLOSE=0; DIFFER=0; FAIL=0

{
  echo "C-FLAT Embench-IOT Domain-Switch Verification"
  echo "Date: $(date)"
  echo "Host: $(uname -n)"
  echo ""
  printf "%-20s %15s %15s %10s %s\n" "Benchmark" "Got" "Expected" "Delta%" "Status"
} > "$RESULTS_FILE"

run_benchmark() {
  local NAME="$1"
  local BIN="$BIN_DIR/cflat_${NAME}"
  local EXPECTED="${TABLE2_CFLAT[$NAME]}"

  if [ ! -f "$BIN" ]; then
    echo -e "${RED}  ✗ Binary not found: $BIN${NC}"
    printf "%-20s %15s %15s %10s %s\n" "$NAME" "MISSING" "$EXPECTED" "-" "NO_BINARY" >> "$RESULTS_FILE"
    FAIL=$((FAIL + 1))
    return
  fi

  echo -ne "${CYAN}Running cflat_${NAME}...${NC} "

  # Run the binary and capture output; timeout after 5 min
  local OUTPUT
  OUTPUT=$(timeout 300 "$BIN" 2>&1) || {
    echo -e "${RED}TIMEOUT/ERROR${NC}"
    printf "%-20s %15s %15s %10s %s\n" "$NAME" "ERROR" "$EXPECTED" "-" "RUN_FAIL" >> "$RESULTS_FILE"
    FAIL=$((FAIL + 1))
    return
  }

  # Extract world switch count from "World Switches: NNN"
  local GOT
  GOT=$(echo "$OUTPUT" | grep -oP 'World Switches: \K[0-9]+' || echo "")

  if [ -z "$GOT" ]; then
    echo -e "${RED}(no count found)${NC}"
    echo "--- Output ---"
    echo "$OUTPUT" | tail -20
    printf "%-20s %15s %15s %10s %s\n" "$NAME" "NO_COUNT" "$EXPECTED" "-" "PARSE_FAIL" >> "$RESULTS_FILE"
    FAIL=$((FAIL + 1))
    return
  fi

  # Compute percentage delta
  local DELTA PCT STATUS COLOR
  DELTA=$(( GOT - EXPECTED ))
  # Use awk for floating-point percentage
  PCT=$(awk -v got="$GOT" -v exp="$EXPECTED" 'BEGIN{
    if (exp == 0) { print "inf"; exit }
    d = (got - exp) / exp * 100
    printf "%.2f", d
  }')

  # Classify result
  local ABS_PCT
  ABS_PCT=$(awk -v p="$PCT" 'BEGIN{ if (p<0) p=-p; printf "%.2f", p }')

  if awk -v a="$ABS_PCT" 'BEGIN{ exit (a <= 0.01) ? 0 : 1 }'; then
    STATUS="EXACT"; COLOR="$GREEN"; MATCH=$((MATCH + 1))
  elif awk -v a="$ABS_PCT" 'BEGIN{ exit (a <= 1.0) ? 0 : 1 }'; then
    STATUS="CLOSE (<1%)"; COLOR="$YELLOW"; CLOSE=$((CLOSE + 1))
  else
    STATUS="DIFFER"; COLOR="$RED"; DIFFER=$((DIFFER + 1))
  fi

  echo -e "${COLOR}${STATUS}${NC}"
  printf "${COLOR}%-20s %15s %15s %9s%% %s${NC}\n" "$NAME" "$GOT" "$EXPECTED" "$PCT" "$STATUS"
  printf "%-20s %15s %15s %9s%% %s\n" "$NAME" "$GOT" "$EXPECTED" "$PCT" "$STATUS" >> "$RESULTS_FILE"
}

for NAME in $(echo "${!TABLE2_CFLAT[@]}" | tr ' ' '\n' | sort); do
  run_benchmark "$NAME"
done

# ── Summary ───────────────────────────────────────────────────────────────────
TOTAL=$((MATCH + CLOSE + DIFFER + FAIL))
echo ""
echo -e "${BOLD}${GREEN}════════════════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}Summary (${TOTAL} benchmarks)${NC}"
echo -e "  ${GREEN}Exact match  (≤0.01%): $MATCH${NC}"
echo -e "  ${YELLOW}Close match   (<1%):   $CLOSE${NC}"
echo -e "  ${RED}Different     (≥1%):   $DIFFER${NC}"
echo -e "  ${RED}Failed/missing:        $FAIL${NC}"
echo ""
echo "  Full results: $RESULTS_FILE"
echo ""

{
  echo ""
  echo "Summary:"
  echo "  Exact match  (≤0.01%): $MATCH"
  echo "  Close match   (<1%):   $CLOSE"
  echo "  Different     (≥1%):   $DIFFER"
  echo "  Failed/missing:        $FAIL"
} >> "$RESULTS_FILE"
