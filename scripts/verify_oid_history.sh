#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/verify_oid_history.sh [repo_path] [ref_old] [ref_new]
# Defaults:
#   repo_path=../cadencesimple
#   ref_old=HEAD~1
#   ref_new=HEAD
#
# This script:
# - checks out two commits in the Cadence project,
# - compiles emit_instances and scan_connectivity if sources exist,
# - runs the extractors on sch.oa for each commit,
# - runs the correlator to produce resolved_netlist for each commit,
# - writes a short verification summary comparing candidate OIDs for matching instance names.
#
# Outputs (under aaic/):
#  - parsed_instances_<label>.json
#  - parsed_connectivity_<label>.json
#  - resolved_netlist_<label>.json
#  - resolved_netlist_<label>.md
#  - aaic/verify_oid_history_report.md

REPO_PATH="${1:-../cadencesimple}"
REF_OLD="${2:-HEAD~1}"
REF_NEW="${3:-HEAD}"

OUT_DIR="aaic"
mkdir -p "$OUT_DIR"

orig_ref="$(git -C "$REPO_PATH" rev-parse --abbrev-ref HEAD 2>/dev/null || git -C "$REPO_PATH" rev-parse HEAD)"
echo "Original ref in $REPO_PATH: $orig_ref"
echo "Verifying OID stability between $REF_OLD and $REF_NEW in repo $REPO_PATH"

# helper to build tools if sources present
build_tools() {
  echo "Building tools if sources exist..."
  if [ -f src/emit_instances.cpp ]; then
    echo "Compiling emit_instances"
    g++ -std=c++11 -O2 -o ./emit_instances src/emit_instances.cpp -Isrc || true
  fi
  if [ -f src/scan_connectivity.cpp ]; then
    echo "Compiling scan_connectivity"
    g++ -std=c++11 -O2 -o ./scan_connectivity src/scan_connectivity.cpp -Isrc || true
  fi
}

# run extractor on a given commit ref and label
run_for_ref() {
  local ref="$1"
  local label="$2"
  echo
  echo "=== Checking out $ref ==="
  git -C "$REPO_PATH" checkout --quiet "$ref"

  # locate schematic (try common path)
  SCH_PATH="$REPO_PATH/RC/simple/schematic/sch.oa"
  if [ ! -f "$SCH_PATH" ]; then
    echo "ERROR: schematic not found at $SCH_PATH"
    exit 1
  fi

  # (re)build tools from workspace sources
  build_tools

  INST_OUT="$OUT_DIR/parsed_instances_${label}.json"
  CONN_OUT="$OUT_DIR/parsed_connectivity_${label}.json"
  RES_JSON="$OUT_DIR/resolved_netlist_${label}.json"
  RES_MD="$OUT_DIR/resolved_netlist_${label}.md"

  # emit instances: prefer compiled emit_instances, fallback to existing parsed file if present
  if [ -x ./emit_instances ]; then
    echo "Running ./emit_instances $SCH_PATH -> $INST_OUT"
    ./emit_instances "$SCH_PATH" > "$INST_OUT" || echo "emit_instances returned non-zero"
  elif [ -f aaic/parsed_instances.json ]; then
    echo "emit_instances binary not found, copying aaic/parsed_instances.json -> $INST_OUT"
    cp aaic/parsed_instances.json "$INST_OUT"
  else
    echo "No emit_instances binary and no aaic/parsed_instances.json fallback; skipping"
    echo "{}" > "$INST_OUT"
  fi

  # run connectivity scanner
  if [ -x ./scan_connectivity ]; then
    echo "Running ./scan_connectivity $SCH_PATH -> $CONN_OUT"
    ./scan_connectivity "$SCH_PATH" > "$CONN_OUT" || echo "scan_connectivity returned non-zero"
  elif [ -f aaic/parsed_connectivity.json ]; then
    echo "scan_connectivity binary not found, copying aaic/parsed_connectivity.json -> $CONN_OUT"
    cp aaic/parsed_connectivity.json "$CONN_OUT"
  else
    echo "No scan_connectivity binary and no aaic/parsed_connectivity.json fallback; skipping"
    echo '{"connectivity_candidates": []}' > "$CONN_OUT"
  fi

  # correlate (use script)
  if [ -f scripts/correlate_oids.py ]; then
    echo "Running correlator for label $label"
    python3 scripts/correlate_oids.py --instances "$INST_OUT" --connectivity "$CONN_OUT" --out-json "$RES_JSON" --out-md "$RES_MD" || echo "correlator returned non-zero"
  else
    echo "correlator script missing; creating empty $RES_JSON"
    echo '{"resolved": []}' > "$RES_JSON"
    echo "# empty resolved for $label" > "$RES_MD"
  fi

  echo "Finished run for $label"
}

# ensure we restore original ref on exit
cleanup() {
  echo "Restoring original ref $orig_ref in $REPO_PATH"
  git -C "$REPO_PATH" checkout --quiet "$orig_ref" || true
}
trap cleanup EXIT

# run for both refs
run_for_ref "$REF_OLD" "old"
run_for_ref "$REF_NEW" "new"

# produce quick diff summary: list nets and OIDs for both and compare
REPORT="$OUT_DIR/verify_oid_history_report.md"
cat > "$REPORT" <<EOF
# OID history verification report

Comparing extracted/resolved netlists between refs: $REF_OLD (old) and $REF_NEW (new).

Files produced:
- $OUT_DIR/parsed_instances_old.json
- $OUT_DIR/parsed_connectivity_old.json
- $OUT_DIR/resolved_netlist_old.json
- $OUT_DIR/resolved_netlist_old.md

- $OUT_DIR/parsed_instances_new.json
- $OUT_DIR/parsed_connectivity_new.json
- $OUT_DIR/resolved_netlist_new.json
- $OUT_DIR/resolved_netlist_new.md

## Quick check: nets containing 'R0' or likely renamed components

EOF

# extract candidate lines for human inspection
jq -r '.resolved[] | select(.net_name != null) | "\(.net_name) \(.file_offset) \(.oids // [])"' "$OUT_DIR/resolved_netlist_old.json" 2>/dev/null | sed 's/^/OLD: /' >> "$REPORT" || true
jq -r '.resolved[] | select(.net_name != null) | "\(.net_name) \(.file_offset) \(.oids // [])"' "$OUT_DIR/resolved_netlist_new.json" 2>/dev/null | sed 's/^/NEW: /' >> "$REPORT" || true

echo
echo "Verification run complete. See $REPORT and the per-ref resolved outputs for details."