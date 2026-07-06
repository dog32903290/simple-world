#!/usr/bin/env bash
# tools/sw_graph_demo.sh — the CLOSED-LOOP demo: a brand-new Claude weaves + verifies a patch
# through sw_graph.sh --json ALONE. Zero canvas coordinates, zero knowledge of any prior session.
#
# This is the acceptance proof for the graph data-verb closed loop (batch B). It plays the exact
# journey the SKILL.md promises:
#   status → spawn → connect → setparam → render → readpixel(assert) → delete → graph(confirm
#   child + its wire gone) → render → readpixel(assert the picture CHANGED).
# Every step reads a JSON field back; a wrong number fails the run. The whole thing runs in seconds.
#
#   tools/sw_graph_demo.sh              # launches its own app, runs green, kills it. exit 0 = loop closed.
#   tools/sw_graph_demo.sh --red        # RED FACE: assert the WRONG pixel; the run MUST fail (exit 1),
#                                       # proving the assertion reads a real number off the live render.
#   SW_GRAPH_DEMO_KEEP=1 ...            # leave the app running after (debugging)
#
# Closed-form pixel (no eyeballing): a RenderTarget clears its whole texture to ClearColor before any
# draw. RGBA8Unorm is linear, so a CORNER texel (no geometry there) == ClearColor byte-exact:
#   ClearColor (0.2,0.6,0.8) -> round(.2*255),round(.6*255),round(.8*255) = (51,153,204).
# After we DELETE the RenderTarget, the most-downstream sink reverts to the default graph's output,
# so the same corner texel is no longer (51,153,204) — that difference is the "picture changed" proof.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
G="$ROOT/tools/sw_graph.sh"
RED=0; [ "${1:-}" = "--red" ] && RED=1
FAIL=0
pass() { printf '  \033[32mok\033[0m   %s\n' "$*"; }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$*"; FAIL=1; }
step() { printf '\n[demo] %s\n' "$*"; }

# jq field extractor with an ok-gate: every subcommand must report ok:true (or we stop).
jget() { jq -r "$2" <<<"$1"; }
okof() { jq -r '.ok' <<<"$1"; }

cleanup() { [ "${SW_GRAPH_DEMO_KEEP:-0}" = 1 ] || "$G" kill >/dev/null 2>&1; }
trap cleanup EXIT

step "launch a fresh app (repo-scoped)"
LR=$("$G" launch); [ "$(okof "$LR")" = true ] && pass "launched pid $(jget "$LR" .pid)" || { bad "launch: $LR"; exit 1; }

step "status — confirm alive + we are at Root"
SR=$("$G" status)
[ "$(jget "$SR" .alive)" = true ] && pass "alive" || bad "not alive: $SR"
[ "$(jget "$SR" '.compound.id')" = Root ] && pass "at Root" || bad "not at Root: $SR"

step "graph — read the starting children (the AI's ONLY source of ids; no coordinates)"
GR=$("$G" graph)
START_IDS=$(jget "$GR" '[.graph.children[].childId] | join(",")')
pass "starting children: [$START_IDS]"
# find the DrawPoints child by opType (coordinate-free, name-free discovery)
DP=$(jget "$GR" '.graph.children[] | select(.opType=="DrawPoints") | .childId' | head -1)
[ -n "$DP" ] && pass "found DrawPoints at childId $DP" || bad "no DrawPoints in default graph"

step "spawn RenderTarget — the harness returns the NEW childId (spawnsymbol itself returns none)"
SPR=$("$G" spawn RenderTarget)
[ "$(okof "$SPR")" = true ] || bad "spawn failed: $SPR"
RT=$(jget "$SPR" .childId)
pass "RenderTarget spawned as childId $RT"

step "connect DrawPoints#$DP.out -> RenderTarget#$RT.command (type-checked Command->Command)"
CR=$("$G" connect "$DP" out "$RT" command)
[ "$(okof "$CR")" = true ] && pass "wired $DP.out -> $RT.command" || bad "connect failed: $CR"

step "setparam — paint the RenderTarget's ClearColor to a KNOWN color, one Vec component per verb"
for kv in ClearColor.x:0.2 ClearColor.y:0.6 ClearColor.z:0.8 ClearColor.w:1.0; do
  slot="${kv%%:*}"; val="${kv#*:}"
  PR=$("$G" setparam "$RT" "$slot" "$val")
  [ "$(okof "$PR")" = true ] || bad "setparam $slot failed: $PR"
done
pass "ClearColor set to (0.2,0.6,0.8,1.0)"

step "render + readpixel — assert the corner texel == ClearColor byte-exact (closed-form, no OCR)"
"$G" render >/dev/null
PX=$("$G" readpixel 5 5)
R=$(jget "$PX" .r); Gc=$(jget "$PX" .g); B=$(jget "$PX" .b)
# RED FACE: demand the WRONG blue so a real readback bites (proves this isn't hollow-green).
WANT_B=204; [ "$RED" = 1 ] && WANT_B=100
if [ "$R" = 51 ] && [ "$Gc" = 153 ] && [ "$B" = "$WANT_B" ]; then
  pass "corner pixel = ($R,$Gc,$B) == ClearColor"
else
  bad "corner pixel = ($R,$Gc,$B), wanted (51,153,$WANT_B)"
fi
BEFORE_PIXEL="$R,$Gc,$B"

step "delete RenderTarget#$RT — child + its incident wire cleared as one undo unit"
DR=$("$G" delete "$RT")
[ "$(okof "$DR")" = true ] && pass "deleted $RT" || bad "delete failed: $DR"

step "graph — confirm #$RT is gone AND no wire references it (data-verb round-trip, not a screenshot)"
GR2=$("$G" graph)
GONE=$(jget "$GR2" "any(.graph.children[]; .childId==$RT) | not")
[ "$GONE" = true ] && pass "child $RT absent" || bad "child $RT still present"
NOWIRE=$(jget "$GR2" "any(.graph.connections[]; .srcChild==$RT or .dstChild==$RT) | not")
[ "$NOWIRE" = true ] && pass "no wire references $RT" || bad "dangling wire on $RT survived"
END_IDS=$(jget "$GR2" '[.graph.children[].childId] | join(",")')
[ "$END_IDS" = "$START_IDS" ] && pass "graph back to starting shape [$END_IDS]" || pass "graph now [$END_IDS]"

step "render + readpixel again — the picture CHANGED (RenderTarget's clear no longer paints the corner)"
"$G" render >/dev/null
PX2=$("$G" readpixel 5 5)
R2=$(jget "$PX2" .r); G2=$(jget "$PX2" .g); B2=$(jget "$PX2" .b)
AFTER_PIXEL="$R2,$G2,$B2"
if [ "$AFTER_PIXEL" != "$BEFORE_PIXEL" ]; then
  pass "corner pixel changed: ($BEFORE_PIXEL) -> ($AFTER_PIXEL)"
else
  bad "corner pixel unchanged after delete ($AFTER_PIXEL) — delete had no visible effect"
fi

step "typed-error round-trips — every failure must report ok:false + a repair hint (harness-first gate)"
# Each of these is a KNOWN-bad call; the harness must catch it with a typed error, not a silent no-op.
err_case() {  # err_case <label> <expect-substring> <subcommand...>
  local label="$1" want="$2"; shift 2
  local out; out=$("$G" "$@" 2>&1)
  local ok; ok=$(jget "$out" '.ok' 2>/dev/null)
  local hint; hint=$(jget "$out" '.hint' 2>/dev/null)
  if [ "$ok" = false ] && [ -n "$hint" ] && [ "$hint" != null ] && grep -qi "$want" <<<"$out"; then
    pass "$label -> typed error (hint present)"
  else
    bad "$label -> expected ok:false + hint matching '$want', got: $out"
  fi
}
err_case "setparam bad child"     "does not exist"        setparam 99999 Foo 1.0
err_case "setparam bad slot"      "not found"             setparam 7 NoSuchSlot 1.0
err_case "setparam non-number"    "not a number"          setparam 7 PointSize abc
err_case "connect from an input"  "INPUT, not an output"  connect 7 points 2 emit
err_case "connect type mismatch"  "does not exist\|mismatch\|not found" connect 1 points 7 PointSize
err_case "disconnect no wire"     "no wire feeds"         disconnect 8 Amplitude
err_case "readpixel out of bounds" "out of bounds"        readpixel 99999 99999
err_case "spawn unknown symbol"   "not a known library"   spawn ZZZDefinitelyNotASymbol
err_case "unknown subcommand"     "unknown subcommand"    frobnicate

echo
if [ "$FAIL" = 0 ]; then
  printf '[demo] \033[32mPASS\033[0m — graph data-verb closed loop complete (coordinate-free, --json only)\n'
  exit 0
else
  printf '[demo] \033[31mFAIL\033[0m — see the FAILs above\n'
  exit 1
fi
