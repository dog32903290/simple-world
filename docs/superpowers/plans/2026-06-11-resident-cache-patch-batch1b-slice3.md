# Resident Cache (Batch 1b first cut) + Incremental Patch (Slice 3 first cut) — Progress / Handoff

> **Status: ✅ DONE (two cuts, headless, additive).** 2026-06-11. Branch
> `codex/js-to-cpp-contract-migration`. This is a progress/handoff record (work already landed),
> not a to-run plan. Both cuts are headless, pure-CPU, NOT wired to production, NOT touching Metal.

**Goal of these two cuts:** make "resident" actually resident. Slice 1 flattens a nested
`SymbolLibrary` into a frame-stable `ResidentEvalGraph`; slice 2 cooks point buffers off it. But
the graph was rebuilt-able only by `buildEvalGraph` and re-evaluated from scratch every pull — so
the two reasons a resident graph exists over a per-frame-rebuilt one were both unfulfilled:

- **value not recomputed** (incremental evaluation / cache) → **batch 1b first cut**
- **structure not rebuilt** (incremental edit / patch) → **slice 3 first cut**

**Authority:** `external/tixl` @ SHA `395c4c55` (`docs/runtime/PARITY_TARGET.md` — do NOT pull).
**Blueprint (SSOT):** `specs/2026-06-10-compound-graph-design.md` — 承重決策 6 (version-chasing
dirty + per-output cache), 決策 7 (LIVE = always-dirty), 健檢二補 (patch version 規則組). The
slice ✅ notes there carry the canonical summary; this file is the longer handoff.

**Scope discipline:** the float **value graph only** (決策 6: 值圖 = eager 後序一趟, safe &
equivalent). Command/flow graph cache (pull-driven, Command-always) is deliberately untouched and
must not be contradicted (spec line 120 boundary). Engine `resident_eval_graph.*` (slice 1) and
production `cook` are zero-changed by both cuts.

---

## Cut 1 — Batch 1b: version-chasing dirty + per-output cache

**Files:** `app/src/runtime/resident_eval_cache.cpp` (+ decls in `resident_eval_graph.h`),
golden `resident_eval_cache_selftest.cpp`, `--selftest-residentcache`.

**Mechanism (TiXL `DirtyFlag` version-chasing, NOT content hash):**
- `ResidentOutputCache { baseVersion, sourceVersion, valueVersion, cachedFloat, isLiveSource }`
  lives ON the resident node, per output slot (C5; 拍板「節點 = slot」, not a parallel layer).
  `dirty == valueVersion != sourceVersion`. Initially dirty (valueVersion 0 != sourceVersion 1).
- **`baseVersion`** = the slot's OWN accumulated version (LIVE bump / edit-time push `++` this).
  Monotonic, never overwritten. **`sourceVersion = baseVersion + Σ upstream sourceVersions`**,
  recomputed each pull (multi-input combine = sum, so any input change dirties it). A leaf has
  upstreamSum 0 → sourceVersion = baseVersion. *(This base/sum split is the A4 fix — see Cut 2.
  The original was a pure `sourceVersion = upstreamSum` overwrite that erased a node's own version.)*
- `pullResidentFloat(g, path, slot, ctx)`: eager post-order, one pass. Recurses Connection inputs
  (always walks the cone — cheap), computes sourceVersion, recomputes + caches ONLY when dirty,
  else returns `cachedFloat` with no evaluate (the skip IS the win — 貴的靜態 op 算一次存著).
  An unresolvable upstream contributes a fixed version 1 (never 0 — D1 fix).
- `bumpLiveSources(g)`: `++baseVersion` for every `isLiveSource` slot, each frame (Trigger=Always,
  決策 7 / 🪤#1 per-frame invariant). `initResidentCache(g)`: per-output cache + isLiveSource
  (op declares always-dirty; slice scope = `Time`).

**Golden `--selftest-residentcache`** (all teeth): STATIC short-circuit (mutate an upstream const
WITHOUT a bump → stays cached 15, proving recompute is skipped), edit-push (bump → propagate sum →
27), LIVE per-frame (Time 14→35), dangling (orphaned upstream → computes 5, not frozen). `-bug`
skips `bumpLiveSources` → LIVE frozen at 14 (卡舊).

**Refuter (independent opus):** 5 SURVIVE (diamond / same-frame repeat pull / deep LIVE chain /
partial-dirty / sum-aliasing), 1 BROKEN+fixed:
- **D1** (`cecdaba`): a derived slot whose Connection upstream doesn't resolve summed to
  sourceVersion 0, colliding with initial valueVersion 0 → permanent false-clean (卡舊), even an
  edit-push couldn't rescue it. Broke TiXL's invariant (sourceVersion from 1, only ++, never 0).
  Fix: unresolvable upstream contributes fixed version 1.

**Commits:** `371e8ab` (feat) → `cecdaba` (D1 fix) → `badd58c` (spec). Mechanism later revised by
`5561e42` (A4, Cut 2).

---

## Cut 2 — Slice 3: incremental patch (the structural half)

**Files:** `app/src/runtime/resident_eval_patch.cpp` (+ decls in `resident_eval_graph.h`),
golden `resident_eval_patch_selftest.cpp`, `--selftest-residentpatch`.

**Two of the six S11 edits** (edit in place, preserve cache on untouched nodes, so patch == a
freshly rebuilt graph with the edit baked in):
- **`patchSetConstant`** (S1 value edit, `InputSlot.cs:57-63` / `ChangeInputValueCommand.cs:122`):
  set a Constant input's value, then `++baseVersion` on this node's outputs (edit-time push).
  Downstream goes dirty via the pull-time upstream sum; untouched siblings keep their cache.
- **`patchAddConnection`** (S11①, `Slot.cs:198-205`): rewire a Constant input to a Connection,
  then set `valueVersion = UINT64_MAX` sentinel (= TiXL `ValueVersion=-1`) to force a first-pull
  recompute. NOT a sourceVersion bump (would corrupt the derived sum — 健檢二補 ②).

**Golden `--selftest-residentpatch`** (all == rebuild): set-const (poison an untouched sibling's
const out-of-band, patch the other → 9×cached-3 = 27, NOT 9×99 — proves only the edited cone
recomputes), **derived-node value edit** (edit Multiply.b while Multiply.a is wired → 5×10 = 50),
add-connection (wire Time→Multiply.a → 7→35). `-bug` edits the constant skipping the patch's
invalidation → frozen at 15 (卡舊).

**Refuter (independent opus):** 6 SURVIVE (rewire / patch sequences / diamond / wrong-target /
multi-output over-invalidation / dangling-add), 1 BROKEN+fixed:
- **A4** (`5561e42`): `patchSetConstant` on a DERIVED node (one with a Connection input) was
  silently dropped — its `++sourceVersion` was destroyed at pull time by the `sourceVersion =
  upstreamSum` overwrite → stale value edit, patch != rebuild (15 vs 50). The golden missed it
  (it only edited a pure leaf). **Root cause in the 1b cache mechanism, not patch alone** — the
  overwrite discarded a node's own version contribution. Fix (root, not patch-local): split
  `baseVersion` (own, monotonic) from `sourceVersion = baseVersion + upstream sum`. Brings us
  closer to TiXL (SourceVersion is accumulated, never overwritten). This is why Cut 1's mechanism
  description above already reflects base/sum.

**Commits:** `b526e1f` (feat) → `5561e42` (A4 fix, also revises Cut 1's cache.cpp) → `415ce17` (spec).

---

## Verification (both cuts, at `5561e42`)
`--selftest-residentcache` PASS, `--selftest-residentpatch` PASS, both `-bug` variants FAIL (teeth).
Regression green: residenteval / residentcook / compoundmodel / graph / valuecook / pointgraph /
radialop. `check-arch` OK (all four new files are runtime leaves). File sizes healthy:
cache.cpp 118, patch.cpp 45, resident_eval_graph.h 150 (all < 400).

## Named-deferred (not silently dropped)
- **Slice 3 rest:** the other four S11 edits — disconnect (③ restore prior update action +
  ForceInvalidate, `Slot.cs:233-245`), add/remove child, change-definition-default (IsDefault
  filter, `Symbol.Child.cs:677-698`), IO change (orphan-connection cleanup); the full six-edit
  `patch == rebuild` golden; per-output precise invalidation (currently bumps ALL outputs);
  topological-order robustness for compound siblings.
- **1b rest (Command/flow layer):** Command-always (C2, `_valueIsCommand`), the four op primitives
  (trigger dirty-as-event / Loop re-eval / ForceInvalidate external push / stateful FxTime
  time-gate), diamond count-based selftest, one-pass == TiXL two-pass golden, TimeClip time-remap
  re-entry, automation-driven LIVE (S3 curve store), derived-and-LIVE.
- **Not started:** production swap + GPU buffer cross-frame cache (cookResident) = batch 2 / slice 4.

## Resume (next cut — pick one)
1. **Slice 3 rest** (recommended): finish the structural half — the other four S11 edits + the
   six-edit patch==rebuild golden. "Disconnect" brings in spec ③ (restore the pre-connection update
   action + ForceInvalidate), an un-touched load-bearing line. The patch machinery is hot and its
   version rules are refuter-verified for the first two edits.
2. **1b rest** — the Command/flow graph layer (pull-driven, Command-always, four primitives). Higher
   value once cookResident's cache lands, but the objects live in the GPU/stateful graph (harder to
   test headless).
3. **Slice 2b** — cmd/texture executor parity for cookResident + stateful op state on resident nodes
   (production-swap prep).

---

## Cut 3 — Slice 3 REST: the remaining S11 edits (2026-06-11, second session) ✅

**Files:** `resident_eval_patch.cpp` (+`patchRemoveConnection`), NEW `resident_eval_patch_lib.cpp`
(definition-level broadcast: `patchLibSetDefault` surgery w/ IsDefault filter;
`patchLibAddChild/RemoveChild/RemoveInputDef` = lib edit + `rebuildWithCacheMigration` — ONE
canonical wiring codepath, 3 migration rules incl. Connection-RESOLVABILITY as an input-diff),
golden `resident_eval_patch_lib_selftest.cpp`, `--selftest-residentlibpatch` (11 asserts, all
patch == rebuild + cache probes; `-bug` teeth).

**New invariant (generalizes D1/A4):** a slot's sourceVersion must NEVER DECREASE across an edit
*sequence* — disconnects ABSORB the dropped upstream contribution into baseVersion; migration
rule 2 uses the monotonic floor `max(baseVersion, sourceVersion)+1` AND mirrors it into the
sourceVersion field (the field only refreshes on pull — back-to-back edits with no pull between,
i.e. a batch-4 command group, read it stale).

**Refuter (independent, EXECUTABLE repros):** 8 survive, 4 BROKEN — all fixed, each repro now a
golden: A-1 stale-field regression (editSeqNoPull), A-2 stale kept-default under a wire
(keptDefault=27), A-3 set-constant dropped on wired slots vs TiXL SetTypedInputValue
(wiredStore=21), A-4 compound setDefault silent lib/g desync (compoundDefault=720).

**Named contract duty (now in code comments):** resident-level patches edit the PROJECTION only —
the command layer must pair the matching lib edit, or a later structural patchLib* discards it.

**Named-deferred:** per-output precise invalidation; compound-child AddChild (recursive inline);
isLiveSource OR-stickiness under future type-swap edits; per-edit surgical patch as a later
optimization over O(graph) migration (semantics are pinned by the goldens).

## Resume (next cut — pick one)
1. **1b rest** — Command/flow layer: Command-always (`_valueIsCommand`), the four op primitives
   (dirty-as-event / Loop re-eval / ForceInvalidate push / stateful FxTime gate), count-based
   diamond selftest split by type (value=1/pass, Command=per-pull).
2. **Slice 2b / production swap prep** — cmd/texture executor parity for cookResident + stateful
   op state on resident nodes; converge cook/cookResident (also pays the point_graph.cpp 477-line
   arch debt).
3. **Batch 2 存檔 v2** — symbols[] library + two-phase load + migration (schema per the spec's
   健檢修正 S15-S20 block).

---

## Cut 4 — Slice 2b + PRODUCTION SWAP (2026-06-11, third session) ✅

Goal re-anchored by 柏為: 把 compound editor 體驗做完 (decisions follow TiXL, no asks). Chosen
route: 2b → swap (option 2), because every editor-experience batch (導航/combine/render) needs
production actually running on the resident graph.

**Slice 2b (`df88aa1`):** the resolved-param seam — drivers pre-resolve ALL Float ports
(flat `resolveNodeParams` full spine / resident `resolveResidentFloatInputs` drivers), ops read
`cc.params` via cookParam/cookVecN/cookInputParam and never touch a graph model. Kills the
wire-blind param class across all ops; force params travel with the wire (no more firstOfType);
cookResident gets the full three-flow terminal + per-path persistent buffers + stateful state
(Impl keys converged to strings: path / "#id"). point_graph.cpp 489→343 (debt paid; Impl+regs →
point_graph_internal.h, resident → point_graph_resident.cpp). Teeth caught a spec gap:
RadialPoints' RadiusOffset/StartAngle/Cycles weren't spec ports → appended (NOT inserted — pin
ids are port-index based; insertion re-targets saved wires; v2 schema moves to slot ids).
Golden `--selftest-residentparity`.

**Refuter (independent opus, executable repro):** 1 BROKEN fixed — `ensureState` never resized
on count growth → GPU OOB over the sim's persistent particle buffer (production-reachable via
Count drag). Fix: re-create state when count grows (mirror of ensureOut). Promoted golden
`--selftest-statecount` (flat+resident legs). Plus 2 alignments: resident Automation stub now
falls back to the projected constant (== flat's fall-through; S3 can't inherit a divergence);
vec-inputDefs omission closed structurally by the bridge generating inputDefs from NodeSpec.

**Production swap (`1e64afe`→`635e1c1`):** `graph_bridge.{h,cpp}` (`libFromGraph`: flat → lib,
child id == node id → paths == ids → per-path GPU state survives rebuild; doubles as batch-2's
old-file importer). `ResidentNode::extOut[3]` mirrors flat outCache for AudioReaction.
`app/frame_cook.{h,cpp}` (main back to 333): mirror rebuild-on-revision → AudioReaction cook →
`cookResident`. **The live app no longer runs flat cook.** Mirror contract: every g_graph
mutation bumps `doc::graphRevision()` (commands + doOpen/doNew + 2 Inspector live-drag sites).
Golden `--selftest-graphbridge`: real default graph + Const→Radius + AudioReaction→Speed wires,
3 frames, flat vs resident BYTE-IDENTICAL (stateful GPU sim included). Full sweep 30+ green,
all -bug teeth bite.

**✅ Live smoke (display woke 12:16, eye/hand driven):** resident cook renders + animates
live (clean.png readback, frames differ); GUI add-node (Const) → revision → mirror rebuild ✓;
GUI wire Const.out→RadialPoints.Radius → picture changed the SAME frame (206→69 lit — the
resolved-param seam live in production) ✓; Cmd+Z removed the wire and the SIM STATE SURVIVED
the rebuild (per-path identity working as designed) ✓; RadialPoints' new ports + AudioReaction
live meter visible in the canvas. (Node-click selection missed = the known zoom≠1 map-drift
trap, unrelated to the swap.) Diagnosis recorded: display asleep → window server gives MTKView
no display link → app can't tick in background; check `pmset -g log | grep Display` FIRST.

**Named-deferred:** command layer pairs patch*/patchLib* instead of rebuild (semantics pinned
by patch goldens); cookResident → pullResidentFloat (consume the 1b float cache + bumpLiveSources
per frame); S1 SourceRegistry 收編 (AudioReaction LIVE authority to definition layer);
defaultDrawTarget/viewTarget still read flat (shell-level, dies with g_graph).

## Resume (after Cut 5)
~~批次 2 存檔 v2~~ ✅ DONE same session (Cut 5 below).

---

## Cut 5 — 批次 2 存檔 v2 (2026-06-11, same session) ✅

`runtime/compound_save.{h,cpp}` + golden `--selftest-savev2` + app wiring (doSave writes v2,
doOpen reads v2+v1 with S15 repair warnings) + `graphFromLib` inverse (transitional flat-editor
leg). Key decisions: v2 serializes ONLY compounds (atomics = registry + fixed UUID refs, 決策 4,
TiXL-isomorphic); S16 self-describing compound defs (array order == definition order); S15
local-drop tolerance (whole-file failure only for unparseable JSON); v1 auto-migrates via
libFromGraph. Refuter: 4 BROKEN fixed (NaN-safe writer / sw-type: namespace hijack -> compound-
first resolution / inverse contract made honest = SEMANTIC roundtrip, conn ids normalize / dup-id
first-wins), repros promoted to golden legs (nanClamp/nsNoHijack/oddIdSemantic).

**⚠ Named risk for 批次 4:** crude_json asserts (debug abort) on non-ASCII at PARSE — a CJK
compound name (柏為 WILL type one) = a file that kills the load. Resolve before combine ships
user-named symbols: escape on write, or swap/patch the parser, or sanitize names.

**Live smoke of the production swap** also completed this session (see the amended Cut 4 note):
add-node/wire/undo via eye+hand, picture reacts same-frame, sim state survives rebuilds.

## Cut 6 — 批次 3 N1: compounds as operators ✅ (2026-06-11, `ec92d77`)

Architecture decision for 批次 3 (照 TiXL, no projection layer): the canvas reads the CURRENT
Symbol directly — TiXL's GraphCanvas renders Symbol children, composition switch = same canvas
different symbol. The flat Graph/g_graph/mirror/graphFromLib all die at the end of this batch.

N1 (landed): `specFromSymbol` + `refreshCompoundSpecs` + findSpec dynamic-table fallthrough
(built-ins win on clash). A compound child now resolves ports/inspector exactly like an atomic
node. Golden `--selftest-compoundspec` (+teeth).

## Cut 7 — 批次 3 N2: lib-native document ✅ (2026-06-11)

The flat `Graph g_graph` DIED as the editing model. doc = `g_lib` (SymbolLibrary) +
`g_compositionPath` (root-only in N2; `currentSymbol()` walks it, truncating dangling
prefixes). The cut had to carry the minimal canvas re-point too (commands edit the lib →
a canvas still reading g_graph wouldn't even compile): editor_ui/node_draw render
`currentSymbol()->children`, int pin scheme unchanged on child ids. TiXL shape exactly:
canvas reads Symbol, commands edit Symbol, NO flat layer between.

What landed:
- **Commands rewritten as lib edits** keyed by (lib, symbolId) — AddChild (lazy-imports the
  referenced atomic Symbol from the registry, 照 TiXL "a child always references an existing
  Symbol") / DeleteChildren (+incident wires, (index,item) snapshots both arrays) / AddWire /
  DeleteWires ((index,wire) snapshot = multi-input order survives undo) / SetOverride
  (hadOld ? restore : ERASE — definition defaults never polluted by 0-residue) / MoveChildren.
  `--selftest-command` rewritten against the lib (incl. child-ORDER restore + non-root-symbol
  keying + override-erase).
- **frame_cook lib-native**: resident graph built straight from doc::g_lib on
  doc::libRevision() change (graphRevision renamed; rebuild-on-revision stays, patch wiring
  later). AudioReaction cooker iterates RESIDENT nodes (params via resolveResidentFloatInputs
  — registry defaults verified == old hardcoded fallbacks), state keyed by PATH, extOut read
  back by the UI face through `framecook::residentOut(path)`.
- **save/load straight lib<->v2**: doOpen's graphFromLib refusal died — files with compound
  children now open. graphFromLib survives selftest-only (migration goldens). isDirty =
  libToJsonV2 byte-compare.
- **UI re-point**: node_draw draws SymbolChild; node_faces reads effectiveInput + residentOut;
  inspector (split to ui/inspector.cpp, 鐵律 4) edits overrides; stateless 64-bit link ids
  (srcPin<<32|dstPin — SymbolConnection rightly has no id); output_window/main viewTarget via
  currentSymbol; eye state.json now {selectedNode, compositionPath, lib:<v2>} (agent tooling
  note: `.graph` is GONE from state.json).

**Refuter (independent agent): 4 BROKEN all fixed, repros promoted where testable:**
1. Inspector UAF — per-push refreshCompoundSpecs swapped the dynamic spec table mid-frame
   under live NodeSpec*; refresh moved to frame boundary (frame_cook rebuild branch).
2. child-id reuse — nextChildId(max+1) resurrected dead per-path state (GPU sim buffers /
   AudioReaction hitCount). Fix: Symbol.nextChildId monotonic floor, serialized in v2
   (tolerant: absent → max+1), v1 migration carries Graph.nextId; AddChildCommand burns ids
   (undo does NOT lower). Proven live: add(104)→delete→add = 105. Cost (accepted, == old
   flat nextId behavior): add+undo leaves the counter bumped → dirty star stays on.
3. DeleteChildren undo appended children (order lost → byte-dirty forever) — (index,child)
   restore, golden asserts child order.
4. Vec DragScalarN live-write materialized overrides for UNTOUCHED components (no undo
   entry) — only write moved components; + erase-residue when a drag releases at its exact
   start value (scalar + vec).
SUSPECT deferred, named: currentSymbolId() truncates the path inside a getter (dead in N2,
N3 must validate the path once per frame at a defined point); CommandStack hardwires the
global doc (single-document world); per-path op state for deleted paths leaks until app
close (pre-existing, bounded; prune when it matters).

43 selftests green, all -bug variants bite, check-arch OK. Live (eye/hand): add Const →
lib child; drag wire Const.out→ParticleSystem.Speed → particles FREEZE same-frame (Const
default 0); Cmd+Z → wire gone, particles move again, sim state alive (not reset).

## Resume — 批次 3 remaining cuts (in order)
- **N3 (ui): composition navigation.** Canvas already iterates currentSymbol() (N2 carried
  it); N3 adds: double-click a compound child pushes compositionPath; breadcrumb bar pops;
  per-layer view state (ed config save/restore or one EditorContext per visited symbol — TiXL
  uses one canvas + swapped view; check ed::Config). viewTarget already cooks
  doc::residentPathFor(childId) — nested paths come free. ⚠ named from refuter: validate/
  truncate g_compositionPath ONCE per frame at a defined point (currently a getter side
  effect — two panels could see two symbols within one frame after a mid-frame delete).
  Boundary-sentinel wires need their pins drawn (canvas currently skips them; root has none
  but compound interiors will).
- **N4: 眼手驗 + goldens.** Enter subgraph -> exit -> canvas correct (req_state already
  carries compositionPath); add/wire/undo INSIDE a compound; reuse: edit definition once,
  both instances change on screen (柏為 完成定義 item). Inspector UAF repro from refuter #1
  becomes live-testable here (compound with Float inputDef + slider drag).
- Then 批次 4 combine (boundary detection + create Symbol + rewire; ⚠ CJK-name crude_json
  named risk must be resolved before user-named symbols; generated ids must avoid the
  "sw-type:"/uuid namespaces), 批次 5 cross-layer undo + reuse 收尾.
