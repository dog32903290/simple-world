#!/usr/bin/env bash
# tools/default_value_parity.sh — DEFAULT-VALUE parity gate (值閘), sibling to nodespec_integrity.sh.
#
# WHY: nodespec_integrity.sh守 param COUNT parity (缺不缺參數). It does NOT守 the参數的 default VALUE.
# A node can have every TiXL [Input] present yet ship a WRONG inspector default — silent divergence from
# TiXL (e.g. GridPoints CountZ = 1 vs TiXL 10; TransformPoints Space = Point vs TiXL Object). This gate
# closes that gap with the SAME dual-source diff shape:
#
#   sw side  : app/build/simple_world --dump-nodespec <Type>  → the DEF\t… lines (PortSpec.def/strDef,
#              straight from the live registry — no C++ aggregate-init parsing).
#   TiXL side: <Type>.t3  → Inputs[].DefaultValue (the TiXL SSOT for defaults; NOT in the .cs). The .t3
#              is comment-stripped then JSON-parsed; GUID→param-name comes from the inline /*Name*/.
#
# Join key = node + param name. Normalization (vector-expand / enum-name→index / bool→1.0-0.0 /
# type-zero fallback) lives in tools/default_value_parity.py. Output: one MISMATCH line per drifting
# default: <Node>\t<Param>\t<sw>\t<TiXL>\t<kind>.
#
# Usage:
#   tools/default_value_parity.sh <NodeType>      # one node
#   tools/default_value_parity.sh --all           # every registered sw type that has a matching .t3
#   tools/default_value_parity.sh --all --out <f>  # …and write the mismatch list to <f>
#
# Read-only on external/tixl. Requires a built app/build/simple_world.
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SW_BIN:-$REPO_ROOT/app/build/simple_world}"
TIXL_LIB="${SW_TIXL_LIB:-$REPO_ROOT/external/tixl/Operators/Lib}"
PY="$REPO_ROOT/tools/default_value_parity.py"

if [ ! -x "$BIN" ]; then
  echo "✗ default_value_parity: binary not found/executable: $BIN" >&2
  echo "  build first: cmake --build app/build -j8" >&2
  exit 3
fi
if [ ! -d "$TIXL_LIB" ]; then
  echo "✗ default_value_parity: TiXL Lib not found: $TIXL_LIB (set SW_TIXL_LIB)" >&2
  exit 3
fi

# t3_path <Type> — resolve <Type>.t3 under Lib, excluding _obsolete / leading-underscore helper dirs.
# Echoes the path or "" if none (→ node absent on the TiXL side, skipped from the sweep).
t3_path() {
  find "$TIXL_LIB" -name "$1.t3" 2>/dev/null | grep -vE '_obsolete|/_' | head -1
}

# compare_one <Type> — emit mismatch lines to stdout, a TALLY line to stderr. Returns 0 if the node has
# a .t3 (compared), 1 if no .t3 (skipped, not an error).
compare_one() {
  local t="$1" t3
  t3="$(t3_path "$t")"
  if [ -z "$t3" ]; then
    echo "SKIP	$t	(no .t3 under Lib)" >&2
    return 1
  fi
  "$BIN" --dump-nodespec "$t" 2>/dev/null | python3 "$PY" "$t" "$t3"
}

OUT=""
MODE=""
ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --all) MODE="all"; shift ;;
    *) ARG="$1"; shift ;;
  esac
done

if [ "$MODE" != "all" ] && [ -z "$ARG" ]; then
  echo "usage: $0 <NodeType> | --all [--out <file>]" >&2
  exit 2
fi

TMP="$(mktemp -t defval_parity.XXXXXX)"
TALLY="$(mktemp -t defval_tally.XXXXXX)"

if [ "$MODE" = "all" ]; then
  TYPES="$("$BIN" --dump-nodespec-types 2>/dev/null | sort -u)"
  compared=0; skipped=0
  while IFS= read -r t; do
    [ -z "$t" ] && continue
    if compare_one "$t" >>"$TMP" 2>>"$TALLY"; then
      compared=$((compared+1))
    else
      skipped=$((skipped+1))
    fi
  done <<< "$TYPES"
else
  if compare_one "$ARG" >>"$TMP" 2>>"$TALLY"; then :; fi
fi

MISMATCHES="$(grep -c '' "$TMP" 2>/dev/null || echo 0)"
NODES_WITH_DRIFT="$(cut -f1 "$TMP" | sort -u | grep -c '' 2>/dev/null || echo 0)"
TIXL_ONLY_TOTAL="$(awk -F'\t' '/^TALLY/{s+=$4} END{print s+0}' "$TALLY")"

# Header + mismatch table.
{
  echo "# default_value_parity — drift list (node\tparam\tsw\tTiXL\tkind)"
  echo "# generated $(date -u +%Y-%m-%dT%H:%M:%SZ) by tools/default_value_parity.sh"
  if [ "$MODE" = "all" ]; then
    echo "# swept: compared=$compared  skipped(no .t3)=$skipped"
  fi
  echo "# TOTAL default-value mismatches: $MISMATCHES  across $NODES_WITH_DRIFT node(s)"
  echo "# kind distribution:"
  awk -F'\t' 'NF>=5{c[$5]++} END{for(k in c) printf "#   %-24s %d\n", k, c[k]}' "$TMP" | sort
  echo ""
  sort "$TMP"
} > "${OUT:-/dev/stdout}"

if [ -n "$OUT" ]; then
  echo "[default_value_parity] wrote $MISMATCHES mismatch line(s) across $NODES_WITH_DRIFT node(s) → $OUT"
  if [ "$MODE" = "all" ]; then echo "[default_value_parity] compared=$compared  skipped(no .t3)=$skipped"; fi
fi

rm -f "$TMP" "$TALLY"
# Gate semantics: exit 1 when ANY drift exists (so CI/sweep can fail loud). A caller producing the
# census just wants the list — it can ignore the exit code.
[ "$MISMATCHES" -eq 0 ]
