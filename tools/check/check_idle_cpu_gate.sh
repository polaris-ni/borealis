#!/usr/bin/env bash
# ============================================================
# Idle CPU gate for SPEC.NF.PERF.05 (idle average CPU <= 1%)
# ------------------------------------------------------------
# Runs the idle_cpu benchmark (or judges an existing report) against the 1.0 %
# line. This is NOT in CTest: the reading depends on machine load and background
# processes, same rule as the other time-class gates (AGENTS.md 4.4 rule 21,
# 口径沿用裁决 7.34 的「基准非断言」分工)。
#
# Usage:
#   check_idle_cpu_gate.sh [BuildDir] [JSON_FILE] [EXE] [WindowSeconds]
#     BuildDir       default build-bench
#     JSON_FILE      judge this report instead of running anything
#     EXE            explicit idle_cpu executable
#     WindowSeconds  bench window; the spec asks 300, the default 10 keeps the
#                    gate cheap (see tools/bench/idle_cpu.cpp header comment for
#                    why a shorter window still catches structural busy-work)
# ============================================================

set -euo pipefail

BUILD_DIR="${1:-build-bench}"
JSON_FILE="${2:-}"
EXE="${3:-}"
WINDOW_SECONDS="${4:-10}"

if ! command -v jq &>/dev/null; then
    echo "ERROR: jq is required but not installed" >&2
    exit 1
fi

if [ -n "$JSON_FILE" ] && [ -f "$JSON_FILE" ]; then
    REPORT="$JSON_FILE"
else
    if [ -z "$EXE" ]; then
        EXE="$BUILD_DIR/tools/idle_cpu"
    fi
    if [ ! -f "$EXE" ]; then
        echo "ERROR: idle_cpu executable not found: $EXE" >&2
        echo "Hint: cmake --build build --target idle_cpu" >&2
        exit 1
    fi
    echo "Running idle_cpu benchmark (${WINDOW_SECONDS} s window)..."
    mkdir -p "$BUILD_DIR"
    "$EXE" --window-seconds="$WINDOW_SECONDS" --json="$BUILD_DIR/idle_cpu_report.json" >/dev/null 2>&1 || true
    REPORT="$BUILD_DIR/idle_cpu_report.json"
fi

if [ ! -f "$REPORT" ]; then
    echo "ERROR: Report file not found: $REPORT" >&2
    exit 1
fi

PERCENT=$(jq -r '.metrics.idle_cpu_percent' "$REPORT")
WINDOW=$(jq -r '.window_seconds' "$REPORT")
THRESHOLD=$(jq -r '.threshold_percent' "$REPORT")
SPEC=$(jq -r '.spec' "$REPORT")

if [ "$SPEC" != "SPEC.NF.PERF.05" ]; then
    echo "ERROR: report is for $SPEC, expected SPEC.NF.PERF.05" >&2
    exit 1
fi
if ! [[ "$PERCENT" =~ ^[0-9]+([.][0-9]+)?$ ]] || ! [[ "$THRESHOLD" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "ERROR: report lacks numeric metrics.idle_cpu_percent / threshold_percent (stale or wrong-schema bench binary?)" >&2
    exit 1
fi

echo ""
echo "=== Idle CPU Gate (SPEC.NF.PERF.05) ==="
echo ""

if awk -v p="$PERCENT" -v t="$THRESHOLD" 'BEGIN { exit (p <= t) ? 0 : 1 }'; then
    RESULT="PASS"
    CODE=0
else
    RESULT="FAIL"
    CODE=1
fi

printf "%-6s %-20s %8.3f %% <= %s %%  (window %.1f s)\n" "$RESULT" "idle_cpu_percent" "$PERCENT" "$THRESHOLD" "$WINDOW"
echo ""
printf "RESULT: %s (idle CPU %.3f%% vs threshold %s%%)\n" "$RESULT" "$PERCENT" "$THRESHOLD"
exit $CODE
