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
# outside the gate until 2026-07-06) + the mathv fuzz TUs (selftests_mathv_*.cpp — same tooth
# contract, MATH_VERIFY_WORKFLOW.md §5).
for f in "$ROOT"/app/src/*_golden.cpp "$ROOT"/app/src/app/*_golden.cpp "$ROOT"/app/src/runtime/*_golden.cpp \
         "$ROOT"/app/src/selftests_mathv_*.cpp; do
  [ -e "$f" ] || continue
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

# ── P5-ORACLE (HARD) — provenance gate for CPU refs (mathv_ref_*.h + every *_oracle.h): the
# GOLDEN_STANDARD "P5-safe oracle 判準" conditions ① and ② are greppable, so they gate here
# (③ R≠D≠MSL-author is a工單 property, ④ refuter hand-derivation is semantic — both live in the
# checklist, not here). A ref that includes sw shader/Metal code — or that cannot state its TiXL
# transcription source in the file header — is a self-certifying oracle (P5) and blocks.
for f in "$ROOT"/app/src/mathv_ref_*.h "$ROOT"/app/src/*_oracle.h "$ROOT"/app/src/runtime/*_oracle.h; do
  [ -e "$f" ] || continue
  # (a) no #include/#import of shader or Metal code — the ref must be host-pure TiXL math.
  if grep -nEi '^[[:space:]]*#[[:space:]]*(include|import).*(shader|metal)' "$f" >/dev/null; then
    echo "[golden-lint] P5-oracle: ${f#$ROOT/} — includes sw shader/Metal code (ref must be a host-pure TiXL transcription)"
    fail=1
  fi
  # (b) no Metal API usage (a ref reaching for MTL::/NS:: is not a scalar oracle).
  if grep -nE 'MTL::|NS::' "$f" >/dev/null; then
    echo "[golden-lint] P5-oracle: ${f#$ROOT/} — uses Metal API symbols (MTL::/NS::)"
    fail=1
  fi
  # (c) header provenance: first 25 lines must carry 'TRANSCRIBED from' + 'external/tixl'.
  if ! head -25 "$f" | grep -q 'TRANSCRIBED from' || ! head -25 "$f" | grep -q 'external/tixl'; then
    echo "[golden-lint] P5-oracle: ${f#$ROOT/} — header missing 'TRANSCRIBED from' + 'external/tixl' provenance (GOLDEN_STANDARD P5-safe oracle 判準 ①)"
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

  # ── MATHV-EPS (SOFT, --audit only) — eps loosened beyond the class default WITHOUT a same-line
  # `measured` note. The class defaults are the EpsSpec ctor values (mathv_compare.h): atol=1e-6,
  # rtol=1e-5, rtolTight=1e-3, deltaBranch=1e-4, wideMul=10, exemptMax=0.01. Any TU assignment above
  # its default must carry `// measured: <derivation>` ON THE SAME LINE (calibration-note template:
  # turbulence_parity_golden.cpp:227-233). Report-only: a measured widening is legitimate physics.
  m=0
  for f in "$ROOT"/app/src/selftests_mathv_*.cpp; do
    [ -e "$f" ] || continue
    hits=$(grep -nE '\.(atol|rtolTight|rtol|deltaBranch|wideMul|exemptMax)[[:space:]]*=[[:space:]]*[0-9]' "$f" \
           | grep -v 'measured') || true
    [ -n "$hits" ] || continue
    while IFS= read -r h; do
      ln=${h%%:*}
      fv=$(echo "${h#*:}" | sed -nE 's/.*\.(atol|rtolTight|rtol|deltaBranch|wideMul|exemptMax)[[:space:]]*=[[:space:]]*([0-9][0-9.eE+-]*)f?.*/\1 \2/p' | head -1)
      [ -n "$fv" ] || continue
      field=${fv%% *}; val=${fv##* }
      case "$field" in
        atol) def=1e-6 ;; rtolTight) def=1e-3 ;; rtol) def=1e-5 ;;
        deltaBranch) def=1e-4 ;; wideMul) def=10 ;; exemptMax) def=0.01 ;;
        *) continue ;;
      esac
      if awk -v v="$val" -v d="$def" 'BEGIN { exit (v + 0 > d + 0) ? 0 : 1 }'; then
        echo "[golden-lint] MATHV-EPS SUSPECT: ${f#$ROOT/}:$ln — $field=$val exceeds class default $def with no same-line 'measured' note (refuter: demand the derivation)" >&2
        m=$((m+1))
      fi
    done <<< "$hits"
  done
  echo "[golden-lint] --audit: $m mathv eps-escalation suspect(s) for refuter review (report-only, does not gate)" >&2
fi

exit $fail
