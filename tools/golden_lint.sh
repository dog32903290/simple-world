#!/usr/bin/env bash
# tools/golden_lint.sh — source-level lint for golden anti-patterns (docs/agent/GOLDEN_STANDARD.md).
#
# Born from the 2026-07-03 oracle audit (docs/agent/GOLDEN_ORACLE_AUDIT.md): ~25 goldens printed
# "injectBug did not trip" and then returned 1 anyway — the --bite gate (exit-code only) could
# never see a dead tooth. This lint machine-checks the greppable slice of that rot (P1).
#
#   tools/golden_lint.sh            # exit 0 = clean, exit 1 = violations listed
#
# Wired into tools/run_all_selftests.sh so every selftest sweep re-checks it (gate-or-it-rots).

set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0

# P1 — vacuous bite polarity: a "did not trip / no bite" diagnostic whose branch returns 1.
# A dead tooth MUST exit 0 so run_all_selftests.sh --bite lands it on the NO-BITE list.
# Window logic: after the diagnostic, the FIRST return decides — `return 0` = correct polarity,
# `return 1` = vacuous (the RED path's later `return 1` is fine and must not false-positive).
for f in "$ROOT"/app/src/*_golden.cpp; do
  if awk '
    { line = tolower($0) }
    w > 0 {
      if ($0 ~ /return 0;/)      { w = 0 }
      else if ($0 ~ /return 1;/) { bad = 1; w = 0 }
      else w--
    }
    line ~ /did not trip|tooth has no bite|tripped no tooth/ { w = 5 }
    END { exit bad ? 1 : 0 }
  ' "$f"; then :; else
    echo "[golden-lint] P1 vacuous-bite: ${f#$ROOT/} — did-not-trip branch must 'return 0' (NO-BITE list catches it)"
    fail=1
  fi
done

exit $fail
