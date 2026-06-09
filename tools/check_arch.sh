#!/usr/bin/env bash
# tools/check_arch.sh — architecture dependency-direction tripwire (ARCHITECTURE.md 鐵律 2).
#
# A rule nobody checks gets violated. This greps every zone's source for #includes that cross
# a forbidden zone boundary and fails (exit 1) on the first offence — so a leaf->leaf or
# leaf->up dependency is caught the moment it's written, not in a human review weeks later.
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
fail=0

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
exit $fail
