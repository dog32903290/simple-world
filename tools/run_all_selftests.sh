#!/usr/bin/env bash
# tools/run_all_selftests.sh — run the entire --selftest table; optionally prove every tooth bites.
#
# Replaces the hand-rolled loop every agent rewrote (and zsh's no-word-split bit twice in 批次8).
# Parses kTable out of src/selftests.cpp so the list can never drift from the code.
#
#   tools/run_all_selftests.sh                # green sweep: every --selftest[-name] must exit 0
#   tools/run_all_selftests.sh --bite         # ALSO run every -bug variant: each must exit NON-zero
#   tools/run_all_selftests.sh --bin <path>   # explicit binary (default: app/build/simple_world)
#
# Exit 0 only if everything passed (and, with --bite, everything bit).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/app/build/simple_world"
BITE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --bite) BITE=1 ;;
    --bin)  shift; BIN="$1" ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done
[ -x "$BIN" ] || { echo "[selftest-all] no binary at $BIN (build first)" >&2; exit 2; }

SRC="$ROOT/app/src/selftests.cpp"
names=$(grep -oE '^\s*\{"[a-z0-9-]*"' "$SRC" | tr -d ' {"')

pass=0; failed=(); nobite=()
while IFS= read -r n; do
  flag="--selftest"; [ -n "$n" ] && flag="--selftest-$n"
  if "$BIN" "$flag" >/dev/null 2>&1; then pass=$((pass+1)); else failed+=("${n:-base}"); fi
  if [ "$BITE" = 1 ]; then
    # a -bug variant that exits 0 is a tooth that cannot bite = a blind eye
    if "$BIN" "$flag-bug" >/dev/null 2>&1; then nobite+=("${n:-base}"); fi
  fi
done <<< "$names"

# non-uniform entry (no -bug variant)
if "$BIN" --selftest-dispatch >/dev/null 2>&1; then pass=$((pass+1)); else failed+=(dispatch); fi

echo "[selftest-all] PASS=$pass FAILED:[${failed[*]:-}]"
[ "$BITE" = 1 ] && echo "[selftest-all] NO-BITE:[${nobite[*]:-}] (every name here is a tooth that can't fail)"
[ ${#failed[@]} -eq 0 ] && { [ "$BITE" = 0 ] || [ ${#nobite[@]} -eq 0 ]; }
