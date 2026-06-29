#!/usr/bin/env bash
# tools/nodespec_integrity.sh — param-completion INTEGRITY GATE for point generators.
#
# WHY: the param-completion fan-out exposes every TiXL [Input] of a generator as a real inspector
# knob. The failure mode is SILENT: miss one baked param and the node looks done but diverges from
# TiXL. This gate makes the omission LOUD by comparing two independently-derived counts:
#
#   sw side  : app/build/simple_world --dump-nodespec <Type>  →  FOLDED_LOGICAL_COUNT
#              (a VectorN param folds to 1; the sw output port + grid-capacity "Count" are excluded —
#               see dumpNodeSpec in app/src/selftests.cpp for the exact rule.)
#   TiXL side: grep the node's .cs for `[Input(` on non-comment lines (each InputSlot<VectorN> is
#              ONE [Input] line → already folded, no expansion needed).
#
# PASS when sw == TiXL. FAIL (nonzero) with a diff when they differ — a mismatch means either a
# missing sw param (sw < TiXL) or an extra/forked one (sw > TiXL); the implementer reads the dump +
# the .cs to decide which inputs to add (or to record as a named FORK, e.g. PointsOnMesh's deferred
# IsEnabled graph-wrapper).
#
# Usage:
#   tools/nodespec_integrity.sh <NodeType>        # gate one generator
#   tools/nodespec_integrity.sh --all-generators  # gate the whole known generator set
#
# Read-only on external/tixl. Requires a built app/build/simple_world (run cmake --build first).
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SW_BIN:-$REPO_ROOT/app/build/simple_world}"
TIXL_GEN="$REPO_ROOT/external/tixl/Operators/Lib/point/generate"

if [ ! -x "$BIN" ]; then
  echo "✗ nodespec_integrity: binary not found/executable: $BIN" >&2
  echo "  build first: cmake --build app/build -j8" >&2
  exit 3
fi

# type → .cs basename map. Default is "<Type>.cs"; only the mismatches need an override here.
cs_for_type() {
  case "$1" in
    DoyleSpiralPoints) echo "DoyleSpiralPoints2.cs" ;;  # sw type vs TiXL .cs name
    *)                 echo "$1.cs" ;;
  esac
}

# The generator set the gate sweeps for --all-generators. = the sw NodeSpec generator types.
ALL_GENERATORS=(
  RadialPoints LinePoints GridPoints SpherePoints
  HexGridPoints DoyleSpiralPoints RepetitionPoints CommonPointSets
  BoundingBoxPoints MeshVerticesToPoints PointsOnMesh PointTrailFast PointTrail
)

# tixl_input_count <Type> — folded TiXL [Input] count (comment lines stripped so commented-out
# inputs like RepetitionPoints' Vector3 Scale don't inflate the count). Echoes the count, or "?"
# if the .cs is missing (so the caller can flag a missing authority file).
tixl_input_count() {
  local cs="$TIXL_GEN/$(cs_for_type "$1")"
  if [ ! -f "$cs" ]; then echo "?"; return; fi
  grep -vE '^[[:space:]]*//' "$cs" | grep -c '\[Input('
}

# sw_folded_count <Type> — the FOLDED_LOGICAL_COUNT line from --dump-nodespec, or "?" if unknown.
sw_folded_count() {
  local out
  out="$("$BIN" --dump-nodespec "$1" 2>/dev/null)"
  if [ -z "$out" ]; then echo "?"; return; fi
  echo "$out" | sed -n 's/^FOLDED_LOGICAL_COUNT: //p'
}

# gate_one <Type> — returns 0 if sw==TiXL, 1 otherwise. Prints a one-line verdict.
gate_one() {
  local t="$1"
  local sw tixl
  sw="$(sw_folded_count "$t")"
  tixl="$(tixl_input_count "$t")"
  if [ "$sw" = "?" ]; then
    printf '  ✗ %-22s sw=UNKNOWN (node type not registered?)  TiXL=%s\n' "$t" "$tixl"
    return 1
  fi
  if [ "$tixl" = "?" ]; then
    printf '  ✗ %-22s sw=%s  TiXL=MISSING .cs (%s)\n' "$t" "$sw" "$(cs_for_type "$t")"
    return 1
  fi
  if [ "$sw" = "$tixl" ]; then
    printf '  ✓ %-22s sw=%s == TiXL=%s\n' "$t" "$sw" "$tixl"
    return 0
  fi
  local delta=$(( sw - tixl ))
  local dir
  if [ "$delta" -lt 0 ]; then dir="sw MISSING $(( -delta )) param(s)"; else dir="sw has $delta EXTRA param(s)"; fi
  printf '  ✗ %-22s sw=%s != TiXL=%s  (%s)\n' "$t" "$sw" "$tixl" "$dir"
  return 1
}

rc=0
if [ "${1:-}" = "--all-generators" ]; then
  echo "[nodespec_integrity] sweeping ${#ALL_GENERATORS[@]} generators (sw folded vs TiXL [Input])..."
  for t in "${ALL_GENERATORS[@]}"; do
    gate_one "$t" || rc=1
  done
  if [ "$rc" -eq 0 ]; then echo "[nodespec_integrity] OK — every generator's param set matches TiXL."
  else echo "[nodespec_integrity] FAIL — one or more generators diverge from TiXL (see ✗ rows)."; fi
elif [ -n "${1:-}" ]; then
  gate_one "$1" || rc=1
else
  echo "usage: $0 <NodeType> | --all-generators" >&2
  exit 2
fi
exit "$rc"
