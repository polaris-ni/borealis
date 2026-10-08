#!/usr/bin/env bash
# ============================================================
# Memory gate for SPEC.NF.PERF.04 (single-session RSS <= 150 MB)
# ------------------------------------------------------------
# Runs the memory_usage benchmark (or judges an existing report) against the two
# lines recorded in its JSON: loaded <= 150 MB (the requirement's own number) and
# idle <= 50 MB (a regression guard, not a spec-named line -- see the bench header).
# NOT in CTest: RSS readings move with machine load and allocator state.
#
# Usage:
#   check_memory_gate.sh [BuildDir] [JSON_FILE] [EXE]
# ============================================================

set -euo pipefail

BUILD_DIR="${1:-build-bench}"
JSON_FILE="${2:-}"
EXE="${3:-}"

if ! command -v jq &>/dev/null; then
    echo "ERROR: jq is required but not installed" >&2
    exit 1
fi

if [ -n "$JSON_FILE" ] && [ -f "$JSON_FILE" ]; then
    REPORT="$JSON_FILE"
else
    if [ -z "$EXE" ]; then
        EXE="$BUILD_DIR/tools/memory_usage"
    fi
    if [ ! -f "$EXE" ]; then
        echo "ERROR: memory_usage executable not found: $EXE" >&2
        echo "Hint: cmake --build build --target memory_usage" >&2
        exit 1
    fi
    echo "Running memory_usage benchmark..."
    mkdir -p "$BUILD_DIR"
    "$EXE" --json="$BUILD_DIR/memory_report.json" >/dev/null 2>&1 || true
    REPORT="$BUILD_DIR/memory_report.json"
fi

if [ ! -f "$REPORT" ]; then
    echo "ERROR: Report file not found: $REPORT" >&2
    exit 1
fi

SPEC=$(jq -r '.spec' "$REPORT")
IDLE=$(jq -r '.measurements.idle_rss_mb' "$REPORT")
LOADED=$(jq -r '.measurements.loaded_rss_mb' "$REPORT")
IDLE_MAX=$(jq -r '.thresholds.idle_max_mb' "$REPORT")
LOADED_MAX=$(jq -r '.thresholds.loaded_max_mb' "$REPORT")

if [ "$SPEC" != "SPEC.NF.PERF.04" ]; then
    echo "ERROR: report is for $SPEC, expected SPEC.NF.PERF.04" >&2
    exit 1
fi
for v in "$IDLE" "$LOADED" "$IDLE_MAX" "$LOADED_MAX"; do
    if ! [[ "$v" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        echo "ERROR: report lacks numeric measurements/thresholds (stale or wrong-schema bench binary?)" >&2
        exit 1
    fi
done

echo ""
echo "=== Memory Gate (SPEC.NF.PERF.04) ==="
echo ""

CODE=0
if awk -v p="$IDLE" -v t="$IDLE_MAX" 'BEGIN { exit (p <= t) ? 0 : 1 }'; then
    printf "%-6s %-20s %8.3f MB <= %s MB  (regression guard)\n" "PASS" "idle_rss_mb" "$IDLE" "$IDLE_MAX"
else
    printf "%-6s %-20s %8.3f MB <= %s MB\n" "FAIL" "idle_rss_mb" "$IDLE" "$IDLE_MAX"
    CODE=1
fi
if awk -v p="$LOADED" -v t="$LOADED_MAX" 'BEGIN { exit (p <= t) ? 0 : 1 }'; then
    printf "%-6s %-20s %8.3f MB <= %s MB  (spec line)\n" "PASS" "loaded_rss_mb" "$LOADED" "$LOADED_MAX"
else
    printf "%-6s %-20s %8.3f MB <= %s MB\n" "FAIL" "loaded_rss_mb" "$LOADED" "$LOADED_MAX"
    CODE=1
fi

echo ""
if [ "$CODE" -eq 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
fi
exit $CODE
