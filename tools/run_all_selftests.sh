#!/usr/bin/env bash
# tools/run_all_selftests.sh — run the entire --selftest table; optionally prove every tooth bites.
#
# Replaces the hand-rolled loop every agent rewrote (and zsh's no-word-split bit twice in 批次8).
# Asks the BINARY for its selftest names (--selftest-list) so the list can never drift from the
# code — covers kTable AND the self-registered image-filter ops (ImageFilterOp registrars, whose
# names are NOT in selftests.cpp source). Falls back to grepping kTable if --selftest-list is
# unsupported (older binary).
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

# Source-level golden lint (P1 vacuous-bite polarity — docs/agent/GOLDEN_STANDARD.md). Runs on every
# sweep so the anti-patterns from the 2026-07-03 oracle audit cannot silently return.
if ! "$ROOT/tools/golden_lint.sh"; then
  echo "[selftest-all] golden_lint FAILED (fix the listed goldens before trusting --bite)" >&2
  exit 2
fi

# Prefer the binary's own list (covers self-registered image-filter ops). Fall back to the source
# grep if the binary predates --selftest-list.
names=$("$BIN" --selftest-list 2>/dev/null)
if [ -z "$names" ]; then
  SRC="$ROOT/app/src/selftests.cpp"
  names=$(grep -oE '^\s*\{"[a-z0-9-]*"' "$SRC" | tr -d ' {"')
fi

pass=0; failed=(); nobite=()
while IFS= read -r n; do
  flag="--selftest"; [ -n "$n" ] && flag="--selftest-$n"
  if "$BIN" "$flag" >/dev/null 2>&1; then pass=$((pass+1)); else failed+=("${n:-base}"); fi
  if [ "$BITE" = 1 ]; then
    # a -bug variant that exits 0 is a tooth that cannot bite = a blind eye. Second layer: a variant
    # that PRINTS a dead-tooth diagnostic but still exits non-zero (P1 vacuous polarity) is caught by
    # grepping its output — belt over the source lint's suspenders.
    bugout=$("$BIN" "$flag-bug" 2>&1); bugrc=$?
    if [ $bugrc -eq 0 ]; then
      nobite+=("${n:-base}")
    elif printf '%s' "$bugout" | grep -qiE 'did not trip|tooth has no bite|tripped no tooth'; then
      nobite+=("${n:-base}(vacuous-exit)")
    fi
  fi
done <<< "$names"

# non-uniform entry (no -bug variant)
if "$BIN" --selftest-dispatch >/dev/null 2>&1; then pass=$((pass+1)); else failed+=(dispatch); fi

echo "[selftest-all] PASS=$pass FAILED:[${failed[*]:-}]"
[ "$BITE" = 1 ] && echo "[selftest-all] NO-BITE:[${nobite[*]:-}] (every name here is a tooth that can't fail)"
[ ${#failed[@]} -eq 0 ] && { [ "$BITE" = 0 ] || [ ${#nobite[@]} -eq 0 ]; }
