#!/usr/bin/env bash
# tools/sw_drive.sh — eye/hand driver with mtime-wait (the live-verify loop, as a tool).
#
# Live drivers (批次7–8) hand-rolled the same dance for every step: write hand command ->
# sleep a guessed 1s -> touch req_* -> sleep a guessed 1.5s -> read -> hope it wasn't frozen.
# Fixed sleeps are both SLOW (most frames land in <300ms) and FLAKY (a stalled app passes
# silently). This waits on the output file's mtime instead: returns the moment the app
# actually responded, errors loudly if it never does (= the old "eye 凍" failure, surfaced).
#
#   tools/sw_drive.sh shot clean|full|map        # request + wait + print absolute path
#   tools/sw_drive.sh do "<hand line>"           # one hand command, waits one frame-ish
#   tools/sw_drive.sh do "click 242 48" "key Z"  # several lines, in order
#   tools/sw_drive.sh state                      # request map + print state.json path
#
# SW_EYE_DIR overrides the sentinel dir (default <repo>/app/.eye). Timeout: SW_DRIVE_TIMEOUT (s).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EYE="${SW_EYE_DIR:-$ROOT/app/.eye}"
TIMEOUT="${SW_DRIVE_TIMEOUT:-5}"

# FRACTIONAL mtime (%Fm, ns on APFS), compared by inequality. %m (whole seconds) deadlocked
# the moment two app responses landed inside the same second — which the do-loop's fast map
# round-trips (<100ms each) do every time (批次9 first live use of this tool caught it).
mtime() { stat -f %Fm "$1" 2>/dev/null || echo 0; }

# wait_fresh <file> <since>: until file's mtime CHANGES from since, or die after $TIMEOUT.
wait_fresh() {
  local f="$1" since="$2" t=0
  while [ "$(mtime "$f")" = "$since" ]; do
    sleep 0.1; t=$((t+1))
    if [ $((t % 10)) -eq 0 ] && [ $((t / 10)) -ge "$TIMEOUT" ]; then
      echo "[sw_drive] TIMEOUT waiting for $f — app frozen or not running" >&2
      return 1
    fi
  done
}

cmd="${1:-}"; shift || true
case "$cmd" in
  shot)
    what="${1:?clean|full|map}"
    out="$EYE/$what"; [ "$what" = map ] && out="$EYE/map.json" || out="$EYE/$what.png"
    since="$(mtime "$out")"
    touch "$EYE/req_$what"
    wait_fresh "$out" "$since" || exit 1
    echo "$out"
    ;;
  state)
    # 批次9 fix: this used to touch req_map and wait on state.json — which req_map NEVER
    # produces, so it always fell through to the map fallback and echoed a STALE state.json
    # (the zombie-state trap, now structural instead of luck). req_state is the producer.
    out="$EYE/state.json"; since="$(mtime "$out")"
    touch "$EYE/req_state"
    wait_fresh "$out" "$since" || exit 1
    echo "$out"
    ;;
  do)
    [ $# -ge 1 ] || { echo "usage: sw_drive.sh do \"<hand line>\" [...]" >&2; exit 2; }
    for line in "$@"; do
      printf '%s\n' "$line" > "$EYE/hand"
      # hand commands span several frames (click = 3 steps, drag = 15). A fixed number of
      # map round-trips returned while long gestures were still mid-flight (批次9 fix);
      # map.json now carries "hand_pending" — round-trip until the queue reports drained.
      tries=0
      while :; do
        since="$(mtime "$EYE/map.json")"
        touch "$EYE/req_map"
        wait_fresh "$EYE/map.json" "$since" || exit 1
        grep -q '"hand_pending": false' "$EYE/map.json" && break
        tries=$((tries+1))
        if [ "$tries" -ge 100 ]; then
          echo "[sw_drive] hand queue never drained (100 frames) — gesture stuck?" >&2
          exit 1
        fi
      done
      # one extra settle frame: the LAST step (e.g. mouse-up) lands the same frame the
      # queue empties; effects (selection, popups) surface the frame after.
      since="$(mtime "$EYE/map.json")"; touch "$EYE/req_map"; wait_fresh "$EYE/map.json" "$since" || exit 1
    done
    echo "[sw_drive] done: $# command(s)"
    ;;
  *)
    echo "usage: sw_drive.sh shot clean|full|map | do \"<hand line>\"... | state" >&2
    exit 2
    ;;
esac
