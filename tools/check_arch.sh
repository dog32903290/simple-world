#!/usr/bin/env bash
# tools/check_arch.sh — architecture tripwire (ARCHITECTURE.md rules 2 + 4).
#
# Pass 1 (dep-direction): greps every zone's source for #includes that cross a forbidden zone
# boundary and fails on the first offence — so a leaf->leaf or leaf->up dependency is caught
# the moment it's written, not in a human review weeks later.
#
# Pass 2 (line-count ratchet, ARCHITECTURE.md rule 4, "單檔 > ~400 行 = 警訊"):
#   - Scopes: all .cpp/.h/.mm under app/src/ except third_party/ and app/src/main.cpp.
#   - Files listed in tools/linecount-grandfather.txt have a recorded CAP (only-decrease ratchet).
#     Exceeding the cap → FAIL. Shrinking is always OK (update the cap entry when a file is split).
#   - Files NOT listed in the grandfather must be ≤ 400 lines or → FAIL.
#   - New files must be ≤ 400 lines (ARCHITECTURE.md rule 4).
#
# Run from the repo root:  tools/check_arch.sh        (or: cmake --build app/build --target check-arch)
#
# The law (ARCHITECTURE.md):
#   ui -> app -> runtime ; app -> platform ; main = the shell, depends on all.
#   Leaves (runtime / platform / verify): depend on NEITHER each other NOR anything above.
#   ui must reach platform only THROUGH app (no direct ui -> platform).
#   No app <-> ui cycle (app must not include ui).
set -uo pipefail

SRC="${1:-app/src}"
GRANDFATHER="${GRANDFATHER:-tools/linecount-grandfather.txt}"
fail=0

# ---------------------------------------------------------------------------
# Pass 1: dependency-direction check
# ---------------------------------------------------------------------------

# check_zone <zone> <forbidden-zone>...
# Every file under app/src/<zone> may NOT '#include "<forbidden>/..."'.
check_zone() {
  zone="$1"; shift
  while IFS= read -r f; do
    for bad in "$@"; do
      if grep -Eq "^[[:space:]]*#include[[:space:]]+\"$bad/" "$f"; then
        line=$(grep -nE "^[[:space:]]*#include[[:space:]]+\"$bad/" "$f" | head -1)
        echo "  ✗ $zone leaf/layer violation: $f  ->  $bad/   (${line%%:*})"
        fail=1
      fi
    done
  done < <(find "$SRC/$zone" -type f \( -name '*.cpp' -o -name '*.mm' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null)
}

echo "[check-arch] verifying single-direction zone dependencies (ARCHITECTURE.md)..."

# Leaves: only their own zone + std/system. Never each other, never up.
check_zone runtime  app ui platform verify
check_zone platform app ui runtime  verify
check_zone verify   app ui runtime  platform
# ui reaches platform only via app (ui->app->platform). ui->app / ui->runtime / ui->verify(hook) are fine.
check_zone ui       platform
# No app<->ui cycle. app->runtime / app->platform / app->verify are fine.
check_zone app      ui

if [ "$fail" -eq 0 ]; then
  echo "[check-arch] OK — every zone respects the dependency direction."
else
  echo "[check-arch] FAIL — break the include above. If a leaf genuinely needs another leaf's"
  echo "             work, invert it: expose a callback/data seam and let app own + wire it"
  echo "             (see ARCHITECTURE.md 'leaf seam' pattern; e.g. audio_capture -> audio_monitor)."
fi

# ---------------------------------------------------------------------------
# Pass 2: line-count ratchet (ARCHITECTURE.md rule 4)
# ---------------------------------------------------------------------------
echo ""
echo "[check-linecount] verifying file size ratchet (ARCHITECTURE.md rule 4, ≤400 lines)..."

lc_fail=0

if [ ! -f "$GRANDFATHER" ]; then
  echo "  ✗ LINECOUNT: grandfather file not found: $GRANDFATHER"
  lc_fail=1
fi

# grandfather_cap <path> — prints the cap for a grandfathered file, or empty string.
# Uses grep on the file: portable, no bash-4 assoc arrays needed.
grandfather_cap() {
  local entry
  entry=$(grep -m1 "^${1}:" "$GRANDFATHER" 2>/dev/null) || true
  if [ -n "$entry" ]; then
    echo "${entry##*:}"
  fi
}

# Scan all in-scope files.
while IFS= read -r f; do
  lc=$(wc -l < "$f" | tr -d ' ')
  cap=$(grandfather_cap "$f")
  if [ -n "$cap" ]; then
    # File is grandfathered — enforce only-decrease ratchet.
    if [ "$lc" -gt "$cap" ]; then
      echo "  ✗ LINECOUNT: $f ($lc lines > cap $cap) — split it or lower the cap in $GRANDFATHER"
      lc_fail=1
    fi
  else
    # Not grandfathered — must be ≤ 400.
    if [ "$lc" -gt 400 ]; then
      echo "  ✗ LINECOUNT: $f ($lc lines > 400, new file) — split to ≤400 or it must be grandfathered first"
      lc_fail=1
    fi
  fi
done < <(find "$SRC" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) \
           | grep -v '/third_party/' \
           | grep -v "${SRC}/main\\.cpp" \
           | sort)

if [ "$lc_fail" -eq 0 ]; then
  echo "[check-linecount] OK — all files within their line-count caps."
else
  echo "[check-linecount] FAIL — split oversized files or update $GRANDFATHER (only-decrease ratchet)."
fi

# ---------------------------------------------------------------------------
# Final exit: fail if EITHER pass failed.
# ---------------------------------------------------------------------------
[ "$fail" -eq 0 ] && [ "$lc_fail" -eq 0 ] && exit 0 || exit 1
