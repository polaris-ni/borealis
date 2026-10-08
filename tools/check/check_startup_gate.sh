#!/usr/bin/env bash
# ============================================================
# Startup time gate for SPEC.NF.PERF.03 (cold start <= 1.5 s)
# ------------------------------------------------------------
# Runs the startup_time benchmark and judges it against the 1500 ms threshold.
# This is NOT in CTest: startup time depends on machine load, disk speed and
# background processes.
# ============================================================

set -euo pipefail

BUILD_DIR="${1:-build-bench}"
JSON_FILE="${2:-}"
EXE="${3:-}"

# Find the executable
if [ -z "$EXE" ]; then
    EXE="$BUILD_DIR/tools/startup_time"
fi

# Run or read JSON
if [ -n "$JSON_FILE" ] && [ -f "$JSON_FILE" ]; then
    REPORT="$JSON_FILE"
elif [ -f "$EXE" ]; then
    echo "Running startup_time benchmark..."
    mkdir -p "$BUILD_DIR"
    "$EXE" --json="$BUILD_DIR/startup_report.json" >/dev/null 2>&1
    REPORT="$BUILD_DIR/startup_report.json"
else
    echo "ERROR: startup_time executable not found: $EXE" >&2
    echo "Hint: build with cmake --build build --target startup_time" >&2
    exit 1
fi

if [ ! -f "$REPORT" ]; then
    echo "ERROR: Report file not found: $REPORT" >&2
    exit 1
fi

# Parse JSON (requires jq)
if ! command -v jq &>/dev/null; then
    echo "ERROR: jq is required but not installed" >&2
    exit 1
fi

TOTAL_STARTUP=$(jq '.durations_ms.total_startup' "$REPORT")
FONT_LOADING=$(jq '.durations_ms.font_loading' "$REPORT")
FRAMEWORK_INIT=$(jq '.durations_ms.framework_init' "$REPORT")
FIRST_FRAME=$(jq '.durations_ms.first_frame' "$REPORT")
PASSED=$(jq -r '.passed' "$REPORT")
SPEC=$(jq -r '.spec' "$REPORT")

if [ "$SPEC" != "SPEC.NF.PERF.03" ]; then
    echo "ERROR: report is for $SPEC, expected SPEC.NF.PERF.03" >&2
    exit 1
fi
if ! [[ "$TOTAL_STARTUP" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "ERROR: report lacks numeric durations_ms.total_startup (stale or wrong-schema bench binary?)" >&2
    exit 1
fi

THRESHOLD_MS=1500.0

echo ""
echo "=== Startup Time Gate (SPEC.NF.PERF.03) ==="
echo ""
echo "Phase Breakdown:"
printf "  Font loading:      %8.3f ms\n" "$FONT_LOADING"
printf "  Framework init:    %8.3f ms\n" "$FRAMEWORK_INIT"
printf "  First frame:       %8.3f ms\n" "$FIRST_FRAME"
echo ""

# Gate judgment
if [ "$PASSED" = "true" ]; then
    GATE_STATUS="PASS"
    EXIT_CODE=0
else
    GATE_STATUS="FAIL"
    EXIT_CODE=1
fi

printf "%-6s %-20s %8.3f ms <= %-8s  %s\n" "$GATE_STATUS" "total_startup" "$TOTAL_STARTUP" "$GATE_STATUS" "Threshold: ${THRESHOLD_MS} ms"
echo ""

if [ "$EXIT_CODE" -eq 0 ]; then
    printf "RESULT: PASS (startup time %.3f ms <= %.1f ms)\n" "$TOTAL_STARTUP" "$THRESHOLD_MS"
else
    printf "RESULT: FAIL (startup time %.3f ms > %.1f ms)\n" "$TOTAL_STARTUP" "$THRESHOLD_MS"
fi

exit $EXIT_CODE
