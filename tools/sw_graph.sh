#!/usr/bin/env bash
# tools/sw_graph.sh — the COORDINATE-FREE graph-patch harness for an arbitrary Claude.
#
# WHY a separate file from sw_drive.sh: sw_drive is the generic eye/hand round-trip PRIMITIVE
# (shot/do/state + mtime-wait). This is the graph-patch DOMAIN layer on top of it — every
# subcommand emits machine-readable --json, owns typed errors + repair hints, and turns the
# raw hand verbs (spawnsymbol/connect/setparam/deletenode/readpixel) into an id-driven API an
# agent drives with ZERO canvas coordinates. One file, one job (ARCHITECTURE 鐵律 4): this is
# the closed-loop接通點 the batch-B ticket asks for. It NEVER re-implements the mtime-wait —
# it shells out to sw_drive.sh for the hand `do` and the graph/state/pixel round-trips.
#
# ── Subcommands (all print one JSON object to stdout; exit 0 on success, non-0 on typed error) ──
#   status                              app alive? + current compound id/name (+pid)
#   graph                               the current compound's children/ports/connections (graph.json verbatim)
#   spawn <symbolId>                    instantiate a lib symbol as a child of the current root -> {childId}
#   connect <src> <srcSlot> <dst> <dstSlot>   wire (src,srcSlot)->(dst,dstSlot) in the current compound
#   disconnect <dst> <dstSlot>          remove the wire feeding (dst,dstSlot)
#   setparam <child> <slot> <value>     set a Float input override (SAME SetOverrideCommand as the slider)
#   delete <child>                      delete a child + all wires incident on it (one undo unit)
#   readpixel <x> <y>                   read ONE texel of the clean render texture -> {r,g,b,a}
#   render                              request a clean render -> {path} (absolute clean.png)
#   enter <child>                       drill into a compound child (breadcrumb push)
#
# ── Typed errors ── every failure prints {"ok":false,"error":"...","hint":"..."} and exits non-0.
#   Never a silent/dumb failure: app-not-running, bad childId, bad slot, out-of-bounds pixel, type
#   mismatch — each names the fix. The harness VALIDATES against the live graph.json before firing
#   a verb (bad id/slot caught here, not swallowed by the no-op hook downstream).
#
# ── Lifecycle ── `SW_GRAPH_MANAGE=1 sw_graph.sh launch [file]` starts a repo-scoped app; `... kill`
#   stops ONLY this repo's app (CWD-attributed, never pkill-by-cmdline — the三度燒傷 trap). Most
#   agents just point at an already-running app; launch/kill are opt-in for a self-contained run.
#
# ── Env ──
#   SW_EYE_DIR   sentinel dir (default <repo>/app/.eye) — same var sw_drive honours.
#   SW_GRAPH_TIMEOUT  per-round-trip mtime-wait seconds (default 5, forwarded to sw_drive).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DRIVE="$ROOT/tools/sw_drive.sh"   # sw_drive honours SW_EYE_DIR; we reuse its mtime-wait for hand/shot
EYE="${SW_EYE_DIR:-$ROOT/app/.eye}"
BIN="$ROOT/app/build/simple_world"
export SW_EYE_DIR="$EYE"
export SW_DRIVE_TIMEOUT="${SW_GRAPH_TIMEOUT:-5}"

# ---- JSON emit helpers (no jq needed to PRODUCE output; jq only READS eye files) ----
emit_err() {  # emit_err <error> <hint> ; exits 1
  # escape backslash + double-quote for embedding in JSON string literals
  local e h
  e=$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')
  h=$(printf '%s' "$2" | sed 's/\\/\\\\/g; s/"/\\"/g')
  printf '{"ok": false, "error": "%s", "hint": "%s"}\n' "$e" "$h"
  exit 1
}
need_jq() { command -v jq >/dev/null 2>&1 || emit_err "jq not found on PATH" "install jq (brew install jq) — the harness reads eye JSON with it"; }

# ---- eye round-trips (delegate the mtime-wait to sw_drive; surface its failure as typed error) ----
fresh_graph() {  # writes graph.json, echoes its path; typed error if the eye is frozen/absent
  # req_graph is produced by the eye poll; sw_drive has no `graph` verb, so touch+wait here but
  # REUSE its mtime primitive shape. We inline a minimal wait (same fractional-mtime discipline).
  mkdir -p "$EYE"
  local f="$EYE/graph.json" since t=0
  since=$(stat -f %Fm "$f" 2>/dev/null || echo 0)
  touch "$EYE/req_graph"
  while [ "$(stat -f %Fm "$f" 2>/dev/null || echo 0)" = "$since" ]; do
    sleep 0.1; t=$((t+1))
    [ "$t" -ge $((SW_DRIVE_TIMEOUT*10)) ] && emit_err "app not responding (graph.json never refreshed)" \
      "is simple_world running and writing to $EYE? start it (SW_GRAPH_MANAGE=1 $0 launch) or point SW_EYE_DIR at the live app's .eye"
  done
  echo "$f"
}
hand_do() {  # hand_do "<verb line>" — one hand command via sw_drive (waits the queue drained)
  "$DRIVE" do "$1" >/dev/null 2>&1 || emit_err "hand queue stuck or app frozen: $1" \
    "the app stopped draining hand commands — check it is alive ($0 status) and not mid-modal-dialog"
}

# ---- live-graph validation (catch bad id/slot HERE, before the no-op hook swallows it) ----
child_exists() {  # child_exists <graph.json> <childId>
  jq -e --argjson c "$2" 'any(.children[]; .childId == $c)' "$1" >/dev/null 2>&1
}
slot_of() {  # slot_of <graph.json> <childId> <slotId> -> prints "isInput multiInput dataType" or empty
  jq -r --argjson c "$2" --arg s "$3" \
    '.children[] | select(.childId==$c) | .ports[] | select(.id==$s) | "\(.isInput) \(.multiInput) \(.dataType)"' \
    "$1" 2>/dev/null | head -1
}

# ---- lifecycle (repo-scoped; CWD attribution, never pkill-by-cmdline) ----
repo_pids() {
  local pid cwd
  for pid in $(pgrep -f "build/simple_world" 2>/dev/null); do
    cwd=$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p')
    case "$cwd" in "$ROOT"|"$ROOT"/*) echo "$pid" ;; esac
  done
}

cmd="${1:-}"; shift || true
case "$cmd" in

  # ── status: is THIS repo's app alive, and what compound is current? ──
  status)
    need_jq
    live_pids="$(repo_pids | tr '\n' ' ' | sed 's/ *$//; s/ /,/g')"
    [ -n "$live_pids" ] && live_pids="[$live_pids]" || live_pids="null"
    # liveness = the eye actually answers a graph request (a pid alone can be a zombie mid-teardown)
    f="$EYE/graph.json"; t=0
    since=$(stat -f %Fm "$f" 2>/dev/null || echo 0)
    mkdir -p "$EYE"; touch "$EYE/req_graph"
    while [ "$(stat -f %Fm "$f" 2>/dev/null || echo 0)" = "$since" ]; do
      sleep 0.1; t=$((t+1))
      if [ "$t" -ge $((SW_DRIVE_TIMEOUT*10)) ]; then
        printf '{"ok": true, "alive": false, "pid": %s, "hint": "no eye response at %s — app not running or wrong SW_EYE_DIR"}\n' \
          "$live_pids" "$EYE"
        exit 0
      fi
    done
    cid=$(jq -r '.compound.id // "?"' "$f")
    cname=$(jq -r '.compound.name // "?"' "$f")
    depth=$(jq -r '.breadcrumb | length' "$f")
    printf '{"ok": true, "alive": true, "pid": %s, "compound": {"id": "%s", "name": "%s"}, "depth": %s, "eye": "%s"}\n' \
      "$live_pids" "$cid" "$cname" "$depth" "$EYE"
    ;;

  # ── graph: the whole current compound, verbatim (children/ports/connections) ──
  graph)
    need_jq
    f="$(fresh_graph)"
    # emit as {ok, graph:{...}} so callers can .graph.children[] uniformly
    jq -c '{ok: true, graph: .}' "$f"
    ;;

  # ── spawn <symbolId>: instantiate -> return the NEW childId (spawn-diff, spawnsymbol returns none) ──
  spawn)
    need_jq
    sym="${1:-}"; [ -n "$sym" ] || emit_err "spawn: missing <symbolId>" "usage: $0 spawn <symbolId> (e.g. RenderTarget)"
    before_f="$(fresh_graph)"
    before=$(jq -c '[.children[].childId]' "$before_f")
    hand_do "spawnsymbol $sym"
    after_f="$(fresh_graph)"
    # array subtraction: whatever id is in after but not before is the freshly-spawned child.
    newid=$(jq -r --argjson b "$before" '([.children[].childId] - $b) | .[0] // empty' "$after_f")
    if [ -z "$newid" ]; then
      emit_err "spawn produced no new child for symbol '$sym'" \
        "the spawn was refused (e.g. a compound that would nest itself — a cycle). check '$0 graph' and pick a valid symbol"
    fi
    otype=$(jq -r --argjson n "$newid" '.children[] | select(.childId==$n) | .opType' "$after_f")
    # Real-backend gotcha: spawnNodeAt (toolbar.cpp) accepts ANY symbolId string and makes a child
    # with it — an UNKNOWN symbol resolves to neither an atomic spec nor a compound def, so its port
    # surface is empty (graph_dump portsOf returns []). A 0-port child is a dangling instance, not a
    # real spawn: roll it back with delete and report a typed error, so a typo can't silently pollute
    # the graph. (A genuine 0-input/0-output op does not exist in the lib, so 0 ports == unresolved.)
    pc=$(jq -r --argjson n "$newid" '.children[] | select(.childId==$n) | .ports | length' "$after_f")
    if [ "$pc" = "0" ]; then
      hand_do "deletenode $newid"
      emit_err "spawn: '$sym' is not a known library symbol (spawned a 0-port dangling child, rolled back)" \
        "check the symbol id spelling; valid atomic op types and compound def ids are what the menu offers"
    fi
    printf '{"ok": true, "childId": %s, "opType": "%s"}\n' "$newid" "$otype"
    ;;

  # ── connect <src> <srcSlot> <dst> <dstSlot> ──
  connect)
    need_jq
    src="${1:-}"; ss="${2:-}"; dst="${3:-}"; ds="${4:-}"
    { [ -n "$src" ] && [ -n "$ss" ] && [ -n "$dst" ] && [ -n "$ds" ]; } || \
      emit_err "connect: need <src> <srcSlot> <dst> <dstSlot>" "usage: $0 connect 7 out 104 command"
    f="$(fresh_graph)"
    child_exists "$f" "$src" || emit_err "connect: src child $src does not exist in the current compound" "run '$0 graph' to see valid childIds"
    child_exists "$f" "$dst" || emit_err "connect: dst child $dst does not exist in the current compound" "run '$0 graph' to see valid childIds"
    srcinfo=$(slot_of "$f" "$src" "$ss")
    [ -n "$srcinfo" ] || emit_err "connect: src slot '$ss' not found on child $src" "run '$0 graph' and read child $src .ports[].id"
    read -r sinp _smulti stype <<< "$srcinfo"
    [ "$sinp" = "false" ] || emit_err "connect: src slot '$ss' on child $src is an INPUT, not an output" "wire FROM an output (isInput:false) TO an input (isInput:true)"
    dstinfo=$(slot_of "$f" "$dst" "$ds")
    [ -n "$dstinfo" ] || emit_err "connect: dst slot '$ds' not found on child $dst" "run '$0 graph' and read child $dst .ports[].id"
    read -r dinp _dmulti dtype <<< "$dstinfo"
    [ "$dinp" = "true" ] || emit_err "connect: dst slot '$ds' on child $dst is an OUTPUT, not an input" "wire TO an input (isInput:true)"
    [ "$stype" = "$dtype" ] || emit_err "connect: type mismatch — src '$ss' is $stype, dst '$ds' is $dtype" "a wire needs matching dataTypes; pick slots of the same type"
    hand_do "connect $src $ss $dst $ds"
    # confirm the wire landed (the hook validates too; we prove it round-trip)
    after="$(fresh_graph)"
    got=$(jq -r --argjson sc "$src" --arg sst "$ss" --argjson dc "$dst" --arg dst "$ds" \
      'any(.connections[]; .srcChild==$sc and .srcSlot==$sst and .dstChild==$dc and .dstSlot==$dst)' "$after")
    [ "$got" = "true" ] || emit_err "connect: wire did not appear after the verb (rejected by the graph?)" "the app declined the wire — likely a slot already occupied or a validation rule; check '$0 graph'"
    printf '{"ok": true, "connected": {"src": %s, "srcSlot": "%s", "dst": %s, "dstSlot": "%s"}}\n' "$src" "$ss" "$dst" "$ds"
    ;;

  # ── disconnect <dst> <dstSlot> ──
  disconnect)
    need_jq
    dst="${1:-}"; ds="${2:-}"
    { [ -n "$dst" ] && [ -n "$ds" ]; } || emit_err "disconnect: need <dst> <dstSlot>" "usage: $0 disconnect 104 command"
    f="$(fresh_graph)"
    child_exists "$f" "$dst" || emit_err "disconnect: dst child $dst does not exist" "run '$0 graph' to see valid childIds"
    had=$(jq -r --argjson dc "$dst" --arg d "$ds" 'any(.connections[]; .dstChild==$dc and .dstSlot==$d)' "$f")
    [ "$had" = "true" ] || emit_err "disconnect: no wire feeds ($dst,$ds) — nothing to remove" "run '$0 graph' and check .connections for a wire whose dstChild/dstSlot match"
    hand_do "disconnect $dst $ds"
    after="$(fresh_graph)"
    still=$(jq -r --argjson dc "$dst" --arg d "$ds" 'any(.connections[]; .dstChild==$dc and .dstSlot==$d)' "$after")
    [ "$still" = "false" ] || emit_err "disconnect: a wire still feeds ($dst,$ds) after the verb" "check '$0 graph' — the slot may have had no wire, or the id/slot was wrong"
    printf '{"ok": true, "disconnected": {"dst": %s, "dstSlot": "%s"}}\n' "$dst" "$ds"
    ;;

  # ── setparam <child> <slot> <value> ──  (Float override; Vec via .x/.y/.z/.w; Enum=index; Bool=0/1)
  setparam)
    need_jq
    child="${1:-}"; slot="${2:-}"; val="${3:-}"
    { [ -n "$child" ] && [ -n "$slot" ] && [ -n "$val" ]; } || \
      emit_err "setparam: need <child> <slot> <value>" "usage: $0 setparam 104 ClearColor.x 0.2"
    case "$val" in ''|*[!0-9.+-]*) emit_err "setparam: value '$val' is not a number" "the setparam verb takes ONE float; Vec components use <base>.x/.y/.z/.w, Enum=index, Bool=0/1";; esac
    f="$(fresh_graph)"
    child_exists "$f" "$child" || emit_err "setparam: child $child does not exist" "run '$0 graph' to see valid childIds"
    # slot may be a Vec component (base.x) whose exact id IS listed in ports; validate against ports.
    sinfo=$(slot_of "$f" "$child" "$slot")
    [ -n "$sinfo" ] || emit_err "setparam: slot '$slot' not found on child $child" "run '$0 graph' and read child $child .ports[].id (Vec params expose .x/.y/.z/.w component ids)"
    read -r sinp _m stype <<< "$sinfo"
    [ "$sinp" = "true" ] || emit_err "setparam: slot '$slot' is an output, not a settable input" "setparam targets input params (isInput:true)"
    hand_do "setparam $child $slot $val"
    printf '{"ok": true, "setparam": {"child": %s, "slot": "%s", "value": %s}}\n' "$child" "$slot" "$val"
    ;;

  # ── delete <child> ── (child + incident wires, one undo unit)
  delete)
    need_jq
    child="${1:-}"; [ -n "$child" ] || emit_err "delete: missing <child>" "usage: $0 delete 104"
    f="$(fresh_graph)"
    child_exists "$f" "$child" || emit_err "delete: child $child does not exist" "run '$0 graph' to see valid childIds"
    hand_do "deletenode $child"
    after="$(fresh_graph)"
    gone=$(jq -r --argjson c "$child" 'any(.children[]; .childId==$c) | not' "$after")
    [ "$gone" = "true" ] || emit_err "delete: child $child still present after the verb" "the app declined the delete — check '$0 graph'"
    wires=$(jq -r --argjson c "$child" 'any(.connections[]; .srcChild==$c or .dstChild==$c)' "$after")
    [ "$wires" = "false" ] || emit_err "delete: wires incident on $child survived (dangling)" "expected the delete to clear all incident wires as one unit — investigate the deletenode hook"
    printf '{"ok": true, "deleted": %s, "incidentWiresCleared": true}\n' "$child"
    ;;

  # ── readpixel <x> <y> ── one texel of the clean render texture
  readpixel)
    need_jq
    x="${1:-}"; y="${2:-}"
    { [ -n "$x" ] && [ -n "$y" ]; } || emit_err "readpixel: need <x> <y>" "usage: $0 readpixel 5 5"
    rpj="$EYE/readpixel.json"; mkdir -p "$EYE"
    since=$(stat -f %Fm "$rpj" 2>/dev/null || echo 0)
    printf 'readpixel %s %s\n' "$x" "$y" > "$EYE/hand"
    t=0
    while [ "$(stat -f %Fm "$rpj" 2>/dev/null || echo 0)" = "$since" ]; do
      sleep 0.1; t=$((t+1))
      [ "$t" -ge $((SW_DRIVE_TIMEOUT*10)) ] && emit_err "readpixel.json never written" \
        "app frozen, or no render target bound yet (readpixel needs a cooked frame) — try '$0 render' first"
    done
    err=$(jq -r '.error // empty' "$rpj")
    if [ -n "$err" ]; then
      emit_err "readpixel out of bounds: $err" "pick x/y inside the render texture dimensions"
    fi
    jq -c '{ok: true, r: .r, g: .g, b: .b, a: .a, x: .x, y: .y}' "$rpj"
    ;;

  # ── render ── request a clean render, return its absolute path
  render)
    p="$("$DRIVE" shot clean 2>/dev/null)" || emit_err "render failed (eye not answering)" "is the app alive? '$0 status'"
    printf '{"ok": true, "path": "%s"}\n' "$p"
    ;;

  # ── enter <child> ── drill into a compound child
  enter)
    need_jq
    child="${1:-}"; [ -n "$child" ] || emit_err "enter: missing <child>" "usage: $0 enter 104"
    f="$(fresh_graph)"
    child_exists "$f" "$child" || emit_err "enter: child $child does not exist" "run '$0 graph' to see valid childIds"
    hand_do "entercompound $child"
    after="$(fresh_graph)"
    got=$(jq -r --argjson c "$child" '.breadcrumb[-1] == $c' "$after" 2>/dev/null)
    [ "$got" = "true" ] || emit_err "enter: did not descend into $child (not a compound?)" "entercompound is a no-op on atomic children; only compound children can be entered"
    cid=$(jq -r '.compound.id' "$after")
    printf '{"ok": true, "entered": %s, "compound": "%s"}\n' "$child" "$cid"
    ;;

  # ── launch [file] / kill ── opt-in repo-scoped lifecycle (SW_GRAPH_MANAGE gate is advisory)
  launch)
    file="${1:-}"
    [ -x "$BIN" ] || emit_err "no binary at $BIN" "build first: (cd app && cmake --build build --target simple_world)"
    # kill any stale repo app first (multi-instance .eye pollution)
    pids="$(repo_pids)"; [ -n "$pids" ] && { kill $pids 2>/dev/null; for _ in $(seq 1 50); do [ -z "$(repo_pids)" ] && break; sleep 0.2; done; }
    mkdir -p "$EYE"
    # CRITICAL fd hygiene: `launch` is routinely called as `LR=$(sw_graph.sh launch)`. A command
    # substitution does not return until EVERY process holding the write end of its stdout pipe has
    # closed it — so the backgrounded app MUST inherit none of our fds, or the $(...) hangs forever
    # (observed: the demo wedged at launch). Redirect the app's 0/1/2 to /dev/null explicitly.
    ( cd "$ROOT/app" && ASAN_OPTIONS=detect_leaks=0 "$BIN" ${file:+--open "$file"} </dev/null >/dev/null 2>&1 & disown ) </dev/null >/dev/null 2>&1
    for _ in $(seq 1 40); do
      f="$EYE/graph.json"; since=$(stat -f %Fm "$f" 2>/dev/null || echo 0); touch "$EYE/req_graph"; sleep 0.4
      [ "$(stat -f %Fm "$f" 2>/dev/null || echo 0)" != "$since" ] && { pid="$(repo_pids | head -1)"; printf '{"ok": true, "launched": true, "pid": %s}\n' "${pid:-null}"; exit 0; }
    done
    emit_err "app never answered the eye after launch" "check the build and $EYE; something crashed on startup"
    ;;
  kill)
    pids="$(repo_pids)"
    if [ -z "$pids" ]; then printf '{"ok": true, "killed": []}\n'; exit 0; fi
    kill $pids 2>/dev/null
    for _ in $(seq 1 50); do [ -z "$(repo_pids)" ] && break; sleep 0.2; done
    still="$(repo_pids)"; [ -n "$still" ] && kill -9 $still 2>/dev/null
    printf '{"ok": true, "killed": [%s]}\n' "$(echo $pids | tr ' ' ',')"
    ;;

  ""|-h|--help|help)
    cat >&2 <<'EOF'
sw_graph.sh — coordinate-free graph-patch harness (all subcommands print JSON)
  status | graph | spawn <sym> | connect <s> <ss> <d> <ds> | disconnect <d> <ds>
  setparam <c> <slot> <val> | delete <c> | readpixel <x> <y> | render | enter <c>
  launch [file] | kill        (repo-scoped lifecycle; opt-in)
Env: SW_EYE_DIR (default <repo>/app/.eye), SW_GRAPH_TIMEOUT (default 5s)
EOF
    exit 2
    ;;
  *)
    emit_err "unknown subcommand: $cmd" "run '$0 help' for the subcommand list"
    ;;
esac
