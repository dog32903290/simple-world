#!/usr/bin/env bash
# tools/golden_lint.sh — source-level lint for golden anti-patterns (docs/agent/GOLDEN_STANDARD.md).
#
# Born from the 2026-07-03 oracle audit (docs/agent/GOLDEN_ORACLE_AUDIT.md). Two layers:
#
#   tools/golden_lint.sh          # HARD gate: P1 vacuous-bite only. exit 0 = clean, exit 1 = block.
#   tools/golden_lint.sh --audit  # + P3 want-flip SUSPECTS (report-only, always exit per P1). This is
#                                 #   the refuter's attack list for NEW goldens — machine pre-screen,
#                                 #   human (refuter) verdict. Emitted to stderr so it never gates build.
#
# P1 is greppable → hard gate (wired into run_all_selftests.sh, every sweep). P2/P4/P5 are semantic
# (identity-point sampling / no-oracle / self-referential) — NOT greppable, they live in the refuter
# checklist in GOLDEN_STANDARD.md, not here. P3 (want-flip) sits between: greppable enough to SUSPECT,
# too false-positive-prone to gate (the 2026-07-03 turbulence escape was found by exactly this screen).

set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AUDIT=0
[ "${1:-}" = "--audit" ] && AUDIT=1
fail=0

# ── P1 (HARD) — vacuous bite polarity: a "did not trip / no bite" diagnostic whose branch returns 1.
# A dead tooth MUST exit 0 so run_all_selftests.sh --bite lands it on the NO-BITE list.
# Window logic: after the diagnostic, the FIRST return decides — `return 0` (or a ternary whose
# no-bite arm is 0, e.g. `return bites ? 1 : 0;`) = correct polarity; `return 1` or a ternary whose
# no-bite arm is nonzero (`? 1 : 2` — the 2026-07-06 t3import family escape: exit 2 read as "bite"
# by --bite, dead tooth invisible) = vacuous (the RED path's later `return 1` must not false-positive).
# Glob covers ALL golden homes: app/src/ + app/src/app/ + app/src/runtime/ (runtime/ held 40 goldens
# outside the gate until 2026-07-06).
for f in "$ROOT"/app/src/*_golden.cpp "$ROOT"/app/src/app/*_golden.cpp "$ROOT"/app/src/runtime/*_golden.cpp; do
  if awk '
    { line = tolower($0) }
    w > 0 {
      if ($0 ~ /return 0;/ || $0 ~ /\? 1 : 0;/)                 { w = 0 }
      else if ($0 ~ /return 1;/ || $0 ~ /\? 1 : [1-9][0-9]*;/)  { bad = 1; w = 0 }
      else w--
    }
    line ~ /did not trip|tooth has no bite|tooth cannot bite|tripped no tooth|toothless|seam is hollow/ { w = 5 }
    END { exit bad ? 1 : 0 }
  ' "$f"; then :; else
    echo "[golden-lint] P1 vacuous-bite: ${f#$ROOT/} — did-not-trip branch must 'return 0' (NO-BITE list catches it)"
    fail=1
  fi
done

# ── P3 (SOFT, --audit only) — want-flip suspects: an EXPECTED/pass value assigned from `injectBug ?`.
# A real tooth corrupts the cook and keeps the want FIXED; if the WANT itself flips, the assert only
# proves "these two numbers differ", not "the assert is bound to the real cook path". Report-only:
# legit path-branch asserts (severed field → different physical claim) also match, so a human decides.
if [ "$AUDIT" = 1 ]; then
  n=0
  for f in "$ROOT"/app/src/*_golden.cpp "$ROOT"/app/src/app/*_golden.cpp "$ROOT"/app/src/runtime/*_golden.cpp; do
    # assignment to an expectation-ish var, from injectBug ?, not inside a printf string (no `"`).
    hits=$(grep -nE '(expected|Expected|want|Want|dExpected|pass|ref[A-Za-z]*)[^"]*=[^"]*injectBug[[:space:]]*\?' "$f" \
           | grep -vE 'printf|"') || true
    if [ -n "$hits" ]; then
      while IFS= read -r h; do
        echo "[golden-lint] P3 want-flip SUSPECT: ${f#$ROOT/}:${h%%:*} — expectation assigned from injectBug? (refuter: real flip or legit path-branch?)" >&2
        n=$((n+1))
      done <<< "$hits"
    fi
  done
  echo "[golden-lint] --audit: $n P3 want-flip suspect(s) for refuter review (report-only, does not gate)" >&2
fi

exit $fail
