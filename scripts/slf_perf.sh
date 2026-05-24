#!/usr/bin/env bash
# Run the SLF probe matrix under perf stat and append all output to one file.
#
# Usage:
#   ./scripts/quiet-run.sh ./scripts/slf_perf.sh [output_file]
#
# Defaults output to tmp/slf_perf.txt (relative to repo root). Truncates
# the file at the start of each run, so re-runs replace prior results.

set -euo pipefail

cd "$(dirname "$0")/.."

out="${1:-tmp/slf_perf.txt}"
mkdir -p "$(dirname "$out")"
: > "$out"

events="cycles,instructions,ld_blocks.store_forward,machine_clears.memory_ordering"

run() {
    local mode="$1" dis="$2" n="$3"
    {
        echo "=== mode=$mode dis=$dis n=$n ==="
        perf stat -e "$events" -- ./bench/build/probe "$mode" "$dis" "$n"
        echo
    } >> "$out" 2>&1
}

# Pathological band: predict huge ld_blocks.store_forward for naive,
# near-zero for cyccpy.
run naive  33 1048576
run cyccpy 33 1048576

# Perfect-overlap band: predict low SLF for both (naive load aligns
# exactly with a prior store, so forwarding succeeds).
run naive  32 1048576
run cyccpy 32 1048576

# Past-the-window band: predict low SLF for both (store has retired
# by the time the matching load issues).
run naive  200 1048576
run cyccpy 200 1048576

echo "Wrote results to $out"
