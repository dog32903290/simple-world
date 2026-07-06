---
name: sw-graph-harness
description: Drive simple_world's node graph from the CLI — spawn/connect/setparam/delete nodes and read back pixels, ALL coordinate-free through JSON, then verify the render numerically. Use when you must weave or mutate a patch in a running simple_world and prove it worked WITHOUT clicking canvas coordinates or eyeballing a screenshot. Triggers on 織 patch / 加節點 / 連線 / 改參數 / 刪節點 / 驗證畫面 via CLI, "build a graph programmatically", "verify the render numerically".
---

# sw-graph-harness — coordinate-free graph patching for simple_world

You (any Claude, no prior-session context needed) can build and verify a node patch in a **running**
simple_world entirely through one CLI, `tools/sw_graph.sh`. Every subcommand prints **one JSON object**
to stdout. You never touch a canvas coordinate and never OCR a screenshot — you drive by node **ids and
slot names** read from the graph, and you verify by reading a **pixel number** off the render texture.

## When to use / when NOT to
- USE when: adding nodes, wiring them, setting Float params, deleting nodes, and checking the render —
  programmatically, in a headless-ish loop, with machine-checkable results.
- Do NOT fall back to the mouse `hand`/`sw_drive.sh do click ...` path for these operations. The whole
  point of this harness is that graph structure is addressed by **id**, not pixel position. If you find
  yourself computing a canvas coordinate to add or wire a node, stop — use `spawn`/`connect` instead.
- Do NOT use for things this harness does not cover (see **Known limitations** below): String params,
  List/Dict params, multi-input fan-in ordering, gestural UI (drag-select, rename dialogs). Those still
  need the mouse `hand` layer or are simply unsupported today.

## Entrypoint & backend
- Command: `tools/sw_graph.sh <subcommand> [args]` (repo-relative; run from the repo root).
- Backend: the **real** running `app/build/simple_world` — the harness talks to it through the app's
  `.eye` sentinel dir (writes `req_graph`/`hand`, reads back `graph.json`/`readpixel.json`). There is no
  mock. If the app isn't running, every mutating subcommand returns a typed `app not responding` error.
- Eye dir: `<repo>/app/.eye` by default; override with `SW_EYE_DIR` to point at another instance.
- Round-trip timeout: `SW_GRAPH_TIMEOUT` seconds (default 5). It waits on the output file's **mtime**
  (never a fixed sleep) so it returns the instant the app answers and errors loudly if it froze.
- `jq` must be on PATH (the harness reads the eye JSON with it).

## Starting / checking the app
```bash
tools/sw_graph.sh status          # {"ok":true,"alive":true,"pid":[...],"compound":{"id":"Root",...}}
tools/sw_graph.sh launch          # start a repo-scoped app (kills stale repo instances first)
tools/sw_graph.sh launch my.swproj  # ...opening a project (repo-relative path)
tools/sw_graph.sh kill            # stop ONLY this repo's app (CWD-attributed; never pkill-by-cmdline)
```
If an app is already running (e.g. the user has it open), skip `launch` and just `status`. `launch`/`kill`
are for a self-contained run where you own the app's lifecycle.

`launch` takes ~8-10s to return (Metal + CJK font-atlas init before the eye first answers) — this is
normal, not a hang. It fully detaches the app (redirects its stdin/stdout/stderr), so `LR=$(sw_graph.sh
launch)` is safe in a command substitution and won't wedge. Run the harness as a **foreground** shell
command; the app persists across separate calls as long as you don't kill it.

## The weave → render → verify workflow
```bash
G=tools/sw_graph.sh

$G status                                   # confirm alive + which compound is current
$G graph                                    # read children[].childId, ports[], connections[] — your id source
NEW=$($G spawn RenderTarget)                # -> {"ok":true,"childId":104,"opType":"RenderTarget"}
RT=$(jq -r .childId <<<"$NEW")
$G connect 7 out $RT command                # wire DrawPoints#7.out -> RenderTarget#104.command (type-checked)
$G setparam $RT ClearColor.x 0.2            # Float override, SAME command the Inspector slider pushes
$G setparam $RT ClearColor.y 0.6
$G setparam $RT ClearColor.z 0.8
$G setparam $RT ClearColor.w 1.0
$G render                                   # -> {"ok":true,"path":".../clean.png"}
$G readpixel 5 5                            # -> {"ok":true,"r":51,"g":153,"b":204,"a":255,...}  VERIFY here
$G delete $RT                               # child + all incident wires, one undo unit
$G graph                                    # confirm childId gone AND no connection references it
```
**How to discover ids without coordinates**: `graph` returns every child's `opType` and every port's
`id`/`dataType`/`isInput`. Select the node you want by `opType` (`.graph.children[] | select(.opType=="DrawPoints")`)
and the slot by its `id`. Spawn returns the **new** childId (the underlying `spawnsymbol` verb returns
nothing, so the harness derives it by diffing the child list before/after — you get the id for free).

## Numeric verification (the payoff — no eyeballing)
A `RenderTarget` clears its whole texture to `ClearColor` before any draw. `RGBA8Unorm` is linear, so a
**corner** texel (where no geometry draws) equals `ClearColor` byte-exact:
`ClearColor (0.2,0.6,0.8) -> (round(.2*255), round(.6*255), round(.8*255)) = (51,153,204)`.
So `readpixel 5 5` returning `{"r":51,"g":153,"b":204}` is a **closed-form** proof the patch cooked and
rendered — a real number off the live texture, not a guess. Assert on it directly (±2 rounding slack).

## Success JSON examples
```json
{"ok": true, "alive": true, "pid": [75944], "compound": {"id": "Root", "name": "Root"}, "depth": 0, "eye": "/…/app/.eye"}
{"ok": true, "childId": 104, "opType": "RenderTarget"}
{"ok": true, "connected": {"src": 7, "srcSlot": "out", "dst": 104, "dstSlot": "command"}}
{"ok": true, "setparam": {"child": 104, "slot": "ClearColor.x", "value": 0.2}}
{"ok": true, "r": 51, "g": 153, "b": 204, "a": 255, "x": 5, "y": 5}
{"ok": true, "deleted": 104, "incidentWiresCleared": true}
```

## Failure JSON examples (typed error + repair hint — read `.hint`, then fix and retry)
```json
{"ok": false, "error": "app not responding (graph.json never refreshed)", "hint": "is simple_world running…? start it (… launch) or point SW_EYE_DIR at the live app's .eye"}
{"ok": false, "error": "setparam: child 999 does not exist", "hint": "run 'tools/sw_graph.sh graph' to see valid childIds"}
{"ok": false, "error": "setparam: slot 'NoSuchSlot' not found on child 104", "hint": "run '… graph' and read child 104 .ports[].id (Vec params expose .x/.y/.z/.w component ids)"}
{"ok": false, "error": "connect: type mismatch — src 'points' is Points, dst 'command' is Command", "hint": "a wire needs matching dataTypes; pick slots of the same type"}
{"ok": false, "error": "connect: src slot 'points' on child 7 is an INPUT, not an output", "hint": "wire FROM an output (isInput:false) TO an input (isInput:true)"}
{"ok": false, "error": "readpixel out of bounds: out of bounds (512x512)", "hint": "pick x/y inside the render texture dimensions"}
{"ok": false, "error": "spawn: 'ZZZ' is not a known library symbol (spawned a 0-port dangling child, rolled back)", "hint": "check the symbol id spelling; valid atomic op types and compound def ids are what the menu offers"}
```
Every subcommand exits **0** on `ok:true`, **non-0** on `ok:false`. Gate your script on the exit code
or on `.ok`.

## Artifacts & logs
- `app/.eye/graph.json` — last graph dump (children/ports/connections of the current compound).
- `app/.eye/readpixel.json` — last pixel readback `{x,y,r,g,b,a}` (or `{…,error}` on out-of-bounds).
- `app/.eye/clean.png` — last clean render (absolute path returned by `render`).
- The app itself logs to wherever you redirected it on `launch` (the harness sends it to /dev/null).

## Verify command (replayable end-to-end demo)
```bash
tools/sw_graph_demo.sh          # green: launches, weaves, verifies, deletes, re-verifies. exit 0 = loop closed.
tools/sw_graph_demo.sh --red    # RED FACE: asserts the WRONG pixel; MUST exit 1 (proves the check bites).
```
This is the closed-loop acceptance proof: it plays the exact workflow above with numeric assertions and
a red-face variant. Run it after any change to the harness.

## Known limitations & FORBIDDEN fallbacks
- **String / List / Dict / MultiInput params: NOT settable via `setparam`.** The `setparam` verb sets a
  single **Float** override (Vec components addressed as `.x/.y/.z/.w`, Enum by index, Bool as 0/1).
  There is no verb for text/list/dict values. Do NOT pretend you set a String param — say it's unsupported.
- **No multi-input fan-in ordering.** `connect` wires one src→dst; it does not control the order of
  multiple wires into a multi-input slot.
- **Spawn lands at a fixed canvas offset** (the node's screen position is not yours to choose — and you
  don't need it; you address the node by id afterward).
- **`enter` only descends into compound children.** On an atomic child it errors (`not a compound`).
- FORBIDDEN: falling back to mouse-coordinate drags (`sw_drive.sh do "click X Y"` / `drag`) to add or
  wire nodes. If `connect`/`spawn` can't express it, it's a real gap — report it, don't hand-drag.
- FORBIDDEN: claiming the render "looks right" without a `readpixel` number. If you can't name a
  closed-form expected pixel, say so; don't assert a visual you didn't measure.
- FORBIDDEN: `pkill`-ing simple_world by cmdline to clean up. Use `tools/sw_graph.sh kill` (CWD-scoped) —
  a cmdline pkill has repeatedly killed the WRONG app (main tree / a sibling worktree).

## Auto-trigger note (for 柏為)
This SKILL.md lives in-repo (`docs/agent/`), so it is documentation, not an auto-loaded skill. If you want
Claude to auto-trigger it, symlink it into `~/.claude/skills/sw-graph-harness/SKILL.md` yourself — I did
not install it into your global environment.
