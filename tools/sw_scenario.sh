#!/usr/bin/env bash
# tools/sw_scenario.sh — replayable live-verification scenarios ("活體牙").
#
# WHY: agent-driven live verification spends ~15s of model inference per micro-step
# (D3 ran 75 min for 273 steps; the app itself answers in 0.3s). Everything
# deterministic belongs in a SCRIPT the agent replays in seconds, spending its
# intelligence only on failures and on judgment calls (feel/sound). Same philosophy
# as --selftest teeth, extended to the live GUI.
#
#   tools/sw_scenario.sh run tests/scenarios/foo.scn     # exit 0 = all asserts green
#   tools/sw_scenario.sh run foo.scn --keep              # keep app running after (debugging)
#
# The runner owns the app lifecycle: kills stale instances (FULL path — the
# "simple_world$" / bare "build/simple_world" pkill traps), launches fresh
# (optionally --open <file> via the `open` directive), waits for the eye.
#
# Scenario format — one directive per line, # comments:
#   open <relpath.swproj>        relaunch app with --open <repo-relative path>
#   do <hand line>               one hand command; @SYM anywhere resolves to the CENTER
#                                of map.json item "SYM" (imgui coords), e.g.
#                                  do click @tl_key:8:Amplitude:0
#                                  do rclick @node:7
#                                  do drag @tl_key:8:Amplitude:0 +40 +0   (+dx +dy from center)
#   wait <secs>                  sleep (damp animations, audio settle)
#   assert_state <jq-bool-expr>  req_state -> state.json; expr must evaluate to true
#   assert_map <jq-bool-expr>    req_map -> map.json; expr must evaluate to true
#   assert_map_has <SYM>         map.json contains item SYM
#   capture <clean|full> <name>  screenshot into the run dir under <name>.png
#   assert_diff <name>           current <same-kind> shot differs from capture <name> (bytes)
#   assert_same <name>           ... is byte-identical to capture <name>
#   echo <text>                  progress marker in the transcript
#
# Every failed assert prints the line number + directive + evidence path, and the run
# exits 1 after finishing the remaining lines (so one run reports ALL failures).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DRIVE="$ROOT/tools/sw_drive.sh"
EYE="$ROOT/app/.eye"
BIN="$ROOT/app/build/simple_world"
RUNDIR="$(mktemp -d /tmp/sw_scenario.XXXXXX)"
FAIL=0
KEEP=0

note() { printf '[scn] %s\n' "$*"; }
fail() { printf '[scn] ✗ line %s: %s\n' "$1" "$2" >&2; FAIL=1; }

# Kill every simple_world instance BELONGING TO THIS REPO — matched by the process's CWD,
# not its cmdline. Cmdline matching has burned twice: "simple_world$" misses --open instances,
# and a relative "./build/simple_world" launch (manual shells) never contains the absolute
# path. CWD scoping also spares worktree selftest binaries (the D4 cross-kill trap).
repo_pids() {
  local pid cwd
  for pid in $(pgrep -f "build/simple_world" 2>/dev/null); do
    cwd=$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')
    case "$cwd" in "$ROOT"|"$ROOT"/*) echo "$pid" ;; esac
  done
}
app_kill() {
  # WAIT for true death: ASan teardown takes seconds; launching the next instance while
  # the old one is dying leaves TWO apps racing on app/.eye (multi-instance pollution).
  local pids; pids="$(repo_pids)"
  [ -n "$pids" ] && kill $pids 2>/dev/null
  for _ in $(seq 1 50); do [ -z "$(repo_pids)" ] && return 0; sleep 0.2; done
  pids="$(repo_pids)"; [ -n "$pids" ] && kill -9 $pids 2>/dev/null; sleep 0.5
}
app_launch() {  # $1 = optional file to open
  app_kill
  # Ensure .eye dir exists BEFORE sw_drive tries to `touch req_map` inside it.
  # The app creates it via ensureDir() on first write, but sw_drive's probe touches
  # req_map first — which fails silently when the directory doesn't exist yet,
  # making the wait loop spin to timeout. mkdir -p is idempotent and harmless.
  mkdir -p "$EYE"
  ( cd "$ROOT/app" && ASAN_OPTIONS=detect_leaks=0 "$BIN" ${1:+--open "$1"} >/dev/null 2>&1 & disown ) 2>/dev/null
  # eye answers only once the frame loop is up — probe with a map request.
  for _ in $(seq 1 40); do
    if "$DRIVE" shot map >/dev/null 2>&1; then return 0; fi
    sleep 0.5
  done
  echo "[scn] app never answered the eye after launch" >&2; exit 2
}

# resolve_syms <line>: replace every @SYM token with "x y" = center of map item SYM.
resolve_syms() {
  local line="$1" out="" tok
  for tok in $line; do
    if [[ "$tok" == @* ]]; then
      local sym="${tok#@}" mode=center
      # @SYM^ = top-center (title row, y0+8): nodes must be grabbed by the TITLE, not the
      # body center (imgui-node-editor hit-test — the long-standing 點標題非中心 trap).
      [[ "$sym" == *^ ]] && { mode=title; sym="${sym%^}"; }
      local m; m="$("$DRIVE" shot map)" || { echo "__RESOLVE_FAIL__ $sym"; return; }
      local xy
      if [ "$mode" = title ]; then
        xy=$(jq -r --arg s "$sym" '
          .items[]? | select(.label == $s) | .imgui_rect |
          "\((.x0 + .x1) / 2 | floor) \(.y0 + 8 | floor)"' "$m" | head -1)
      else
        xy=$(jq -r --arg s "$sym" '
          .items[]? | select(.label == $s) | .imgui_rect |
          "\((.x0 + .x1) / 2 | floor) \((.y0 + .y1) / 2 | floor)"' "$m" | head -1)
      fi
      if [ -z "$xy" ]; then
        # one retry on a fresh frame: popup/lane items appear a frame late sometimes
        sleep 0.4
        m="$("$DRIVE" shot map)" || { echo "__RESOLVE_FAIL__ $sym"; return; }
        if [ "$mode" = title ]; then
          xy=$(jq -r --arg s "$sym" '.items[]? | select(.label == $s) | .imgui_rect |
            "\((.x0 + .x1) / 2 | floor) \(.y0 + 8 | floor)"' "$m" | head -1)
        else
          xy=$(jq -r --arg s "$sym" '.items[]? | select(.label == $s) | .imgui_rect |
            "\((.x0 + .x1) / 2 | floor) \((.y0 + .y1) / 2 | floor)"' "$m" | head -1)
        fi
      fi
      [ -z "$xy" ] && { cp "$m" "$RUNDIR/resolve_fail.$sym.json" 2>/dev/null
                        echo "__RESOLVE_FAIL__ $sym"; return; }
      out="$out $xy"
    else
      out="$out $tok"
    fi
  done
  echo "${out# }"
}

# apply +dx +dy arithmetic that may follow a resolved center (drag @sym +40 +0).
apply_offsets() {
  local -a w=($1) o=() i=0
  while [ $i -lt ${#w[@]} ]; do
    local cur="${w[$i]}"
    if [[ "$cur" == +* || "$cur" == -* ]] && [[ "$cur" =~ ^[+-][0-9]+$ ]] && [ ${#o[@]} -ge 2 ]; then
      # offset applies to the PRECEDING coordinate pair -> emit (lastX+dx) replacing pattern
      local n=${#o[@]}
      if [[ "${w[$((i+1))]:-}" =~ ^[+-][0-9]+$ ]]; then
        local dx="$cur" dy="${w[$((i+1))]}"
        o+=("$(( ${o[$((n-2))]} + dx ))" "$(( ${o[$((n-1))]} + dy ))")
        i=$((i+2)); continue
      fi
    fi
    o+=("$cur"); i=$((i+1))
  done
  echo "${o[*]}"
}

run_line() {
  local ln="$1" raw="$2"
  local cmd rest
  cmd="${raw%% *}"; rest="${raw#* }"; [ "$rest" = "$raw" ] && rest=""
  case "$cmd" in
    open)
      app_launch "$ROOT/$rest" ;;
    fixture)
      # fixture <src> <dst> — reset a writable fixture from a pristine source (repo-relative).
      # A scenario that SAVES into its project file mutates it; without this reset the second
      # run opens the mutated file and every state-dependent resolve drifts (D4 save_restart:
      # Radius already animated -> menu says "Remove Animation" -> insp:Animate unresolvable).
      local src="${rest%% *}" dst="${rest#* }"
      cp "$ROOT/$src" "$ROOT/$dst" || fail "$ln" "fixture: cp $src -> $dst failed" ;;
    do)
      # @SYM resolution gets a BOUNDED retry (3×0.7s, re-fetching the map each time): under
      # back-to-back sweep churn a popup/inspector item can lag a few frames behind the click
      # that opens it (the known first-click-miss/hover-frame trap) — solo runs never see it,
      # sweeps lost 3-4 scenarios per pass to it. Retries are LOGGED, never silent: a symbol
      # that truly never appears still fails after the window, so real reds stay red.
      local resolved attempt
      for attempt in 1 2 3; do
        resolved="$(resolve_syms "$rest")"
        [[ "$resolved" == __RESOLVE_FAIL__* ]] || break
        [ "$attempt" -lt 3 ] && { note "(resolve retry $attempt: ${resolved#__RESOLVE_FAIL__ })"; sleep 0.7; }
      done
      if [[ "$resolved" == __RESOLVE_FAIL__* ]]; then
        fail "$ln" "do: cannot resolve ${resolved#__RESOLVE_FAIL__ } in map.json"; return; fi
      resolved="$(apply_offsets "$resolved")"
      "$DRIVE" do "$resolved" >/dev/null || fail "$ln" "do: hand queue stuck ($resolved)" ;;
    wait)
      sleep "$rest" ;;
    assert_state|assert_map)
      # strip one pair of surrounding quotes (shell habit in scenario files; jq never wants them)
      [[ "$rest" == \'*\' ]] && rest="${rest:1:${#rest}-2}"
      [[ "$rest" == \"*\" ]] && rest="${rest:1:${#rest}-2}"
      local kind="${cmd#assert_}" f
      if [ "$kind" = state ]; then f="$("$DRIVE" state)"; else f="$("$DRIVE" shot map)"; fi
      [ -z "$f" ] && { fail "$ln" "$cmd: eye not answering"; return; }
      local got; got=$(jq -r "$rest" "$f" 2>/dev/null)
      [ "$got" = "true" ] || fail "$ln" "$cmd $rest -> $got (evidence: $f copied to $RUNDIR/L$ln.json)" \
        && cp "$f" "$RUNDIR/L$ln.json" 2>/dev/null ;;
    assert_map_has)
      local f; f="$("$DRIVE" shot map)"
      local got; got=$(jq -r --arg s "$rest" '[.items[]? | select(.label == $s)] | length > 0' "$f")
      [ "$got" = "true" ] || fail "$ln" "assert_map_has $rest -> absent" ;;
    capture)
      local kind="${rest%% *}" name="${rest#* }"
      local p; p="$("$DRIVE" shot "$kind")" || { fail "$ln" "capture: eye not answering"; return; }
      cp "$p" "$RUNDIR/$name.png"; echo "$kind" > "$RUNDIR/$name.kind" ;;
    assert_diff|assert_same)
      local name="$rest" kind; kind="$(cat "$RUNDIR/$name.kind" 2>/dev/null || echo clean)"
      local p; p="$("$DRIVE" shot "$kind")" || { fail "$ln" "$cmd: eye not answering"; return; }
      if cmp -s "$p" "$RUNDIR/$name.png"; then
        [ "$cmd" = assert_same ] || fail "$ln" "assert_diff $name: frames byte-identical"
      else
        [ "$cmd" = assert_diff ] || fail "$ln" "assert_same $name: frames differ (see $RUNDIR)"
        cp "$p" "$RUNDIR/$name.after.png" 2>/dev/null
      fi ;;
    echo)
      note "$rest" ;;
    *)
      fail "$ln" "unknown directive: $cmd" ;;
  esac
}

main() {
  [ "${1:-}" = run ] || { echo "usage: sw_scenario.sh run <file.scn> [--keep]" >&2; exit 2; }
  local file="$2"; [ "${3:-}" = "--keep" ] && KEEP=1
  [ -f "$file" ] || { echo "[scn] no such scenario: $file" >&2; exit 2; }
  [ -x "$BIN" ] || { echo "[scn] no binary at $BIN (build first)" >&2; exit 2; }

  note "run $file  (evidence dir: $RUNDIR)"
  app_launch ""   # default launch; `open` directive relaunches with a project

  local ln=0
  while IFS= read -r raw || [ -n "$raw" ]; do
    ln=$((ln+1))
    raw="${raw%%#*}"; raw="$(echo "$raw" | sed 's/^ *//;s/ *$//')"
    [ -z "$raw" ] && continue
    run_line "$ln" "$raw"
  done < "$file"

  [ "$KEEP" = 1 ] || app_kill
  if [ "$FAIL" = 0 ]; then note "PASS ($file)"; else note "FAIL ($file) — evidence in $RUNDIR"; fi
  exit "$FAIL"
}
main "$@"
