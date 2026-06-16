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

## Cut 8 — 批次 3 N3: composition navigation ✅ (2026-06-11)

TiXL semantics researched at source (ProjectView/_compositionPath, GraphStates double-click,
GraphTitleAndBreadCrumbs, InputNode/OutputNode boundary items, ViewedCanvasAreaForSymbolChildId):
- **Gestures (照 TiXL MagGraph)**: double-click a COMPOUND child = pushComposition (atomics
  refuse); double-click background = popComposition; breadcrumbs in the toolbar jump to any
  level. Selection + pin CLEAR on every switch (bare child ids alias across symbols).
- **doc**: currentSymbolId() now a PURE walk (validPathPrefix); truncation happens ONCE per
  frame in validateCompositionPath (frame_cook frame start) — the N2 getter-side-effect
  SUSPECT closed. push/pop/truncate validate first. residentPathFor walks the VALID prefix.
- **Boundary items**: a compound's own inputDefs/outputDefs draw as movable canvas nodes
  (= TiXL Input/OutputNode); SlotDef grew x/y (serialized in v2). Pins ride
  pinId(0, combinedIndex) (1..99, disjoint from child pins >= 101); ed node ids negative.
  inputDef = source inside, outputDef = sink; sentinel wires now draw + create + delete +
  undo through the same command path. Boundary items NOT deletable (def removal = S13, 批次5);
  positions sync directly (no undo step — named asymmetry). Per-instance view-area memory
  (TiXL UserSettings) simplified to NavigateToContent-on-switch (= TiXL's no-saved-view
  fallback) — named simplification, revisit if 柏為 misses it.
- **viewTarget became a resident PATH** (frame_cook::run(pg, targetPath)); fallback chain
  pinned -> selected -> current symbol terminal -> ROOT terminal, so entering a compound
  with no realizable child keeps showing the composition's picture (TiXL: navigation never
  blanks the output window).
- **--open <file> CLI seam** (doOpenPath, quiet failure to stderr): 柏為-adjacent direct
  open AND the agent's only dialog-free test-file loader.
- New golden `--selftest-navigation` (+teeth): push/pop/truncate, atomic-refuse,
  dangling-trim (pure getter vs validator), self-nesting refusal.

**Refuter: 3 BROKEN + 3 SUSPECT, all fixed, all live-verified:**
1. B1 `--open` bad path hung forever in pre-NSApp NSAlert runModal (stack-sample proven) →
   quiet stderr failure + default-doc fallback (proven live: app boots, 2 stderr lines).
2. B2/S3 pin/selection alias across switch (same child id, different symbol → viewport
   silently shows wrong node) → cleared on every nav trigger (proven live: selected 1 -> 0).
3. B3 residentPathFor used the un-validated path; main resolves the cook target BEFORE the
   frame validator → one black frame after an ancestor-undo → valid-prefix walk + ops
   validate first.
4. S2 entering a SELF-NESTED compound instance = permanent black (resident build's S14 guard
   skips the subtree; the "current terminal" branch wins over the root fallback) →
   pushComposition mirrors the S14 guard (golden leg added).
5. S1 ≥101 combined defs alias boundary pins onto child 1 (crafted file only) → load-time
   limit (99) with S15 local-drop warnings.

45 selftests green + all -bug teeth + check-arch. Live (eye/hand, hand-crafted
/tmp/nav_test.swproj with an Emitter compound): --open loads, double-click enters
(boundary in/out items + breadcrumbs appear), picture stays alive inside (root-terminal
fallback), background double-click exits, breadcrumb Root jumps out, wire boundary->child
Count lands in the lib as (0,Radius,1,Count), Cmd+Z removes it inside the compound.

## Cut 9 — 批次 3 N4: 眼手驗收 + goldens ✅ (2026-06-11) — 批次 3 CLOSED

Checked-in drill asset `app/testdata/compound_smoke.swproj` (TWO EmitterComp instances ->
CombineBuffers -> ParticleSystem <- Turbulence -> DrawPoints; inputDef Radius def=3.0 — ON
PURPOSE ≠ registry default 2.0, so a dead boundary binding can't pass as "default flowed").
Golden `--selftest-testproj` (+teeth): zero-warning load (asset-rot guard), reuse isolation
through the REAL file (3.0/4.0), viewProducerPath legs incl. the S14-mirror recursion case.
Inspector grew one-line eye hooks (`param:<id>` rects) — the hand can drive sliders forever.

**The drill itself found a real bug**: selecting/pinning a COMPOUND instance cooked its own
resident path — which doesn't exist (compounds inline away) -> black viewport. Fix =
`viewProducerPath` (runtime): viewing a compound child resolves to its first outputDef's
producer, recursively; empty -> terminal fallback. main's chain became sequential
fall-through (pinned -> selected -> current terminal -> root terminal).

**Refuter: 1 BROKEN (reproduced with a scratch binary) + 1 low SUSPECT, fixed:**
1. viewProducerPath lacked the S14 symbol-on-path mirror — a MUTUAL-recursive chain ending
   at an atomic returned a non-empty path the builder skipped -> bypassed the fallback ->
   sticky black while pinned. Fixed (chain collected during prefix walk; golden leg added:
   in-code A⊃B⊃A lib, view-from-inside returns "", legit path "9/2" still resolves).
2. Combo recordItem grabbed the open popup's last Selectable rect (hand mis-click risk) ->
   pre-widget rect via GetCursorScreenPos+CalcItemWidth.

**柏為 完成定義 item — PROVEN LIVE (eye/hand, ASan on):**
- instance override: select Em#2, drag Radius slider -> only ITS ring changes (em1
  untouched), undo restores the old override value; the drag+release IS the N2 UAF repro
  scenario — ASan silent.
- definition edit: enter Emitter via instance 1, drag inner RadialPoints Count to min,
  exit -> WHOLE picture collapses 10181->96 bright px (both instances; one-instance-only
  would leave ~half), Cmd+Z FROM ROOT undoes the definition edit (commands keyed by
  symbolId work cross-composition) -> erase path -> picture recovers.
- compound preview: selecting an Emitter shows its inner producer's points preview (3376
  bright), deselect -> terminal sim picture (27623). Never black.

46 selftests green + all -bug teeth + check-arch.

## Cut 10 — 批次 4: combine ✅ (2026-06-11)

**Prerequisite landed first — crude_json UTF-8 (the named CJK risk, root cause sharper than
filed):** the writer was fine (raw UTF-8 out); the PARSER died — `const char*` peek sign-
extends bytes ≥0x80 on arm64 (string parse fails wholesale) and the \uXXXX path asserted
c<128. sw-patch(utf8) in vendored crude_json: unsigned peek, raw bytes pass through, \u
escapes encode to UTF-8 incl. SURROGATE PAIRS (refuter: standard tools like python
json.dumps emit non-BMP as pairs — a file run through them must not die). Goldens in
--selftest-savev2: CJK raw byte-stable, 中文 decodes, 😀 -> 😀.

**Combine (照 TiXL Combine.cs, research at source; forks named in combine.h):**
- runtime/combine.{h,cpp}: moves selection into new "Compound-N" (ASCII id outside
  sw-type:/uuid namespaces; name = user UTF-8); child ids regenerated 1..N; overrides
  verbatim; internal wires remapped; inbound crossing -> inputDef (id from target slot,
  deduped; def from target's SlotDef — fork, TiXL uses type default) + boundary wire +
  parent rewire; outbound -> one outputDef per DISTINCT (src,slot) (fork: TiXL one per
  connection) + N rewires; parent gets ONE instance at selection bbox center; refused
  >99 defs (boundary pin scheme limit, mirror of the loader guard — refuter #1).
- doc::doCombine: NOT undoable (照 TiXL UndoRedoStack.Clear) — clears our stack; spec
  refresh stays at the frame boundary (N2 UAF lesson). COST (named, TiXL same): moved
  children's resident paths change -> per-path sim state resets on combine.
- ui/combine_dialog.{h,cpp}: right-click node -> context menu (ed::Suspend) -> modal name
  dialog; failure keeps the dialog + shows the reason; success clears ed's stale selection.
- Goldens: --selftest-combine (+teeth): structure on the real default graph (CJK name,
  2 children/3 wires/1 in/1 out def), eval-identical pre/post (Const->Multiply), v2
  roundtrip, 99-cap refusal leaves the lib untouched.

**Refuter (probe binaries, 17 probes): 1 BROKEN (99-def cap) + 1 SUSPECT (surrogate pairs)
fixed + UI nits (stale ed selection / failure wipes name) fixed; parity drifts named in
combine.h. Confirmed safe: parent-boundary crossings eval-identical through double
indirection; combine cannot CREATE self-nesting; dup slot names pair correctly.**

Live (eye/hand): right-click Turb -> Combine -> Compound-1 in lib; enter it (boundary
items + child correct); full user loop open -> combine -> toolbar-Save -> restart --open ->
clean reload (zero repair warnings), picture alive. 47 selftests + teeth + check-arch.

**Drill 經驗 (agent notes):** native menu accelerators (Cmd+S) are NSMenu-level — the hand
cannot reach them, drive the toolbar buttons instead; floating windows (Output/Inspector)
eat right-clicks — pick node spots clear of them; popup items need hover-frame before the
click (first click after open may miss).

## Cut 11 — 批次 5: S13 def removal + CJK atlas + compounds-in-Add-menu ✅ (2026-06-11,
`0b67ca5`→`6d191de`; dispatcher-mode session: opus implementers/refuters, Fable review+commit)

**S13 def removal (`0b67ca5`) — boundary items deletable, 照 TiXL UNDOABLE (fork from combine):**
TiXL research overturned the working assumption: `RemoveInputsOrOutputsCommand.IsUndoable => true`
(Editor/UiModel/Commands/Graph/RemoveInputsOrOutputsCommand.cs:22); combine's history-clear reason
(orphaned new symbol) does NOT apply to def removal (creates nothing). ONE lib-surgery codepath
`removeInput/OutputDefFromLib` + `restoreSlotDefToLib` (compound_graph): def + inner sentinel wires
+ lib-wide parent wires ((index,wire) capture, multi-input order) + instance overrides; snapshot
restore = byte-faithful undo. `patchLibRemoveInputDef` collapsed onto the same codepath (-39);
+`patchLibRemoveOutputDef`. Mixed delete macro children-first (照 Modifications.cs:184-191; the
capture-in-Do rationale survives macro REVERSAL too — per-command snapshots are self-symmetric).
Golden `--selftest-defremoval` 7+2 legs (+teeth). Refuter (6 ASan probes): 7 SURVIVE, **1 BROKEN
fixed = ZOMBIE OVERRIDE**: loader never scrubbed overrides keyed to dead defs (wires yes,
overrides no) → never self-heals, resurrects into eval if same-id def returns (10→495 probe).
Fix: phase-2 scrub mirrors wire scrub (drop+warn+self-heal); the golden's false comment
("effectiveInput ignores it" — it does NOT) rewritten; teeth proven by neutering production scrub
→ RED.

**CJK font atlas (`b31b104`) — the named gap closed:** ui/cjk_font.{h,cpp}, system font MergeMode
over default (ASCII metrics bit-identical), GetGlyphRangesChineseFull 21484 glyphs, atlas
2048x4096 +8.36MB. Terrain fact: NO PingFang.ttc on this machine → candidate chain landed on
STHeiti Light.ttc face 0 (real Heiti TC); 52-byte Arial-Unicode redirector stub guarded by size
check; all-fonts-missing → ASCII fallback + one stderr line. Golden `--selftest-cjkfont`
(FindGlyph 中灣體測 / ASCII unpolluted / fallback; -bug stays RED even on font-less machines).
Worktree-built (zero file overlap with S13), patch applied post-N1. ⚠ atlas build ~2.6s at
startup under debug+ASan (named, accepted; release will shrink).

**Compounds in Add menu + cycle guard (`fe3ac5a`) — reuse GUI path opened:** toolbar popup grew a
compound section (s.atomic flag, Separator, name||id label — CJK renders now, eye key = ASCII id).
TiXL truth: cycle prevention is UI-FILTER-ONLY on the OPEN ancestor chain (SymbolFilter.cs:107-120),
core AddChild unguarded (Symbol.Instantiation.cs:14) — i.e. TiXL itself can be tricked into
transitive cycles. Named fork (spec-mandated, S14 silent-skip is the cost of a miss):
`addChildWouldCycle` = transitive subtree reachability vs edit target, DFS+visited (total on
already-cyclic libs). Grey-out+tooltip instead of TiXL's omission (flat menu: omission reads as
"missing"). Three gates: menu grey / addNode pre-push hard refuse + status line / command doIt
defensive early-out (did_ flag, no no-op on undo stack). Goldens `--selftest-cycleguard` (+command
legs: refused push = libToJsonV2 byte-identical, undo inert). Narrow-charter refuter: BROKEN 0 —
every SymbolChild write-path constructively acyclic or gated; the ungated loader is NAMED behavior
(cyclic file accepted silently; 4-topology ASan probe: build/view/save/reload/gate all total,
roundtrip byte-stable, S14 skips only the cyclic child, siblings survive).

**Live acceptance (eye/hand drill, 6/6 PASS)**: CJK titles real glyphs; S13 delete live (render
band moved, status spoke) + same-layer undo; **cross-layer undo from ROOT restores an inner def
edit**; reuse heart-loop: Add-menu 3rd EmitterComp instance → inner def edit → ALL THREE rings
change (1732→80433 px) while instance override survives → undo chain; cycle guard greys + no-op
clicks; full save→kill→reopen zero-warning loop. Drill found **boundary single-click+Delete dead**
(box-select worked) → fixed same session (`6d191de`): root cause was ONE bug not two —
pinId(0,0)=1 collided with child node id 1 in imgui's ID pool → conflict tooltip stole focus →
IsWindowFocused gated the delete key out. (The "boundary never enters ed selection" theory was the
collision's artifact; vendored selection is id-agnostic.) Fix: kBoundaryPinBase=1<<20 own band,
decode helpers boundary-aware, disk untouched (slot strings). 99-cap behavior kept; comments now
honest (OUR conservative ceiling — TiXL has NO port cap; no longer an encoding constraint).

**Dispatcher-mode notes (this session's working method, 柏為-sanctioned):** big chunks → opus
implementers with full-contract work orders (spec text verbatim + TiXL pointers + laws + traps);
independent opus refuters per load-bearing cut (narrow charters for narrow surfaces); Fable reads
every diff, spot-runs evidence, owns forks + commits. Token cost ≈ 1.27M subagent + lean main
context. New traps for the next agent: **zsh does NOT word-split unquoted $vars** (a multiline
selftest list became ONE arg → app booted GUI silently); **unknown --selftest-* flags boot the GUI
instead of erroring** (looks like a hang; kill it) — worth a 3-line arg-guard next batch.

## Cut 12 — 批次 6: copy/paste + rename + S3 curves + 粒子衰減根治 ✅ (2026-06-11 晚,
`28145b4`→`44c210d` 六 commit; dispatcher-mode 2nd run: 7 opus 工單 + Fable review/forks/commits)

**copy/paste (`28145b4`+`9c42882`, 契約 4 第三刀):** runtime/copy_paste extract(兩端內選/外線剪)
+clipboard JSON(transient 不進 registry, imgui OS clipboard)+planPaste(oldToNew/環檢丟 self-nest)。
Fork 四條全過 refuter 覆核: **multi-input 不 reverse**(TiXL reverse 補償其 insert-at-front, 我們
append 模型=combine 同招)/overrides=全 per-child 狀態(bypass/outputData 欄位未生, 接縫具名)/floor
燒掉 undo 不降(jsonNoFloor 證實質 byte-identity)/paste 傳遞環檢比 TiXL 強(既定 fork)。GUI 雙路:
Cmd+C/V(io.KeyCtrl; **活體判決: NSMenu 不攔, 通**)+node/背景右鍵。Refuter-A 9 probe: 8 SURVIVE,
**1 BROKEN 修=敵意 clipboard abort**(type-confused array 元素直通 crude_json operator[] 的
std::terminate, 不受 NDEBUG 保護 release 也炸, Cmd+V production 可達 → 兩個 is_object() 閘+repro
轉 golden 3 腿)。順手債修: 未知 --selftest-* 旗標 exit 2 不再默開 GUI。

**rename (`d962312`):** SymbolChild.name(尾端擺位=既有 positional init 零 churn)+childReadableName
(空名 fallback def 名, 照 Symbol.Child.cs:35)+RenameSymbol/RenameChild 命令(新檔 rename_commands,
refused 不上 stack)+右鍵 Rename Node/Definition modal。**Fork(承重): 定義名不做 IsIdentifierValid
—— TiXL 限制源自 C# 動態編譯, 資料驅動不存在, CJK 放寬是本單存在理由。**
**★具名風險解除: crude_json 非 ASCII「寫得出讀不回」已被前批 sw-patch 解掉**(dump raw UTF-8/parse
c<128 assert 已拆/孤代理乾淨拒/arm64 unsigned char patch), CJK「粒子發射器」savev2 roundtrip 位元
等同 PASS。雙擊改名手勢未做(右鍵代替, 具名); IME 打字流程需柏為親手測(hand 無法注字, 見下)。

**S3 Curve/Animator (`45dd52f`+`b73e239`):** curve.{h,cpp} 全抄 TiXL(Curve.cs/VDefinition.cs/
SplineInterpolator.cs), **D12 四洞全蓋**(6 內插含 Horizontal/Pre+Post 4 mapper/Tension+Weighted
gate Newton+bisect/TimePrecision=4 banker's round); 定義層 Animator(P2: scoreGraph 作廢)+curveRef
"<childId>:<inputId>[#index]"; resident flattener 投影 Automation driver, sampleAutomation@
ctx.localTime(播放頭佔位=自由 time, S5 落地改一行), 三 eval 路共用; **isLiveSource 補 Automation
推導(1b 具名延後關閉), Constant↔Automation toggle=STATIC↔LIVE 同翻(cache-count 證)**; savev2
animator 段(寫滿欄位換無條件 byte-stable)。Refuter-C: **2 BROKEN 修**——①combine 蒸發定義層動畫
(copyAnimationsTo 零呼叫點+parent 殭屍; TiXL Combine.cs:170-190 兩頭都做; 修+golden; combine 本就
not-undoable 照 UndoRedoStack.Clear, 曲線還原走 snapshot)②load 量化器 half-away vs live/TiXL
half-even(tie-time 落錯 slot; Curve::roundTime 升單一真理點)。Mutation 實證牙盲區補:3鍵非對稱
Smooth(中鍵 tangent clamp 死碼區)+Oscillate 極大-u fold。**copy/paste 曲線接縫同批閉合**(spec
「曲線跟著 copy」: ClipboardData 帶 curves/JSON S15 容錯/doIt 裝 target animator+undo 乾淨清)。
GUI 無(timeline/Animate 手勢=下批 lane); Vec 多通道投影只取 #0(具名)。

**粒子衰減根治 (`44c210d`, 批次5 drill 的 22003→1700 嫌疑):** investigator D 先行(bit-for-bit
repro+五假設排除表): 根因=移植時刻意簡化「只發一次/IsAutoCount 寫死 1=永生/CollectCycleIndex
寫死 0」, turbulence 持續注入+drag 2% → 無界外漂出 viewExtent(maxR 2→85)。shader 無罪(TiXL 忠實
移植), 病在 host dispatch 政策。**拍板走「分岔預設照 TiXL」規則直接決**(TiXL 有直接對應物=
ParticleSystem.t3 內部子圖, 非品味級)。D2 修: 逐幀 emit+cycle=frame*emitCount%pool+IsAutoCount=0
(CompareInt: Max=100000>0)+**關鍵 parity 缺口=pool≫emit 才能轉**(pool=emit×180 cap 262144,
countTransform flat/resident 對稱), 政策 SSOT 收 particle_params.h。D 的 repro 翻面=
--selftest-decay(5min 快轉穩定帶; -bug 注入舊政策完整重現衰減 RED)。**graphbridge 牙鈍連帶修**
(Fable 全表掃出): pool 變大亮斑飽和, 3 幀次像素位移被淹→假綠; 拉 20 幀讓位移跨像素, 牙咬回。

**活體驗收 5/5 PASS** (eye/hand drill): 右鍵 Copy/Paste 節點 5→6→7、Cmd+C/V 鍵盤通(NSMenu 不攔)、
敵意 clipboard(中文+crafted JSON×3) app 全存活 no-op、rename dialog 開/關+compound-gating 對
(atomic 無 rename_def)、**粒子 98s 亮像素震盪帶 216k–234k 無單調崩**、save→kill→reopen 零警告+
活粒子 58FPS。全表 selftest 全綠+全牙(零 TOOTHLESS)+check-arch。

**New traps (this session):** ①**isolation worktree 可能開在舊 main 基底**(a54b8c0, 兩個 agent
中招)——工單第零步必帶 `git log -1` 基底自檢+`git reset --hard <sha>`; worktree 缺 third_party
(gitignored)→symlink 主樹。②**stale binary 驗收陷阱**: 活體驗收前 pkill+確認 relink, 跑舊 binary
會誤判功能缺失。③**hand 無 AddInputCharacter**→打不了字進 InputText(rename/combine 命名活體閉環
打不滿), 下批補 `text <utf8>` 指令(io.AddInputCharactersUTF8)。④**hand 左鍵點節點不選中**(down→up
跨幀被判 drag; 右鍵 capture 不受影響)。⑤pipe 後 `$?` 是 tail 的——驗 exit code 別過 pipe。
⑥`git apply -3` 會 stage 進 index——commit 前先 `git reset HEAD` 再選擇性 add(本批踩過一次
誤掃 commit, soft reset 救回)。

**律法債帳(具名續背):** graph_commands.cpp 419/compound_save.cpp 449/editor_ui.cpp 470(>400;
integrator 已開機械拆分 chip)。Token 帳: ~1.4M subagent(7 工單)+lean main。

## Cut 13 — 批次 7: hand 注字 + S5 Transport + S2 殘欄 + S3 GUI ✅ (2026-06-11 夜→06-12 晨,
`21756d5`→`ac11871` 七 commit; dispatcher 三航: 4 implementer + 2 refuter + 2 drill + 1 fixer)

**hand 注字+選中修 (`21756d5`, verify 葉):** `text <utf8>` 指令(AddInputCharactersUTF8, CJK 可注)
+cold-click 競態真根因(按下幀 hover=上一幀游標位置→BuildControl 早退→ClickedNode=null)→click 展開
3 幀 move→down→up。活體自驗=rename dialog 注「心跳偵測」真渲染。**agent 從此能打字+可靠選中。**

**S5 Transport 真兩鐘 (`c40bdaf`+refuter修 `9f5cd25`):** runtime/transport 葉子(advance 純公式),
fxTime 規則逐條抄 Playback.cs(播放=兩鐘鎖/暫停+scrub=fx 跟跳/暫停 idle=fx 續跑)=ledger 待釘項釘掉;
ctx 兩鐘填真值(point_graph_resident 佔位砍, S3 sampleAutomation 自動接上);CompositionSettings(BPM)
入 savev2;FRAME_SCHEDULER golden 三語義搬運;toolbar Play/Pause/Pos/BPM。refuter 13 probe 11 SURVIVE
(Playback.cs 逐條對齊含播放中 scrub 真相/resume fx 倒退=TiXL 既定/double 萬幀 drift 1.2e-11),
**2 BROKEN 同關節修=bars→secs 換算**: ①AudioReaction 該吃 bars(TiXL LocalFxTime=FxTimeInBars,
EvaluationContext.cs:49)卻吃秒→BPM≠240 debounce 縮放錯 ②bpm=1e-300 穿 >0 閘→secondsFromBars=inf
(敵意檔可達)→[1,999] sane 閘+repro 轉 golden。ctx.time 留秒=我方 Metal sim 單位 fork 具名。
**活體 PASS: Pause 後 position 三取樣全同凍住、fxTime 續走 16.40、粒子 diff 52340px 還在動=
L8 兩鐘分離肉眼可證。**

**S2 殘欄 (`54f3853`+refuter修 `3fefed1`):** isBypassed(直通 Inputs[0]→Outputs[0], pull+eval 兩路;
未連線拒絕;白名單)+per-output isDisabled(凍結=停止追版,不碰 valueVersion→解凍自然追上;Command 同
機制 no-op)+triggerOverride(isLiveSource 第三項推導接上=1b 關閉)+copy/paste bypass 接縫閉合+savev2
outputs[] 段。refuter 11 SURVIVE+**1 BROKEN=假旋鈕**(白名單收 5 型但 GPU cook 不讀 bypass 旗→
Points bypass 鈕按了畫面不動)→**白名單收窄到 Float**(誠實規則:型別進白名單的條件=執行端也直通;
buffer 直通=修B 排下批)。**自查咬出 refuter 沒組合的洞 P1×P7**: production toggle→rebuild→cache
冷→「凍結在最後結果」變凍結在 0(TiXL 無 rebuild artifact)→transplantDisabledCaches 騎 rebuild 縫
+leg4c 真投影路全鏈證。互斥閘補(TiXL Slot.cs:50-53 第二 op 拒絕, 雙向+leg8)。

**S3 GUI (`25f2e96`+活體修 `ac11871`, 本批主刀):** Inspector Float 右鍵 Animate(建曲線+首key=當前值;
driver 翻 Automation=flattener isAnimated 同源不可能 drift)/Remove Animation/P1 手感(拖已動畫
slider=播放頭寫 key);timeline 浮窗(dope-sheet: lane per (child,input)/playhead 豎線可 scrub/單擊選
/拖 key 改時間/雙擊加/Delete+Backspace 刪);六命令 undo 包(keyframe undo=整條 Curve 快照, fork 具名:
單-key tangent 殘渣使 per-key undo 非 byte-faithful)。範圍鎖死具名(無 TimeClip/Layer/loop/變速/
bezier 把手/內插切換/多選/縮放)。**活體驗收兩輪+fixer 咬出三刺全修(headless 全綠但手一摸就斷)**:
①右鍵路由劫持—canvas Show*ContextMenu 吃浮窗上的右鍵(Animate 出成 Paste)→canvasOwnsMouse 閘
(imgui hover 算遮擋=判別器),三向複證 ②BUG-A P1 手感死—已動畫 slider 每幀被 sample(playhead) 重置
→live-write(批次3 vec 同款:拖曳中直寫 key、放手 push SetCurveSnapshotCommand before/after 快照)
③BUG-B heap-UAF—timeline range-for 迭代 curve.table() 迴圈內 push 命令→map::erase 砍迭代中節點
(ASan 134)→PendingAction 延遲執行。**活體全鏈閉環(Fable 親手 eye/hand 跑完 fixer 被砍剩的尾):**
Animate→scrub bar4→拖 slider 寫 key(3.000→2.514)→值隨曲線直讀(bar0=3.000/bar4=2.514)→雙擊加 key
→拖 key 474→560 不崩→Delete 刪 key→一步 undo(要先點 canvas 取焦點=Cmd+Z 既有焦點閘)→Play 畫面活。

**New traps (批次7):** ①**eye 輸出會凍**(app 失去前景/顯示鏈暫停)→讀前 re-touch req_* + 驗 mtime,
凍了重啟 app;殭屍 state.json 會給昨天的值 ②**點 canvas 空白取焦點會反選節點**→Inspector 變空,
後續 param 操作全落空(先選回來) ③popup 選單項多數無 eye hook→rclick 後立即 req_full 截圖找座標
④full.png 是 retina 2x, crop 座標要 ×2 ⑤undo/Delete 鍵需 canvas 視窗焦點(批次5 Delete 同款)。

**律法債帳:** animation_commands.cpp 488/compound_save.cpp ~520/graph_commands.cpp 419/
editor_ui.cpp 483(>400; 機械拆分 chip 已開)。Token 帳: ~1.3M subagent(9 工單)。

## Cut 14 — 批次 8: 五項全開 (2026-06-12 晨→, 柏為「走」全單) 🔄 verdict 回填中
`eeca2b1`→`c6e426a` 五 commit; dispatcher 編隊: 5 implementer + 3 refuter + 1 live-driver。
施工順序由檔案重疊決定: A1 拆檔先行(獨佔 CMakeLists) ∥ B/C worktree 並行 → A2 → A3 序列
(同咬 timeline/curve); 活體集中尾段(eye/hand singleton)。

**A1 >400 四檔拆+AR pin (`eeca2b1`):** compound_save 538→210+compound_load 349 (serialize/
deserialize 縫); animation_commands/graph_commands 各拆 selftest TU; editor_ui 470→348+
canvas_ids (pin/link ID 編碼內聚)。frame_cook 抽 cookAudioReactionNodes 接縫+arclock 牙
(bars 域: debounce 按 BPM 縮放, 改回秒域立紅=真回歸證過)。

**C soundtrack (`66d1c87`):** platform/audio_playback 葉 (AVAudioEngine+PlayerNode; 輸出端
不踩 capture 的 AUHAL 輸入雷, 理由具名)。follow rule 抄 TiXL SoundtrackClipStream.cs:157-229:
wall clock master, |drift|>0.04s×|speed| 才 hard-seek, pause 不 seek, 越界 pause。target 用
playhead position 非 fxTime (timeline audio 跟播放頭凍)。fork 具名: 無變速/無 BASS offset 補償
(AVAudioEngine 延遲不同, 活體校)/暫停 seek 立即。--selftest-soundtrack 六腿。

**A2 S6 timeline 成熟 (`a94b409`):** zoom 游標錨 (ScalableCanvas.cs:382-415)+右拖 pan; 多選
(點選/rubber-band 三模式 DopeSheetArea.cs:941-1049)+群組拖移一步 undo
(SetCurveGroupSnapshotCommand); 內插右鍵選單五項逐字 CurveEditing.cs:98-462; Curves 第二視圖
(值軸+polyline+2軸拖+垂直 fit)+tangent 把手逐式 CurvePoint.cs (折斷/鏡像)。dope 垂直
value-nudge=fork 具名 (TiXL dope 鎖水平), 活體手感不對可砍。沿視圖縫拆 5 TU; 突變中央化
timeline_edit::executePending (批次7 UAF 律); 拖曳 liveness 改 raw mouse-down (key 重排換
imgui id, 自查洞)。

**B bypass 修B (`49933c9`):** cookResident 三流 redirect — Points (cookNode 主輸出=上游
cooked buffer)/Command (skip-self chain 穿過)/Texture2D (終端 dispatch 前 bypass-chain 解析
+depth 64 閘) = TiXL ByPassUpdate (Slot.cs:176-179)。白名單 Float+Points+Command+Texture2D,
GAP 具名留外 (ParticleForce/Vec/string 無執行 leg=誠實規則)。flat cook() 不動具名 (flat Node
無 bypass 欄, production 走 cookResident)。--selftest-bypasscook 三 leg+contrast。
既知 gap: Texture2D 直通=終端限定 (無 image filter 消費者, 第一顆進場時 gather 處補);
compound child bypass 不投影 (批次7 既定)。

**A3 Vec 多通道 (`c6e426a`):** animateFloatVector=Animator.cs:97-126; 同源分組關節
AnimGroup/animGroupForSlot (Inspector/投影/牙三方共用 positional consume=不可能 drift);
fork 具名: 我方 Vec=N 連續 Float port→群組收在 HEAD slot id 下。resident 投影過
animGroupForSlot→makeRef(#channel), patch 路自動繼承。timeline lane label .x/.y/.z
(TableView.cs:17-20), 手勢零 Vec 特例。拖單 component 只寫該通道 key。

**refuter 壓測 (3 航 11 BROKEN → 3 commit 全修, 每條 RED 證牙):**
- **refuter-B** (2 BROKEN→`677de07`): ①compound child bypass=死旋鈕 (childIsBypassable 只比
  邊界型別, cook inline 分支不讀旗; 修B 把 Points 入白名單反而讓最常見的自製點濾鏡 compound
  長出死旋鈕)→照批次7 判例收窄: compound 回 false, cook 級實作=修C 排下批, leg X=修C 要翻的
  明文契約 ②redirect 註解稱 depth-capped 實際只蓋 terminal→kCookDepthCap=64 穿全 7 呼叫邊,
  bypass 環不再 ASan 爆 (正常 wire 環 incidental 蓋住, parked 帳不動)。SURVIVE 帳: 鏈式
  atomic bypass/型別閘/互斥/count 動態 alias/Command skip-self。盲區記帳: Command/Texture2D
  在 production 無同型 atomic, 唯一入口=compound→收窄後暫無處出現, 修C 落地自然接通。
- **refuter-C** (4 BROKEN→`2a2cde9`): ①engine 一停永死 (engineStarted 一次性旗+零 observer;
  拔耳機=無聲到重開 app)→每次查 isRunning+AVAudioEngineConfigurationChangeNotification
  (atomic 旗+主執行緒重活) ②dt 夾 0.25×audio 自由跑=卡頓倒帶 (卡 2s 音樂倒 1.75s)→dt 分流:
  transport 吃真 wall dt (TiXL Stopwatch parity), 0.25 限縮 sim-only ③seek(duration) playing
  旗卡死→早退清旗 ④失敗 cache 重選同路徑救不回+引錯權威→applySoundtrackPick 明確重選必重試,
  真權威=PlaybackUtils.cs:35 (每幀重試), 我方不每幀重試=具名 fork。SURVIVE 帳: resync 公式
  speed 相消等價/敵意檔/換檔/BPM 跳變 parity/target=position。潛伏雷入 parked: decide() 無
  rate 輸入 (變速 UI 接上時無聲爆)。
- **refuter-A2** (5 BROKEN→`2a675b6`): ①②負時間 per-key 夾鉗→併鍵毀資料+幽靈 selection+
  Delete 計數膨脹誤滅整條動畫 (三連環)→剛體平移 clamp+每幀去重+數去重實存鍵 ③tangent 拖被
  每幀 updateTangents 打回 (TiXL HandleTangentDrag 全程不呼)→拖曳路徑不跑, 只留結構路徑
  ④漏搬 Linear→Tangent 升格 (CurvePoint.cs:289-298)→補 ⑤zoom 小數 wheel 分岔→整數步
  1.2^⌈|wheel|⌉+clamp [0.02,100]; 跨 composition selection 滲漏順手堵 (symbolId 變更清)。
  新 --selftest-timeline 27 legs (-bug 17 翻紅)。
- **live-driver 三輪**: D1 砍前咬三刺+修全收 (`5c1e424`): dope/curve lane bg 偷 press→點 key
  選不中 (SetNextItemAllowOverlap)/timeline 焦點 Cmd+Z 死 (canvas handler window-scoped,
  timeline 自帶)/eye 凍根治 (MTKView display-link 停轉>250ms keep-alive 手動 draw); hand 長出
  rdrag+keydown/keyup。D2 砍前懸案: k1 點選 key 顏色淡黃≠選中黃 (功能 vs 視覺待裁)。
- [x] **D3 (Fable) 活體收尾 ✓ (`95b53a1`)**: 17 項清單 16 PASS。**k1 懸案破案=功能全對非路由洞**
  (拖/刪/undo 三證綠; 淡黃=選中色過 sRGB framebuffer gamma 抬升 (255,210,90)→(255,234,160),
  與未選/hover 清楚可分——選中黃要不要更飽和=柏為品味拍板, 非 FAIL)。兩個根因修:
  ①keep-alive 餓死 double-click (display-link 停轉下 ~4fps 重畫湊不齊 ImGui 雙擊窗→有 hand
  step 即 0.05s 手勢速; 前兩輪雙擊漏的真因) ②drag 缺 settle 起手幀 (press 沿用前幀 HoveredId,
  AllowOverlap 下被 lane-bg 偷走變框選)。唯一活體受阻項具名: node-editor 右鍵選單對 hand
  rclick 注入不穩定 (hit-test 脆, 手冊既有「點標題非中心」同族)——bypass 語義由
  --selftest-bypasscook+命令層拒絕碼雙證, 「右鍵→Bypass→旗標翻」的 GUI 示範缺活體穩定重現。
  **新環境雷: `pkill -f "simple_world$"` 殺不掉 `--open x.swproj` 實例 (cmdline 尾不是
  simple_world)→多實例同寫 .eye 致 pos/map 亂跳; 正解 `pkill -f "build/simple_world"`**。
- [ ] **柏為親測項**: ①拔耳機復活 (播放中拔→一秒內回聲+不倒帶) ②soundtrack 真出聲+音畫齊
  ③dope 垂直 nudge 手感 (fork 項, 不對直接砍) ④選中黃飽和度品味拍板 (k1)

**柏為親手玩法 (批次8 新東西):** ①Timeline 滾輪 zoom/右拖 pan/框選多 key 拖/右鍵切內插/
"Curves" 鈕進曲線視圖拖 tangent 把手 ②Inspector 對 Vec 參數 (如 Center) 右鍵 Animate→
三條 .x/.y/.z lane, 拖單一分量只長該通道 key ③節點右鍵 Bypass (Points 修飾 op 現在真的動)
④toolbar Soundtrack 鈕載音檔→Play 音畫同走。

## Resume — next
- 批次 8 程式帳已結: 5 implementer + 3 refuter (11 BROKEN 全修) + live 三輪, 計 9 commit
  (`eeca2b1` `66d1c87` `a94b409` `49933c9` `c6e426a` `5c1e424` `677de07` `2a2cde9` `2a675b6`)。
  懸欄: D3 活體收尾 + 柏為親測三項 (上方驗收狀態欄)。
- Candidate next (pick by 柏為 priority):
  1. **修C: compound child bypass 真實作** (cook 級 inline 分支讀旗→passthrough; leg X 翻面;
     Command/Texture2D production 入口隨之打開; bypass_cook_selftest 396 行先拆)
  2. **S6 殘項**: snap/beat 細分/curve 視圖雙擊加 key/zoom 阻尼 (damping fork 帳)
  3. **soundtrack 收尾**: offset 校時旋鈕 (柏為聽出延遲才開)/變速 (要 transport rate 先長,
     連動 decide() rate 輸入的潛伏雷)
  4. **Texture2D gather 直通** (第一顆 image filter op 進場時)
- 新 parked (批次8): cookNode 正常 wire 環 fail-safe 是 incidental 非 contracted (>64 深
  合法鏈也被蓋); decide() 無 rate 輸入; soundtrack 選檔不進 undo; SelKey (lane,roundTime)
  識別殘留限制 (header 具名); Y 軸 clamp [1e-4,1e6] fork; dope value-nudge fork 待柏為手感。
- Parked (unchanged): per-instance view-area memory; boundary moves no undo; per-path op state
  leaks until app close; CommandStack hardwires single doc; boundary↔boundary passthrough;
  crude_json never escapes object KEYS; loader accepts cyclic files silently (S14 catches);
  粒子 pool 政策常數非旋鈕; dt ceiling 0.25 selftest 不可達(frame_cook 層); 負 rate 倒播無斷言;
  BPM 改值非 undoable; resume 後 fx 倒退=TiXL 既定無明示斷言鎖。

## Cut 15 — 批次 9: E0 工具+修C+rate 變速+S6 殘項 (2026-06-12 晚) 🔄 D4 活體回填中
`33110a7`→`d90da1d` 七 commit; 編隊 4 implementer + 3 refuter + 3 fixer + D4 live(跑中)。
工具三件套 (`9882f87`, 批次間隙落地) 全程服役: worktree setup ccache 熱編 3s/run_all --bite/
sw_drive mtime-wait。

**E0 eye/hand 升級 (`33110a7`):** popup 選單項進 map (insp:*/native_menu_items; ctx:*/tlctx:*
批次8 已有); timelineSelectionJson 進 state.json (k1 類問題機讀); rclick settle 假設被牙否證
(D3 不穩真因=節點被浮窗壓住, 遮擋閘照設計); hand.cpp 404→248 拆 selftest TU; sw_drive 活體
首用抓三蟲 (state 子命令 touch 錯 req/mtime 整秒死鎖→%Fm/do 提早返回→hand_pending 輪詢)。

**E1 修C compound bypass (`23f857b`):** flattener bypassed compound 不 inline, main output
別名 main input driver (ProducerMap 通貨, 三態統一) = Slot.cs:176-179 Instance 級; 內部零
resident 足跡; childIsBypassable 拆 atomic 閘 (Symbol.Child.cs:232-248 逐字); viewProducerPath
橫向走防黑屏; --selftest-bypasscompound 五腿 (leg X 翻面/roundtrip/Cmd+Tex production 入口/
巢狀/undo-redo)。

**E3 rate+變速 (`7a8f36e`):** transport setRate (±16/死 rate 復活=TimeControls.cs:130-133);
decide() speed 輸入 (refuter-C 潛伏雷正法); AVAudioUnitVarispeed (變速變調=BASS Frequency
parity); toolbar Speed; fork: 負速 soundtrack=Pause (AVAudio 無倒放)/|speed| 窗外 [0.25,4]
=Pause/rate sticky 不持久化。

**E2 S6 殘項 (`7f7dd9a`):** snap (SnapResult 數學/anchors=raster+playhead+非選中 keys/指示器
1s 淡出+tl_snap hook); ruler=BeatTimeRaster 15 段 ladder 逐行 (換檔/bar.beat/fade); curve
雙擊加 key (值=取樣 cs:337, fork=雙擊 vs Alt+click); zoom 阻尼=DampScaling 逐行 (雙態/
f=min(dt/0.06,1)); timeline 牙 27→45 CHK。

**refuter 三航 10 BROKEN → 3 fixer commit 全修:**
- **E1 (`9aade66`):** B1 宣告序幽靈路徑→Kahn 拓撲序 (連批次5 非 bypass compound→compound
  舊傷治根; 環殘餘=宣告序補走具名); B2 compound childIn 不查 animator→補種子, compound 輸入
  動畫從全聾變真投影 (GUI Animate 假旋鈕家族); B3 patch 靜默 no-op→void→bool 訊號化 (false=
  零足跡須 rebuild, 'patch==rebuild' 破翼變可斷言); 盲區=viewProducerPath 橫步帶 srcSlot;
  resident_eval_graph 爆 400→拆 flatten.cpp (295)。
- **E2 (`ea9087b`):** 雙擊毀滅鏈→insert 改 clone-previous-key (cs:339-341, 撞號自然無損,
  fork=clone-next when no previous); curve 視圖 Shift 極性反了→per-view (TiXL 兩視圖本就相反
  cs:461 vs :927); snap indicator stamp 移 clamp 後 (驗證面去污染); Vec 兄弟自吸→dope 全通道
  同 roundTime 入選 (cs:976-987) exclusion 自然蓋; damp 中拖漂移→絕對座標映射 (InverseTransformX
  結構天然免疫); 順手 drag active 擋 Cmd+Z (stale snapshot)。
- **E3 (`d90da1d`):** 窗內變速 resync 機關槍 (rate≥1.5 → 81-99% 幀 hard-seek)→真兇=批次8 漏抄
  cs:226 第三個 speed (resyncOffset=TriggerDelay×speed, speed=1 恰 0 隱形)+我方獨有 seek-settle
  guard (position 重啟窗凍結 vs BASS mixer 不停=結構差, 光抄 offset 不夠); offset 常數=本機實測
  0.030 (Scarlett median 42ms) 非 BASS 2/60 具名; 閉環追逐牙 rate{1,1.5,2,4} 全 0 hard-seeks
  (injectBug 重演 81/98% 簽名); 順帶 rate1 穩態 drift −35→−10ms; eps 殭屍 (0.001 兩閘縫→Play
  永死)→inclusive; 4.0 窗界遲滯 (re-entry 3.8)。

**驗收欄 (回填):**
- [x] **D4 (Fable) 活體 12/12 PASS ✓** + 一修 (`50d781f`: child pin ed-ID 撞號, id≥101 節點
  拖不動 → kChildPinBase=1<<19 獨立 band + --selftest-canvasids)。要點: dope/curve 吸附極性
  各自對/雙擊撞號 key 風格存活/damp 中拖零漂移/Vec 全通道入選同移/compound bypass GUI 畫面
  roundtrip (362.3→255.4→362.3)/宣告序反證/compound 輸入 Animate 播放爬升/rate 2× Δpos 機讀
  /Speed 0.001 起播/存檔重啟全回。
  **D4 咬出未修 (批次10 排修):** ①E3 違約: speed < varispeed 窗 (0.25) 時 position 完全凍結,
  違反「窗外 Pause 但視覺照全速」具名 fork (soundtrack.h:31-33); 窗內 0.5 正常 ②position 過
  soundtrack EOF 後突跳 (109→129)。
  **D4 殘刺 (記帳):** 疊放節點命中順序 (繪製舊蓋新, z 模型可疑); 新節點固定 spawn (120,120)
  必疊 node:1; 浮窗 body-drag 誤拖三次 (建議 ConfigWindowsMoveFromTitleBarOnly); 重啟同圖
  FPS 30→3 未查根因; eye lane 標籤無通道後綴 (.x/.y/.z 都叫 Center.x)。
  **hand 雷新增:** keychord ctrl Z 無聲 no-op (Mac 要 cmd Z); DragScalar text 是附加不覆蓋
  (先 cmd+a); 浮窗被誤拖後 map 全是幻影 (碰窗 drag 後必重抓 map)。
- [x] **sw_scenario 活體牙 runner ✓ (`d267118`)**: 確定性活體進腳本 (do @SYM/assert_state jq/
  assert_diff 像素), agent 只看紅項。timeline_snap.scn 12.9s 跑完 D4 清單 1/6 段 (vs agent
  迴圈十幾分鐘); RED 面證過。app 生命週期=cwd 歸屬判定 (pkill cmdline 三度燒傷的正解)。
  **流程新規 (批次10 起): implementer 活體可證的行為要附 .scn (與 selftest 牙同地位)。**
- [ ] 柏為親測: ①Speed 拖 2×/4× 聽變速變調 (高八度) ②變速中拔耳機 ③藍牙耳機 rate 1 會不會
  風暴 (offset 常數=Scarlett 實測, 藍牙延遲更高) ④雙擊毀滅鏈修後手感 ⑤批次8 四項若未測續欄

**新 parked (批次9):** 負速 position 無下界 (advance 不 clamp vs scrub ≥0 不對稱); 0.25 下窗
界同形 flutter 未加遲滯; 多輸出 compound 次要輸出 dangling (兩處同命: builder+viewProducerPath);
root Constant redirect 表達缺口; patch 三態訊號 (landed/no-op/unpatchable) 留修D; compound
childIn 種子 anim group=scalar identity (compound SlotDef 今日皆 scalar); B1 環殘餘 wire-cycle
拒絕應在 command 層 (同 addChildWouldCycle 位置)。

## Resume — next (批次10 候選)
0. **D4 咬出的兩條排修**: sub-window speed 凍結 position (E3 違約) + soundtrack EOF position
   突跳; 順手評估殘刺 (spawn 疊放/浮窗 body-drag/FPS 30→3/eye lane 通道後綴)
1. Texture2D gather 直通 (第一顆 image filter op 進場時) — 修C 後 production 入口已開
2. soundtrack offset 校時旋鈕 (柏為聽出延遲才開; 藍牙風暴若實證→offset 常數要變旋鈕或自動量測)
3. 負速倒播全鏈 (timeline/curve/shader 吃負 bars 的 UI+渲染行為, 現無牙)
4. dope value-nudge 手感拍板後續 (柏為親測④)
5. scenario 庫擴編 (D4 清單其餘段→.scn; implementer 新規=活體可證行為附 .scn)

## Cut 16 — 批次 10: D4 排修+殘刺+負速定案+scenario 庫成軍 (2026-06-13 凌晨) ✅
`980e2dd`→`fb4d569` 六 commit; /sw-batch 自走首航 + WORKFLOW 分層首用 (2 Opus impl +
2 Sonnet impl + Opus/Sonnet refuter 各一 + fixer 由 orchestrator 親收)。候選 0/3/5 組批,
1 (Texture2D gather) 閘 image-filter-op 進場、2/4 閘柏為親測 → 跳過不擋批。

**A — D4-E3 兩條咬帳 (`980e2dd`): 查證不重現, 契約鎖牙。** position 寫入者全樹只有
transport.advance + 使用者 scrub, 無音訊回寫路徑。不捏造修補: soundtrack leg ⑪
(playhead-isolation 不變量, soundtrack_selftest_d4.cpp 照 runAnimGuiS6Legs 拆分前例,
selftest TU 398→501 破 400 線被 commit 律法自檢咬住→拆) + e3_subwindow_speed/
e3_eof_position.scn + state.json 露 .transport.rate。
**refuter-R1 (Opus) 判決補完: 109→129 突跳可構造 = 單次 10s stall × rate4 × unclamped
dt, 數字嚴絲合縫——但 TiXL Playback.cs:104-118 raw Stopwatch 同樣無上限 = 逐字 parity
非 bug; B4 治穩態 FPS≠治單次長 stall (拖窗/debugger)。A1 凍結 = D4 把 soundtrack 窗
Pause 誤讀成 playhead 凍 (唯一真凍結=rate≤0.001=TiXL eps pause)。兩條咬帳正式銷案。**

**B — D4 殘刺四項 (`8dc866c`):** ①spawn 跟右鍵點 (= GraphView.cs:861 InverseTransform
語義; fork=submenu 直列型別非 SymbolBrowser 搜尋面板) + d4_spawn_position.scn ②浮窗
io.ConfigWindowsMoveFromTitleBarOnly=true (canvas NoMove 不受影響; 日後要拖的浮窗不可
NoTitleBar) ③tl_lane key 補 :ln.index 通道後綴 (Vec 多通道同 key 互踩; UI 人讀面本就
各自 .x/.y/.z, R2 雙面驗證) ④**FPS 30→3 根因 = isDirty() 每幀 libToJsonV2 全序列化**
(updateWindowTitle 觸發) → g_libRevision-keyed cache + 兩個繞 revision 直寫點
(soundtrackPath/bpm) 接 invalidateDirtyCache。
**refuter-R2 (Sonnet) 咬出 1 BROKEN→fixer (`de7abb7`): doSave/doSaveAs 更新快照不 bump
revision → 存檔後 isDirty() 殘留 stale true (標題 • 不滅)。修=兩出口接 invalidate +
navigation selftest dirty-cache-save leg (咬 doSave 本體真寫檔, injectBug 紅證)。**
假引文記帳: B3 真路徑 Editor/Gui/TableView/TableView.cs (內容對); B4 引 UndoRedoQueue.cs
查無對應, 真依據=Core/Animation/CurveSampleCache.cs ChangeCount/_revision 模式。
FPS root-cause 鏈活體未重現 (testdata 2.9KB 太小, 序列化 <1μs; cache 正確無害, 因果鏈
誠實記為未實證)。

**C — 負速倒播全鏈 (`6c1b278`): TiXL 研究定案=鏈本已對齊, 零程式碼改, 補牙釘死。**
①advance 不 clamp (Playback.cs:114-118 PlaybackSpeed 有號無下界) — parked「負速 position
無下界」拍板: 照 TiXL 不 clamp; scrub ≥0 維持既有 fork, 兩者並存是設計 ②倒播入口:
TiXL=BUTTON (TimeControls.cs:464-470); fork=我方用 Speed 旋鈕拖過 0 (插鈕會位移 Speed
rect 弄紅兩條 Speed-drag scn); Transport::playBackwards() 留 TiXL 按鈕語義做 helper
(四態上牙, 未接 UI; 其 forward→-1 一步到位 vs TiXL stop-first 已具名) ③負刻度: raster
fmod 自然浮出。牙=transport ①d (過 0 UNCLAMPED/scrub 不對稱/playBackwards 四態) + ④擴
(automation @ 負 playhead=首鍵值) + timeline ⑧ 負刻度兩牙 + negspeed_playback.scn。
**refuter-R1: 全 SURVIVE。負域 ruler label "0.-1" 醜 = TiXL BeatTimeRaster.BuildLabel
位元等同地醜 (繼承的本質醜, 修了才是 fork, 不修)。**

**D — scenario 庫成軍 (`612733e`): 1→11 條。** 六條 d4_*.scn (curve_editing/vec_channels/
compound_bypass/compound_anim/transport_rate/save_restart) 每條綠+RED 證。runner 三修:
app_launch mkdir -p $EYE (雞蛋問題, lane C 獨立撞到雙重確認)/fixture <src> <dst> 指令
(會 Save 的 scenario 必須每跑重置——合流咬出 save_restart 非冪等: 第二跑 Radius 已有動畫
insp:Animate 解析不到)/@SYM 解析有界重試 ×3 留痕 (`fb4d569`, sweep 連跑掉 3-4 條單跑
全綠的 churn 假紅, 修後兩輪 sweep 11/11 零 retry)。
不可 scenario 化具名: 音訊出聲/拔耳機/藍牙=實體; 雙擊手感=主觀; 宣告序=headless 已證。

**驗收欄:**
- [x] 每 commit orchestrator 親手復跑 66/66 --bite 零 NO-BITE + check-arch + scenario 庫
- [x] refuter 波 9 SURVIVE + 1 BROKEN (修畢); A no-repro 判決過 Opus 對抗
- [x] scenario 全庫兩輪 sweep 11/11
- [ ] 柏為親測 (批次10 新增): ①Speed 旋鈕拖到負值倒播手感 (fork=旋鈕非 TiXL 按鈕, 不順
  手再議接 playBackwards 鈕) ②右鍵加節點 spawn 落點手感 ③浮窗改 title-bar-only 拖動的
  操作慣性 ④存檔後標題 • 即滅 (de7abb7)
- [ ] 柏為親測 (批次9 繼承): Speed 2×/4× 變調/變速拔耳機/藍牙 rate1 風暴/雙擊手感/批次8 四項

**新 parked (批次10):** main.cpp 425 行第二刀拆分 (law debt, 排批次11 首位); leg ⑪ 走
followFrame 非 production syncFrame (coverage gap, 修=frame lambda 改 routed, 小);
sweep 連續區段 flake 根因未深究 (retry 治標留痕, retry 觸發率>0 時回來看); B4 FPS
root-cause 活體實證需 >1MB animator 重 project fixture; 負域 ruler label "0.-1" (TiXL
等同, 上游醜); D4 殘刺餘項: 疊放節點命中順序 (z 模型) 未動。
**繼承 parked (批次9):** 0.25 下窗界 flutter 遲滯; 多輸出 compound 次要輸出 dangling;
root Constant redirect; patch 三態訊號; B1 wire-cycle 拒絕 command 層化。

## Resume — next (批次11 候選)
0. main.cpp 第二刀拆分 (law debt 首位; 425 行, 沿職責縫)
1. leg ⑪ 改走 syncFrame (R1 coverage gap, 小)
2. **parity 缺口掃描批**: 候選見底——派 Explore 編隊對照 external/tixl 盤點「TiXL 有、
   我們沒有/不一樣」(op 庫/UI 視覺/互動/快捷鍵), 產出新 Resume 候選清單。Texture2D
   gather + 第一顆 image filter op 自然在此入列 (修C 後 production 入口已開)
3. soundtrack offset 校時旋鈕 (柏為聽出延遲才開) — 閘柏為
4. dope value-nudge 手感拍板後續 — 閘柏為

## Cut 17 — 批次 11: main.cpp 清帳 + parity 缺口掃描 (2026-06-13 凌晨) ✅
`a1210b4` + 掃描總帳 docs/runtime/PARITY_GAP_SCAN_2026-06-13.md。
- **M1 ✓**: main.cpp 425→258 (AppDelegate/ViewDelegate→app_delegate.{h,cpp}, app shell 層,
  零邏輯變化)。記帳: app_delegate.h 暴露 Renderer private layout (下一刀 renderer.h 收);
  kBgR/G/B 住 selftests.h 語義不對。
- **M2 停手 (照規)**: syncFrame 焊死 playback()/g_followState singleton 無外部實例接口;
  正確接縫=抽 syncFrameCore(AudioPlayback&,FollowState&,...) 兩端同核——重構級, parked。
- **掃描四路 ✓**: op 庫 17/500+ (image filter 族 0 顆, 第一顆=Blur)/UI 視覺 (節點配色已對齊;
  缺網格/連線型別色/圓角縮放/idle fade/Annotation)/互動 (~117 action 接 ~6; playback 鍵
  Space/L/J/K 已驗證真缺)/子系統 (Output Window/資源載入/Player/Variations/MIDI-OSC)。
  **假缺口已過濾進總帳**(double-click 鑽入/右鍵選單=已有)。
- sweep 殘餘 flake 1/11 (save_restart, 單跑綠; retry 只蓋 do 解析面, 記帳續觀察)。

## Resume — next (批次12, 已拍板見 PARITY_GAP_SCAN §批次12)
0. lane I (Opus): Blur op + Texture2D gather 直通 (image filter 開口, Cut 16 點名)
1. lane V (Sonnet): 視覺小包 — canvas 網格/連線型別色/圓角 zoom 縮放/hover (MagGraph 錨點)
2. lane K (Sonnet): 互動小包 — playback 鍵 (Space/L/J/K/frame-step)+Cmd+D+F focus;
   快捷鍵起資料驅動表 (鐵律7, 別散打)
3. (lane I 合流後) math op fan-out 頭批 (Add/Sub/Div/Clamp/Remap/Abs/Floor/Lerp, 資料驅動量產)
4. parked 滾動: syncFrameCore 接縫/renderer.h 第三刀/Annotation 子系統 spec 批/柏為親測欄

## Cut 18 — 批次 12: image filter 開口 + 視覺/互動 parity 第一刀 + math 量產 (2026-06-13 凌晨) ✅
`1faf39a`→`fa0b968` 六 commit; 編隊 1 Opus impl + 3 Sonnet impl + Opus/Sonnet refuter 各一,
fixer 全由 orchestrator 親收。選批=PARITY_GAP_SCAN §批次12 拍板。

**I — Blur + Texture2D gather (`45424f8`+fixer `6b182ea`)**: image filter 族 (55+ 顆) 開口。
cookTexNode 遞迴 gather (flat+resident)/defaultDrawTarget sink 偏好 (柏為可見承重線)/Blur.hlsl
逐行移植 (Gauss[10]/兩 pass 壓一顆 op fork/Wrap 固定 clamp fork)。refuter-R-I 全 SURVIVE,
兩工項收: blurchain 升 resident golden (production cookResident 真路徑進 --bite 級, 綠 9492
紅 0)+紅因註解修正 (injectBug 真機制=termOK 翻 3)。記帳: cookBlur +0.4ms@256² perf debt/
雙 sink 選首 (pin 解未開)/wire-level cycle gate 全圖缺 (前世既有)。
**V — 視覺小包 (`b6942bf`+fixer `fa0b968`)**: TiXL 網格 (Drawing.cs:377-426)/連線型別色
(UiColors.cs:96-104 逐字)/圓角 5px×zoom (<0.5x 關)/hover blink (sin t×10)。refuter-R-VK 咬出
**V3 BROKEN=ed 內建灰網格沒關、蓋過 TiXL 層** (修=StyleColor_Grid alpha 歸零) + **V2
MISMATCH=DrawConnection.cs:40-44 是雙層 variation** (正常線 alpha 0.64 非 0.8, 修=variation4
組合)。fork: GetCurrentZoom=InvScale 取倒數/grid 忠實極淡=parity。
**K — 互動小包 (`1faf39a`)**: keymap 資料驅動表起步 (8 行, 新快捷鍵全走表, 散打四鍵不強遷
具名)/Space/L/J/K/Shift+←→ playback (J=toggle fork, 接 playBackwards)/Cmd+D (clone 偏移
+24,24 fork vs TiXL paste 落滑鼠)/F focus (NavigateToSelection)。refuter 全 SURVIVE (文字
輸入閘活體證/Cmd+D 帶曲線/J+Space 語義照 TiXL)。合流修兩刀: scn capture clean→full
(**clean=Metal render 輸出, 未接線 clone/viewport pan 永不變**——新雷)+@canvas token 不存在。
**F — math 8 顆 (`698d414`)**: Add/Sub/Div/Clamp/Remap/Abs/Floor/Lerp 全對 TiXL 源碼。fork:
Div 除零→0 非 NaN/Remap 省 BiasAndGain+Mode/Lerp 省 Clamp input/Floor=C# 截斷向零。Remap
三 port→五 port 標準化 (⚠舊 .swproj 三 port 連線無聲退 default, testdata 無案例)。律法拆分:
registry 484→407+value_eval_ops.{h,cpp} (雷: EvaluationContext 是全域雙編譯 struct, 前向
宣告塞 sw 會造第二型別)。**合流插曲: apply 只跑 --check 乾跑就當成功→主樹假紅——合流者
自己的 pipe 也要試壓**。

**驗收欄:**
- [x] 六 commit 全 orchestrator 親手復跑 70/70 --bite 零 NO-BITE + check-arch
- [x] refuter 波: R-I 6 項 SURVIVE+2 工項; R-VK 1 BROKEN+1 MISMATCH (修畢)+6 SURVIVE
- [x] scenario 全庫 16/16 sweep 綠 (11→16 條: blur_chain/math_op_chain/keys_playback/
  keys_duplicate_focus/visual_grid_links)
- [x] 親眼驗收: 網格/型別色線/Blur 鏈畫面 (full.png)
- [ ] 柏為親測 (批次12 新增): ①Blur 親手接鏈+拖 Size 看糊化 ②Space/J/K/Cmd+D/F 手感
  ③canvas 視覺 (網格極淡=TiXL 忠實, 嫌淡可開 fork 調亮) ④連線型別色分辨度
- [ ] 柏為親測 (繼承): 批次10 四項+批次9 五項

**新 parked (批次12):** idle fade 需 cook「有更新」訊號 (V 缺最後一塊); Annotation 子系統
spec 批; Cmd+F quick-add palette (C4); Displace/ColorGrade 等下一波 filter; DrawLines/
DrawBillboards; keymap 散打四鍵遷表; Remap 舊檔遷移 (有人報案再做); cookBlur PSO/scratch
快取; eye 量 FPS 必須前景 (背景=keep-alive 4Hz 設計行為, 二分四點證過)。

## Resume — next (批次13 候選)
0. (無排修項——refuter 兩波全收)
1. Displace + 第二波 image filter (Blur 開口後依賴全綠; 含 cookBlur 快取順手)
2. Cmd+F quick-add palette (作曲速度關鍵, PARITY_GAP_SCAN §C4)
3. idle fade (cook 訊號接縫) + Annotation spec 批 (子系統級, 契約先行)
4. DrawLines/DrawBillboards (point draw 族)
5. parity 缺口掃描第二輪 (第一輪四面已收割; UI 視覺剩餘項+互動第二梯隊自然滾動)

## Cut 19 — 批次 13: filter 二波+quick-add+draw 族+Annotation 契約 (2026-06-13 凌晨) ✅
`6cdb02f`→`7b45c91` 七 commit; 編隊 3 Opus impl + 1 Sonnet impl + 1 Opus spec + 3 refuter
(R-D2/R-L=Opus 對抗, R-P=Sonnet 複核), fixer 全 orchestrator 親收。suite 73→75; scn 庫 16→19;
sweep 19/19。

**A-spec (`6cdb02f`)**: Annotation 七契約 (純 UI 物件 cook 永不見/扁平 struct 幾何包含/savev2
可選段/三專屬命令/Shift+A+拖帶框內節點/繪製常數全收/idle fade lastUpdatePass 接縫)+三批施工。
拍板: fork-B 解除 (crude_json 非 ASCII=批次4 已根治的舊帳, spec 內校正框)/內嵌 savev2/補改色
命令/Cmd 拖只搬框照 TiXL 字面 (手感列柏為親測)/Label undo 實作批復驗。
**D2 (`d1231e0`)**: Displace 逐行 (含忠實保留 TiXL quirk: NormalMap≡SignedNormalMap 恆假分支,
R-D2 token 級+活體 diffpx=0 雙證)/多 Texture2D 輸入承重線 (inputTextures[4] port 序佔位,
Blur 零改動)/tex_op_cache (PSO+scratch, blur 數值 bit 不變)。R-D2 6/6 SURVIVE; 記帳=混尺寸
scratch thrash (perf)/flat leg injectBug 走 termOK 非黑路徑 (行為 probe 證對, 牙覆蓋不對稱)。
**P (`5ac703c`+fixer `9c24992`)**: Cmd+F quick-add (SymbolBrowser 行為骨架/substring fork/
環檢灰掉/qa:* eye hook)。源碼定案=TiXL 背景雙擊本就是 popComposition, 零衝突。**R-P 咬出
1 BROKEN: focus static edge-detection 只活一次→重開面板 WantTextInput 假→Space 穿透踢
transport (活體實證)**——修=module-level g_focusNextFrame 每開必設+scn 加重開+Space shield
永久牙。記帳: keymap 唯一性斷言只蓋 label 不蓋 key combo/keyboard-only 鑽入路徑出現時面板要
顯式通知。
**L (`0f5e4c1`+fixer `7b45c91`)**: DrawLines+DrawBillboards。承重線=RenderDrawItem 加 DrawKind
維 (append-only, 14 處 init 全安全)+executor 形狀無關 (per-kind PSO/primitive, 唯一 executor
=flat+resident 全涵蓋)。W=NaN 斷線分隔符 (AppendPoints.hlsl 哨兵)——R-L 修帳: 我方
CombineBuffers 不注入 NaN, 斷線邏輯=前瞻 parity 預留具名 fork。R-L 7/7 SURVIVE (N=0,1
underflow 雙閘 GPU 證/blend 決定性 bit 級/swap=取代釘進 scn 斷言)。fork class=無相機 baked
ortho (同 lane A Orient 前例)。

**驗收欄:**
- [x] 七 commit 全親手復跑 75/75 --bite 零 NO-BITE+check-arch; sweep 19/19
- [x] refuter 三波: R-D2 6 SURVIVE/R-P 1 BROKEN(修)+8 收/R-L 7 SURVIVE+2 記帳修
- [ ] 柏為親測 (批次13 新增): ①Cmd+F 開面板打字選節點的手感 (含重開) ②Displace 接
  DisplaceMap 拖 Twist/Shade ③DrawLines/Billboards 接粒子鏈看線/方塊 ④LineWidth 0.02
  正交下偏細 (viewExtent 硬編 3.5, 嫌細=param 化排程訊號)
- [ ] 繼承: 批次12 四項+批次10 四項+批次9 五項

**新 parked (批次13):** viewExtent param 化/miter join 視覺債/blend points-opaque vs
lines-alpha 不一致/混尺寸 scratch thrash/flat displacechain injectBug 黑路徑補牙 (行為已
probe 證)/keymap key-combo 唯一性斷言/quick-add keyboard 鑽入顯式通知/Annotation 實作
三批 (spec 就緒)。

## Resume — next (批次14 候選)
0. (無排修項)
1. Annotation 批 A: 資料模型+savev2 段+三專屬命令 (spec 契約 0-3, 驗收清單在 spec §7)
2. idle fade 實作 (spec 契約 6 接縫已設計: cache slot lastUpdatePass+node_draw fade)
3. 互動第二梯隊小包: keyframe 鍵 (C/Shift+C/B/N)+Alt+←→ 導航 (keymap 表加行)
4. filter 三波 (Invert/ColorCorrection 級單 pass) 或 point modify 族 (AddNoise/FilterPoints)
5. 掃描第二輪 (第一輪殘項滾動)

## Cut 20 — 批次 14: Annotation 批A + idle fade + keymap 二梯隊 (2026-06-13 清晨) ✅
`0f6f5d4`→`57dac67` 六 commit; 1 Opus impl + 2 Sonnet impl + 2 Opus refuter + 1 Sonnet refuter
+ 2 fixer (一 orchestrator 親收一 Sonnet)。suite 77/77; scn 庫 22 條 sweep 22/22。

**AN (`0f6f5d4`+fixer `5ff5c84`)**: Annotation 批A 照 spec 契約 0-3——struct/savev2 可選段
(fork-A2 省略預設灰, 浮點容差 bit 級驗過)/四命令 by-id。**fork-F 復驗定案: TiXL
AnnotationRenaming.cs:96 在命令外直寫 Label=undo 真漏, 我方補雙欄具名**。R-AN 咬出 **B1=load
端 id 不 dedup** (命令層唯一性被磁碟繞過, Delete/ChangeText 只打第一顆留幽靈)→修=last-wins
(TiXL dict 語義)+牙。批B 硬約束記帳: combine/copy-paste 必帶幾何包含 annotation 搬+刪原
(Combine.cs:170,250-254); 命令快照在 ctor=建即用契約。
**IF (`12133e3`+fixer `57dac67`)**: idle fade (60 幀無更新→暗 60%, DrawNode.cs:38-50 逐式)。
**R-IF 坐實 BROKEN: stamp 只打 isLiveSource → Time 下游整條鏈每幀真重算卻假暗** (TiXL
Slot.cs:160-168 每重算 slot 都 SetUpdated)。修=computeLiveDownstreamClosure (BFS, rebuild 時
revision-keyed) stamp 閉包; **連帶挖出 production 從未 initResidentCache → 原 stamp+S2
disable-freeze transplant 在 production 全空轉 (靜默失效), 一併修活**。鐵線 (值/版本 bit
不變) P3+P5 牙釘死。
**K2 (`4125c05`)**: C/Shift+C 插 key (MacroCommand 一步 undo, undo byte-faithful R-K2 驗過)/
./, 跳 key/Alt+←→ 雙棧導航歷史。合流補 N3 老雷 (直寫 path 繞過 push/pop 漏 pin/selection
清理=裸 id 跨 symbol alias)。B/N 跳過具名 (loop range 子系統不存在)。epsilon comment 說謊修
(1e-6 自稱=TiXL 0.001f→具名 fork)。

**驗收欄:**
- [x] 六 commit 親手復跑 77/77 --bite 零 NO-BITE+check-arch; sweep 22/22
- [x] refuter: R-AN 1 BROKEN(修)+5 SURVIVE; R-IF 1 BROKEN(修)+4 SURVIVE; R-K2 全 SURVIVE
  +1 comment 修
- [ ] 柏為親測 (批次14 新增): ①C/Shift+C 插 key 手感 ②./, 跳 key ③Alt+←→ 導航 ④idle fade
  視覺 (靜態節點暗、live 鏈亮——「啟動即 idle」tradeoff 嫌怪=回報訊號)
- [ ] 繼承: 批次13/12/10/9 各欄

**新 parked (批次14):** Annotation 批B/C (互動+繪製+eye; R-AN 兩硬約束在帳)/nav 雙棧無
headless 圍欄/idle_fade.scn 整張 diff 假綠弱點 (P5 蓋確定性)/pull 路徑接管時 stamp 閉包應
移除/keymap key-combo 唯一性斷言 (label only)。

## Resume — next (批次15 候選)
0. (無排修項)
1. Annotation 批B+C 合併 (繪製+互動+eye hook——互動沒繪製無法活體, 合併施工; R-AN 兩硬約束
   必收: combine/copy-paste 帶 annotation+建即用)
2. point modify 族頭批 (AddNoise/FilterPoints, 鄰居模板豐富)
3. filter 三波 (Invert/ColorCorrection 級單 pass, 照 Blur/Displace 前例)
4. 互動殘項 (P pin-to-output/fence selection preview)
5. 掃描第二輪

## Cut 21 — 批次 15: Annotation 可操作 + 兩族 op 擴編 + 看門狗體系 (2026-06-13 上午) ✅
`4e46627`→`dcf38ee` 十 commit (含 watchdog 規則 `1db89a6`/`52b7be5`)。**事故與制度**: 批次15
首發兩 agent 在 05:28-05:34 無聲死亡 (session 閒置收割), 三小時後柏為人肉發現→接力 agent 進
同 worktree 續工零重做＋柏為定看門狗規則 (WORKFLOW §六: >30min 必查死活; agent_watchdog.sh
盯 transcript mtime; §六補遺: 收割後殺狗——完工與死亡在 mtime 同形, 首日假警報實戰修正)。

**ANB (`cca2e8b`+拆分 `7e4787b`+fixer `905cbad`)**: Annotation 上畫面+可操作 (Shift+A 建/拖帶
框內節點/Cmd 只搬框/雙擊 CJK 改名/縮放/一步 undo/ann:* eye hook)。接力棒咬出**前棒幾何反向**
(框 ⊆ bbox→真實框永遠搬不走; 補的 combine/copypaste 牙當場咬到=牙先行又一證)。律法債 539 行
具名→40 分鐘內拆清 (annotation_internal.h 縫)。**R-ANB 2.5 BROKEN 全修**: ①N3 家族再現=
annotation 手勢/rename 狀態跨 composition 不清 (id 每 symbol 都 ann-c1, 殘留 rename 把 A 的字
寫進 B 的同名框)→resetAnnotationGesture() 三導航路全接 ②collapsed 拖帶隱形節點→collapsed
carries NOTHING+具名 (真 collapse=child hiding 排後批)+牙 ③fork-G hit 偷點擊具名 (header/
chevron/角的 sliver)。點測 vs nRect 矩測 fork 補具名。
**PM (`4e46627`)**: AddNoise (snoise+切線幀旋轉, Shepperd 入 quat.metal.h)+FilterPoints
(scatter-copy/imod2)。R-PMF3 probe: Rotation normalized<6e-8 deterministic/Shepperd 四分支
recon<1e-15。fork: MSL col-major 等價/Count Float port/上限 8192 保守 (cap 註解誠實化
`dcf38ee`)。
**F3 (`bbfe97a`+fixer `dcf38ee`)**: Tint/ChromaticAbberation/AdjustColors 三顆 (hlsl 逐行,
GainAndBias 內聯/HSB 全鏈)。**R-PMF3 MISMATCH: ChromaB 四預設全非 .t3 真值** (5/1/3/-0.1)→修。
image filter 族現役 5 顆。registry 597 行=資料表結構性增長, 拆表切點 (L263/L457) 已勘。

**驗收欄:**
- [x] 十 commit 親手復跑 84/84 --bite 零 NO-BITE+check-arch; sweep 25/25
- [x] refuter: R-ANB 2.5 BROKEN(修)+3 SURVIVE; R-PMF3 1 MISMATCH(修)+9 SURVIVE/MATCH
- [ ] 柏為親測 (批次15 新增): ①Shift+A 建框+拖帶節點手感 (Cmd 只搬框=fork-E) ②框雙擊改中文名
  ③AddNoise 接粒子鏈拖 Strength ④Tint/ChromaB/AdjustColors 三顆濾鏡效果 (ChromaB 預設已照
  TiXL) ⑤collapsed 框=純視覺 fold (拖不帶節點, 真 collapse 排後批——嫌怪=訊號)
- [ ] 繼承: 批次14/13/12/10/9 各欄

**新 parked (批次15):** registry 按家族拆表 (切點已勘, 批次16 首位)/Annotation 真 collapse
(child hiding+CollapsedIntoAnnotationFrameId)/fork-G under-layer 重訪 (柏為嫌 on-top 再做)/
rename 中 Cmd+Z 走舊路徑未驗 WantTextInput 閘/N3 annotation 修的活體封口 scn/nav 雙棧
headless 圍欄 (續)/AddNoise Rotation 顯式牙。

## Resume — next (批次16 候選)
0. registry 按家族拆表 (law debt, 切點 L263/L457 已勘——機械)
1. point 變換族 (PolarTransformPoints/WrapPoints/BoundPoints, PARITY_GAP_SCAN §A 次梯隊)
2. particle 力族頭批 (DirectionalForce/VectorFieldForce)
3. 互動殘項 (P pin-to-output/fence selection 活預覽)
4. 掃描第二輪 (第一輪原料庫快見底)

## Cut 22 — 批次 16: registry 拆表 + 互動殘項 + point 變換族 + particle 力族 (2026-06-13 午; **Opus orchestrator 首批**) ✅
`118695e`→`7fefb44` 六 commit。--bite **93 FAILED:[]NO-BITE:[]** / check-arch / scenario **27/27**(kill+wait)。Fable 不可用→Opus 接 orchestrator 首航(見 WORKFLOW §七)。

**事實(交付)**:
- **R**(`118695e`): node_registry 600→7 家族表+中央 builder, 全<400, 35 op 零位移。point/particle 各獨立家族檔=Phase B 並行使能。
- **X**(`40f5ca9`): P=pin 選取節點輸出(g_pinnedNode 視圖層不序列化, 驅動 Output 顯示源+shell cook target, 0選取→toggle unpin); fence 拖框即時高亮預覽。2 .scn 活體牙。
- **P**(`4742203`): PolarTransform/Wrap/BoundPoints 3 顆 point 變換, 照 AddNoise 配方(shader+params+葉檔+register+家族表), 各 golden。
- **F**(`8eeb36d`): DirectionalForce/VectorFieldForce, 走 cookParticleSim 的 `_ForceKind` pinless 判別子派發(舊 TurbulenceForce 圖 byte 等價), inspector.cpp +1 藏 `_` 埠。

**refuter verdict(對抗波抓到真 bug)**:
- refuter-P **BROKEN→修**(`0c2b201`): PolarTransform 旋轉建 Z·Y·X ≠ TiXL `CreateFromYawPitchRoll`=Y·X·Z(Hamilton 非交換), 真機 GPU maxPosErr 3.3-4.76→0。polarprobe 升永久牙(Rot≠0+非均勻Scale 防回歸)。golden 漏因只測 Rot=0(任何序塌 identity)。
- refuter-檢核 **CONCERN→修**(`7fefb44`): fence rectsOverlap 非嚴格`<` vs ed commit 嚴格 imgui `ImRect::Overlaps` 邊緣相切分歧(selftest case3 瞎牙斷錯 parity)→`<=`+case3 want=false。5 op 邊界(NaN/0/empty guard 全有)+NO-BITE PASS。
- refuter-F **SURVIVE**(3 牙落地 `7fefb44`): 舊圖 byte 等價(FNV 差分 184320 粒子×30 幀逐 byte 同)/`_ForceKind` 不洩漏 UI/存檔。probe: simop FNV/forcekindcorrupt/forcekindoob。

**fork 具名(本批與 TiXL 分岔)**: X[P鍵 no-FocusMode/no-Cmd-P/single-select; fence preview-only-overlay(內建 ed 放開才 commit)/screen-space/strict-overlap]; P[PolarTransform TRS in-shader 純量組 pivot=0/shear=0≡host TransformMatrix/Euler Y·X·Z/WrapPoints floored-mod 非 MSL fmod=負側承重]; F[`_ForceKind` 判別子(TiXL 靠節點 type)/VectorFieldForce 無 field-graph 退化(1,1,1)對角/hash 葉檔內聯避撞 filterpoints]。

**🟡 柏為親測 (批次16 新增)**:
- ① 3 顆點變換 op 拖參數: PolarTransform(Mode 圓柱↔球面 + Rotation/Scale)、WrapPoints(box 摺疊)、BoundPoints(box 夾)
- ② 2 顆力場接粒子鏈: DirectionalForce(預設下沉)、VectorFieldForce(對角推; 無場時退化, 嫌怪=訊號待 field-graph)
- ③ 按 **P** 釘選取節點輸出=顯示源(再按同一顆 unpin)
- ④ 拖框選取看**即時高亮預覽**(放開後選取應與拖動中預覽一致)
- 繼承未測: 批次15(Shift+A 框/AddNoise/三濾鏡)、14/13/12/10/9 各欄

**事故與制度(Opus orchestrator 首航血)**:
- **看門狗假死×2**: X(57min 長 Opus lane)與 F 都在 >30min 安靜段被 30min 閾值誤判死亡。X 被早摘半飛快照——**gate 紀律「不在 red 上 commit」擋住, 沒封錯版**(decay 假紅意外救場), X 真完工後改用權威 worktree。→ WORKFLOW §六 修: 30→60min + 「STALE≠死, 先等 harness 完工通知/查 process 活性, 別急 harvest 活樹」。
- **並行 GPU/eye 爭用**: 多 agent 同跑 GPU selftest 跨樹 SIGKILL decay(solo exit0); 連發 scenario 不等 app 真死→.eye 爭用假紅。→ 合流驗證必待 agent 靜默; scenario sweep 加 kill+wait 間隔。
- **soundtrack intrinsic flake**: timing-sensitive 對系統排程抖動敏感, 偶發紅, solo 重跑過(非 batch 引入)。

## Resume — next (批次17 候選)
0. **🔴 排修頭位: 旋轉序共病** — `transformpoints.metal`+`randomizepoints.metal` 複製同 Z·Y·X bug(既有出貨 op, golden 不測 Rot→隱形)。修 Y·X·Z + 全旋轉 op golden 補多軸旋轉牙。(refuter-P 外溢, 背景 task_eef5757e)
1. **🔴 排修: compound_load override clamp** — load.cpp:235 對 known-def override 夾 PortSpec [minV,maxV](治所有 `_` 判別子/enum 的 OOB 載入, 非只 _ForceKind)。(refuter-F 外溢)
2. particle 深化: force 鏈多顆串接(TiXL force→force; 現單 port)/field-graph 子系統(解 VectorFieldForce 退化)。
3. point 族補完(SelectPoints/draw 族; PARITY_GAP_SCAN §A3)。
4. 互動殘(PARITY_GAP_SCAN §C: F focus/G auto-layout/playback 鍵第二梯隊/Cmd+F quick-add)。
5. 掃描第三輪(原料庫見底時派 Explore 對 external/tixl)。

## Cut 23 — 批次 17: 旋轉序共病修 + override-clamp premise 證偽 + 缺口掃描第三輪 (2026-06-13 午後; Opus orchestrator 二航) ✅
`871464a` 一 commit(fix Lane T)+ 本 docs commit。--bite **95 中 94 綠**(唯一紅=soundtrack @4x 預存環境 flake,見下)/check-arch OK/scenario **27/27**。Lane T(Opus impl)+ Lane L(Sonnet, 證偽棄)+ Lane S(Explore×3 掃描)+ refuter(Opus, SURVIVE)。

**事實(交付)**:
- **Lane T**(`871464a`, 🔴 真 bug): `transformpoints.metal` 旋轉四元數 Z·Y·X→**Y·X·Z**(鏡射批次16 PolarTransform 修)。承重引文=`external/tixl/Operators/Lib/render/_/TransformMatrix.cs:30-39`(GPU TransformPoints 走的 host matrix child op: yaw=Rotation.Y/pitch=Rotation.X/roll=Rotation.Z → `Quaternion.CreateFromYawPitchRoll` = Hamilton Y·X·Z),shader `TransformPoints.hlsl:70` 由 `qFromMatrix3Precise` 抽出。只動旋轉組法;pivot/scale/space 套用結構不變(pivot≠0 對完整 host CreateTransformationMatrix maxPosErr=2e-6,GraphicsMath.cs scalingRotation=Identity+scalingCenter==rotationCenter==pivot 抵消化簡)。golden 漏因=只測 Rot=0。牙: **xfprobe**(多軸非等角{37,53,71}+非均勻 scale,CPU CreateFromYawPitchRoll 期望對 GPU)/**rndrotlock**(incremental 序鎖)。
- **randomizepoints 共病假設證偽**: `RandomizePoints.hlsl:124-128` = incremental `qMul(rot,axis)` X→Y→Z 逐步 renormalize,與我方 `:168-171` **逐字相同** = 忠實。**不改 shader**,加 rndrotlock 鎖序。blast radius = **transformpoints only**(orient/sphere/setattr 查無同病,refuter 復查確認)。
- **Lane L(override load-clamp, 候選 item-1)premise 證偽 → 棄(零 code)**: orchestrator 讀 `point_ops.cpp:193-217` 確認 `cookParticleSim` 派發是 **if/else-if/else**,OOB `_ForceKind`(99/負/任意)落 `else`=Turbulence,**結構上無 misroute/UB**(refuter-F 批次16 SURVIVE 即此故);`forcekindoob` 牙(point_ops_selftest.cpp:410-436)已鎖此安全行為(kind99/kindNeg→symmetric turbulence,no crash,injectBug=99 變 down-push→FAIL)。TiXL 亦不 load-clamp(`SymbolJson.cs:269` `SetValueFromJson` 無 clamp,range 純 UI)。**memory 寫的「🔴 排修」是 batch16 防禦性假設(外溢),非 RED-proven bug——讀源碼即證偽。**
- **Lane S 缺口掃描第三輪**(三維度,候選見下 Resume): S1 op 庫(27 顆,前 10 cheap point modifier)/ S2 UI 視覺源碼常數 / S3 互動快捷鍵。**S1 sizing 不可盡信**(把已做的 DirectionalForce/VectorFieldForce 列為缺口、把 SelectPoints[5 shape+discard]稱 trivial)——當原料,逐顆復核。

**fork 具名**: Lane T[Euler Y·X·Z ≡ `CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)`;shader 直接 qMul 組旋轉跳過 host float4x4+qFromMatrix3,純旋轉等價]。`TransformCpuPoint.cs:67` 用 yaw=X(不同綁定,CPU-only,非 GPU 路徑,不影響)。

**refuter verdict(Opus 對抗,SURVIVE)**: ① Y·X·Z 是 TiXL 序(numpy/scipy 獨立重推 quat-dist=0;double-transpose 陷阱 4e-16 死)② xfprobe 真牙——**6 種錯序逐一改 shader 重編全 RED**(Z·X·Y=3.34/Z·Y·X=3.16/X·Y·Z=1.42/Y·Z·X=1.86/X·Z·Y=2.28),只 Y·X·Z 綠 ③ rndrotlock 真鎖(combined-quat bug-face 0.929 RED)④ pivot 2e-6 真交叉驗證(host-matrix oracle 獨立)⑤ 邊界(Strength0/1/PointSpace/負角/>360/NaN)全過 ⑥ randomize byte-faithful。**2 非阻塞 CONCERN(defense-in-depth,未修,具名)**: (C1) 既有 `runTransformPointsSelfTest` golden 用 90,90,90 等角→Z·X·Y 也碰巧過(但 xfprobe 大聲抓 Z·X·Y,牙**集合**健全,僅 golden 註解樂觀;未來若要可改非等角)(C2) pivot 忠實依賴 TiXL scalingCenter==rotationCenter(現恆真,probe 診斷會抓變動)。

**事故與制度(本批最重要)**:
- **bypasscompound 回歸險些出貨——orchestrator clean-base 診斷攔下**: Lane L 的 load-clamp 把合法 Float override(PortSpec range=UI hint 非硬約束)夾壞→破 bypasscompound leg2(reload 後 bypass flag inert)。合流後 --bite 紅,orchestrator `git stash` 退到 clean base b818c74 跑→bypasscompound **綠**(clamp 是元兇),Lane T 單獨也綠→定位 100% Lane L。**教訓: 「問 TiXL 不問假設」是命脈——memory 的防禦性候選未經 RED-proven 就當 🔴,差點賠一個回歸。親手復跑(clean-base 隔離)是攔截閘。**(Lane L dossier 宣稱「git stash 證 pre-existing」是錯的——它 stash 不完整。)
- **並行 GPU 爭用假紅再現**: 我同訊息併發 Lane T+L,Lane L worktree 的 bypasscompound 紅含 contention 成分→**clean-base 單跑才是權威**(批次16 同雷)。
- **soundtrack @4.00x chase resync storm(102/117 hard-seeks)**: clean base b818c74 同紅,solo 5/5 一致(批次16 是偶發,**今降級為一致**),1x/1.5x/2x 全綠。AVAudioUnitVarispeed 4x 在本機當前音訊排程撐不住=環境敏感,**與本批零關係**(871464a 零 audio 檔)。→ 柏為域(聲音永遠是人的),已記柏為欄。

**🟡 柏為親測 (批次17 新增)**:
- ① TransformPoints 節點轉非軸對齊角度(如 X37 Y53 Z71)→ 現在旋轉序對了(批次16 前 Z·Y·X 會轉歪);肉眼對 TiXL 同參數應一致
- ② **soundtrack 4x 變速播放**: @4x chase 在本機現在會 resync 風暴(1x/2x 正常)——是退步還是環境?要你的耳朵判(可能 BT/裝置狀態)
- 繼承未測: 批次16(3 點變換 op/2 力場/P 釘選/拖框預覽)、批次15–9 各欄

## Resume — next (批次18 候選; 無 🔴 排修待辦, 純推進——缺口掃描三維度原料已備)
施工序=三條不重疊 lane 的「便宜平行」(op shader ∥ ui draw ∥ ui keymap;WORKFLOW §八)。**S1 sizing 逐顆復核(原料非定論)**。
1. **point modifier 量產(S1 tier1, 最划算)**: 照 AddNoise 配方挑 3-5 顆 count-preserving 無外部依賴的——`SnapPointsToGrid`/`ClearSomePoints`(W→NaN 邏輯刪)/`MapPointAttributes`/`PointAttributeFromNoise`/`SamplePointsByCameraDistance`/`TransformSomePoints`(F-mask TRS)/`SnapToPoints`。**SelectPoints/SoftTransformPoints 非 trivial**(5 volume shape/falloff/discard)→各自一條 Opus lane,別塞。
2. **UI 視覺第二刀(S2, 服務北極星路線 B「節點視覺一樣」)**: 源碼常數對齊——連線粗細 idle-fade(0.25–2px 動態 vs 我方固定 1.5; editor_ui.cpp:202)/節點標題字級(我方 13px vs TiXL 18px×scale; cjk_font.cpp:16)/節點多餘 1px 邊框拿掉(TiXL 常態無邊框; node_draw.cpp:74)/選取邊框 2.5→1px/canvas bg 去藍(0.12,0.14,0.18→0.12,0.12,0.12)。**連線 bezier 形狀(S 型 vs TiXL 混合弧+snap 三角)= moderate/major**,單獨評估。pin 方→三角 moderate。
3. **互動 trivial 第三梯隊(S3)**: `Home`=jump to start time / `Shift+L`=half-speed / `I`·`U`=open/close operator(雙擊邏輯已有,各一行 keymap)/ 雙擊 annotation rename(inline rename widget 已有)/ `Shift+D`=toggle disabled(inspector 已有 checkbox)。**fence Shift/Ctrl 三模式= moderate**(需 modifier 傳進 ed SelectionCommit)。**G auto-layout / shake-to-disconnect / Alt-drag pan = moderate,各自評估**(TiXL G 自己也是 TODO)。
4. particle 深化(force 鏈/field-graph)= subsystem 級(§D),需 ShaderGraphNode 子系統前置,排後(WORKFLOW §八:子系統才升 session 平行)。
5. 掃描第四輪(本批三維度原料夠 2-3 批用,候選見底再派)。

## Cut 24 — 批次 18: TransformSomePoints + UI 視覺常數 + 互動快捷鍵 (2026-06-13 午後; Opus orchestrator 三航) ✅
`edc8cfe`(三 lane)+`680848d`(Lane P fixer)二 commit。--bite **97 中 96 綠**(唯一紅 soundtrack @4x 預存環境 flake)/check-arch OK/scenario **28/28**。三條不重疊 lane 便宜平行(op∥ui draw∥ui keymap, WORKFLOW §八),全 Sonnet implementer。

**事實(交付)**:
- **Lane P — TransformSomePoints**(point/transform count-preserving modifier): shader+params+leaf+register+家族表+golden+xfsomeprobe。旋轉 **Y·X·Z**(繼承批次17,RED 證 Z·Y·X→maxPosErr 1.44)。WIsWeight×W channel 加權(TiXL 機制)。**預設 transform-all 查證忠實**: TiXL .t3 DefaultValue Take=1/Skip=0/LengthFactor=1.0/WIsWeight=false → group 邏輯 indexInGroup 0<Take 1 → 全部點被變換。selection-range(Take/Skip/RangeStart/OnlyKeepTake/Scatter)誠實 deferred——**NodeSpec 不暴露=無死旋鈕**(批次8 假旋鈕雷的反面)。
- **Lane V — UI 視覺常數對齊 TiXL**(路線B): 節點常態邊框 1px→0(DrawNode.cs:121-127 只 AddRectFilled)/選取邊框 2.5→1px(DrawNode.cs:147)/hover 邊框 2→1px(DrawNode.cs:156)/canvas bg 去藍 0.12,0.14,0.18→0.12,0.12,0.12(UiColors.cs:29)。color selftest 動態讀 kBg 仍綠。eye 佐證生效。
- **Lane K — 互動快捷鍵**(S3): Home=jump to start(FactoryKeyMap.cs:31)/Shift+L=half-speed(:25)/I·U=open·close operator(:57-58 複用雙擊進出 compound)/Shift+D=toggle disabled(:47)。keymap.cpp 資料表加列+handler,hand.cpp 補 "home"。活體 keys_lane_k.scn 12 步全 PASS。

**orchestrator 合流復查 verdict(本批的牙)**: **Lane P 抓出憑空發明的 Strength port**(TiXL TransformSomePoints.cs `grep Strength`=0)——違反 UI parity 北極星。fixer 移除三處(NodeSpec/params→_pad_w 維 64B/shader lerp)+golden injectBug 重做(Strength=0 → Z·Y·X/identity/WIsWeight=0,全 RED 證)。**這是批次8「假旋鈕」雷的變種——憑空加 TiXL 沒有的旋鈕,合流讀 NodeSpec+TiXL .cs 對照才抓到。** Lane V/K 低風險(rubric 複核級)由結構閘覆蓋(scenario 28/28+color/keymap selftest+TiXL 源碼引文),未派獨立 refuter。

**fork 具名**: P[TRS in-shader 無 Pivot/Shear port·Euler Y·X·Z·Space WorldSpace baked→ObjectSpace(cook ctx 無 view transform)·selection-range deferred(NodeSpec 不暴露)·WIsWeight×W=TiXL 加權]; V[節點無常態邊框=照 TiXL]; K[I-requires-selection(我方 single-select 無 HoveredItem)·Shift+L no-ladder-below-half(Inspector Speed 最小 0.5)·Shift+D single-select main-output]。

**S1 缺口掃描 sizing 復核(重要,原料校正)**: S1 tier1 估值**不可盡信**——逐顆查 TiXL .cs input 型別發現: 僅 **TransformSomePoints** 真 cheap standalone(Points+scalar TRS+F-mask); **SnapPointsToGrid**(2nd BufferWithViews)/**MapPointAttributes**(Texture2D+Curve+Gradient)/**ClearSomePoints**(2nd buffer) 皆有隱藏 buffer/texture/curve 依賴=非 trivial。下批挑 cheap point op 必先 grep .cs input 型別,別信掃描估值。

**🟡 柏為親測 (批次18 新增)**:
- ① TransformSomePoints 節點: 拖 TRS 變換點雲;WIsWeight 開→用點的 W 當權重 lerp(選擇性變換的雛形,完整 Take/Skip selection 待後批)
- ② **節點視覺變了**(對齊 TiXL): 節點無邊框(扁平)/選取線變細(1px)/畫布背景純灰不帶藍——順眼嗎?還是邊框拿掉太空?(品味你定)
- ③ 新快捷鍵: Home=播放頭歸零 / Shift+L=半速 / I=進節點(選中時) U=出 / Shift+D=停用節點
- 繼承未測: 批次17(TransformPoints 旋轉/soundtrack 4x)、16–9 各欄

## Resume — next (批次19 候選; 無 🔴 排修, 純推進)
1. **cheap point op 第二批** — **驗證型掃描已跑(批次19,grep .cs InputSlot 型別)**: CHEAP∧缺 共 25 顆(只 Points buffer+scalar,無 Texture2D/Curve/Gradient/2nd-buffer/Field)。**⚠ 「cheap inputs」≠「trivial impl」**: SelectPoints(5 volume shape+discard)/SnapPointsToGrid(多 param) 是 cheap-input 但 moderate-impl。**高信心真 trivial(1 buffer/generator+scalar,pattern 已有,fidelity 面小)= 批次19 在做: ClearSomePoints(modify,W→NaN by range)/WrapPointPosition(transform,fold to box,≠我方 WrapPoints torus)/HexGridPoints(generate,hex 格,GridPoints pattern)**。次批候選(verified cheap):SubdivideLinePoints/ReorientLinePoints(切線算 rot)/SimNoiseOffset·SimForceOffset(⚠sim-state 待確認 stateless)/BlendPoints·PickPointList(combine 雙 buffer)/SnapPointsToGrid/SelectPoints(moderate-impl 各自 lane)。HAS-DEP 排後:Texture2D 族(MapPointAttributes/Sample*/TransformWithImage)/Field 族(MoveToSDF/SelectPointsWithSDF=ShaderGraphNode 子系統)/Mesh 族(PointsOnMesh/MeshVertices)。
2. **UI 視覺第三刀(S2 緩做項)**: 節點標題字級 13→18px(需評估 CJK atlas/node 尺寸連動)/連線 idle-fade 粗細(0.25–2px,editor_ui.cpp:201-203 connectionLineColor+lthick 要接 dt/idle factor)/pin 方→三角(node_draw.cpp:31 AddRectFilled→AddTriangleFilled,moderate)。連線 bezier 形狀=major 再議。
3. **互動 S3 第二梯隊**: fence Shift=add/Ctrl=remove 三模式(moderate,modifier 傳進 ed SelectionCommit)/雙擊 annotation rename(inline rename widget 已有)/G auto-layout(moderate,TiXL 自己也 TODO,演算法要先定)。
4. particle 深化(force 鏈/field-graph)= subsystem(§D ShaderGraphNode 前置)排後。
5. soundtrack @4x(task_adc40d12 已派 chip)——柏為域,非阻塞。
6. 掃描第四輪(原料見底再派)。

## Cut 25 — 批次 19: 3 顆 cheap point op (killed-agent salvage) + fence 脆牙修 (2026-06-13 晚; Opus orchestrator 四航) ✅
`552aff4` 一 commit。--bite **100 中 99 綠**(唯一紅 soundtrack @4x 預存環境 flake)/check-arch OK/scenario **28/28**。實作=背景 Sonnet fan-out agent(漏設 isolation:worktree→**直接在主樹幹活**);完工過 check-arch 後被 session 閒置收割(killed,未交 dossier)。

**事實(交付)**:
- **WrapPointPosition**(transform): box-fold,**≠我方 WrapPoints(torus)**。逐字照 WrapPointPosition.hlsl(Padding=Size.x*0.1/offsetFactor/W edge-fade/NaN recovery)。fork: UseCamera/WriteLineBreaks baked 0。
- **SnapPointsToGrid**(transform): grid-round,floored-mod(非 MSL fmod)/4 modes(Center/Corners/AxisCenter/AxisEdge)/ApplyGainAndBias。fork: Scatter/UseWAsWeight/UseSelection baked。**🟡CONCERN(待後批 refuter)**: ApplyGainAndBias 是從 bias-functions.hlsl **重建非逐字**,預設 GainAndBias 近 identity 低風險。
- **HexGridPoints**(generate): Pattern=2 Hexa 逐字(HexOffsetsAndAngles 12 表/hexAttrIndex/HexScale=0.578)。fork: Pattern baked 2;**Count port=我方 generator 輸出容量慣例(非 TiXL 旋鈕,TiXL 用 CountX×Y×Z)**。
- **(計畫 vs 實際)**: Cut 24 Resume item 1 寫 ClearSomePoints,但實際換成 SnapPointsToGrid——ClearSomePoints 需 Take/Skip selection 邏輯(clear SOME=moderate-impl),非 trivial,移後批。

**orchestrator 合流自驗(killed agent 無 dossier→更嚴格)**:
- 逐顆對 TiXL .hlsl 復查 fidelity + NodeSpec ports vs TiXL .cs(ours⊆TiXL,無發明行為旋鈕)+ 3 顆 golden 親手 RED 證(normal exit0/injectBug exit1)。
- **修 1 真缺陷**: HexGridPoints PI 常數 `3.141578f`(killed agent 打錯)→`M_PI_F`(兩處,角度微偏非忠實;siblings 用 M_PI_F)。
- **fence_preview 回歸=脆牙非 code bug(clean-base 證偽)**: `.selectedNode==1` 假設 ed 選取序 id-sorted,但 `=ed::GetSelectedNodes(...,1)[0]` 是 **hash-ordered**;批次19 registry 變大→hash 序翻轉 1→2(**SAME committed set**——`partial-overlap-matches-commit` 仍綠證集合正確,`fenceLastCovered==[1,2]` 仍綠)。修=放寬成 order-independent(`==1 or ==2`)+ 註解記因。

**事故與制度(四航血)**:
- **背景 agent 漏設 isolation:worktree → 在主樹幹活**: 反而省了 harvest(成品直接在主樹),但 killed 後沒 dossier。**教訓: 派背景 implementer 必設 isolation:worktree(否則主樹被佔+被殺無隔離);或接受主樹模式但要快收**。killed-agent 成品=gates 過≠fidelity 過,合流自驗補回 dossier(抓到 PI 缺陷+fence 脆牙真因)。
- **fence 脆牙**: 任何對 `.selectedNode`(單值 cap-1,ed hash 序)的多選斷言都脆;後批若多選驗證頻繁→考慮 eye 露 `.selectedNodes` 全集(order-independent)。

**🟡 柏為親測 (批次19 新增)**:
- 3 顆點 op 拖參數: WrapPointPosition(box 摺疊,點超出盒子折回)/SnapPointsToGrid(點吸附網格)/HexGridPoints(生成六角格點雲)

## Cut 26 — 批次 20: /sw-node-batch 誕生 + 家族並行三顆 op (2026-06-14 凌晨→午; Opus orchestrator) ✅
**新 workflow `/sw-node-batch`**(`.claude/commands/`)=/sw-batch 特化版,消除 point GPU op 家族並行撞點。3 commit:
- **收割 Lane C**(`19ca60d`): 接 06-13 夜 overnight 殘留,7 顆 math value op(Sqrt/Pow/Modulo/Ceil/SmoothStep/Log/Cos)。
  refuter 0 BROKEN(3 SURVIVE/4 CONCERN=預設值未對齊 TiXL .t3 已修:Sqrt/Pow/SmoothStep Value→1.0/Log Base→1.0)。
- **Phase 0 避撞**(`25bc724`): registerBuiltinPointOps 拆 6 個 point_ops_register_<family>.cpp(中央凍結家族呼叫)
  + CMake file(GLOB point_ops*.cpp / shaders/*.metal CONFIGURE_DEPENDS)。零行為變更,28 op 盤點吻合。
  **RED 證承重路徑=RadialPoints**(唯一不自註冊;HexGrid 等 golden 在自己檔內又 register→拔 registrar 不紅)。
- **Phase 1 三顆**(`01224ad`): generators=DoyleSpiralPoints2(Opus)/point_modify=ClearSomePoints(Sonnet)/
  image_filter=ChannelMixer(Sonnet)。家族 worktree 並行零衝突(各改自己家族檔;共享 point_ops.h/kTable/CMake deps
  orchestrator 合流統一加)。--bite PASS=102(soundtrack @4x 預存環境紅非本批)/check-arch 綠/三顆 green+bug red。

**事實(三顆)**:
- **DoyleSpiralPoints2**: Doyle 圓堆積螺旋。**承重發現=非 .hlsl 逐字**:核心算術在 `_DoyleSpiralRoot.cs` 的 2D
  Newton-Raphson 求根(CPU,起點 z=2/t=0 迭代 P/Q→A/B/R)→GPU kernel 吃 A/B/R。掃描判 cheap(值參數)但 cheap-input≠
  trivial-impl。⚠️**無 TiXL ground-truth**:TiXL 焊死 Windows/DX11 跑不起來→只能讀碼;迭代求根讀碼確信度低,最強驗證
  (跑兩邊比 A/B/R)做不到→agent 自洽 C++ 復算+幾何斷言(對數臂 16.68x)。柏為認可不外包 Win 對照。
- **ClearSomePoints**: hash(Ratio,Seed,Repeat,Resolution) 標 packed_float3 NaN kill。3 fork 具名(Resolution=0
  guard/packed_float3 NaN≠HLSL float3/hash11u inline)。4 牙。`.cs` 的 Spaces/OffsetModes enum 非 InputSlot=無發明。
- **ChannelMixer**: 4x4 通道矩陣 mix(out=Σ Multiply*ch+Add,MixChannels.hlsl 逐字)。fork:GenerateMipmaps 讀不 dispatch。2 牙。

**流程修復(柏為喊停試壓→改 `/sw-node-batch`)**:
- **昨夜 overnight 丟 5 顆真因**=agent 不 commit + worktree 被收割(session 閒置 harness 回收,未 commit 改動蒸發)→
  **Phase 2 固化先於驗證**(完工通知即進 worktree commit 到 branch,先固化後驗證;今晚三顆 branch e2d2423/324024a/60163a4)。
- **昨夜 A/B 自創 5 顆 TiXL 沒有的 op**(BoxGrid/Tube/Concentric/SelectByRange/ScaleAboutCenter)真因=lane agent
  自掃缺口+自挑→**掃缺口+挑 op 收歸 orchestrator 前置裁決**,工單只給指定 op + TiXL 路徑 + 模板。
- **無人值守 commit 邊界**:機械/無視覺/selftest 強驗→主線;有視覺需柏為肉眼驗→停 branch+PANEL 待驗(完成定義不被「不中斷」吃掉)。
- 撞點分析修正:四撞點→兩真撞點(register+CMake);selftest 非並行撞點(golden 在自己 op 檔)。

**🟡 柏為親測 (批次20)**: 三顆 op app 開好在 palette(DoyleSpiralPoints 搜尋可見),柏為認可視覺+DoyleSpiral 處理方式。
  完整親手玩(拖 DoyleSpiral→DrawPoints 看螺旋/ClearSomePoints 拉 Ratio/ChannelMixer 拉通道)=隨手。

## Cut 27 — 批次 21: 2 顆家族並行 op (ToneMapping/SnapToPoints) + count-policy driver 修 + Cut25 CONCERN 清 (2026-06-14 午; Opus orchestrator, `/sw-node-batch` 第二航) ✅
三條 worktree lane 並行(2 implementer Sonnet + 1 refuter Opus),家族 disjoint 零衝突;merge 共享檔(point_ops.h/selftests.cpp/CMakeLists)純加性兩邊留。
commit 序:2c5a6db(refuter merge)→db98ff9(ToneMapping merge,含 AgX 修 40bb5eb)→14e90ff(SnapToPoints merge)→c58f487(count-policy driver 修)。
--bite **PASS=104**(批20 102→+2:tonemapping/snaptopoints;refuter parity 併進既有 snaptogrid golden 不增數)/NO-BITE:[]/check-arch 綠/scenario point_modify_chain+blur_chain PASS。soundtrack @4x 既有環境紅非本批。

**前置掃缺口攔下三個 compound 陷阱(問 TiXL 不問假設這關擋住,沒丟給 agent)**:
- **HSE**(ledger 標「最純 per-pixel」)實有 `FxTexture` 第二輸入 + `.cs` 無 shader 路徑 = compound(.t3 圖接線),非單 .hlsl。
- **ColorGrade** vignette 參數在 `.cs` 但不在 `compute-ColorGrade.hlsl` cbuffer → vignette 是 compound 內另一 op。
- **PointAttributeFromNoise** 無 .hlsl + Gradient curve + 8-way attribute enum × 4 通道路由 = moderate-compound(即使關 UseRemapCurve)。
→ 教訓:ledger 的 cheap 判定 sizing 不可信(承襲批18/DoyleSpiral),orchestrator 必親讀 .cs/.hlsl 確認「單 .cs + 單 .hlsl + 全參數在一個 cbuffer」才算真 cheap。

**事實(兩顆 op)**:
- **ToneMapping**(image_filter): 單 texture per-pixel,enum Mode(Aces/Reinhard/Filmic/Uncharted2/AgX/AgX_Punchy/None)用 float if/else 判別子(貼 _ForceKind)。fork[verbatim-TiXL-bug]:`ToneMap.hlsl:105 'Mode<4.5'` 應為 5.5→AgX_Punchy(5)在 TiXL 也不可達,逐字保留。**orchestrator 復查抓到真 bug**:agent 在 `m*col` 約定下又把 AgX 兩個矩陣額外轉置一次→抵銷成錯(metal-cpp 轉置陷阱,過度修正),Reinhard selftest 沒覆蓋矩陣故漏咬。正解=Metal 欄=HLSL 列原序(column-major 讀 row-major data 即轉置)。經驗證實:AgX parity maxErr 0.01375(broken)→0.00044(fixed)。補 AgX parity 牙(C++ row-vector 復算 ToneMap.hlsl)鎖死。
- **SnapToPoints**(point_combine): 2-buffer 索引配對 lerp(A=Points1[i]→Snap=Points2[i],smoothstep(BF+D,D,dist)*MaxAmount),模板 CombineBuffers。fork[count-guard]:Points2 index clamp 防 OOB(TiXL 假設等長無 guard)。**agent 誠實標出真 production bug→orchestrator 修 shared-runtime**:SnapToPoints 落 point_combine 家族,但語義是 transform Points1(Points2 當 snap target),輸出筆數=Points1 非 sum。原 cook driver 對多 Points 輸入一律 sum(CombineBuffers concat 合約)→output buffer=2N、kernel 只寫前 N、後 N 垃圾→下游 DrawPoints 畫 N 個垃圾點(柏為一接就見=不是忠實 clone)。selftest bypass graph 沒咬到。修:OpReg 加 `countFromFirstPointsInput`(default false=sum 不變,CombineBuffers 不動),flat+resident 兩處 driver 套,SnapToPoints opt-in;加 `PointGraph::debugCookedCount` accessor + graph golden(RadialPoints×2→SnapToPoints 斷言 cooked==N 非 2N),驗牙:關旗標→cooked=64 FAIL,開→32 PASS。

**Cut 25 CONCERN 清(refuter BROKEN→修)**: SnapPointsToGrid 的 `applyGainAndBias` 重建非逐字——漏 TiXL `g` 分支(`g>=0.5` 應 schlick→bias,我們永遠 bias→schlick)+ 漏兩個 hard early-out(value>0.9999→1/<0.00001→0)。舊 golden 只跑 neutral(0.5,0.5)=恒等故全盲。逐字重寫對齊 bias-functions.hlsl scalar form + 加 parity 牙(gain=0.8/bias=0.3 命中 g>=0.5 分岔支):maxErr 0.032(broken)→0.000(fixed)。

**流程驗證(`/sw-node-batch` 第二航,固化先於驗證生效)**: 每條完工通知到即進 worktree commit 快照到 branch([固化快照]),三條都沒被收割蒸發。orchestrator 親手復跑每顆 green/bug(agent 說綠不算數)→抓到 AgX 轉置 bug(agent 漏)→自修。合流後 merge 衝突=三共享檔純加性手解。

**🟡 柏為親測待辦 (批次21)**: ①ToneMapping app 拖→接 RenderTarget→看 Mode 切換(Aces/Reinhard/AgX 色調曲線差異)+Exposure 拉。②SnapToPoints 拖兩個生成器→接 Points1/Points2→拉 MaxAmount/Distance 看點被吸過去,**確認下游 DrawPoints 無垃圾點**(count-policy 修的肉眼驗收)。两顆都 selftest 強驗+refuter,過閘已入主線,視覺驗收非阻塞。

## Cut 28 — 批次 22: 分流快織 11 顆 cheap op (math 6 + point_modify 3 + image 2) (2026-06-14 午後; Opus orchestrator, `/sw-node-batch` 第三航) ✅
**起因(柏為提問→拉線)**: 柏為問「節點慢=之前 DirectX/Windows 語法、換 Metal 每細節重定義?」→拉開兩組混淆: (a)生產慢≠執行慢; (b)語法翻譯≠parity 驗證。實證 DoyleSpiral .metal vs TiXL .hlsl: 數學本體幾乎逐字抄(cos/sin/pow/qMul HLSL≈MSL),真慢在「證明逐位元一樣」(parity ledger/refuter/不發明 port)。結論=分流: 便宜 op 衝量、複雜 op 慢工。柏為定: 一次 ≥10 顆才停。
**缺口盤點**: 73 核心 point op 中複雜(sim/mesh/texture/curve/雙buffer)~30(41%,regex 偏低,實際近半)、便宜 43。便宜桶已做 21→剩 22,但**逐顆 grep .cs 復核砍掉 7 顆假 cheap**(MoveToSDF/SelectPointsWithSDF=SDF volume、PointTrail/PointTrailFast/KeepPreviousPointBuffer=feedback、PairPointsFor*=count-changing、Subdivide/Resample/Spline=可變輸出)→跨家族補 math 純值 + image fx 湊 12 顆(留餘裕)。
**commit 序**: 9ef8add(math 6)→deec744(point_modify 3)→bf5bd0c(image 2)。--bite **PASS=109**(批21 104→+5: reorientlinepoints/selectpoints/softtransformpoints 三 selftest + pixelate + sharpen; math 6 在 mathops 條目內不增名)/NO-BITE:[]/check-arch 綠。soundtrack @4x 既有環境紅非本批。

**事實(11 顆)**:
- **math 6**(CPU 值節點,`value_eval_ops.cpp`/`node_registry_math.cpp`): Round/Atan2/Sigmoid/AddVec3/SubVec3/ScaleVector3。refuter 6/0 逐字: Sigmoid 指數**無負號** `1/(1+e^(Stretch·v))`(Sigmoid.cs:18 真實,非標準 logistic)/Atan2 引數序 **(X,Y)** 非 (Y,X)(Atan2.cs:17)/Round RoundRatio=0 passthrough+負值 fmod(Round.cs:23-34)/vec3 component-wise(ScaleVector3 B 為向量乘非 scalar),無發明 port。
- **point_modify 3**(count-preserving GPU): ReorientLinePoints(切線 qAlignForward2+qSlerp(Amount),丟 .hlsl main() 未讀的 dead ports Center/UpVector/WIsWeight/Flip)/SelectPoints(volume Sphere/Box/Plane/Zebra/Noise+ApplyGainAndBias→FX1/FX2,position 不動)/SoftTransformPoints(volume falloff 軟變換 Position/Rotation/FX1)。
- **image 2**(per-pixel filter): Pixelate(tile 量化,fork[具名]: TiXL per-cell Shape texture 省略=.t3 default white no-op,math 可證)/Sharpen(3×3 desaturated-Laplacian unsharp mask,fork: Clamp 取樣 vs TiXL MirrorOnce 1px edge ring/RGBA8 vs R16F HDR)。

**refuter 5/0(point_modify 3+image 2 高風險 fork 逐字)**:
- **旋轉序最高風險→沒爆**: SelectPoints volume rotation **Y·X·Z** 與 probe-verified `transformpoints.metal:44-46`(已證偽 Z·Y·X)同構; SoftTransformPoints 旋轉**根本非 Euler/非 CreateFromYawPitchRoll**=逐點 RotateAxis 的 X→Y→Z angle-axis 序(refuter 釐清 orchestrator prompt 把兩件事混為一談,逐字對 SoftTransformPoints.hlsl:104-115)。批次16/17 qMul 接反雷沒重演。
- TransformVolume 解析重建逐字等價(`-VolumeCenter/(VolumeStretch·VolumeSize)`,.t3 確認 volume 無 rotate slot)。
- **ApplyGainAndBias 兩顆逐字**(對 bias-functions.hlsl scalar form,與 Cut 27 SnapToGrid 修正版一致)→memory 🟡 公式本體可清; **但 SnapToGrid 該檔本身不在本批,其他塑形仍需親驗 snaptogrid.metal,不能僅憑本批全清**。
- Pixelate/Sharpen kernel 逐字、無憑空旋鈕(批8/18 假旋鈕雷沒重演)。

**TransformFromClipSpace BLOCKED(基建缺口,agent 誠實擋下不出貨)**: 整 kernel = `mul(P, CameraToWorld)/w`,需 camera/view matrix; `.cs` 零可操作 port(Spaces 是 private 非 [Input]),`.t3` 接 render-time 注入的 TransformsConstBuffer。simple_world `PointCookCtx` 無 camera seam(grep 零 consumer)。發明 camera port 違反「不發明 port」、bake identity=假 no-op→agent **不出貨假的**,回報基建缺口。→Resume 批次23: 需先在 PointCookCtx 開 camera/view matrix 接縫探針。

**流程新血(`/sw-node-batch` 第三航)**:
- **run_all_selftests.sh 自己不 build**(只解析 selftests.cpp kTable 原始碼+跑現有 binary,第25行假設 binary 已存在)。合流後直接跑 run_all=驗**舊 binary**→point_modify 假 FAILED(新 flag 不認得)+math 假 PASS(舊 golden)。**鐵律: 合流後必先 `cmake -S app -B app/build`(reconfigure 重展 glob 抓新 .metal/.cpp)+`cmake --build`,再 run_all --bite**。worktree 因 agent_worktree_setup 自帶 build 沒露此雷,主樹合流才現形。
- `grep -c` 無匹配 exit 1 會短路 `&&` 鏈(自傷一次→build 沒跑→假 exit 1)。
- 三共享檔(point_ops.h/selftests.cpp/CMakeLists)`git apply --check` 純加性疊加(image append 位置與 point_modify 不重疊,皆 CLEAN); 順手補 point_modify 漏的 3 params.h 進 `SW_SHADER_DEPS`(incremental metallib dep tracking)。

**CONCERN**: `node_registry_point_modify.cpp` 459 行(>400 soft warning)=純 data-driven NodeSpec 表(rule 7 sanctioned 非 logic),check-arch 過。候選 per-sub-family 拆,cross-cutting 決策留 orchestrator,非阻塞。

**🟡 柏為親測待辦(批次22)**: image 2 顆視覺驗收(非阻塞,selftest 像素級強驗 kernel 數學已入主線)——①Pixelate 拖→接 RenderTarget→看像素塊外觀+Divisor/TileAmount。②Sharpen 看 HDR ringing 強度(RGBA8 golden 看不出 overshoot 1.05 vs 5.0,都 clamp 255)+MirrorOnce vs Clamp 邊緣。math/point_modify 9 顆無視覺,selftest+refuter 強驗=已完成。

## Cut 29 — 批次 23: 裝 10 顆可用節點 (5 math vec3 + 5 image filter) (2026-06-14 午後; Opus orchestrator, `/sw-node-batch` 第四航) ✅
**柏為新指令(規則變更)**: 不問柏為、**不測畫面(柏為自測)**、裝到 10 顆能用為止、最後交付**節點使用手冊**(作用/用途/單位/範圍/關係)。品質閘仍 selftest 強驗+護欄,跳過 refuter 重對抗(純值+per-pixel filter 風險低)+跳過視覺驗證。
**commit**: `0205543`(一個 commit 含兩 lane,合流時 math+image 同進 working tree,amend 補正)。--bite **PASS=114**(批22 109→+5 image selftest;math 5 在 mathops 內不增名)/NO-BITE:[]/check-arch 綠。

**事實(10 顆)**:
- **math 5**(vec3,value_eval_ops): Magnitude/DotVec3/Vec3Distance(→Float)/Vector3Components(vec3 拆 X/Y/Z 三 Float,讓 vec3 接 Float-only op)/RotateVector3(Rodrigues 等效 CreateFromAxisAngle,fork[angle-degrees]÷180×π/fork[axis-normalize])。
- **image 5**(per-pixel filter): DetectEdges(4-鄰域絕對差**非 Sobel**+OutputAsTransparent 透明線稿)/ChromaticDistortion(徑向膨脹+N-sample 色散)/VoronoiCells(iq 兩-pass,輸入圖=特徵場)/PolarCoordinates(雙向直角↔極座標 Mode enum)/EdgeRepeat(鏡像線對折,**MirrorRepeat sampler=TiXL .t3 Wrap=Mirror 非 clamp**)。

**前置(3 Explore)**: math 8 GREEN 挑 5(跳純 int)/image 5 真 GREEN(逐顆驗單.cs+單.hlsl+單 cbuffer;砍 MirrorRepeat 3-cbuffer/Dither·Steps·BubbleZoom 需 Gradient/DirectionalBlur·Mosaic 需第二 texture)/**point 家族見底**(剩 TransformFromClipSpace 卡 camera seam+BoundingBoxPoints count-changing+atomic)。

**流程新血(必記)**:
- **`git diff --name-only` 迴圈 cp 在此環境失效**(渲染怪癖→cp 沒落地,主樹 0 處,build 跑舊碼假綠)。鐵律=合流**明確列檔名 cp**,診斷靠 grep 主樹 vs worktree 內容數。
- **同家族兩 sub-lane 共享檔=`git merge-file current base other` 三方合併**(A/B 都 append「Sharpen 之後」同位置→自動合不重疊→4 檔全乾淨)。
- commit 粒度怪癖:假 hash 回顯未落地→改動留 working tree。**教訓:commit 後 `git branch --contains <hash>` 驗真存在,別信回顯。**

**🟡 柏為親測**: image 5 全視覺,selftest 像素級強驗 kernel 已入主線,柏為自測畫面。math 5 無視覺=完成。**交付=節點手冊直接給柏為。**

## Resume — next (批次24 候選; 無 🔴 排修, 純推進)
**批次23 已消化**: math vec3 5 + image filter 5。**point cheap 家族見底**(便宜桶+generators/transform/combine 掃完,剩的卡基建/compound/count-changing)。
0. **基建解鎖(開新 cheap 礦脈前置)**: ①TransformFromClipSpace/BoundingBoxPoints 需 PointCookCtx camera matrix+count-policy(N→1)接縫。②CommonPointSets/RepetitionPoints CPU 生成需 map-buffer 接縫。③SnapToGrid ApplyGainAndBias 其他塑形親驗。
1. **image_filter 第二梯隊**(逐顆驗單 cbuffer): KeyColor/ConvertColors/Posterize/Levels/Invert/HueSaturation。
2. **math 第二梯隊**: CrossVec3/NormalizeVector3/EulerToAxisAngle(batch23 Explore secondary)。
**批次22 已消化**: cheap point op 一梯隊(point_modify 3 + math 6 + image 2)。TransformFromClipSpace 卡基建。
0. **TransformFromClipSpace 解鎖**: PointCookCtx 開 camera/view matrix 接縫(subsystem 探針,非 leaf lane)→解鎖 clip-space 系 op。SnapToGrid 該檔 ApplyGainAndBias 其他塑形親驗(本批只清公式本體)。
1. **cheap point op(家族並行,用 `/sw-node-batch`)**——候選庫=`docs/agent/overnight/lane_*.md`,但 **image_filter 第一梯隊已被掃描誤判**(HSE/ColorGrade=compound,見 Cut27),剩真 cheap:
   - image_filter: **ToneMapping 已做**;真單-.hlsl 候選待逐顆親驗(KeyColor/Pixelate/ConvertColors 先確認單 cbuffer)。Tint/AdjustColors/ChannelMixer/ToneMapping=已做。
   - point_modify: SubdivideLinePoints/ReorientLinePoints(切線 rot Y·X·Z,W weight)。PointAttributeFromNoise=compound 排除。SelectPoints=moderate。
   - point_combine: **SnapToPoints 已做**(count-policy 機制現成,後續 2-input op 可重用 countFromFirstPointsInput)。
   - generators: CommonPointSets/RepetitionPoints(⚠CPU 生成=新接縫,先做 PointCookCtx map buffer 接縫探針再開)。
2. **UI 視覺第三刀**: 標題字級 13→18(CJK atlas/node 尺寸連動)/連線 idle-fade(editor_ui.cpp:201-203)/pin 方→三角(node_draw.cpp:31)。
3. **互動 S3 二梯隊**: fence Shift/Ctrl 三模式/雙擊 annotation rename/G layout(moderate)。
4. particle 深化=subsystem(§D)排後。soundtrack @4x=task_adc40d12 chip 柏為域。
5. DoyleSpiral 視覺/數值 ground-truth: 可外包 Windows TiXL lane(`to_windows_tixl` kit)Player 截圖對照(柏為暫認可不做)。

**`/sw-node-batch` 教訓沉澱(第二航)**: ①orchestrator 前置掃缺口必親讀 .cs/.hlsl 認 compound(單.cs+單.hlsl+全參數一個 cbuffer 才真 cheap),ledger sizing 不可信。②agent 完工≠正確:矩陣轉置/count-policy 這種 selftest 沒覆蓋的承重點,orchestrator 親手復查抓得到(AgX 經驗性牙+count graph golden)。③shared-runtime 修(count-policy driver)是 orchestrator 域,加 per-op 旗標 default 不變舊行為。

## Cut 30 — 批次 24: 一次 20 顆 (math 10 + point_modify 2 + image 3 + particle 3 + combine 2) (2026-06-14 午後→傍晚; Opus orchestrator, `/sw-node-batch` 第五航) ✅
**柏為指令**: 一次做 20 顆。**commit**: `1be97a4`(feat 20 顆,squash 自固化/檢查點/merge 雜訊) + `0281c77`(fix Dither _PRIME0,refuter BROKEN→修)。--bite **PASS=122**/NO-BITE:[]/check-arch 綠。soundtrack=已知 @4x 並行環境紅(standalone PASS),非本批。

**事實(20 顆,5 家族;generators 本批 0=誠實放棄,TiXL 剩餘 generator 全需 input points/mesh)**:
- **math 10**(value_eval_ops,無 shader): InvertFloat/CrossVec3/LerpVec3/NormalizeVector3/RoundVec3/AddVec2/DotVec2/Vec2Magnitude/Vector2Components/ScaleVector2。vec2 走 Atan2 的「拆兩 Float port」慣例;MultiInput 不支援故砍 Sum/PickFloat。
- **point_modify 2**: OffsetPoints(qRotateVec3 沿點朝向平移,無新旋轉序)/PointAttributeFromNoise(旋轉序 X→Y→Z;RemapNoise gradient 為 TiXL optional port 具名少接)。
- **image_filter 3**(per-pixel single-pass,HLSL→Metal 逐行): Dither/NormalMap/ChromaKey。NormalMap/ChromaKey **無 .cs**(port 權威=cbuffer)+無 .t3 預設(預設值推定,待柏為)。
- **particle force 3**(_ForceKind 判別子 append=3/4/5): VelocityForce/AxisStepForce/SnapToAnglesForce,全 stateless。
- **point_combine 2**: PairPointsForLines(count=max(A,B)×3 含 NaN divider)/PickPointList(Index%N 直通選中 buffer)。

**refuter(3 Opus 對 .hlsl/.cs 逐行,聚焦旋轉序+HLSL→Metal 忠實度)**: particle 3 SURVIVE(hash 兩輪/f² 雙乘 quirk/atan2 arg 序/4 平面模式/角度量化全對)+point_modify 2 SURVIVE(qRotateVec3/X→Y→Z/simplex 全常數 0.91·1234·0.123·42.0·0.6/enum 路由/Center 正負/RemapNoise fork 全 verbatim)+image **NormalMap·ChromaKey SURVIVE**(splat 化簡/swizzle 展開/4 分支/min 鄰域全對)。**Dither BROKEN→修**: hash11u `_PRIME0` 港成憑空 1597334677u,權威=13331u(hash-functions.hlsl:4);只影響 Method≥0.5 hash 分支,Bayer 預設路徑+golden 抓不到→`0281c77` 對權威逐字修。

**named fork / parity 註記**: PointAttributeFromNoise RemapNoise optional 少接(else 分支=預設路徑,行為等價)/SnapToAngles CameraSpace=identity bake(解析還原,待 camera seam 還原 b2 矩陣)+RandomSeed `(int)` 轉型 latent(Seed 永 baked 0,.cs 不暴露)/image_filter 3 視覺正確性無 resident TiXL 可對。

**覆蓋缺口(誠實,非缺陷;數學 verbatim 但無 runtime 牙)**: ①Dither hash 分支無 parity 牙(錯常數仍會產生追亮度抖動→smoke 牙抓不到此類,須 resident TiXL 才驗得了,靠 refuter 逐行+verbatim 常數確立)。②PointAttributeFromNoise Rotate_X/Y/Z 屬性路徑只逐行讀驗、無 runtime 牙。

**🟡 柏為親測**: image_filter 3(Dither 抖動圖樣/NormalMap 法線/ChromaKey 去背)+point_modify 2(OffsetPoints 朝向平移/PointAttributeFromNoise noise 密度)+particle 3(粒子運動)=全視覺,selftest 像素/數值級已入主線,柏為自測畫面。math 10+combine 2 無視覺=完成。NormalMap/ChromaKey 推定預設也請柏為驗。

**流程事故(重大,已沉澱記憶)**:
- **worktree base 陷阱**[[worktree-base-main-trap]]: `isolation:worktree` 從 `main`(a54b8c0,落後 262 commit,無批次10-23 接縫)切而非活躍分支 719e8f1。3 條 implementer 自救 `git reset --hard codex/js-to-cpp-contract-migration`+symlink external;particle/combine 重派 step-0 fail-fast 攔下(~15s 零造假)→改**非隔離主樹序列**跑繞過(主 checkout 永遠正確 base)。
- **particle agent 被 harness kill** 死在「正要驗 RED」前一刻,但非隔離=活兒留主樹未被收割→orchestrator 接力補 RED 驗證收尾(非重做)。
- **通知漏接空耗**[[subagent-death-detection]](柏為點出): kill 通知有送到但 orchestrator 回「No response requested」閒置 40min。三防護=①每通知必動作②背景派工配 agent_watchdog.sh(純 run_in_background,**nohup/& 雙背景化會切斷叫醒訊號**——踩過)③單序列 lane 用前景不背景。

## Resume — next (批次25 候選; 無 🔴 排修, 純推進)
**批次24 已消化**: math 10(vec2/vec3 純算術見底大半)+point_modify 2+image 3+particle 3+combine 2。
0. **基建解鎖(開新礦脈前置,仍卡)**: ①TransformFromClipSpace/BoundingBoxPoints 需 PointCookCtx camera matrix+count-policy(N→1)接縫——SnapToAngles CameraSpace 也等這個還原 b2。②CommonPointSets/RepetitionPoints CPU 生成需 map-buffer 接縫。
1. **math 第三梯隊**(若還有純算術 cheap): DotVec4/CrossVec2 類/Vec2ToVec3/EulerToAxisAngle/Compare(需 Bool 輸出 port 接縫先確認)/IsGreater。多數需新 port 型別(Bool/vec4)接縫,非純 leaf。
2. **image_filter 第三梯隊**(逐顆驗單 cbuffer 單 texture): ConvertColors/Posterize/Levels/HueSaturation——多數要先確認非 Gradient/雙 texture。
3. **particle force 第二梯隊**: 需查 stateless 候選(多數 force 需 sim-state/target buffer/curl noise field)。
4. **覆蓋缺口補牙**(若取得 resident TiXL): Dither hash 分支 + PointAttributeFromNoise Rotate 路徑 parity 牙。
5. **UI 視覺第三刀**(同 Cut29 Resume): 標題字級 13→18/連線 idle-fade/pin 三角。
6. **柏為親測回收**: 批次24 的 8 顆視覺 op 親驗結果回收(預設值/視覺手感)。

## Cut 31 — 批次 25: cheap-leaf 礦脈挖盡判決 + 開「stateful value op」接縫 + 9 顆 (2026-06-14 傍晚→夜; Opus orchestrator, `/sw-node-batch` 第六航) ✅
**柏為指令**: 一次做 50 顆 → 掃描後判定 cheap-leaf 不存在 50 顆 → 柏為「走你推薦的」→ 開 stateful seam。
**commit**: `b52bbfd`(Phase 0 seam+Damp/Spring)+`ed54c62`(DampAngle/DeltaSinceLastFrame/FreezeValue)+`561598c`(DampVec2/3 SpringVec2/3)。--bite **PASS=123**/NO-BITE:[](statefulvalue 8 齒)/check-arch 綠。soundtrack=已知 @4x 環境紅。

**承重判決(本批最重要的事,非節點數)**: 五條獨立 Explore 掃描(math/image/point_modify+transform/particle/combine+generators)對 `external/tixl` 全家族 → **cheap-leaf(加葉子不動接縫)礦脈已挖盡,真剩 ~0-2 顆**:
- math 0(剩全是 stateful/Bool 輸出/MultiInput/vec4/context-dependent)
- image_filter 0(剩全是 compound .t3/Gradient/Curve/2nd-texture/multi-pass)
- point_modify+transform 0(剩全要 Texture2D/Gradient/Field/2nd-buffer/Mesh/camera/改數量)
- particle force 0(4 顆 stateless 全做完,剩全要 field/SDF/mesh/target/state/spatial-hash)
- combine 0 / generators 1-2(RepetitionPoints/CommonPointSets,且需 CPU-gen map-buffer 接縫)
→ 結論:批次 1-24 的生產模式(織 cheap leaf)結束。往下每顆節點必先**開一條架構接縫**,每縫解鎖一族。

**開的縫 = stateful value op(沿用 AudioReaction 模式,resident 核心零改動)**: 輸出依賴前幾幀記憶的 value 節點 → evaluate=nullptr,frame_cook 每幀 cook 一次寫 ResidentNode::extOut,evalResidentFloat 走 **generic no-evaluate 路徑**(resident_eval_graph.cpp:58-65 早已非 type-specific,故核心免改)。新增:`runtime/stateful_value_ops.{h,cpp}`(per-instance state keyed by path=survives rebuild+per-instance in compound;data-driven opType→stepFn 表=rule 7,加 op 不動 frame_cook)+ `frame_cook.cpp::cookStatefulValueNodes`(AudioReaction 的 value-graph 兄弟)。

**裝的 9 顆**(全 TiXL Lib/numbers/.../process 逐字):
- **Damp**(float/process/Damp.cs+DampFunctions/MathUtils.SpringDamp): method 0=Lerp(target,current,damping)/1=critically-damped k=0.5/(damping+0.001),dt clamp[0,1/60]。first-eval seed=Value。
- **DampAngle**(float/process/DampAngle.cs): 經最短角差 re-target 再 damp;**無 _isFirstEval**(從 0 damp)。
- **DampVec2/3**(vec2/vec3): 逐分量 dampenFloat;**無 _isFirstEval**(與 scalar Damp 不對稱,faithful)。
- **Spring/SpringVec2/3**(float|vec/process/Spring*.cs): _springed=Lerp(_springed,(target-result)*Strength,Tension);result+=_springed。**無 dt**(frame-rate dependent,忠實)。
- **DeltaSinceLastFrame**(floats/process): Value−上幀;Threshold port 存在但 TiXL math 未用→保留 port parity。
- **FreezeValue**(float/process): sample-hold,2 輸出(Result+DeltaSinceFreeze),Mode 0=FreezeWhileTrue/1=上升沿取樣。

**named fork**(全): UseAppRunTime 輸入 + 1ms MinTimeElapsedBeforeEvaluation guard 砍掉(frame_cook 每幀 cook 一次=無 sub-ms double-eval 可防,該 guard 在 TiXL 的存在理由消失)。dt 用 raw wall delta(這些是 CPU value sim,frame-rate dependent 如 TiXL Playback.LastFrameDuration),各 op 內部 clamp。

**selftest**(frame-driven golden,hand-computed 軌跡;8 齒咬): Damp linear 0.2/0.36/0.488 + DampedSpring dt=10 **clamp 證**0.000277 + Spring overshoot 0.5/1.0/1.25→settle + DampAngle wrap -5/-7.5(證最短角差非追+350)+ Delta 5/3/0 + Freeze mode0 hold + mode1 上升沿 + DampVec3(1,2,4)逐分量(證無 channel bleed)+ SpringVec2。

**誠實 BLOCKED(非缺陷)**: PeakLevel=4 輸出(>extOut[3])+FoundPeak 是 Bool 輸出 → 需 Bool seam(第二條未開的縫)。Has*Changed/Was*Trigger 同卡 Bool。

**🟡 柏為親測**: 9 顆全有「手感」維度(拖 Damping/Tension 看平滑/彈跳速度、FreezeValue 凍結時機、DampAngle 繞角),selftest 數值級已入主線,柏為自測手感對不對。

## Resume — next (批次26 候選; 無 🔴 排修, 純推進——但每條都是「開縫」非「織葉」)
**批次25 已消化**: stateful value seam 開通 + 9 顆(Damp/DampAngle/DampVec2/3/Spring/SpringVec2/3/DeltaSinceLastFrame/FreezeValue)。
0. **Ease 族(同 stateful seam,最近的下一塊)**: Ease/EaseVec2/EaseVec3(TiXL float|vec/process/Ease*.cs)。需先港 `Core/Utils/EasingFunctions.cs`(305 行=10 curve×3 direction=30 fn,標準 easing 公式,可委 codex)+ 絕對時間 state(startTime/initial/target/prevInput,用 frame_cook 傳的 timeSecs)+ input-changed restart 邏輯。state s[8] 夠(4 floats)。**這是最乾淨的下一 tranche,seam 已在,只缺 EasingFunctions 子港**。
1. **Bool 輸出 port 接縫(第二條縫)**: 解鎖 ~20 顆 math 邏輯(Compare/IsGreater/HasValueChanged/HasValueIncreased/HasValueDecreased/WasTrigger)+ PeakLevel 的 FoundPeak。⚠ 先確認有 Bool consumer(否則=織沒人踩的線);TiXL Bool 餵 trigger/switch——需同時開一個 Bool 消費端才有意義。
2. **第二 texture 輸入接縫(第三條縫)**: 解鎖 displace-with-map/TimeDisplace/HSE 等視覺 filter。柏為域(位移圖最直接看得到)。
3. **Gradient/Curve 接縫**: 解鎖 RemapColor/ColorGrade/Steps(image)+MapPointAttributes(point)。
4. **Field/SDF 子系統 + camera matrix 接縫**(大): 解鎖整個 particle field-force 族 + TransformFromClipSpace/BoundingBoxPoints/SnapToAngles CameraSpace。
5. **柏為親測回收**: 批次24 八顆視覺 + 批次25 九顆手感。

## Cut 32 — 批次 26: 接縫期(柏為定「先把縫都織完整再大量做節點」) — 進行中
**柏為指令**: 先織完接縫,再一次大量做節點。→ orchestrator 先驗哪些是真接縫(讀地形,別織不承重的線)。

**承重修正(4 條候選接縫 → 2 條真的)**:
- **Bool ✗ 不是接縫**: float logic op(IsGreater/Compare/IsLess)就是 pure `float→float` 回 0/1(我們本就 Bool=Float 0/1);stateful logic(HasValueChanged/Increased/Decreased/WasTrigger)騎批次25 stateful seam。→ logic 全族零基建可做。
- **2nd-texture ✗ 已織好**: resident cook 早有 `TexCookCtx::kMaxTexInputs=4` + 每個 Texture2D port 自動綁下一槽。雙texture filter 只要宣告 port + shader。
- **Gradient ◑ 非深結構接縫(修正:初判子系統,實則 op+UI)**: gradient 資料可騎現有 float-param 模型(stops = pinless Float params「stop0.pos/r/g/b/a」→ 跟其他 param 一樣被 gather 進 in[],零新型別/儲存/序列化/cooked 路徑——與 Bool/2nd-texture 同樣 dissolve)。`SampleGradient(SamplePos,stops…)→RGBA`(4 Float vec 輸出)=pure evaluate。**真正要的 = Inspector gradient-bar 編輯器 widget**(柏為 是視覺藝術家,gradient 是核心色彩工具;塞進 40 條編號 slider = 把本質複雜翻成假直覺的反模式,不可)。→ Gradient = 一個 op + 一個編輯器 widget(UI 重,非深 infra)。權威=Core/DataTypes/Gradient.cs(Sample 內插)+ color/SampleGradient.cs。⚠ in[] gather 上限 32:stops 上限 ~6(6×5+1=31)或 bump in[]。
- **MultiInput ✓ 真接縫** → **本 cut 已織完**。
- **Field/SDF+camera**: 獨立大子系統,排 mass pass 之後(非 port-seam)。

**MultiInput seam ✅ commit `1879f34`**(= TiXL MultiInputSlot,additive 零回歸): PortSpec.multiInput + ResidentInput.extraConns(單線恆空);flatten loop2 對 multiInput slot 第 2+ Connection APPEND;resident gather 展開 primary+extras 成 in[](in[8]→in[32]);Sum(TiXL float/basic/Sum.cs evalSum=Σ,空=0);editor_ui dst 是 multiInput 則 ADD 非 reconnect(柏為可接 N 線);selftest multiinput(resident Root{Const2,3,5→Sum}=10,壞 gather=2 證齒)。--bite **PASS=125**/NO-BITE:[]/check-arch 綠。named 限制:flat path 單線(legacy)/跨 compound 邊界 multiInput/picker(list+index)/inspector 顯首線。🟡 柏為親驗:加 2-3 Const + Sum 全接 Sum.Input 看和。

**下一步 = Gradient(op + 編輯器 widget,非深 infra)**:①SampleGradient pure evaluate op(stops=pinless Float params,SamplePos→RGBA 4 輸出,內插照 Gradient.cs)②Inspector gradient-bar 編輯器 widget(柏為 authoring=完成定義關鍵;非編號 sliders)③消費 op:SampleGradient(+ 後續 RemapColor 類)。**結論:四條候選接縫只 MultiInput 是真深結構縫(已織);Bool/2nd-texture/Gradient 都 dissolve 成 op/UI**。→ Gradient widget 後即 **mass node pass**(logic 全族+Ease 全族+雙texture filter+gradient 族+Sum/Min/Max+PeakLevel)。Field/SDF+camera = 之後獨立子系統。

## Cut 33 — 批次 27: mass node pass 起點 — Ease 族 + logic 族 7 顆 (2026-06-14 夜; Opus orchestrator, `/sw-node-batch` 第七航) ✅
**柏為指令**: `/sw-node-batch`(無 args)。**接縫期已收**(Cut 31-32:深縫只 MultiInput,已織;Bool/2nd-texture/Gradient dissolve)→ orchestrator 判定 = 不再掃 cheap leaf(已盡,re-scan = 重推已結之論),而是**開 mass node pass**,挑已解鎖且 machine-verifiable 的家族並行。
**commit**: `9fa1d80`(Ease 族)+`175e0dc`(logic 族)。--bite **PASS=124**/NO-BITE:[]/check-arch 綠。soundtrack=既有 AVAudio @4x 環境紅(非本批)。

**承重判決(本批最重要,非節點數)= `/sw-node-batch` 的並行模型對 value/math 家族不成立**: 該指令的並行靠 point op 有 per-op leaf 檔(`point_ops_<name>.cpp`)——這是它自己的承重洞見。但 **value/math 家族沒有 per-op leaf 檔**:每顆 op 都灌進共享的 `stateful_value_ops.cpp` + `node_registry_math.cpp` + 單一 selftest fn(`runStatefulValueSelfTest`/`runMathOpsSelfTest`,kTable 連動都不必)。強開 6 並行 worktree lane = 必撞共享檔。→ 本批改 **sequential 前景 lane,非 isolation(在活躍分支幹活)**:零盲區([[subagent-death-detection]]「單條序列 lane 用前景」)、避開 worktree-from-main 陷阱([[worktree-base-main-trap]])。**教訓沉澱:value 家族若未來要大量 op,才值得做 value-family Phase 0(per-op 檔+GLOB+per-sub-family registrar);現在 7 顆 sequential 就夠,別為假想批次預先重構(Karpathy 先求簡單)。**

**裝的 7 顆**(全 TiXL 逐字):
- **Ease/EaseVec2/EaseVec3**(float|vec2|vec3/process/Ease*.cs,Opus lane):騎批次25 stateful seam。EasingFunctions.cs 30 curve + ApplyEasing 逐字港(Back c1=1.70158/Elastic c4=2π/3,c5=2π/4.5/Bounce n1=7.5625,d1=2.75)。`StatefulValueState s[8]→s[12]`(EaseVec3 需 11 槽,additive 零回歸)。**承重判斷=frame_cook 每幀給零 out[]**(frame_cook.cpp:190)→ TiXL `_initialValue=Result.Value`(上幀輸出)無法從 out[] 取→存 scalar `prevEased` 重建 `lerp(initial,target,prevEased)`=上幀 Result(N 通道共用 scalar t,一 float 夠,faithful 且 fit s[12])。named fork 同 Damp/Spring:UseAppRunTime 輸入/1ms MinTimeElapsed guard/__MotionBlurPass skip 全砍。
- **Compare/IsGreater**(float/logic,stateless,value_eval_ops.cpp):Bool→Float 0/1(Cut 32)。IsGreater named fork `fork-isgreater-stateless`:TiXL `_lastResult` change-gate 是 dirty-flag 優化非值承重→純 stateless。Compare 4 mode + Precision band。
- **HasValueIncreased/Decreased**(float/logic+process,stateful,s[0]=lastValue):v 比上幀,輸出 Float 0/1 flag。

**selftest**: Ease linear 軌跡 + OutQuad p=0.5=0.75(證 EasingFunctions dispatch)+ EaseVec3 通道獨立(證 s[12] layout);IsGreater 邊界 + Compare 四 mode + Precision band;HasIncreased/Decreased threshold band(+0.2 不過 0.5 閾值)。各 injectBug 齒咬,四條 proof(mathops/statefulvalue × normal/bug)orchestrator 親手復跑全綠。

**誠實 BLOCKED(非缺陷)**: HasValueChanged(3 輸出+LocalFxTime min-time gate+PreventContinuedChanges+edge-detect→排後當自己一顆)、WasTrigger/Trigger(需 `context.FloatVariables` trigger-variable 子系統 + Playback.RunTimeInSecs,本 runtime 無→第 N 條縫)。

**事故 salvage**: Lane 2(logic)implementer agent socket 死(15 tool-use 後 API Error)。非隔離→活兒留主樹(只寫了 value_eval_ops.cpp 的 IsGreater/Compare eval fn,66 行)→ orchestrator 接手親手補完(header decl/registry 4 row/stateful 2 step+table/mathops+statefulvalue selftest)+復跑,不重派(spec 已全精確,機械活直接收尾更快)。

**🟡 柏為親測**: Ease 族有「手感」維度(拖 Duration/Direction/Interpolation 看緩動曲線、輸入跳變看 re-target 平滑);logic 族 selftest 數值級已完成,Compare/IsGreater 接 switch/觸發看 0/1。

## Resume — next (批次28 候選; mass node pass 續推)
**批次27 已消化**: Ease 族 3 + logic 族 4。mass pass 啟動。
0. **Gradient(op + Inspector widget)**: Cut 32 點名的 UI-heavy 接縫尾。SampleGradient pure evaluate(stops=pinless Float params,SamplePos→RGBA 4 輸出,內插 Gradient.cs)+ **Inspector gradient-bar 編輯器 widget(柏為 authoring=完成定義,需柏為在場肉眼驗,非無人值守可結)**。⚠ in[] gather 上限 32:stops ≤6。
1. **雙texture filter**(2nd-texture 已織,kMaxTexInputs=4):displace-with-map/TimeDisplace/HSE 等。柏為域(位移圖直接看)。
2. **HasValueChanged**(自己一顆,小心):3 輸出+LocalFxTime min-time gate+PreventContinuedChanges,seam 的 time 已有。
3. **Bool seam(若要 PeakLevel/Trigger 類)**: WasTrigger/Trigger 需 context.FloatVariables trigger-variable 子系統——先確認有消費端再開(否則織沒人踩的線)。
4. **Field/SDF+camera 子系統**(大,獨立): particle field-force 全族 + TransformFromClipSpace。
5. **柏為親測回收**: 批次24 八顆視覺 + 批次25 九顆手感 + 批次27 Ease 手感。

## Cut 34 — 批次 28: HasValueChanged (mass pass 自走第一顆) (2026-06-14 夜; Opus orchestrator, /loop 自走) ✅
**柏為授權自走模式**(22:24「我沒喊停你就往下做」)→ machine-verifiable 顆自走進主線/需眼顆堆待驗清單/真停只在礦盡·大子系統·品味拍板。批次27 誠實 deferred 的 HasValueChanged(machine-verifiable)= 自走第一顆。
**commit `eedb46c`**: TiXL float/logic/HasValueChanged.cs 逐字,騎批次25 stateful seam。3 輸出(HasChanged Bool→Float 0/1·Delta signed·DeltaOnHit)+5 輸入(Value/Threshold/Mode enum Changed/Increased/Decreased/MinTimeBetweenHits/PreventContinuedChanges Bool→Float)。state s[0]=lastValue/s[1]=lastHitTime/s[2]=lastHitDelta/s[3]=wasHit。WasTriggered 上升沿復刻;gate=hasChanged&&(prevent||wasTriggered)→MinTimeBetweenHits 用 wall time。named fork 同 Damp/Ease 砍 _lastEvalTime+0.0002 dedup(每幀一 cook)。--selftest-statefulvalue PASS/-bug FAIL(rc=1,3 齒)/--bite PASS=124 NO-BITE:[]/check-arch 綠。
**工單筆誤校正(查 TiXL 不發明的價值)**: orchestrator 工單把 PreventContinuedChanges 語義寫反(說=1 放行)→implementer agent 抓出+照 TiXL 源碼(非工單)實作=1 抑制連續/0 放行,selftest 證真語義。權威永遠是 external/tixl。
🟡 柏為親測:Mode/Threshold/MinTimeBetweenHits 接觸發看 0/1 脈衝時機。
**Resume 更新**: #2 HasValueChanged ✅ 已消化。剩 #0 Gradient(op machine→主線/widget→待驗清單)/#1 雙texture filter(柏為域)/#3 Bool seam(需消費端)/#4 Field/SDF+camera(大子系統=停手條件)。自走下一顆候選=Gradient SampleGradient op(machine-verifiable pure evaluate,widget 堆清單)或掃 TiXL 找剩餘 machine-verifiable value/math leaf。

## Cut 35 — 批次 29: DetectPulse (mass pass 自走第二顆) + 間隔修正 (2026-06-14 夜; Opus orchestrator, /loop 自走) ✅
**柏為點「沒往下走」→ 根因=我把 /loop 間隔設 1200s(20min)idle,體感像死。修=間隔縮到 ~90s(productive tick 非 poll,turn 本身已長 cache 冷,短間隔合理)+ 當下手動先做一批證明在動。**
**commit `8fe1512`**: DetectPulse(TiXL float/process/DetectPulse.cs 逐字,stateful seam)。2 輸出 HasChanged(Bool→Float)/DebugValue(=dampedValue−newValue 更新前);4 輸入 Value/Threshold/Damping(clamp01)/MinTimeBetweenHits。state s[0]=lastHitTime(init -1e30f)/s[1]=wasHit/s[2]=dampedValue。dampedValue=Lerp(newValue,dampedValue,Damping);WasTriggered 上升沿;gate 含 TiXL 冗餘內層 re-check 逐字。named fork=LocalFxTime→seam time。selftest 7 幀 damped 軌跡+齒咬。--bite PASS=125 FAILED:[]/NO-BITE:[]/check-arch 綠。
**TiXL .t3 預設權威(查不發明×2)**: agent 抓出工單兩處筆誤——①Log Base 工單說無預設用 10→實際 Log.t3 Base=1.0(且 base=1 落 degenerate→0 guard);②DetectPulse Damping 工單猜 0→實際 DetectPulse.t3 Damping=0.95/Value=1/Threshold=0/MinTime=0.075。**教訓沉澱:value op 預設權威=`external/tixl` 的 `.t3`(SymbolJson),工單別猜,implementer 自己查 .t3。** Log 本身早批次已落地(本批確認 faithful 未動)。
🟡 柏為親測:Damping/Threshold/MinTime 接訊號看脈衝偵測。
**Resume(批次30 候選,machine-verifiable 礦剩薄)**: BlendValues(stateless,MultiInput<float> Values + F regular input,Fmod index+Lerp;⚠混 MultiInput+regular port,gather 順序要驗)/Accumulator(stateful,_v 累加,SecondsFromBars bars-time→可用 seam dt 直接替=named fork,enum AccumulationModes+bool Running/ResetTrigger)。**PeakLevel BLOCKED**(4 輸出>out[3]+FoundPeak Bool→需 >3 輸出 seam 小改)。這兩顆後 float/numbers machine-verifiable leaf 近見底→往下=Gradient op-half(widget 堆清單)或撞停手條件(需眼/大子系統)回報柏為。

## Cut 36 — 批次 30: Accumulator + BlendValues DEFER + 礦況盤點 (2026-06-14 夜; Opus orchestrator, /loop 自走) ✅
**commit `1917edf`**: Accumulator(TiXL float/process/Accumulator.cs 逐字,stateful seam,state s[0]=v)。.t3 預設自查(Increment=1/StartValue=0/Modulo=0/Running=true/ResetTrigger=false/Accumulate=PerFrame)。Running 閘累加/ResetTrigger 重載 StartValue(同幀續累加)/PerFrame=+Inc PerSeconds=+Inc*dt/Modulo wrap。named fork=SecondsFromBars→seam wall dt;TiXL 無 _isFirstEval(v 起 0 非 StartValue)。selftest 5 組(PerFrame/freeze/reset/mod3 wrap/PerSeconds dt)+齒咬。--bite PASS=125/NO-BITE:[]/check-arch 綠。
**BlendValues DEFER(非 machine-verifiable leaf,需開接縫)**: MultiInput<float> Values + regular F 混用。flat-path gather(graph.cpp:165)每 port 一槽、**不展開 multiInput**(展開只在 resident path);且 eval in[] 無法穩當分離變長 Values 與 F,TiXL count==0→0 語義我方 gather 未暴露。→ mixed-multiInput+regular 接縫=需驗/開,非自走安全,deferred。
**PeakLevel BLOCKED**: 4 輸出>out[3]+FoundPeak Bool→需 >3 輸出 seam 小改。

**礦況盤點(承重,決定自走還能走多遠)**: float/numbers 的 **high-value 接縫騎乘顆(Ease/logic/stateful Damp·Spring·Freeze·Delta·HasValue·DetectPulse·Accumulator)已挖盡**。剩的 machine-verifiable leaf 降到 **completeness-tier**:
- **clean vec value op**(fit 既有 .x/.y/.z Float port 慣例,真 TiXL 非發明,自走安全):DivideVector2/BlendVector3/EulerToAxisAngle/RemapVec2/PadVec2Range/Int2ToVector2/Vec2ToVec3/GridPosition。約 1-2 批。
- **stateful vec**:HasVec2Changed/HasVec3Changed(鏡 HasValueChanged)。
- **int/ 家族**(AddInts/IntDiv/ModInt/MinInt/MaxInt/IsIntEven/CompareInt/IntToFloat…):Float-only runtime 裡 Int op = 截斷 Float,是型別建模問題、價值低,**先不碰**(若柏為要 Min/Max float op,MinInt/MaxInt 可借語義)。
- **需 seam/blocked**:BlendValues/PickVector2,3(mixed multiInput)/PerlinNoise2,3(noise impl)/MulMatrix·TransformVec3(matrix 輸入)/GetAPrime(prime sieve)/RandomChoiceIndex(RNG)。
**結論=自走還能走 1-2 批 clean vec value op,但已從 high-value 跨進 completeness-tier→這是柏為 steer 點(要不要改去 Gradient widget[需柏為 authoring]或視覺 filter,還是繼續補完 vec/int parity)。** 批次31 預設=vec value 三顆(DivideVector2/BlendVector3/EulerToAxisAngle 逐字驗 TiXL)。

## Cut 37 — 批次 31: 3 clean vec value op (DivideVector2/Vec2ToVec3/EulerToAxisAngle) (2026-06-14 夜; /loop 自走) ✅
**commit `b92d6d2`**: 全 TiXL vec2/vec3 逐字,stateless multi-output(inputs first,k=outIdx-n)。.t3 預設自查。DivideVector2=(A/B)/U 逐分量(div0→0 fork);Vec2ToVec3=(XY.x,XY.y,Z) 直通;EulerToAxisAngle=Rotation(rad)→Axis 單位+Angle(double 中介對齊 TiXL,norm<0.001→(1,0,0) guard)。selftest 各組+齒咬,--bite PASS=125/NO-BITE:[]/check-arch 綠。BlendVector3 DEFER(同 BlendValues mixed-multiInput)。
**Resume(批次32,completeness vec 續)**: RemapVec2(component remap,Mode Clamped/Modulo)/PadVec2Range/Int2ToVector2/GridPosition(pure vec)/HasVec2Changed·HasVec3Changed(stateful 鏡 HasValueChanged)。之後 vec 也近底→int 家族(Float-world 截斷,價值低)或撞停手條件(Gradient widget/視覺/Field-SDF 需柏為)。**持續提醒柏為 steer 點:completeness tier 價值遞減,high-value 剩貨需柏為在場。**

## Cut 38 — 批次 32: RemapVec2 + HasVec2Changed + flat in[8]→in[16] (2026-06-14 夜; /loop 自走) ✅
**commit `70859a8`**: RemapVec2(stateless component remap,Mode Normal/Clamped/Modulo,TiXL vec2/RemapVec2.cs)+HasVec2Changed(stateful,Euclidean dist>Threshold,HasChanged+Delta.x/.y,鏡 HasValueChanged,seam time fork)。.t3 預設自查。**承重小修:flat-path gather `in[8]→in[16]`(graph.cpp)**——RemapVec2 11 Float 輸入超過 flat in[8]→截斷→n<11 回 0(selftest 抓到),resident 路批次26 已 in[32],flat 補齊 additive。--bite PASS=125/NO-BITE:[]/check-arch 綠。skip Int2ToVector2(Float-world 無意義 trivia)。
**HasVec3Changed BLOCKED**(7 輸出 HasChanged+Delta3+DeltaOnHit3 > out[3],同 PeakLevel 需 >3 輸出 seam)。
**礦況:completeness vec 近底**。剩 clean 候選:PadVec2Range/GridPosition(待驗 pure)/MinInt·MaxInt(int 但=我方缺的 float Min/Max 語義,可借但略型別 liberty)。其餘 blocked(HasVec3Changed/PeakLevel >3-out seam;BlendValues/BlendVector3/PickVector mixed-multiInput seam)或 trivia(Int2ToVector2 類)。**批次33=最後 1 批 clean op(PadVec2Range/GridPosition/MinMax 驗後挑)→ 若只剩 trivia/blocked 即撞「礦挖盡」停手條件回報柏為(完成度層該收口,剩高價值=Gradient widget/視覺/Field-SDF/>3-out seam/mixed-multiInput seam 全需柏為或開大縫)。**

## Cut 39 — 批次 33: PadVec2Range + 自走收口(machine-verifiable 礦挖盡,loop 停手) (2026-06-14 夜; /loop 自走) ✅⏹
**commit `0dd93ce`**: PadVec2Range(stateless,pad/scale range 繞中心,Result.x/y 不對稱公式)。--bite PASS=125/NO-BITE:[]/check-arch 綠。
**⏹ 自走 loop 停手(柏為停手條件①「礦挖盡」觸發,非放棄)**: 自走 6 批(28 HasValueChanged / 29 DetectPulse / 30 Accumulator / 31 DivideVector2·Vec2ToVec3·EulerToAxisAngle / 32 RemapVec2·HasVec2Changed+in[8]→in[16] / 33 PadVec2Range)全進主線,--bite 124→125。掃完 float/vec2/vec3/int 全 numbers 家族,clean machine-verifiable pure/stateful leaf **挖盡**。

**剩貨分類(全需柏為在場或開大縫,自走碰不了)**:
- **需開 seam**:HasVec3Changed/PeakLevel(>3 輸出,seam out[3]→out[N]);BlendValues/BlendVector3/PickVector2,3/RemapValues(mixed MultiInput+regular,或 MultiInput<Vec> gather)。
- **context-dependent**:GridPosition(需 context.RequestedResolution aspect seam)。
- **大子系統**:Field/SDF+camera(particle field-force 全族+TransformFromClipSpace);PerlinNoise2,3(noise impl 港);anim/animators(Curve seam)。
- **品味/modeling call(該柏為定)**:MinInt/MaxInt 借作 float Min/Max(Int→Float 型別 liberty);Int 家族整體(Float-world 截斷語義)。
- **需柏為 authoring/眼睛(high-value)**:Gradient(SampleGradient op machine→主線 + Inspector gradient-bar widget 柏為畫色帶);雙texture 視覺 filter(位移圖);批次24/25/27 視覺+手感顆親測回收。

**柏為回來的 steer 選項**:①Gradient widget(我接 op、你畫色帶=完成定義)②指定開哪條 seam(>3-out / mixed-multiInput / camera)③MinInt/MaxInt 型別決定 ④視覺 filter 批 ⑤親測回收。**自走已把所有不需要你的 value/math parity 補滿,loop 停在這裡等你。**

## Cut 40 — 批次 34: >3-output seam 開通 + HasVec3Changed(7-out) + PeakLevel(4-out) (2026-06-14 夜; /loop 柏為 steer「開 seam」) ✅
**commit `e3062ce`**: 柏為打 /loop 指目標=開兩條 seam(>3-output / mixed-multiInput)。本批做第一條。
**seam 加寬(additive 零回歸,先驗後裝)**: ResidentNode::extOut[3]→[8](resident_eval_graph.h)+evalResidentFloat read cap i<3→i<8(.cpp)+frame_cook out[3]→out[8]+copy loop;cookStatefulValueOp 簽名 out[3]→out[8](float* 不變)。先 build+--bite 驗既有 ≤3-out 顆(Damp/AudioReaction…)零回歸,再裝新顆。
**裝 2 顆**:HasVec3Changed(7 輸出 HasChanged+Delta.xyz signed+DeltaOnHit.xyz abs,Mode Changed/Increased/Decreased;state s[0..7])+PeakLevel(4 輸出 AttackLevel/FoundPeak/TimeSincePeak/MovingSum;MovingSum=feedback 累加器讀自身上幀,±30000 wrap;state s[0..2],lastPeakTime init −∞)。named fork 砍 Playback/FxTime dedup,seam time 替。selftest HasVec3Changed DeltaOnHit.y(out[5])+PeakLevel MovingSum(out[3])證 >3-out 讀回。--bite PASS=125/check-arch 綠。
🟡 端到端:evalResidentFloat 讀 extOut[4..6] by-construction 正確,需柏為 GUI 接 HasVec3Changed 讀 DeltaOnHit 親驗一次。
**Resume = 第二條 seam: mixed-multiInput**(BlendValues=MultiInput<float>Values+regular F;BlendVector3=MultiInput<Vec3>;PickVector2/3;RemapValues=MultiInput<Vec2>pairs)。承重難點(批次30 已勘):①flat-path gather(graph.cpp:165)每 port 一槽不展開 multiInput→需教它展開 ②eval in[] 要能分離變長 multiInput 段與其後 regular port ③TiXL count==0→0 語義(connected count)我方 gather 未暴露。解法雛形:multiInput 段展開後其長度需傳給 eval(或約定 multiInput 必在尾/在首+count 旁路)。**這條比 output seam murky+碰 gather 載重線,要先讀世界觀再動。** 之後解鎖 ~5 顆 combine/blend op。

## Cut 41 — 批次 35: BlendValues (mixed-multiInput seam, eval-side 解) + 自走收口 (2026-06-14 夜; /loop 柏為 steer) ✅⏹
**commit `ecbae35`**: 柏為 steer 的 seam 第二條=mixed-multiInput。**承重發現:不需動 gather**——resident gather(resident_eval_graph.cpp:88-110)早已「展開 multiInput prefix→append 後續 regular port」,只要 eval 約定「multiInput 是 prefix,K trailing regular → 段長 n-K」即可。BlendValues(K=1,F=in[n-1])。selftest 走 resident graph(Const 10,20,30→Values + Const 1.5→F=25),flat path 不展開故不可用。--bite PASS=125/check-arch 綠。批次30 把它判 "需開接縫" 是過度保守(沒讀夠 gather 世界觀)。

**⏹ 自走 loop 停手(柏為 steer 的安全部分做完)**: 柏為 /loop 指「開兩條 seam」:
- **seam 1 >3-output ✅ 全做**(批次34 extOut[3]→[8],裝 HasVec3Changed+PeakLevel)。
- **seam 2 mixed-multiInput ◑ 安全部分做完**:float 情形(BlendValues)✅;**vec 情形(BlendVector3/PickVector2,3/RemapValues)= 需開「Vec-multiInput」較大 seam→柏為級結構/UX 決定,非自走**。

**為何 Vec-multiInput 是柏為級(不自走)**: 我方 Float-decomposed 模型每 connection=1 float wire,無 Vec3 wire。Vec3 multiInput 要嘛(a)每 vec 源 3 connections 進 3 個 sub-multiInput(.x/.y/.z),eval 跨步 3;要嘛(b)引入真 Vec connection 型別。兩者都是模型/GUI 怎麼接的 UX 決定,碰 gather+連線模型載重線,不該自走拍板。

**柏為 steer 選項(回來定)**: ①Vec-multiInput seam 走哪個模型(sub-multiInput 跨步 vs 真 Vec wire)→解鎖 BlendVector3/PickVector/RemapValues ②Gradient widget(我接 op 你畫色帶,high-value)③GridPosition 的 context.RequestedResolution seam ④MinInt/MaxInt float Min/Max 型別決定 ⑤Field/SDF 大子系統 ⑥視覺/手感顆親測回收(批次24/25/27/34 累積)。**自走已把所有不需要你決定的 value/math parity + 兩條 seam 的安全部分做完(批次27-35,九批),loop 停這裡等你。**

## Cut 42 — 批次 36: point-op 礦重開 — RepetitionPoints + ResampleLinePoints (2026-06-15; /sw-batch 自走, Opus orchestrator) ✅
**承重轉向(本批最重要,非節點數)**: value/math machine-verifiable 礦(批次27-35)挖盡後,`/sw-batch` 北極星≠value/math 終點而是**整個 TiXL clone**→開 parity 缺口掃描批(image filter / point op 兩路 Explore)。**裁決=point op 是更嚴格的自走礦**: 它的 selftest 在真 Metal device 跑真 shader 斷言 position(headless,見 point_graph_selftest.cpp `MTL::CreateSystemDefaultDevice`),可對 TiXL 公式手算交叉驗;image filter golden 只自洽(沒跑 TiXL 驗不了真 parity)。且 point op 有 per-op leaf 檔(`app/src/runtime/point_ops_<name>.cpp`)→可並行(value/math 共灌一檔不能並行)。**掃描有假缺口**: image 路把已有的 DetectEdges/AdjustColors 列成缺口;point 路把需 camera/gradient/SDF/texture 的 SortPoints/SetAttributesWithPointFields/PointColorWithField/LinearSamplePointAttributes 誤判 [AUTO]→逐顆開 .cs 濾掉,只留真單 buffer+純量參數的(generator + line-topology)。

**commit `5da99e7`(RepetitionPoints)+`320cfcb`(ResampleLinePoints)**。--bite **125→127** FAILED:[]/NO-BITE:[]/check-arch 綠/app-health scenario smoke(d4_spawn_position)PASS。

**worktree-base-main-trap 復發(承重,影響 /sw-node-batch 並行假設)**: `Agent isolation:worktree` 從 **main(`a54b8c0`,落後 262 commit)** 切而非活躍分支 `codex/js-to-cpp-contract-migration`→worktree 上連 `tools/agent_worktree_setup.sh`/`point_ops_*` 都不存在,setup 腳本跑不了(雞生蛋:腳本只在活躍分支)。**兩條 worktree lane 的工單 step-0 地基檢查立停(WRONG BASE),guard 兩次救援零浪費代碼**(`ls point_ops_<模板>.cpp` 不存在=立停的鐵律生效)。→改 **非 isolation 序列前景**在主 checkout 幹活([[worktree-base-main-trap]] 既定修法)。**教訓: `/sw-node-batch` 的「葉子 worktree 並行」對 point-op 暫時不可用,直到 isolation:worktree 改從活躍分支切;現階段 point-op 批=非 isolation 序列前景(零盲區)。**

**裝的 2 顆(全 TiXL 逐字 + Opus refuter SURVIVE)**:
- **RepetitionPoints**(generate,`point/generate/RepetitionPoints.cs`): 逐點 TRS 重複。TiXL 是 CPU StructuredList 生成器(無 .hlsl)→**named GPU-generator fork**(thread i=point i)。承重=`GraphicsMath.CreateTransformationMatrix` 九因子鏈逐字港,**row-vector convention fork**(row4x4+左乘,避 Metal column-major silent transpose)。`u=(i+1+Phase)`/`translation=Translate*u+StartPosition`/`rotation=YawPitchRoll(Rotate.X/Y/Z·u)`/`scale=(1-Scale)*u+1`/`F1=|scale|/√3+StartW`/AddSeparator→末尾 `Point.Separator()`(Scale=NaN marker,countTransform +1 槽,1-cook lag 同 PairPointsForLines)。**refuter SURVIVE**: 九因子逐字對 GraphicsMath.cs + 三組非平凡 pivot+scale+rotation case 手算 delta=0(含 (1,−1,0))。
- **ResampleLinePoints**(modify,`point/modify/ResampleLinePoints.cs`+`.hlsl`): **.hlsl 逐行港**。承重發現=**parameter-space 重採樣非 arc-length**(`sourceF=saturate(f)*(SourceCount-1)`,endpoint exclusive `i/ResultCount`)。separator-aware(`isnan(Scale.x*next.Scale.x)` 丟 tap)/多 tap smoothing(SmoothDistance/Samples)/OOB→NaN Scale/RotationMode Interpolate+Recompute(中央差分切線→qLookAt)。named fork: OOB +1 read clamp 到 SourceCount-1(HLSL StructuredBuffer OOB→0,fraction≈0 端點等價)。**refuter SURVIVE**: .hlsl verbatim + Recompute 切線路徑 L 彎線實證(forward=切線,過彎連續無翻轉,qLookAt byte-identical)。

**工單兩處筆誤被 implementer 抓正(查 TiXL 不問假設的價值,權威永遠是 external/tixl)**: ①RepetitionPoints 工單寫「Scale=0→constant 1」實為 Scale=1(公式 `(1-Scale)*u+1`,Scale=0 給 u+1)→agent 用 Scale=1 手驗;②ResampleLinePoints 工單寫「弧長重採樣」實為 parameter-space→agent 讀 .hlsl 港真語義。orchestrator 工單別猜語義,讓 implementer 對 .cs/.hlsl/.t3。

**🟡 柏為親測**: RepetitionPoints(拖 Rotate/Scale/Pivot/Count 看重複陣列形;AddSeparator toggle 有 1-frame settle=非 bug 同 PairPointsForLines);ResampleLinePoints(SmoothDistance/Samples 視覺塑形;RotationMode=Recompute 看切線朝向沿線)。

**Resume — next (批次37 候選; point-op [AUTO] 礦續推,但礦在變薄)**:
- **剩 [AUTO] generator**: BoundingBoxPoints(AABB reduction→box 頂點,可 readback CPU 算)/CommonPointSets(硬編幾何表 Cube/Tetra…,data-heavy 可只港前幾 Set)/SubdivideLinePoints(線段細分,line topology 同 Resample)。
- **常駐牙補強(refuter 建議)**: ResampleLinePoints 補 Recompute 彎線常駐 case(現只 probe 過);RepetitionPoints 補 pivot+scale+rotation 組合常駐 case(現 golden 只測退化 case,組合 case 靠 refuter probe)。
- **礦況警訊**: point modify 缺口大多 blocked(需 texture: AttributesFromImageChannels/SamplePointColorAttributes/TransformWithImage;SDF: MoveToSDF/SelectPointsWithSDF/PointColorWithField;camera: TransformFromClipSpace/SamplePointsByCameraDistance/SortPoints;gradient/curve: MapPointAttributes/SetAttributesWithPointFields;2nd-buffer: BlendPoints/RepeatAtPoints/PairPointsForGridWalkLines)。**generator/line [AUTO] 挖完(~3 顆)→point op 自走礦也近底**→轉 image filter [AUTO](ConvertColors/FastBlur/KeyColor/HoneyCombTiles 等,但需先建 image golden 接縫=自洽 gate,非 TiXL 交叉驗)或回報柏為 steer(Cut 41 的六選項仍掛著)。

## Cut 43 — 批次 37: point-op [AUTO] 續挖 — SubdivideLinePoints + CommonPointSets (2026-06-15; /sw-batch 自走續, Opus orchestrator) ✅
**接批次36**(同 worktree-trap 教訓:point-op 批=非 isolation 序列前景,葉子 worktree 並行對 point-op 暫不可用)。BoundingBoxPoints 從候選**踢出**(讀 .hlsl 發現=多 kernel clear/atomic-reduce/emit + aux MinMax buffer + ordered-int 原子 min/max + 輸出 count 由 C# 非 .cs slots 設→新機械+輸出 layout 不明,非乾淨 drop-in,排批次38 需 CPU-readback fork 細想)。改裝 SubdivideLinePoints + CommonPointSets。

**commit `a9cc92d`(SubdivideLinePoints)+`625f0b4`(CommonPointSets)**。--bite **127→129** FAILED:[]/NO-BITE:[]/check-arch 綠。

**裝的 2 顆(全 TiXL 逐字 + refuter SURVIVE)**:
- **SubdivideLinePoints**(modify,`points/modify/SubdivideLinePoints.hlsl` 逐行港): 每段細分 InsertCount+1 個內插點。open path(lerp seg→seg+1,f<=0.001 複製源點)+ closed path(數非-sep 段+閉合段 lastValid→firstValid,尾段 buffer 不寫)。Rotation 用 qSlerp。separator-aware。.t3 count 鏈(Count→Clamp(0,1000)→+1=subdiv;bufferSize=clamp(srcCount*subdiv,1,1e6))驗證。**承重 named fork=port id "Count"→"InsertCount"**(cook driver `point_graph.cpp:254-258` 劫持 id=="Count" 當節點 output count→不改會把 output 錯設成 InsertCount 無視 source bag;label 仍 "Count" 對齊 TiXL;refuter 驗 rename 真必要+無副作用)+ file-static stash output count(1-frame sizing lag 同 PairPointsForLines)。**refuter SURVIVE**: .hlsl 逐行(含 closed path)+ open/closed 兩手算 case 一致 + count 鏈 + port-rename 三驗;唯一 parity 風險=open path 末段 OOB 讀=TiXL 原文 UB 忠實復刻(非港的破)。
- **CommonPointSets**(generate,`point/generate/CommonPointSets.cs`): Set enum→7 張硬編頂點表(Cross/CrossXY/Cube/Quad/ArrowX/ArrowY/ArrowZ,S=0.5)。**承重 named fork=CPU-fill(無 shader/無 .metal)**:TiXL 先建 CPU StructuredList 再鏡射 GPU→我方 cook 直接寫 `c.output->contents()`(point_graph_selftest stubGen 範例),比港 GPU constant array 簡單且忠實。per-Set count via countTransform(file-static stash)。separator(NaN Scale)+default attr 逐字。**refuter(Sonnet 機械逐 row 比對)SURVIVE**: 7 張表逐 row(Cube 36 全掃)+ enum 順序 + count + separator + .t3 預設全 verbatim;唯一發現=Arrow* comment 寫「6 rows」實 7(已修,非 bug)。

**工單筆誤再被 implementer 抓正(查 TiXL 不問假設,連三批)**: ①SubdivideLinePoints 我假設「generate 家族」但其讀 Points input=transform 性質→agent 放 point_modify(鏡 ResampleLinePoints)②CommonPointSets 我工單舉例「Octahedron/Cube=8 角」實際 enum={Cross,CrossXY,Cube,Quad,Arrow*}+Cube=36 線框頂點非 8 角→agent 讀 .cs 港真表。**orchestrator 工單舉例別猜,讓 implementer 對 .cs/.hlsl/.t3。**

**🟡 柏為親測**: SubdivideLinePoints(拖 InsertCount 看線細分密度;ClosedShape toggle 看閉合段;1-frame sizing lag)/CommonPointSets(切 Set 看 7 種預設幾何;Cube/Arrow 線框形)。

**Resume — next (批次38 候選; point-op [AUTO] 礦近底)**:
- **BoundingBoxPoints**(generate,唯一剩的 generator): 讀 input Points 算 AABB→生 box 頂點。**建議走 CPU-readback fork**(cook waitUntilCompleted 後 readback input contents()→CPU 算 min/max→寫 box 頂點;避 TiXL 的多 kernel+aux MinMax buffer+atomic ordered-int 機械)。需先讀 `BoundingBoxPoints.hlsl` 確認輸出 box 頂點 layout(8 角?24 線框?ResultCount 由 C# Update 設,.cs 無 slot→讀 C# 或 .t3 連線鏈定 count)。
- **常駐牙補強(refuter 累積建議,排修性)**: ResampleLinePoints 補 Recompute 彎線常駐 case + SubdivideLinePoints 補 open-path-含-separator + 末段 OOB case + RepetitionPoints 補 pivot+scale+rotation 組合常駐 case(三者現都只 refuter probe 過,無常駐牙)。
- **礦況**: BoundingBoxPoints 後 **point-op [AUTO] 自走礦見底**(其餘全 blocked: texture/SDF/camera/gradient/2nd-buffer/mesh)→ 下一 inflection = **(a)開 image-filter [AUTO] 礦需先建 image golden 接縫**(resident cook 自洽 golden,ConvertColors/FastBlur/KeyColor 等~15 顆;非 TiXL 交叉驗但機器可驗) **或 (b)回報柏為 steer**(Cut 41 六選項: Vec-multiInput seam / Gradient widget / camera seam / MinInt-MaxInt / Field-SDF / 視覺手感親測回收)。**這是真 inflection,非 cheap leaf——批次38 做完 BoundingBoxPoints 應停下評估 a vs b。**

## Cut 44 — 批次 38: BoundingBoxPoints + point-op [AUTO] 礦見底 ⏹ (2026-06-15; /sw-batch 自走, Opus orchestrator) ✅⏹
**commit `555df4d`**: BoundingBoxPoints(generate,`point/generate/BoundingBoxPoints.{cs,hlsl}`)。讀 input points 算 AABB→**只輸出 1 個點**(Position=center=(min+max)/2,Stretch=max-min,Selected=1,W=1,Color=1,Rotation=identity;NaN-position separator 不計入)。**承重 named fork=CPU-readback**(讀 shared input buffer CPU min/max 迴圈,取代 TiXL GPU atomic ordered-int 化約[clear2+InterlockedMin/Max into aux MinMax buffer]——result bit-identical,只丟 GPU atomic-int 編碼)+**欄位映射=byte-identical struct aliasing**(orchestrator 親手核對 offset:TiXL `LegacyPoint.Stretch@48`≡`Point.Scale@48`≡`SwPoint.Scale@48`;`Selected@60`≡`FX2@60`;`W@12`≡`FX1@12`,權威=shared point.hlsl:9-27 + tixl_point.h static_assert,非 heuristic)+空輸入退化盒(origin,vs TiXL min>max garbage)。輸出 count 恆 1。無 .metal 無 params。golden(unit cube center+size/asym 證 midpoint 非 mean/NaN-skip/graph smoke captured=1)+injectBug 紅。**最低風險顆,offset 親核取代 refuter**。--bite BoundingBoxPoints+所有牙綠;**FAILED:[soundtrack]=已知 AVAudio @4x 間歇環境 flake**(同 binary rc 翻 1/1/0,clean-base stash 也翻=非本批/柏為域,同歷批 Cut)。check-arch 綠。

**⏹ 自走 loop 停手(柏為停手條件①「礦挖盡」觸發,非放棄)**: 本 session /sw-batch 自走 **3 批 5 顆全進主線+全 refuter/親核 SURVIVE**(批36 RepetitionPoints·ResampleLinePoints / 批37 SubdivideLinePoints·CommonPointSets / 批38 BoundingBoxPoints),--bite **125→130**。**point-op [AUTO](逐點純變換,可對 TiXL 公式手算/位置交叉驗的 rigorous 自走礦)挖盡**——連同批27-35 的 value/math machine-verifiable 礦,**所有「能對 TiXL 嚴格交叉驗、不需柏為眼睛/不需開大縫」的自走礦全部見底**。

**剩貨分類(全需柏為在場 或 降驗證門檻 或 開大縫,自走的 rigorous 部分碰不了)**:
- **(a) image-filter [AUTO]**(~15 顆 ConvertColors/FastBlur/KeyColor/HoneyCombTiles/RgbTV/glitch 族,單 texture 純 shader): 機器可驗但**只能 resident cook 自洽 golden,非 TiXL 交叉驗**(跑不了 TiXL 拿不到 ground-truth)→驗證門檻比 point-op 弱;且是**柏為視覺域**(HDR ringing/色彩保真需他眼睛,Cut 28 Pixelate/Sharpen 即此)。需先建 image-golden 接縫(新基建)。
- **(b) 柏為級 steer(Cut 41 六選項仍掛)**: ①Vec-multiInput seam(模型/UX 拍板)②**Gradient widget(我接 op 你畫色帶=high-value 真貨)**③camera seam(解鎖 TransformFromClipSpace/SortPoints/SamplePointsByCameraDistance + Field/SDF)④MinInt/MaxInt 型別 liberty ⑤Field/SDF 大子系統 ⑥視覺/手感親測回收(批24/25/27/34/36/37/38 累積:Ease 手感/point 變形視覺/CommonPointSets 7 預設形/SubdivideLinePoints 線細分/BoundingBox 框)。

**為何停在這(非機械續 image filter)**: image-filter 是「降驗證門檻(自洽非交叉驗)+進柏為視覺域+建新基建」三合一的 pivot,不是同類 cheap leaf 續挖——這是品質/方向決策,柏為 steer 比我猜更對。Cut 41 六選項(尤其 Gradient widget)價值高於機械磨自洽門檻的 image filter。**自走已把所有「不需柏為、能對 TiXL 嚴格驗」的 parity 補滿(批27-38,十二批),loop 停這裡等柏為 steer:(a) 授權我開 image-golden 接縫走視覺 filter 批(接受自洽門檻+你事後肉眼驗) 還是 (b) 挑 Cut 41 某條(Gradient widget 我最推)。**

## Cut 45 — 自癒 workflow 機制 + image-filter 方向選定 + ConvertColors 首跑栽 args(2026-06-15 晨; 柏為 steer) ⏹
**柏為 steer**: 「自己判斷 agent 死掉,在 workflow 增加自己檢查的能力,死掉再派一個,繼續走」→ 兩件:①把 agent 死亡韌性做進 workflow ②繼續走(=選 image filter 方向 a)。

**承重修正(Cut 44 的 (a) 框錯)**: 「image-filter 驗證門檻較弱」是**看錯**——point-op 也從沒跑 TiXL,是對 TiXL 公式手算斷言。image-filter 同理:port `.hlsl` → golden 斷言手算像素值 → refuter 對 `.hlsl` 否證,**同等嚴格**。且 image-golden 基建**批次12 早建好**(`TexCookCtx`/`registerTexOp`/`cachedTexPSO`/`runTintSelfTest` MATH golden,模板=`point_ops_tint.cpp`)→ 加 filter = 乾淨鏡 Tint,非新基建。故 image filter = 乾淨自走礦,方向 a 成立。

**做了①自癒 workflow(已 commit,持久可重用)**: `tools/workflows/self_healing_node_batch.js`(commit `71e1eee`+防呆 `e5813bc`)。核心 `resilient(prompt,opts,label,maxTries)` 包每次 `agent()`:回 `null`=終端死亡 → 帶 salvage 提示換一個再派,implementer 3 次/refuter 2 次,全死才放棄繼續下一 op。sequential 隊形(implementer 寫主樹共享,不可並行寫);每 op implementer(schema)→refuter(schema);Workflow agent 預設非隔離=主樹=正確 base(自動避 worktree-trap)。最終 build/--bite/親核/commit 仍 orchestrator。寫進 `WORKFLOW.md §六補遺3` + [[subagent-death-detection]]。

**做了②首跑 ConvertColors — 栽在 args 傳法(非 agent 死,零損害)**: 我把 op 陣列傳成 **JSON 字串**而非真 JSON 值(Workflow 工具警告過)→ 腳本 `for...of` 對字串**逐字元**跑 → 55 個 "undefined" 工單,全被 step-0 地基檢查 `ls undefined` 擋成 wrong_base、**零檔寫出、樹乾淨**(step-0 守衛紀律完美生效)。已加防呆(string→JSON.parse/非陣列→空/壞項丟棄+log)。**誠實:`resilient()` 死亡重派路徑首跑沒踩到(沒 agent 真死)=untested live。**

**⏹ session 收尾(柏為要進下個 session)**: 工作樹乾淨,HEAD `e5813bc`,全 commit。memory(lane-state 頭/MEMORY.md 索引/subagent-death-detection)+本 Cut 全更新。

**Resume — next (下個 session 直接接)**:
1. **跑 image-filter 批(方向 a,自走礦)**: 用 `Workflow({scriptPath:"tools/workflows/self_healing_node_batch.js", args:[{...}]})`——**args 傳真 JSON 陣列別 stringify**(首跑教訓)。首顆 = **ConvertColors**(已勘乾淨:單 `Texture2d`+Mode enum[RgbToOKLab/OKLabToRgb/RgbToLCh/LChToRgb...逐行讀 `img/adjust/img-fx-ConvertColors.hlsl`];模板 `point_ops_tint.cpp`;GenerateMipmaps/OutputFormat 若不支援 baked+具名 fork)。op spec 範本見上次呼叫(op/kind/baseProbe/template/tixlCs/tixlHlsl/selftest/fn/notes/outputs/golden/refuteFocus 十二欄)。
2. **image-filter [AUTO] 礦池(~15,gap-scan 過 false-positive 已濾)**: ConvertColors/FastBlur(mip-chain 較重)/KeyColor/HoneyCombTiles/RgbTV/Glow/glitch 族——逐顆開 `.cs` 確認單 Texture2d+純量參數(非 2nd-texture/gradient)。
3. **驗 `resilient()` 死亡路徑**: 真跑一批就會踩到(若有 agent 死);沒死也無妨,機制邏輯已在。
4. **柏為域待回收**: Gradient widget(高價值,需你畫色帶)+ 批24-38 視覺/手感親測 + Cut 41 其餘 steer。

## Cut 46 — image-filter 礦開張: ConvertColors (首顆 [AUTO] image-filter + 自癒 workflow live 證鏈) ✅ (2026-06-15 晨; /sw-batch 自走, Opus orchestrator)
**承重達成**: image-filter [AUTO] 礦從「方向選定」變「真產出第一顆」。自癒 workflow(`self_healing_node_batch.js`)首次跑出真 op(Cut 45 只栽 args 零產出)——live 鏈路證實:Workflow(args 傳真 JSON 陣列,非 stringify)→ implementer(schema,主樹非 worktree)→ refuter(schema)→ orchestrator 親核 → commit 全通。`resilient()` 死亡重派路徑仍 untested(本批零 agent 死)。

**ConvertColors(commit `73a1bbd`)**: RGB↔OkLab / RGB↔LCh 色彩空間轉換,4-mode。單-pass texture op 鏡 Tint:`point_ops_convertcolors.cpp`(cookConvertColors + registerConvertColorsOp + runConvertColorsSelfTest)+ `convertcolors.metal`(convertcolors_vs/_fs)+ `convertcolors_params.h`(ConvertColorsParams{float Mode}) + NodeSpec 進 `node_registry_image_filter.cpp`。Mode enum 逐行照 `img-fx-ConvertColors.hlsl` 分支;4 個 float3x3(fwdA/fwdB/invA/invB)+ 4 轉換函數逐行港自 `shared/color-functions.hlsl`。

**★承重 fork [matrix-mul]**(silent color corruption 高危,refuter 主攻): HLSL row-major float3x3,**同矩陣 invB 在 RgbToOkLab 用 mul(M,v)、在 RgbToLCh 用 mul(v,M)**,兩個方向。港成 MSL column-major = `float3x3(HLSL_row0,row1,row2)`(列當行)→ 等價式 HLSL `mul(M,v)`==MSL `v*m`、HLSL `mul(v,M)`==MSL `m*v`,**每個 mul 點逐函數港、不假設單一方向**。refuter **SURVIVE**:代數證明等價式 + 獨立 Python 手算 RgbToLCh(200,100,50)→(199,24,168) 與 GPU 逐 byte 同 + golden injectBug 翻方向→FAIL 咬合確認。

**具名 no-op fork**(照 .cs [Input] 序列出,非假旋鈕): GenerateMipmaps / OutputFormat(TexCookCtx 無 mip/format 接縫)。sampler = point(nearest)+clamp 對 .t3 Filter=MinMagMipPoint。

**驗證(orchestrator 親手復跑)**: --bite PASS=130 NO-BITE:[] / check_arch OK / `--selftest-convertcolors` rc=0(TestA forward 手算 byte-exact + TestB OkLab round-trip)/ injectBug rc=1。scenario: fence_preview(hash 序敏感先例)+ d4_save_restart + point_modify_chain 全 PASS → append NodeSpec 無擾動、零新回歸。

**🟡 發現: `displace_chain.scn` pre-existing red(非本批)**: 連兩跑 FAIL `cannot resolve param:Displacement in map.json`(line 74,點 Displace#106 後 Inspector 拖 param:Displacement)。**clean-base 隔離證實**(stash ConvertColors→重建→同 FAIL)= 與本批無關,是既有 eye-map 解析缺陷(疑似柏為域 UI/eye 層,或 map 解析對 Inspector param 的 phantom)。**排修候選,不擋 image-filter 礦**。

**非阻塞 issue(refuter 揭露,已具名)**: 生產 cook 寫 RGBA8Unorm(全 filter pipeline 共用約束 `point_ops_rendertarget.cpp:223`)→ Mode=0 OkLab 的 a/b 小負值在螢幕貼圖被夾到 0 → 與 TiXL(.t3 OutputFormat=R32G32B32A32_Float)on-screen 在 OkLab/LCh 中間模式視覺不同。kernel 數學 bit-exact 正確;100% 視覺 parity 需開 TexCookCtx float output-format 接縫(跨所有 filter,非單顆)→ 列接縫候選。

**Resume — next**:
1. **續 image-filter 礦(鏈路已證,可一批多顆)**: 礦池 ~14 剩。逐顆開 `.cs` 確認單 Texture2d+純量(非 2nd-texture/gradient): KeyColor / HoneyCombTiles / RgbTV / Glow / glitch 族 / FastBlur(mip-chain 較重排後)。op spec 十二欄範本見 Cut 46 ConvertColors 呼叫。
2. **排修 displace_chain**: eye-map 解析 param:Displacement 缺陷(clean-base 既有);先查 map.json 為何不含 Displace Inspector param(疑 Inspector param 未進 map readback,或 node 106 hit-test)。
3. **接縫候選**: TexCookCtx float output-format(開了 image-filter 色彩中間模式才 100% 視覺 parity,跨所有 filter)。
4. **驗 `resilient()` 死亡路徑** + **柏為域**: Gradient widget(需你畫色帶)/ 批24-38 視覺手感親測 / Cut 41 其餘。

## Cut 47 — 產能 pivot: workflow 防錯亂 + image-filter op 層自登記(平行織解鎖)+ 柏為退出親測 (2026-06-15 午; 柏為 steer + 壓測)
**柏為 steer（重大方向轉向，覆寫舊完成定義）**: ①觀察「/sw-batch 自走 workflow 做一下就精神錯亂，發生很多次」→ 要先修 workflow ②「別管我要自己驗證了，那不是重點，我根本沒辦法對比 TiXL 差異，差不多就好，重點是要能動、有那個意思」→ **退出逐顆親測 parity**。③要「大量增加產能」(不省 token)。

**完成定義轉向（覆寫 [[simple-world-northstar-and-method]] 的「柏為親手測得到」）**: 舊=柏為親手測 parity + 活體牙 .scn 當完成定義。新=**能動 + 視覺有那個意思 + 差不多就好**，不需柏為親測簽核。**但承重翻譯（柏為退出≠放鬆 parity）**: 柏為的眼睛退出 → **headless golden 對 TiXL 公式手算交叉驗 + refuter 變成唯一 parity 防線，保留甚至加重**；scenario 活體牙留著但角色從「給柏為看」變「機器自證能動」。否則「差不多」會滑成「沒對 TiXL、只自洽」。

**壓測判決（press-pass，產能怎麼大量增加）**: 預設提案「開大量平行 agent」六個月後斷在——合流單點(全過 orchestrator 一顆 context) + 共享檔撞車(平行 agent 改同四個列表檔 git conflict) + 柏為親測積壓(C2:北極星若靠親測，產出超過柏為能驗的速度=假完成)。**真承重旁支 = 先拆共享檔成資料驅動**(不拆，加再多 agent 都在合流點排隊撞車)。三前置: ①拆共享檔(本 Cut 做了) ②驗證分流(headless GPU 快 / live scenario GPU 慢，或第二台機) ③產能優先投 headless 可嚴格驗的 op(不吃柏為眼睛)。

**做了①workflow 防錯亂(`edf37fe`，改 `.claude/commands/sw-batch.md`)**: 真因=orchestrator 太容易「下場」吃診斷肉(grep/讀碼/clean-base)→context 塞爆→忘記自己是迴圈→turn 空手結束→靜止 8 小時(本 session 實證:診斷 displace_chain 到一半空手結束)。修=憲法第6條 orchestrator 絕不下場(red 派 subagent 收結論)+步驟4 red→triage 外包(pre-existing 驗證基建/柏為域用 spawn_task 不擋批)+上下文衛生 turn 不准空手結束(必接續或 ScheduleWakeup)。寫進 [[sw-batch-orchestrator-no-fieldwork]]。

**做了②image-filter op 層自登記重構(`edaff22`)**: 加一顆 image-filter op 從「手編四個共享檔(NodeSpec vector/kTable/register list/decls)」→「**一個 leaf `point_ops_<name>.cpp` + 零共享編輯**」。接縫=每 leaf file-scope `ImageFilterOp` registrar(Meyers singleton sink: `imageFilterSpecSink()`/`imageFilterSelfTests()`)，static-init 自登記 cook(registerTexOp)+NodeSpec+selftest；消費端 live read sink。刪 `node_registry_image_filter.{cpp,h}`+`point_ops_register_image_filter.cpp`。**承重 init-order trap(implementer 抓、refuter 證)**: `doc::g_lib`(document.cpp:20)pre-main static 呼叫 findSpec，但只查非-image-filter type(RadialPoints 等)，且 findSpec/specTypes **live 讀不快取** → registration(pre-main dynamic-init)一定先於任何 post-main read。**我接縫設計的盲點**(原以為 findSpec 一定 main 後)被 implementer 實測抓正——驗證了第6條「不下場、讓 subagent 頂盲點」。Opus refuter SURVIVE: 16 NodeSpec literal byte-identical(雙向 token diff)、init-order 所有路徑安全、無 selftest 漏接(未知 --selftest 回 rc=2 不靜默)。--bite PASS=130 NO-BITE:[]/check_arch OK/fence_preview+save_restart+point_modify_chain 綠。

**CMake glob 已資料驅動**: `point_ops*.cpp`+`shaders/*.metal` CONFIGURE_DEPENDS → 加 leaf+metal 不碰 CMakeLists。新 infra 取名 `point_ops_image_filter_registry.cpp` 讓 glob 抓到。

**Resume — next (產能 payoff，平行織已解鎖)**:
1. **平行織 image-filter 礦(本 Cut 的目的)**: 剩 ~13 顆(KeyColor/RgbTV/Glow/HoneyCombTiles/glitch 族/FastBlur…)。**現在可多 worktree agent 同時織不撞車**(各自一 leaf 檔，合流無共享衝突)。先證 3 顆乾淨平行合流，再放大。隊形=葉子 worktree 並行(看門狗背景盯)。每顆 headless golden 對 TiXL .hlsl 手算 + refuter（柏為退出，這是唯一 parity 防線）。
2. **驗證分流(產能前置②)**: scenario 全庫 GPU 序列 ~18min 是合流瓶頸。考慮 headless selftest 與 live scenario 拆開、或合流抽樣 scenario(非全庫)。
3. **其他共享檔家族**(point/value/math)若要平行織也需同樣自登記化(本 Cut 只做 image-filter)。
4. **排修/柏為域(不擋產能)**: displace/blur/filter_wave3 eye-map 回歸(task_602f15ec 獨立工程)/ Gradient widget。

## Cut 48 — 平行織 3-proof 成立 + worktree base-trap 解藥 + sink 洞補完 (2026-06-15 午; 柏為「去解他」→「先收尾暫停」)
**承重達成**：產能模型從「序列 1 顆/19 分」變**真正平行織**——3 顆 image-filter op 在 3 條平行 worktree lane 同時織出、合流零衝突。柏為壓測時定的「先證 3 再談 12」=證完。

**做了①worktree base-trap 解藥(`1cd78f1`，端到端驗證)**：Agent 內建 `isolation:worktree` 確實從 main(a54b8c0,落後307)切（新 probe 實證 HEAD=a54b8c0/重構檔全不存在）。解藥=`tools/agent_worktree_setup.sh`（`git merge --ff-only <主倉HEAD>` 快進+symlink third_party+ccache build；a54b8c0 是祖先→乾淨 ff）。**端到端驗**：手建 a54b8c0 worktree→跑腳本→HEAD=f63eb08+重構檔 PRESENT+binary BUILT。歷史卡 a54b8c0 是因舊工單「ls 偵測就停」沒跑腳本 ff。sw-batch 派工步驟釘成 worktree lane step-0 硬性。詳 [[worktree-base-main-trap]]。

**做了②3 顆平行 image-filter op**：TransformImage(`b026b6b`,offset/stretch/scale/rotation,forks=b2 source-aspect/Offset X 取負/rotation sign idiom 3.141578/sampler Wrap)+MirrorRepeat(`af8378e`,mirror-fold+dual rotation,★fork=cbuffer `__dummy__` 16-byte 對齊≠.cs序)+KochKaleidoskope(`af8378e`,fractal,forks=int-vs-float 迴圈邊界/4-tap AA/Center Y-flip)。各 headless golden 對 .hlsl 手算+injectBug 咬合（柏為退出親測→golden 是唯一 parity 防線，三顆都有）。

**做了③proof 揪出並補完自登記重構的隱藏洞**：3 個獨立 Opus agent 收斂同一診斷——**selftest sink `imageFilterSelfTests()` 被填但無消費者**（`--selftest-list` 沒實作、`runSelftestFromArgs` 只讀 kTable），convertcolors 靠多餘 kTable row 掩蓋。Cut 47 重構的 refuter SURVIVE + 我親核都漏了（沒測純-sink op，被 convertcolors kTable row 掩蓋）。**合流時只取 lane1 的一次性 sink 修補**(selftests.cpp:`--selftest-list`+sink dispatch)，lane2/lane3 **只取 leaf 檔**(各 .cpp/.metal/.h)→照樣被掃到+綠 → **證明 sink 修通後加 op 是純 leaf 添加、零共享編輯=conflict-free**。

**🟡 fence_preview 假紅=glob-staleness**：合流跑 scenario 時看到 fence_preview rc=1，triage(clean-base 4 格全綠重現不出)判定=stale binary（scenario runner `sw_scenario.sh`/`run_all_selftests.sh` 自己不 build，加 leaf .cpp 後要先 `cmake -S app -B app/build` reconfigure 重展 glob 再跑，否則驗舊 registry）。非 code bug/非回歸/fence.scn 的 order-independent 硬化早已到位。親核重 build 後 fence PASS。**流程鐵律延伸：加 leaf .cpp 後，跑 scenario 前也要 reconfigure+build（不只 --bite 前）。**

**驗證(orchestrator 親核)**：--bite PASS=133(130→133)NO-BITE/check_arch OK/三顆 selftest green+bug 咬合/fence_preview 重 build 後 PASS。

**⏹ 柏為「先收尾暫停」**：3-proof 全 commit 落地(b026b6b/af8378e)，樹乾淨 HEAD af8378e，Cut 48+memory 結帳完。

**Resume — next (放大平行織)**:
1. **平行織剩 ~10 顆 image-filter**（sink 通+base 解藥+自登記=conflict-free 已證）：scout 已掃過礦池，spare=ColorGrade(乾淨但重)；再 scout 一批乾淨單-texture op，一次放 3-5 條平行 worktree lane（各 step-0 跑 agent_worktree_setup.sh）。合流配方=cherry-pick 各 leaf（純 leaf 零共享，sink 已通）。
2. **convertcolors kTable row 殘渣清理**（pre-existing wart：sink 通後 convertcolors 雙註冊，移除 kTable row 讓 sink 為唯一路徑）。
3. **point/value/math 家族平行化**需先比照 image-filter 自登記（仍共享檔），要平行才做。
4. **排修/柏為域**：displace/blur/filter_wave3 eye-map(task_602f15ec)/Gradient widget/三顆新 op 視覺手感親測（柏為域，差不多就好）。

## Cut 49 — pixel-shader leaf 礦見底 + seam-ceiling 地圖（2026-06-15 晚; /sw-batch 自走「≥30 節點」directive） ✅
**承重達成（但不是 30，是把牆的形狀測出來）**：directive 要 ≥30 節點。實際**乾淨可平行織的 pixel-shader image-filter leaf 只剩 2 顆**（StarGlowStreaks + ColorGrade，`0d57afe`，--bite 133→**135** NO-BITE:[]/check-arch OK/各 golden 對 TiXL 手算+injectBug 咬合）。30 不可達**不是產能不足，是 seam 天花板**——pixel-shader 單通道 leaf 這一類**已挖盡**（已織 ~20 顆）。

**真正承重發現＝leaf seam 能力天花板（scout/triage 的「conflict-free」舊判準漏掉的）**：image-filter leaf seam 只能裝「單通道 pixel/fragment shader ∧ 只採 level-0（無 mip pyramid）∧ 單一 graph-wired 輸入貼圖（無內部 asset 貼圖）∧ 非 .t3 compound ∧ 無 gradient/curve-LUT 接線」。本批 3 顆 agent 誠實撞牆並具名（沒一顆 bake 假 op）：
- **Glow → NOT-LEAF**：是 `Glow.t3` compound（~20 子節點：5×Blur + 5×Layer2d 合成 + Execute fan-in），**無 Glow.hlsl**。要 Layer2d-BlendMode 合成原語 + Execute/RenderTarget 累積 seam。
- **RgbTV → NOT-LEAF**：單通道 .hlsl 沒錯，但用 `SampleLevel(uv,i)` i=0..7 **mip 金字塔**（全庫零 mip 生成）+ 內部 noise asset 貼圖（非 Image input）。
- **HoneyCombTiles → NOT-LEAF（stall 中發現）**：靠 .t3 的 SampleCurve LUT 貼圖（256×2 R32F）+ MultiInput 接線（curve.cpp/.h 在但需 graph-wire seam）。
- **triage（讀-only）判 5 顆 nominated**：只 **ColorGrade** 是 leaf；ScreenCloseUp=compound .t3、SortPixelGlitch/Crop/ConvertFormat=**compute shader（`-cs.hlsl`，leaf seam 只支援 pixel/fragment，不支援 `[numthreads]` ComputeCommandEncoder）**。triage「新發現」4 顆（Dither/DetectEdges/ChromaticAbberation/PolarCoordinates）**全已實作**（檔名不對應 op 名→scout+triage 連兩次漏 already-done：chromab=ChromaticAbberation、polarcoordinates=PolarCoordinates…）。

**事故/流程**：StarGlowStreaks + HoneyCombTiles 兩條 lane stream-watchdog stall 600s（非 verdict，是 hung，疑大 context 讀 .cs/.hlsl）；StarGlow 已寫完整 leaf→**salvage agent 進同 worktree 收尾不重做**（`e923522`，seam re-check 全清+補 BlendMode enum 對 SharedEnums.RgbBlendModes+golden 雙腿）；HoneyComb 只寫 shader+params 無 cpp→判 compound 入隊不 salvage。**教訓：每個 implement 工單必含 seam-trap 硬閘（mip/asset-tex/compute/compound）+ already-done 硬閘（不靠檔名靠 grep cook type）**——scout 不可信，工單自帶閘才擋得住。

**Resume — next（牆在哪、門怎麼開：全是 seam-infra，每個解一整類，但都是共享檔/承重 engine seam→破壞 conflict-free 平行織模型 = 產能模型重塑 = 柏為 steer 的接縫決策，不在自走 loop 內擅自動 cook seam）**：
1. **🔑 柏為拍板：下一塊 infra seam 投哪個**（每個 unlock 一整類 op，但都是 shared-infra 單條序列、本質複雜 engine seam）：
   - **(A) compute-shader cook seam**（ComputeCommandEncoder 路徑）→ unlock Crop/ConvertFormat/SortPixelGlitch + 一大類 `-cs.hlsl` op。最機械、最少品味、unlock 面最大。
   - **(B) mip-generation seam**（resident-texture mip 鏈）→ unlock RgbTV/FastBlur/Bloom blur-pyramid 類。
   - **(C) Layer2d-BlendMode 合成原語 + Execute/RenderTarget fan-in** → unlock Glow + stylize compound 類。最架構、動 command-graph。
   - **(D) Gradient widget**（柏為畫色帶 + runtime 取樣 + LUT 接線）→ unlock Bloom/BubbleZoom/Steps/RemapColor/SubdivisionStretch 5 顆。**含柏為視覺域（畫色帶）**。
   - **(E) 內部 asset-texture bind seam**（noise/font atlas）→ unlock RgbTV noise/AsciiRender。
2. convertcolors kTable row 殘渣清理（Cut 48 遺留 pre-existing wart）。
3. **排修/柏為域**：displace/blur/filter_wave3 eye-map(task_602f15ec)/三顆新 op(含本批 StarGlow/ColorGrade)視覺手感親測（差不多就好）。
4. point/value/math 家族（Cut 44 已挖盡，無新候選）。

**⏸ 停在 seam-ceiling**：clean pixel-shader leaf 礦見底（停止條件 1 觸發 + 剩餘候選全卡在 seam-infra/柏為域 = 停止條件 2）。不空轉再掃（結構性無 clean 候選，不是候選沒找夠）。下一步是 (A)-(E) 的 infra 投資序，承重 engine seam，回報柏為 steer。

## Cut 50 — compute-shader cook seam（柏為 steer (A)）+ Crop 首顆 -cs.hlsl leaf ✅ (2026-06-15 晚)
**承重達成**：柏為 拍板 (A) → **打開 image-filter leaf seam 的 compute-shader dispatch 路徑**，unlock 整個單-dispatch `-cs.hlsl` op 類（之前 leaf seam 只有 pixel/fragment）。HEAD `22b17c3`（ff-merge：`bc09650` seam+Crop / `22b17c3` golden 補完）。--bite **136**(135→+1 crop) NO-BITE:[]/check-arch OK/crop green+bug RED。

**流程（承重 seam 全工法走完）**：①Plan agent 先出 file:line 藍圖（降 stall 風險 + 不讓 build agent 重推）②Opus build agent（worktree+step-0）照藍圖實作 ③**Opus refuter 獨立對抗**（self-check≠裁判，作者兼裁判陷阱）④fixer 補 refuter CONCERN-A ⑤orchestrator 親手 ff-merge+主樹復跑全閘。

**seam shape（最小改、未來 compute op 零共享編輯）**：cook fn 簽章不變 `void(*)(TexCookCtx&)`，leaf body 自己 dispatch compute encoder。共享改 5 檔**一次**：`image_filter_op_registry.h`(+`ImageFilterComputeOp` registrar + `imageFilterComputeTypes()` set + `imageFilterSizeFns()` map)/`point_ops_image_filter_registry.cpp`(兩 sink+ctor)/`point_graph.cpp` cookTexNode(**input-gather 無條件移到 output-sizing 前**——pixel op 無關故單路徑;有 sizeFn 則從 cooked input dims 算 output size;`needsWrite` 傳 ensureTex)/`point_graph_internal.h`(ensureTex +`bool shaderWrite` OR `ShaderWrite` usage)/`tex_op_cache.{h,cpp}`(`cachedComputePSO` 鏡 cachedTexPSO+clearTexOpCache 釋放 compute PSO)。**未來 compute op = 一個 leaf `point_ops_<name>.cpp`+`.metal`+`_params.h`，file-scope `ImageFilterComputeOp{spec,type,cook,sizeFn,selftest}` 自登記，零共享編輯**（同 pixel leaf 人體工學）。compute 機制抄 `particle_system.cpp:26-33,96-107`（唯一既有 compute 參考）。

**Crop（首顆，`point_ops_crop.cpp`+`crop.metal`+`crop_params.h`）**：逐字港 `CropImage-cs.hlsl`。ports 1:1 Crop.cs(Image/LeftRight Int2/TopBottom Int2/PaddingColor Vec4,預設 Crop.t3 (0,0)/(0,0)/(1,1,1,0))。**named forks**：Int2→2×Float-Vec（kernel `int(v+0.4)` 截斷使分數落整數像素，**+0.4 截斷逐字非 round**）；output size=input−(L+R)×(T+B) clamp≥1（sizeFn,從 cooked input dims）。**MSL 銀彈坑（refuter 全咬）**：t0/u0→MSL 單一 texture namespace→input@texture(0)/output@texture(1) 須一致；`[numthreads(8,8,1)]` 不是 MSL attribute、只活在 host dispatchThreadgroups（host TGX/TGY=8 須對齊 kernel）；ceil-div + kernel `i>=width/height` guard 覆蓋 non-8-divisible 餘塊。golden 對手算（80×76 非8整除/marker 位移/magenta padding @offset16）+ **CONCERN-A 補完=餘塊全覆蓋斷言（sentinel 預填→unwritten==0;floor-div 注入→unwritten=320 RED）**。

**refuter verdict=MERGE-SAFE**：8 攻面全 SURVIVE（3 牙咬證+revert：binding-swap→RED/stray-float→static_assert/floor-div→320 unwritten）。**最險=cookTexNode 無條件 reorder 影響所有 tex op→證 behavior-preserving**：只 recursion-into-upstream 前移，本節點 cook fn 仍在 sizing 後跑；每節點各自 `flatKey(id)` 無 key 撞；136 PASS NO-BITE=全 tex-op golden 綠;transformimage-bug/blur-bug/point_modify_chain 親跑綠。CONCERN-B（cropSize 缺 16384 上 clamp）=benign（Crop 只縮）跳過。

**🟡 假紅澄清**：①--bite 中途 `Abort trap 6` on 某 -bug=soundtrack AVAudio @4x 間歇 flake（直跑 soundtrack-bug rc 翻 1，非 seam，tex-op -bug 全乾淨）②blur_chain scenario FAIL=`cannot resolve node:105 in map.json`=task_602f15ec 既存 eye-map 回歸（spawn 節點不進 eye-map,verify 層非 cook 層;point_modify_chain 同 cook path 親跑 PASS 證 reorder 無傷）非本批。

**本 session 累計落地**：StarGlowStreaks+ColorGrade（Cut 49,`0d57afe`）+ compute seam+Crop（Cut 50,`22b17c3`）= 3 真節點 + 1 承重 infra seam。directive「≥30」結構性不可達已坦白（seam ceiling），柏為 steer 後做了最高槓桿 infra。

**Resume — next**:
1. **✗ 已掃完=單-kernel `-cs.hlsl` image op 礦=0（Crop 是唯一一顆,已做）**。scout 枚舉 TiXL 全 `-cs.hlsl` image op:ConvertFormat(2×ComputeStage+ScaleSize+mip)/WaveForm(2-stage groupshared)/SimpleLiquid×2(2-kernel ping-pong 4×RWTexture)/ColorPhysarum(2-stage RWStructuredBuffer+RWTexture agent-sim)——**全 multi-pass,無一單-kernel**。⇒ **compute seam (A) 實際只 unlock 1 顆(Crop),非一整類**（TiXL compute image op 幾乎全 multi-pass）。**pixel-leaf 類(Cut49)+單-kernel-compute 類(Cut50)雙雙見底**。下個 op(任何類)都需再一塊 infra seam=又一柏為 steer。
2. 其餘 infra (B)mip-gen/(C)Layer2d+Execute/(D)Gradient widget(柏為視覺域)/(E)asset-tex bind — 柏為 steer 序。
3. convertcolors kTable row 殘渣（Cut 48 遺留）。
4. 排修/柏為域：task_602f15ec eye-map/三顆+Crop 視覺手感親測（差不多就好）。

## Cut 51 — UI-visual parity pass（柏為「你幫我決定，別停」→ orchestrator steer 走視覺）✅ (2026-06-16 凌晨)
**承重達成**：op 礦雙見底後，**柏為授權我自決 seam 投資序 + 不准停**。我 steer = **先做 UI-visual parity（北極星路線 B，backlog since Cut 22/24），不盲投重 compute seam**。理由：視覺 parity 是柏為域、可 eye 截圖對 TiXL 源碼常數驗證（不需他在場）、零 seam 風險。HEAD `a86377e`。--bite **136** NO-BITE:[]/check-arch OK/scenario 全庫 zero-regression。

**gap-scan 三 scout 結論（rule 6 全外包，orchestrator 不下場）**：①op-gap scout=**零 no-seam 可驗 op 剩**（pixel+單-kernel-compute 雙見底再確認，22 image-filter cook types 已港）；seam sizing 建議 **(A)mip-gen=HIGH 可驗/中度 engine touch**（RgbTV/FastBlur/Bloom 確定性 golden）> (B)compute multi-pass（ConvertFormat/WaveForm 可驗,SimpleLiquid/Physarum 時序非確定性不可 golden）> (D)gradient+LUT（高可驗但 headline 是 painter UI=柏為手）。②UI-visual scout=7 具名 gap+源碼常數 file:line。③interaction scout=多數「gap」是 features-not-built（3D camera/bookmarks/layouts）非真 parity，加 keymap row 會變 dead row→deprioritize。

**5 gaps closed（單 sequential lane,共享 ui/ 檔→不開 worktree,零 base-trap）**：① canvas bg `ed::StyleColor_Bg`=`(0.12,0.12,0.12,0.98)`(UiColors.cs:29,imgui-node-editor 1453 填整 view) ② node rounding `nodeRounding()` 去 20px cap、留 hard floor `tixlScale<0.5→0`、else `5*scale`(DrawNode.cs:126) ③ conn thickness `connectionThickness(sel,idleFade)=Lerp(0.25,2.0,p)+(sel?2:0)`(DrawConnection.cs:170) ④ conn color `Fade(Lerp(0.6,1.0,p))`(DrawConnection.cs:44) ⑤ **pins=triangle inputs/circle outputs**(最高視覺 impact,DrawNode.cs:629-630/918-919,type-colored)。檔=editor_ui.cpp/node_draw.cpp/node_style.{cpp,h}。

**承重發現（idleFade 信號是真的非捏造）**：implementer 本要 defer idle ramp（以為無信號），實測 **`framecook::residentNodeLastUpdatePass` 已存在**（frame_cook.cpp:126 每幀 stamp lastUpdatePass=frameIndex；UI `RemapAndClamp(framesSince,0,100,1,0)` 與 TiXL `DirtyFlag.FramesSinceLastUpdate` byte-identical,units=frames 兩邊對齊）→織全 ramp 非 baseline。**named forks**：①pins 繪 TiXL scale-1 尺寸非 ×CanvasScale——我方 pins 是 node 內 inline body marker（`GetWindowDrawList()` 已被 imgui-node-editor zoom-transform 當整體縮放）,再乘 CanvasScale 會雙重縮放;TiXL anchor 是 screen-space 絕對座標故須手乘。`nodeRounding` 反而吃 tixlScale（screen-space AddRectFilled）——implementer 在該乘的地方乘、該省的地方省。②idle ramp 用 node-granularity(max across outputs)非 TiXL per-output-slot——我方 resident-cook 層最細信號,同驅動 node-bg fade。③boundary def 形狀翻轉(inputDef→circle/outputDef→triangle)=對齊 TiXL MagGraphLayout.cs:320-362「This looks confusing but is correct」（symbol 的 input 定義在 compound canvas 上是 source→畫成 output/circle）。

**Opus refuter verdict=MERGE-SAFE**：8 攻面全 SURVIVE。最險=「idleFade 信號是否捏造」→證真（frame_cook 實 stamp+remap byte-identical+units 對齊）。pin hit-test airtight by construction（`ImGui::Dummy(9,9)` box byte-unchanged→`ed::BeginPin/EndPin` bounds+`eye::recordRect` 同→connect-drag 不受影響）。每常數 byte-match TiXL 源碼。

**triage clean-base 判決（rule 6,orchestrator 不查根因）**：scenario 全庫 PASS=20 初報 FAIL=8→clean-base 隔離(stash/rebuild/run/pop)證 **ZERO regression**。4 真 FAIL（blur_chain/displace_chain/filter_wave3/**math_op_chain**）=同一 pre-existing 族（**freshly-spawned node 不進 state.json** → cascade「cannot resolve pin/node/param」,verify/state 層非 cook,task_602f15ec;math_op_chain 同 node-creation-not-landing signature 擴充進此族）clean-base 同紅。4 假紅（d4_transport_rate/fence_preview/keys_pin_output/keys_wave2）=已知 flake,本次 green-both-trees（初報 8 是兩 flake 那輪偶紅灌水）。**keys_* 過 both trees=pin-draw diff 沒動 hit-test/map coords 實證**。

**Resume — next**:
1. **★下批=seam (A) mip-gen 投資**（我已 steer:最佳自走 seam=全 headless 可驗、無 painter-UI 依賴、unlock RgbTV/FastBlur/Bloom 確定性 golden）。走 Cut 50 承重 seam 全工法：Plan agent file:line 藍圖→Opus build(worktree+step-0)→Opus refuter 獨立對抗→fixer→orchestrator ff-merge+主樹復跑。seam shape 參考：resident texture mip pyramid 儲存 + `texture::setMipmapLevelOfDetail`/mip-write usage flag binding（op scout 評 medium engine touch）。
2. 其餘 seam 序（我評）：(B)compute multi-pass 只 ConvertFormat/WaveForm 可驗(SimpleLiquid/Physarum 時序不可 golden 降級)、(D)gradient+LUT 需柏為 painter（可先建 engine seam+default gradient headless 驗,painter UI 後補）、(C)Layer2d+Execute 最後（blend-order 路徑依賴 golden 脆）。
3. 其餘 UI-visual gap（次批可續,同 sequential lane）：font 字級/title padding zoom-aware（scout #17/#20 UNKNOWN 需先量）。interaction：把 scattered Undo/Redo/Copy/Paste/Delete 收進 keymap 表（已實作只是不在表,資料驅動鐵律7）——但須先驗哪些 handler 真存在避免 dead row。
4. 排修/柏為域：**task_602f15ec**（spawned-node-not-in-state.json,now 含 math_op_chain;verify/state 層）/三顆+Crop 視覺手感親測（差不多就好）。

## Cut 52 — resident(production) cook seam back-port：Crop 真正進產線 ✅ (2026-06-16 凌晨)
**承重達成（修 Cut 50 產線缺口）**：mip-gen seam 的 Plan agent 順手揭出 **Cut 50 的 Crop 在 `--selftest` 綠、但在活體 app 渲染成亂碼白雜訊 blob**——resident(production)cook 路徑 `point_graph_resident.cpp` 整個缺 Cut 50 seam（`ensureTex(path,w,h)` 無 needsWrite/無 `imageFilterSizeFns()`/input-gather 在 sizing 後），只有 flat/selftest cook（`point_graph.cpp:357-392`）有。**Cut 50 refuter 走 flat 路驗證故漏掉**=「done」的 op 沒真上產線（違北極星「能動」）。HEAD `8a672da`。--bite **137**(+1 cropresident tooth) NO-BITE:[]/check-arch OK。

**empirical 證 gap=TRUE（build agent step-0）**：未修 binary 手接 RenderTarget→Crop 鏈→活體 Crop 輸出 **garbled 512×512 white-noise blob**=compute kernel 寫進無 `ShaderWrite` usage 貼圖 + 尺寸吃 Resolution pin 非 cropped size 的招牌。同時 `--selftest-crop` PASS（flat selftest 自己手建帶 ShaderWrite 的 output tex）→selftest-綠-活體-壞=gap 確認。**blast radius=只 Crop**（唯一 compute leaf;pixel op 在 resident plain ensureTex 就夠故沒壞）。

**fix（mirror flat seam 進 resident,additive）**：`point_graph_resident.cpp:257-296` 重寫鏡 flat：input-gather 移到 sizing 前（無條件,pixel op 無害）/`imageFilterSizeFns()` 從 cooked input dims 算 output size/`needsWrite=imageFilterComputeTypes().count(opType)`→ShaderWrite。用 resident key（`opType`+`path`）非 flat key（`n->type`+flatKey）但語義同、共用同 registrar sink 不 fork。pixel op（default-false,無 sizeFn）byte-identical 路徑。

**proof=新 `--selftest-cropresident`**（`resident_crop_selftest.cpp`+kTable row+`point_graph.h` decl+CMakeLists 列檔[非 point_ops glob 故需手列]）：驅動**真產線** `cookResident→cookTexNode→compute dispatch→displayTex→target()`（= `frame_cook.cpp:290` 呼叫的同函式),斷言 shrunk dims 44×40+marker 移位(32,30)+magenta pad(2,5)+full coverage;seam 關掉→RED。

**Opus refuter verdict=MERGE-SAFE**：5/5 SURVIVE。①faithful mirror（key 各自正確/sizeFn 讀 post-cook dims/分支齊全）②additive 無 pixel-op 回歸（upstream 寫 `texBuf[srcPath]`、本節點寫 `texBuf[path]` disjoint key→reorder 無害;全 ~22 pixel op bite 綠實證）③selftest 真非 tautology（呼真 `cookResident`+讀真 `displayTex`,refuter 自做 RED 證:關 sizeFn→64×64 dimsOk=0 RED）④活體=fair substitute（selftest 走完整產線路,live 多傳的 3 參數只餵 Float/Automation 不碰 sizing seam;GUI 截圖被 task_602f15ec 擋故用 selftest 替代）⑤hygiene（registerTexOp 全域改但 runSelftestFromArgs 跑一個就 return→process 隔離,= `resident_cook_parity_selftest.cpp:129` 既有 idiom;+1 tooth 真咬）。**LOW finding（pre-existing,非阻塞,= flat crop 同病）**：selftest readback 證 sizeFn 半邊、證不到 ShaderWrite flag 半邊（本機 Metal API Validation 關→無 flag 寫也落）;`needsWrite` 防禦性 spec-correct（validation 開/別 GPU 會炸）→**spawn_task `task_2ee58abb` 排 MTL_DEBUG_LAYER=1 跑 crop teeth 補驗**,不擋批。

**Resume — next**:
1. **★下批=Batch 53 mip-gen seam**（Plan agent 藍圖已備:section A 五檔 seam shape/B 首顆 proof/C golden/D risk,在本 session Plan agent 回報）。**首 proof=StarGlowStreaks at Quality>0**（無真正新 pure-mip op:RgbTV 需 mip+noise-asset 非純;StarGlow 是已 ship 但**今天靜默降級**——無 mips→`level(Quality)` clamp 到 0,TiXL 預設 Quality=0 故 Cut49 golden 沒抓到）→mip seam 把它從降級升回正確,golden delta @Quality=2 對手算 LOD box-average 證。**mip-WRITE=`generateMipmaps` blit（非 shader）**,seam=ensureTex 加 `mipped` 參數(level count=`floor(log2(max(w,h)))+1`,RenderTarget.cs:289)+registrar `imageFilterMippedOutputTypes()` sink + cookTexNode(flat+resident **兩邊**)在 leaf cook 後 issue blit。**mip-READ 零 engine**=consumer 設 `setMipFilter(Linear)`+`sample(uv,level(lod))`（StarGlow 已對）。MSL 坑:`SampleLevel→sample(s,uv,level(lod))`/MipFilter 沒設則 level() 靜默回 level0/`get_num_mip_levels()` 非 hardcode 7。
2. UI-visual 續（font 字級/title padding zoom-aware,Cut51 scout #17/#20 需先量）+interaction keymap 收編。
3. 排修/柏為域:task_602f15ec/task_2ee58abb(validation-layer crop 驗)/視覺手感親測。

## Cut 53 — mip-generation cook seam（mip-WRITE on producer / mip-READ zero-engine）✅ (2026-06-16 凌晨)
**承重達成（開 mip seam，鏡 Cut 50 compute seam 工法）**：最小一次共享改 → 未來 mip op = 一 leaf + 零共享編輯。HEAD `d484c4a`。--bite **138**(+1 mipgen tooth) NO-BITE:[]/check-arch OK。

**seam shape（6 檔）**：①`point_graph_internal.h` ensureTex 加 `bool mipped=false`→`mipmapLevelCount=floor(log2(max(w,h)))+1`（TiXL `RenderTarget.cs:289` log2(w) 推廣到 max；**Metal 在 +2 hard-assert→+1 是天花板,refuter 實證**）+ `texMipped` realloc key ②`image_filter_op_registry.h` 宣告 `imageFilterMippedOutputTypes()` sink + ctor `bool mippedOutput=false` ③`point_ops_image_filter_registry.cpp` 定義 singleton+ctor insert ④`point_graph.cpp` flat cookTexNode:`needsMips`→ensureTex+leaf cook 後 issue generateMipmaps blit(commit+wait,level-0 ready)⑤`point_graph_resident.cpp` 同 hooks ⑥new `point_ops_mipgen_selftest.cpp`。**mip-WRITE=`MTLBlitCommandEncoder generateMipmaps`(blit 非 shader,pattern combinebuffers.cpp:39)。mip-READ=零 engine**(consumer 設 MipFilter+`sample(uv,level(lod))`)。

**default-false byte-identical（承重安全,ensureTex 全引擎共用）**：refuter 證**全引擎只 2 個 ensureTex call site**(point_graph.cpp:396/point_graph_resident.cpp:299),都從**空的** production set 推 needsMips→mipped=false 恆真→`texMipped[key]`(operator[] default-insert false)`false!=false`→零 spurious realloc→descriptor byte-identical(tint/blur/crop/starglow golden 全綠實證)。

**proof=`--selftest-mipgen`（3 legs）**：Leg A uniform-red LOD 不變(levels=7+每 LOD 仍 255,0,0);Leg B 2×2 checker **LOD-1==(128,0,0) 手算 box-average 經新 mipped ensureTex 路**(±2 排除 raw level-0 255/0);Leg C RenderTarget(mipped)→StarGlow Q0-vs-Q2 delta 經**真 flat cook**。injectBug=mipped=false→level(1) clamp level0→RED。

**Opus refuter verdict=MERGE-SAFE**：6/6 SURVIVE（①critical default-false byte-identical 全 golden 綠 ②無 production op flagged mipped=Cut49 StarGlow golden 不動 ③mip 公式 +1 是 Metal 天花板實證 ④generateMipmaps usage/ordering OK ⑤refuter 外部 perturb(移除 blit)證 Leg A/B 真 RED 非 tautology ⑥selftest process-isolated 不洩漏）。**2 minor non-blocking**:①**resident mip hook code-review-identical flat 但 selftest 沒驅(只跑 flat pg.cook)→首顆真 mip op 出時須驅 resident 驗(鏡 cropresident,Cut 52 教訓)** ②in-test `-bug` 是 assertion-flip 非 wiring-perturb(refuter 外部 perturb 已補真 bite)。

**design fork（具名）**：TiXL `GenerateMips` 是 **per-INSTANCE bool port**（FX setup 上的輸入旋鈕,使用者逐實例開）,本 seam 是 **per-op-TYPE set**（`imageFilterMippedOutputTypes()`,同 `imageFilterComputeTypes()` 粗化）。**現無 production op flagged mipped→seam 開了但 unlock 0 顆已 ship op**（StarGlow 仍停在 TiXL 預設 degraded-no-op;selftest 只暫時 flag "RenderTarget" 測完 erase）。

**⚠ 誠實帳：Cut 53 是 infra-without-consumer**。Cut 50 compute seam 帶 Crop 一起 ship；Cut 53 mip seam 只帶 selftest proof,**首顆真 production mip op 是下批**（= Crop-equivalent）。

**Resume — next**:
1. **★下批=首顆真 production mip op（決定 mip seam 是否真有產線價值）**。**先派 scout 回答關鍵問**:FastBlur/Bloom/任何 op 能**只用 mip seam**(pure mip-WRITE pyramid 或 mip-READ consumer)ship 嗎,還是全需再一塊 seam(multi-pass compute/Layer2d/noise-asset)? 候選:(a)FastBlur(op scout 標「blur-pyramid 4 levels」——若單 shader 讀多 LOD=pure-mip;若 multi-pass=需 (B) seam)(b)為 StarGlow degradation 解套需 **per-INSTANCE GenerateMips port**（TiXL-faithful,但柏為-facing UI port=偏品味域）。**scout 答「YES 有純 mip op」→Batch 54 織它(順帶驅 resident mip 驗,鏡 cropresident);答「NO 全需再 seam」→坦白回報 mip seam 暫無產線消費者,pivot 到非-seam 可驗工(UI-visual 續/別 op 家族)或先建下塊 seam**。
2. UI-visual 續（font 字級/title padding zoom-aware,Cut51 scout #17/#20 需先量）+interaction keymap 收編。
3. 排修/柏為域:task_602f15ec/task_2ee58abb(validation-layer crop+mip 驗)/視覺手感親測。

## Cut 54 — FastBlur（Dual Kawase pyramid blur）= 首顆真視覺 multi-pass leaf op ✅ (2026-06-16 凌晨)
**承重達成（本 session 首顆真視覺 op，非 infra-theater）**：multi-pass scratch seam + FastBlur 真 op（寬/快金字塔 blur,我方現有 fragment Blur 做不到）。HEAD `e73c2e7`。--bite **140**(+2 fastblur tooth) NO-BITE:[]/check-arch OK。

**★新戰略洞見（reframe Layer2d seam 的必要性）**：FastBlur 在 TiXL 是 `.t3` Layer2d compound,但 STEP-0 查證 **其 Layer2d wrapper 用 `DisabledBlendState`（plain overwrite 非 additive,`DefaultRenderingStates.cs:88`）→ 純 plumbing,可 leaf-port,不需 (C)Layer2d+Execute seam**。**推論:凡 .t3 compound 的 blend=DisabledBlendState(overwrite)/固定 pipeline→leaf-portable as multi-pass;只有真用 blend mode(additive/multiply)合成的 compound 才需 (C)Layer2d seam**。⇒ (C) 的必要性比 Cut49 估的小——一批 .t3 compound 其實可繞過它。

**multi-pass seam（near-zero 共享改,Plan agent 證）**：cook-fn 簽章已允許 N dispatch（`particle_system.cpp:96-107` 跑 3 pass 前例;leaf 自建 encoder）。唯一缺=**ping-pong 中間貼圖 compute-read∧write**。`cachedScratchTex`(`tex_op_cache.cpp:84-99`)硬編 `RenderTarget|ShaderRead`→加 `bool shaderWrite=false,mipped=false`(OR 進 usage+descriptor+`ScratchEntry` realloc key),**default-false byte-identical**(Blur call site 不動)。**兩 cook 皆零改**（中間貼圖活在 leaf via scratch cache,不碰 cook driver）。

**FastBlur leaf（`point_ops_fastblur.cpp`+`fastblur.metal` 2 kernel+`fastblur_params.h`）**：ports 1:1 Image+MaxLevels;down 4-tap box ×0.25 / up 9-tap normalized tent / FillUpsampleKernel 權重排程 + ResolveSteps **逐字港**（refuter byte-verify 每權重 vs TiXL `_ExecuteFastBlurPasses.cs`+down/up .hlsl）。**消費 multi-pass seam 非 mip seam**（金字塔層=discrete half-res RT 非 mip LOD;**Cut 53 mip seam 仍待首顆真消費者**）。MSL 坑:PS→compute uv=`(gid+0.5)/size`/scratch 跨 pass 翻 role 需 read+write usage/`[numthreads]` host-only ceil-div+guard(非8整除 100×100)。

**Opus refuter verdict=BLOCK→fixer 修→MERGE-SAFE**：seam default-false byte-identical SURVIVE/港逐字 SURVIVE（DisabledBlendState=overwrite 證,leaf fork 對）/兩 cook 真驅 SURVIVE。**BLOCK 點=golden 太鬆**：energy-conservation+soft-edge+spread+coverage **全在錯權重下不變**（refuter 實證 down box 0.30 + up tent diag 4.0[完全錯 shape]皆 GREEN;injectBug 只抓 dropped pass）→**這正是柏為 Cut47 pivot 警告的「差不多滑成沒對 TiXL 只自洽」,refuter 是唯一 parity 防線故鬆 golden=真缺陷**。**fixer 修（Opus,math 須對否則假牙）**：加 **exact-pixel closed-form tooth**——controlled 1-level case(MaxLevels=1=down-once+up-once)、16×16 vertical half-plane(constant-in-Y→塌成 1D closed form)、pin 5 pixel(row8 x5=29/x6=61/x7=102/x8=153/x9=194)±2 LSB 對手算正確權重值。**4 perturbation 全 RED 證牙咬**（down box 0.30 + up tent diag 4.0,× flat+resident;restore 全 GREEN）。

**本 session 累計（Cut 51-54）**：①Cut51 UI-visual parity(5 gaps,路線B)②Cut52 resident seam back-port=**Crop 真正進產線**(修 Cut50 缺口)③Cut53 mip seam(infra,待消費者)④Cut54 multi-pass seam+**FastBlur 真視覺 op**。= **3 真 deliverable(視覺parity/Crop產線/FastBlur)+2 infra seam(mip待消費,multi-pass 已被 FastBlur 消費)**。

**Resume — next**:
1. **★下批=leaf-portable compound scout（FastBlur 洞見放大）**：派 scout 盤點 TiXL .t3 compound 哪些 blend=DisabledBlendState/固定 pipeline（→leaf-portable as multi-pass,繞過 (C)Layer2d seam,可確定性 golden 驗）vs 哪些真用 blend mode 合成（→需 (C)）。候選:Glow(Glow.t3)/Bloom(scout 標需 gradient→若 gradient 是 GlowGradient 固定可繞,若 user-curve 則卡 (D))/其他 fx compound。**每顆同 FastBlur 工法:STEP-0 portability 查→leaf multi-pass→exact-pixel tooth golden(非 energy-only!Cut54 教訓)+both cook→refuter**。
2. Cut 53 mip seam 找真消費者：RgbTV(需 +asset-texture seam E)/或 per-INSTANCE GenerateMips port（柏為視覺域 UI rotor）。
3. UI-visual 續（font/title padding,Cut51 #17/#20 需先量）+interaction keymap。
4. 排修/柏為域:task_602f15ec/task_2ee58abb(validation-layer crop+mip+fastblur 驗,本機 Metal validation 關)/視覺手感親測（FastBlur/三顆/Crop）。
5. **(C)Layer2d+Execute seam**=只 leaf-port 不了的真-blend compound 才需,投資序待累積足夠卡 (C) 的候選再評（FastBlur 證很多 compound 可繞）。

## Cut 55 — DirectionalBlur 試港撞 .t3 param-routing trap → 丟棄 + 工法修正（負結果但承重）⚠️ (2026-06-16 凌晨)
**負結果（未 commit，已丟棄）**：DirectionalBlur 試港 leaf（FastBlur 工法+exact-pixel tooth 都做對了），但 **refuter + 獨立 resolution agent 雙證 param routing parity-wrong → 丟棄**（tree 回 Cut 54 `bb0dfb9`,--bite 140）。**真價值=揪出系統性 trap**。

**★承重 trap（保護整條 leaf-port runway）**：TiXL `_multiImageFxSetup`/`_multiImageFxSetupStatic` compound **用 `FloatsToBuffer.cs` 以 .t3 connection-order 填 shader cbuffer，中間夾數學節點(Multiply/Divide/IntToFloat)，NOT 1:1 op-port→shader-param**。DirectionalBlur.t3 實際：`shader.Size ← (op.Size/op.Samples)×RefineSizeFactor×0.03 = 0 @default`(RefineSizeFactor=0)/`shader.NumberOfSamples ← op.RefinementSamples(6)` 非 op.Samples(16)/`shader.Angle ← op.Angle`(direct)。⇒ TiXL DirectionalBlur **預設是 no-op refinement blur**（要 RefineSizeFactor>0 才動）。我方 leaf 假設 Size→Size/Samples→Samples = **parity-wrong（自洽 golden 過但沒對 TiXL）= 正是柏為 Cut47「差不多滑成只自洽」**。**注意 per-op**:refuter 證 Blur.t3 是 direct routing 無數學→不是所有 _multiImageFxSetup 都中招,每顆要查。

**★工法修正（leaf-port runway 必加 STEP-0）**：港任何 `_multiImageFxSetup`-based compound **必先 trace .t3 cbuffer routing**（連線順序→FloatsToBuffer packing→每 cbuffer field 由哪個 op-port/數學鏈餵），不可假設 1:1。pure-compute op（custom executor 如 _ExecuteFastBlurPasses）走自己的 param fill 不經 FloatsToBuffer connection-order，trap 較小但仍要 trace executor。**FastBlur(Cut54)安全**=走 _ExecuteFastBlurPasses 非 _multiImageFxSetup,MaxLevels→Steps direct（refuter 已證）。

**spawn_task `task_258d9510`**：audit 已 ship 的 _multiImageFxSetup-based op（pixelate/voronoicells/kochkaleidoscope/displace/mirrorrepeat/sharpen/chromaticdistortion/detectedges/dither…）的 .t3 routing 對不對（自洽 golden 可能掩蓋 parity bug）。

**DirectionalBlur 後續**：若要真港=須加 RefineSizeFactor/RefinementSamples/RefinementPass ports+完整 routing+2-pass refinement 邏輯=比「簡單方向 blur」大得多 且預設 no-op（柏為要設 RefineSizeFactor 才動）→低優先,非 runway-validation 好選擇。

**Resume — next**:
1. **★下批=用修正工法港一顆 leaf-portable op**（runway 仍成立,只是 STEP-0 多 .t3-routing trace）。**選 pure-compute op 避 FloatsToBuffer trap**：LightRaysFx(god rays,視覺值高,3 Execute compute)/SubdivisionStretch/GlitchDisplace/SortPixelGlitch(compute multi-pass,glitch 系)。每顆 STEP-0=①not already-have(cook type)②trace 完整 param routing(executor/cbuffer 對 op-port,不假設1:1)③portable(無 real-blend/asset/gradient/temporal)④deterministic→leaf+exact-pixel tooth(非energy!)+both cook→refuter。
2. task_258d9510 audit 結果若揪出 parity bug→排修。
3. Cut 53 mip seam 找真消費者 / UI-visual 續 / (C)Layer2d seam（4 顆真-blend:Glow/Bloom/ScreenCloseUp/Blur）/ (D)gradient(5 顆) / (E)asset(AsciiRender)。
4. 排修/柏為域:task_602f15ec/task_2ee58abb/task_258d9510/視覺手感親測。

## Cut 56 — ⏹ 自走 loop 停在 seam-ceiling（leaf-port runway 確定挖盡,停止條件#2+#4）(2026-06-16 凌晨)
**停手理由（constitution-sanctioned,非空手停）**：第二顆 op 試港（修正工法 retry,pure-compute 候選 LightRaysFx/SubdivisionStretch/GlitchDisplace）build agent **rigorous STEP-0 family-wide scan（全 image/fx ~35 op）後 clean STOP**：**無一顆 clean leaf-portable 剩**——每顆都卡 compound 承重 seam（gradient/asset-tex/multi-image/Layer2d/feedback/temporal-random）。FastBlur 是最後一顆（它走 custom executor `_ExecuteFastBlurPasses` 非 FloatsToBuffer 才成立）。**連兩顆 op 試港同根因失敗（DirectionalBlur routing trap 丟棄 + 本次無 clean op）=停止條件#4（不空轉燒第三次）**；**剩餘候選全卡承重 seam 投資決策（含 (D)gradient widget=柏為視覺-authoring 域「你畫色帶」）=停止條件#2（決策擋住所有候選）**。

**具名 seam ceiling（每剩餘 op 的真實阻塞,build agent 實證）**：LightRaysFx=FloatsToBuffer routing trap(DirectionalBlur²)+asset-tex(FxImage white-pixel)+2-pass refine(作者註「太多 artifact 不可用」)；SubdivisionStretch=Gradient-LUT+random；GlitchDisplace=random+buffer-input；BubbleZoom=gradient(FeatherGradient)；DistortAndShade/FakeLight/MosiacTiling/HoneyCombTiles=multi-image input；RgbTV=mip(HAVE Cut53)+noise-asset；TimeDisplace=temporal；AfterGlow*/ColorPhysarum/SimpleLiquid*/Fluid/AdvancedFeedback*=feedback temporal；ScreenCloseUp/SortPixelGlitch/Glow/Bloom=Layer2d/Execute/compound。

**⚠ 教訓（infra-without-consumer 反省）**：本 session 建 2 seam（Cut53 mip / Cut54 multi-pass），**multi-pass 有 FastBlur 消費,mip 仍無真消費者**（speculative seam 風險）。**紀律修正:只在某 op 明確只需該一塊 seam 時才建;現無 op 只需單一未建 seam（全需 compound）→ 不再 speculative 建 seam,等柏為 steer 目標 op + 接受其多-seam 投資**。

**本 session 總帳（Cut 51-56）**：✅ Cut51 UI-visual parity 5 gaps(`a86377e`,路線B)/✅ Cut52 **Crop 真正進產線**(`8a672da`,修 Cut50 selftest-綠-活體-壞缺口)/✅ Cut53 mip seam infra(`d484c4a`,待消費者)/✅ Cut54 **FastBlur 真視覺 op**+multi-pass seam(`e73c2e7`)/⚠️ Cut55 DirectionalBlur 丟棄+`.t3` routing trap 工法修正(`118a633`)/⏹ Cut56 seam-ceiling 停。= **3 真 deliverable + 1 承重 seam(消費) + 1 orphan seam(mip) + 1 系統性工法修正 + 2 follow-up audit(task_2ee58abb/task_258d9510)**。

**Resume — 待柏為 steer（投資序,皆承重 seam=破壞 conflict-free 平行織=產能模型重塑,不在自走 loop 擅動）**：
- **(E) asset-texture seam**（image decode/load→bind texture slot,目前 app 零 image-loading）→unlock RgbTV(+mip 已有→可能首個雙-seam 消費者,給 mip seam 補消費)/部分 LightRaysFx/FakeLight。**最 mechanical+可確定性驗,我的推薦若柏為要我續走**。
- **(D) gradient widget**（柏為視覺域「我接 op 你畫色帶」）→BubbleZoom/SubdivisionStretch/Steps/Bloom。**需柏為親自 authoring**。
- **multi-image input seam**→DistortAndShade/MosiacTiling/HoneyCombTiles。
- **(C) Layer2d+Execute seam**（最大但 brittle blend-order golden+視覺判斷域）→Glow/Bloom/ScreenCloseUp/真-blend compound。
- 非-image 軸:UI-visual 續(font/title padding Cut51 #17/#20)/interaction keymap/task_258d9510 audit 若揪 parity bug。
**柏為一句「走 X」即可續**（推薦 (E) asset-texture+RgbTV:給 orphan mip seam 補消費+ship 真 CRT op）。

## Cut 57 — (E) asset-texture seam phase 1：native PNG decode 子系統 ✅ (2026-06-16 早,柏為 steer「走 (E)」)
**承重達成**：柏為 早上重發「你決定+不要停」→ 我自決 **走 (E)**（Cut 56 我把投資決策收太緊待 steer=under-用授權,柏為 點明後直接動）。app 之前**零 image-loading**→建 macOS-native ImageIO 解碼。HEAD `6094835`。--bite **141** NO-BITE:[]/check-arch OK。**phase 1/2:phase 2=second-tex-bind cook seam + RgbTV**（首消費者,順帶救 orphan mip seam）。

**decoder（`platform/image_decode.{h,mm}`+`_selftest.mm`）**：ImageIO/CoreGraphics PNG→RGBA8→`MTL::Texture`(放 platform 區=native 接口正確分區,check-arch 綠;無 vendored dep)。`resolveAssetPath`(SW_ASSETS_DIR,`Lib:images/...`→`assets/images/...`)。**真 TiXL 噪聲圖 `perlin-noise-rgb.png` 搬進 repo**(shasum `45a17e4f…` byte-identical,external/tixl 未動)。**decode parity vs TiXL 三重獨立證**(PIL+手刻 IDAT decompress+decoder 全 byte-identical):linear RGBA8Unorm(TiXL `Texture.cs:39` R8G8B8A8_UNORM 無 sRGB gamma)/channel RGBA(`:104` WIC Format32bppRGBA)。**named fork**:CoreGraphics 強制 premultiplied alpha→對全不透明 asset(alpha=255)是 no-op(透明 asset 未來需 un-premult 路,header 已記)。

**proof=`--selftest-imagedecode`**:解真 asset 斷言 512×512+4 pin 含 load-bearing R≠G≠B `(100,200)=(106,191,102)`(channel swap 過不了);-bug(R↔B)RED。ground truth 獨立(非 decoder 自證)。**Opus refuter MERGE-SAFE 6/6**(三重 decode 證/shasum/MTL lifetime 對 tex_op_cache/check-arch/asset git-tracked 會 ship)。**非阻塞 note(phase 2 必守)**:RgbTV cook loop 每幀呼叫 decode 須自己包 AutoreleasePool(header 已記)。

**Resume — next**:
1. **★Batch B = (E) phase 2:second-tex-bind cook seam + RgbTV leaf**。second-tex-bind=image-filter cook 目前只綁 t0(input)→加 op 宣告需 asset-texture 綁 t1 的路(mirror registrar 模式,default-none byte-identical)。RgbTV leaf:**trace FloatsToBuffer ~24 param 連線順序路由(Cut 55 鐵律,_multiImageFxSetup compound!)**+input-mip-gen(自身 cook 內 allocate mipped scratch+blit+generateMipmaps+sample LOD 0..7=**消費 orphan mip seam**)+綁 perlin-noise asset@t1(decode 用 Cut 57,**包 AutoreleasePool**)+港 RgbTV.hlsl 1:1。golden:exact-pixel 在 **noise-independent 區**(RGB-shift/scanline 數學主導,非 noise-driven distortion)both cook+refuter。
2. (D)gradient widget(柏為域)/multi-image seam/(C)Layer2d/UI-visual 續/keymap。
3. 排修:task_602f15ec/task_2ee58abb/task_258d9510/視覺手感親測。

## Cut 58 — RgbTV CRT op + second-tex asset-bind seam（(E) phase 2 完成）✅ (2026-06-16 早)
**承重達成**：seam (E) 完工——RgbTV（首顆 asset-seam 消費者 + **救 orphan mip seam**，input-LOD glow）。HEAD `2cfd0e0`。--bite **143** NO-BITE:[]/check-arch OK。

**★NAMED IMPROVEMENT-FORK（柏為 veto 已 queue=task_c6a885db）**：refuter 反查 t1 揪出**TiXL RgbTV 的 perlin 噪聲節點是斷開的**——`t1 ← Blur(空 LoadImage)≈黑`→退化成 uniform noise（TiXL WIP/bug，perlin LoadImage→Grain dangles）。build agent forward-trace 誤判 perlin→t1；refuter backward-trace + fixer 三方確認斷開。**我決定（柏為 你決定+視覺意圖）：連 perlin = 改進 fork**（CRT glitch 真的有空間噪聲），3 處 header/registrar/params 大聲具名「improvement-over-TiXL-WIP,非 noise-path byte-parity」。TiXL-faithful 數學（RGB stripe/vignette/scanline/mip-glow）仍 byte-parity；只 noise distortion 分岔。pre-blur(1.6) 不港（在空節點上，非 perlin，graph 不 cohere）。

**second-tex-bind seam**：`imageFilterAssetTextures()` map→cached decode（calls=1 無 per-frame decode）綁 t1，兩 cook，default byte-identical（只 RgbTV 在 map）。runtime→platform decode 走 fn-ptr（main.cpp）無 zone violation。RgbTV=compute kernel；input-mip-gen 消費 Cut 53；**24-param FloatsToBuffer 路由逐字 trace（Cut 55 鐵律，field-order 無 mis-route）**；s0 sampler=MirrorClampToEdge（TiXL MIRROR_ONCE，原 ClampToEdge=refuter Point3 修）。

**Opus refuter BLOCK→fixer→resolved**：seam/routing/兩cook/lifetime/check-arch SURVIVE；**BLOCK 2 點**：①Point1 t1=perlin（誤）vs TiXL=黑→fixer 確認斷開+決定 improvement-fork+加 GlitchAmount>0 golden ②Point3 s0 sampler→fixer 修 MirrorClampToEdge。golden 兩 cook caseA(GA=0 TiXL-faithful stripe,refuter 驗咬 routing swap)+caseB(GA=1 noise path,RED-proven shade-term zero)+casesDiffer。**caseB pins=self-capture**（fork 無 TiXL ground truth 故 regression-defense 非 parity;caseA 才是 parity 防線）。

**本 session 累計 Cut51-58**：3 真 op（UI-parity/Crop-產線/FastBlur）+ **Cut57-58 seam(E)=PNG decoder + RgbTV 真 CRT op**（救 mip orphan + 開 asset seam）+ multi-pass seam + .t3-routing 工法修正 + 3 audit/decision chip。**mip seam 不再 orphan（RgbTV input-LOD 消費）**。

**Resume — next**:
1. **更多 asset-seam 消費者**（decoder+asset-bind 已開）：FakeLight（multi-image+asset?）/LightRaysFx（asset+FloatsToBuffer routing+2pass refine=之前判 trap²,需重評）——每顆 STEP-0 backward-trace 真實接線（Cut 58 教訓:forward-trace 會誤判）+routing trace（Cut 55）+exact-pixel golden(caseA parity 區)。
2. **LoadImage op**（TiXL 真 source op，PNG→Texture2D）=decoder 的乾淨消費者，但 source op（()→tex）我方 image-filter cook（tex→tex）可能不支援→需評 source-op seam。
3. (D)gradient widget(柏為域)/multi-image seam/(C)Layer2d/UI-visual 續(font/padding)/keymap。
4. 排修/柏為域:task_c6a885db(RgbTV fork veto)/task_602f15ec/task_2ee58abb/task_258d9510/視覺手感親測(FastBlur/RgbTV/Crop)。

## Cut 59 — DistortAndShade（2-input warp/shade，multi-image seam 第 2 消費者）✅ (2026-06-16 午,柏為「繼續做」)
**承重達成**：DistortAndShade（image A 被 image B 位移+shade）。**multi-image input-gather seam 早已存在**（第 1 消費者=Displace，lane D2；point_graph 已 gather 所有 Texture2D input→t0/t1）→**本顆 leaf-only 零新共享 seam**（4th-seam 框架是 stale）。HEAD `abe7691`。--bite **145** NO-BITE:[]/check-arch OK。

**事故+教訓**：首次派工「rejected」但 agent 其實已跑（檔案時戳 10:39-10:51 證），柏為 中途打斷；「繼續做」後 re-dispatch 找到既有檔 vet+rebuild。**rejected≠沒跑（背景可能已落檔），re-dispatch 前必查 tree 實況**。refuter agent 首跑 API socket 死（25 tool_use 無 verdict）→查 tree intact（無殘留 perturbation,.metal untracked 故無法 git checkout 須手復原,但 selftest 綠證乾淨）→re-dispatch refuter。

**STEP-0 backward-trace（Cut 58 教訓，避 RgbTV forward-trace 誤判）**：t0←ImageA port/t1←ImageB port（兩 graph-wired，經 `_multiImageFxSetup.t3` 內 SrvFromTexture2d index 0/1；2 個 ColorGrade child dangle off-path）。**非 RgbTV t1 陷阱**。routing 8-conn 無數學節點=cbuffer order（ShadeColor.xyzw/Displacement/Shade←Shading/Center.xy，sizeof 32）。kernel byte-1:1。sampler Mirror→`MirrorRepeat`（TiXL TextureAddressMode.Mirror，異於 RgbTV 的 MirrorOnce）。

**Opus refuter MERGE-SAFE 6/6**（backward-trace/routing/kernel/golden 獨立 re-derive pin x=16=98 非 self-capture/own RED displacement×1.5→maxDelta25/both cook driven/no-regression）。**KNOWN CONCERN（tracked 非 block）=unwired ImageB**：TiXL 綁 null SRV（Sample→黑→ImageA 不 warp passthrough），我方 fork sample ImageA（self-warp）→default-render 分岔，**但同 shipped Displace fork-class**（`point_ops_displace.cpp:67 if(!map)map=image`）→**spawn_task `task_3fc122a2`=lane-wide「unwired 2nd input→黑 fallback=TiXL-faithful」convention 修（涵 DistortAndShade+Displace）**。golden 數學本體 byte-parity；只 unwired-default-fallback 邊角分岔。

**本 session 累計 Cut51-59**：5 真 op（UI-parity/Crop-產線/FastBlur/RgbTV/DistortAndShade）+ seam(E) asset+decoder + multi-pass seam + .t3-routing 工法修正 + multi-image seam（既存,+1 消費者）+ 4 chip。

**Resume — next**:
1. **更多 multi-image op**：MosiacTiling（2-input?需 backward-trace+routing+default 查）；FakeLight=blend(Layer2d 卡)/HoneyCombTiles=gradient(D 卡) 不適。
2. 更多 asset op（FakeLight/LightRaysFx 需重評 backward-trace+routing+2pass）/LoadImage source op（需 source-op seam）。
3. (D)gradient widget(柏為域)/(C)Layer2d(Glow/Bloom 大unlock,brittle+視覺判斷)/UI-visual 續(font/padding)/keymap。
4. 排修/柏為域:task_3fc122a2(unwired-2nd-input convention)/task_c6a885db(RgbTV veto)/task_602f15ec/task_2ee58abb/task_258d9510/視覺手感親測。

## Cut 60 — Phase A 普查完成：TiXL 全 op 庫接縫地基圖（工法大轉向後第一批，docs-only）📋 (2026-06-16 午後,/sw-batch 自走)
**承重達成**：工法大轉向（`afb7b3e`,柏為 2026-06-16 定）後第一批=**Phase A 地基普查**，取代舊「一顆 op 撞一塊接縫」碎工法。掃 TiXL **931 顆 op**（Operators/Lib，跨 12 類別）→ 每顆標需要哪些 seam → 產**兩張地基表**存磁碟。零程式碼改動（docs-only，故無 --bite/HEAD code commit；地基圖本身是交付物）。lane 接上具名分支 **`sw-parity-lane`**（先前 Cut51-59 全在 detached HEAD `afb7b3e`，無 named branch 含之→易 orphan，本批 attach 修好；main 仍停 stale `a54b8c0`）。

**派工法（context 衛生）**：orchestrator 全程不下場讀 op 源碼（憲法#6）。8 個 Sonnet census agent 並行（A1 simple_world 能力快照 + 7 類別分片 image/render/point/field+mesh/particle+flow/numbers/io+string+data+utils），每個**寫一個 census 檔 + 只回短摘要**（細節落磁碟不入 orchestrator context）。1 個 Opus synthesis agent join 出兩張主表。**事故**：field+mesh agent 首跑 API socket 死（tool_use 43,0 token）→查 tree（檔未落=無殘留）→fresh re-dispatch 綠（Cut 59 教訓:死≠落檔,re-dispatch 前查 tree）。

**產出（`docs/agent/census/`）**：`_CENSUS_BRIEF.md`（共用簡報:seam 詞彙+方法+格式）/ `simple-world-state.md`（A1）/ `ops-<7 類別>.md`（per-op 分類）/ **`SEAM_GRAPH.md`（174 行,接縫依賴圖+Phase B 排序+策略）** / **`OP_BACKLOG.md`（256 行,按狀態分桶開採清單）**。

**A1 關鍵事實**：simple_world 已 port **~112 顆**；brief 列的 10 條 seam **全部 ✓ 有代碼證據**（value-graph/compound/transport/particle-system/image-filter/multi-pass/mip/asset-texture/png-decode/multi-image）；無「宣稱建好卻找不到」。+8 條 brief 未列但已有的 seam。**孤兒 2 顆**=PolarCoordinates/EdgeRepeat（cook+metal 在,但 register fn 未呼叫+無 NodeSpec→選單不可達,死活待查）。

**地基圖核心數字**：
- **READY-LEAF-NOW ~173 顆**（踩已驗證 seam、現在就能進 Phase C，不需蓋任何新 seam）。其中 **~129 顆 numbers/string TRIVIAL**（純 value-graph,R1,零視覺判斷）=最安全並行燃料。
- **三大島 READY-LEAF=0**：point(128)/field(60)/mesh(49) 全卡單一地基 seam，三者互不依賴可平行蓋。
- **Phase B 建議順序（解鎖÷風險）**：B1 `point-buffer`（~90,R1,驗證 op RadialPoints/GridPoints）→ B2 `shader-graph`（~64,R1,SDF 全島,驗證 SphereSDF/BoxSDF）→ B3 `context-var`（15,R1,與 Layer2d 解耦,驗證 SetFloatVar→GetFloatVar 對拍）→ B4 `cpu-upload-texture`（4,Metal replaceRegion 現成,驗證 GradientsToTexture）→ B5 `dx11-api-wrapper`（~25,**底層鑰匙**,驗證 ClearRenderTarget/Draw）。
- **seam 依賴鏈（不能跳）**：`dx11-api-wrapper → camera3d(~50) + Layer2d+Execute(~37)`；`network-io → osc/artnet-dmx/camera-tracking 三族共同 UDP 底層`；`mesh-pipeline(~49)`、`gradient-widget(14)`、`feedback(~11)`、`compute-readback(~7)`、`midi(10)`、`video-input(9)` 各自獨立。
- **命名統一**（synthesis §0）：sdf-field=shader-graph；context-3d=camera3d；structured-list-cpu=cpu-point-list；cpu-readback-texture/readback-cpu=compute-readback。

**策略推薦（synthesis,一句）**：**Phase C 立刻並行啟動，與 Phase B 大 seam 平行織**——~173 顆乾淨葉子踩已驗證 seam、與 seam 建設踩不同檔零互撞，等 Phase B 等於讓已鋪地基閒置。最該先蓋 3 塊=point-buffer / shader-graph（兩自足島可同跑）/ dx11-api-wrapper（3D+2D 大島唯一鑰匙）。

**Phase C 開採前必查 5 項（synthesis 揪出）**：①sw 是否已有 `Gradient` 型別（擋 color-gradient ops）②sw 是否支援 dynamic hlsl 載入（10 顆 `_ImageFxShaderSetup2`）③AdsrCalculator 是否存在 ④孤兒 PolarCoordinates/EdgeRepeat 真死活（補 NodeSpec+kTable 可救 vs 廢棄）⑤multi-image 每顆開採前必 .t3 backward-trace（Cut 55 trap,DirectionalBlur 因此丟棄,仍列 BLOCKED 非 READY）。

**Resume — next（階段=A 完成,進 B+C 並行）**:
1. **Phase C 燃料 lane（可立刻並行）**：從 OP_BACKLOG READY-LEAF-NOW 拉 numbers/string TRIVIAL ~129 顆（最安全,worktree 平行,合流零衝突)。先做開採前必查①②③（決定哪些 image READY-LEAF 真乾淨）。
2. **Phase B 第一塊**：B1 point-buffer 或 B2 shader-graph（兩自足島,全工法:Plan→Opus build→獨立 refuter→fixer→orchestrator 合流;各配驗證 op）。dx11-api-wrapper(B5)解最多 3D 但 R2+底層,排 point-buffer/shader-graph 後。
3. 排修/柏為域:task_3fc122a2(unwired-2nd-input convention)/task_c6a885db(RgbTV→faithful,新改進規則下=parity 修非品味)/task_602f15ec/task_2ee58abb/task_258d9510。

## Cut 61 — Phase C 開採首批：CheckerBoard / Rings / SinForm（3 generator 葉子）✅ (2026-06-16 午後,/sw-batch 自走)
**承重達成**：工法大轉向後**首批 Phase C 並行開採**（image-filter 自登記葉子，Cut 48 模型 post-census 重驗成功）。3 顆 TiXL image 圖案 generator 逐字港、golden 對 TiXL .hlsl 手算。HEAD `5e9b4c4`。--bite **149** FAILED:[] NO-BITE:[]/check-arch OK。

**★可複用慣例（generator-on-filter-seam）**：純 generator（`Image=null`）綁 **1×1 透明黑 dummy texture**，在既有單輸入 image-filter seam 上跑（sample→(0,0,0,0) = 忠實 TiXL null SRV passthrough）→ **不需 source-op seam**。Rings/SinForm 皆此招（refuter 確認忠實）。降低未來 generator 開採門檻。

**事故+教訓（dispatch 錯）**：派 3 background lane 時**漏設 `isolation:"worktree"`** → setup script 偵測「this IS the main repo, nothing to bootstrap」→ 3 lane 全在**主 checkout 並行幹活**（非隔離）。recoverable（葉子各自獨立檔；3 agent 對 shared 檔 point_ops.h/hash.metal.h 互相補 forward-decl 自救）；--bite 全綠證 shared 檔最終一致（漏 decl 會編不過）。**未來並行 mutating lane 必設 `isolation:"worktree"`**。strays（3 個 uncommitted 檔=06-07 imgui-metal-pivot 舊規劃 + skill 編輯 + gemini-research 資源清單，appeared this session）非本批產物→留不 commit/不刪→triage chip `task_879b5335`（含「native-runtime lane 方向是否被 06-07 pivot 取代」的方向疑問待釐清；但 06-16 memory+Cut60+柏為今天 /sw-batch = 權威現行方向=續做,不停）。

**refuter（Opus 對抗，柏為退出視覺驗證後=唯一 parity 防線）三波**：
- **CheckerBoard MERGE-SAFE 首發**：golden 獨立重算（4 pin checker）/cbuffer 14-conn **零數學節點**（非 Cut55 trap，逐 GUID 對 Vector4/2Components）/mod→fract 負值等價/UseAspectRatio Enum{No,Yes}→bool branch/texCoord Y-flip 對齊 TiXL DX11 fullscreen VS（off-symmetry Offset.y 方向也驗）。
- **Rings BLOCK×2 → fixer 修 → 複驗 MERGE-SAFE**：①**Rings.hlsl 刻意混用 `%`(fmod 截斷) 與 `#define mod()`(floor)**，原港全抹成 floor sw_mod → refuter 獨立重算 Offset=-0.8 全幀 67px 差/_Segments=(3,18),Seed=2 419px 差（default golden 巧合相同=self-capture 盲區）→ fixer **8 site 逐一對 HLSL 改正**（5 fmod / 3 sw_mod），複驗 mapping 表全對。②sampler ClampToEdge → **Repeat**（TiXL `_ImageFxShaderSetupStatic` 預設 Wrap=Wrap，原 `[fork-sampler-clamp]` 註解把事實寫反）。
- **SinForm BLOCK×1 → fixer 修 → 複驗 MERGE-SAFE**：`copiesCount` round-half-up→**floor**（HLSL `(int)Copies+0.5` 優先序 `(int)` 只綁 Copies=floor；原港 `(int)(Copies+0.5)`=round，小數≥.5 分岔，自由滑桿可達）；補 Copies=2.7 golden。**「center 讀 230 非 255」經 refuter 確認=忠實**（pixel-center 取樣+feather 數學本就 230，真 TiXL 也讀 230），非 bug。

**驗證紀律**：scenario 全庫 GPU sweep **具名跳過**（本批 provably additive=3 新葉子+point_ops.h/hash.metal.h 純加性編輯，零既有 cook/UI 路徑改動；--bite 全表綠=足夠回歸證；sweep 對 headless 葉子零邊際信號+flake-prone）。**殘留具名（非阻塞）**：Rings 最深 fmod 截斷路徑（`ringIndexFromCenter`，_Segments.y≠0 才觸發 hash chain）缺精確回歸牙——mapping 已逐點驗對、code 正確，只缺 future-regression tooth。

**Resume — next（階段 C 並行進行中）**:
1. **Batch 4 = 續開採剩餘 image READY-LEAF**（ValueRaster/Blob + 備援 FraserGrid/ZollnerPattern/NGon/RoundedRect/FractalNoise/Grain/WorleyNoise）——**這次必設 `isolation:"worktree"`**（每 lane 獨立 worktree，合流 cherry-pick 零衝突）。沿用 generator-dummy-texture 慣例 + STEP-0 portability 硬閘 + Opus refuter。
2. **value-op 自登記小基建**（仿 `imageFilterSpecSink` 建 `valueOpSpecSink`+`ValueOp` registrar，R1 有藍本）→ 解鎖 ~129 numbers/string TRIVIAL 可並行開採（目前共享 `node_registry_math.cpp` 撞檔）。承重 seam 全工法。
3. **Phase B 第一塊 seam**：point-buffer（~90 解鎖，粒子系統已馴服本質複雜=R2 有藍本）blueprint。
4. 排修同 Cut 60 + 新增 task_879b5335（strays/方向疑問）。

## Cut 62 — Phase C 第二批：NGon / ValueRaster / Blob（3 generator 葉子，isolation:worktree 並行）✅ (2026-06-16 晚,/sw-batch)
**承重達成**：再 3 顆 image generator 葉子。HEAD `45003aa`（NGon `8bf0039` / ValueRaster `35e195e`+fix`45003aa` / Blob `ec7ce19`）。--bite **151** NO-BITE:[]/check-arch OK。**isolation:worktree 這次有設（Cut 61 教訓）**→ 各自隔離 worktree、commit-in-worktree、orchestrator cherry-pick 合流（3 commit 全 leaf-only 零共享檔，conflict-free，local forward-decl 是關鍵）。generator-dummy-texture 慣例（Cut 61）穩定複用（6 顆 generator 全此招）。

**★方向危機（柏為現身，session 中段）**：柏為被前一批 surface 的「06-07 imgui-metal-pivot banner（寫『不要再蓋自建 graph/command/runtime』）」嚇到，問「native-runtime parity 是不是我們在做的事」。**我停手調查（停 3 lane + watchdog），挖 git 地面真相**：判決 **(C) 同一條路，banner 措辭誤導，信心 95%**——pivot commit `aa560f3` 是 HEAD 直系祖先 + 06-08→16 commit 全連續 parity 無分叉 + 現役 code 三層並存（imgui-node-editor 皮 + metal-cpp 引擎 + 自建 graph/command/cook，正是 pivot 要的）。banner「不要蓋自建 runtime」真意=不要 clone TiXL 焊死搬不動的 runtime，同份 pivot 檔三次說自建 Metal runtime。寫進 memory [[sw-imgui-metal-pivot-is-parity-path]] 防再嚇。**我的失誤教訓：我把 banner 升級成存在危機卻沒先 open 那份檔/查 git——reasoning from authority 非 verify，差點變 Cut47 的「只自洽」。surface 矛盾對，但讓它懸著嚇人一小時不對。** 柏為「繼續走」→ 自決續 native-runtime（憲法#2 方向 orchestrator 定）→ re-dispatch（停掉的 lane 只留半成品無 commit，重跑）。

**refuter（3 波 Opus 唯讀獨立重算）**：NGon MERGE-SAFE（golden 重算 sdNgon SDF / cbuffer 19float 零數學節點 / mod()全 floor 無 % 混用 / sampler Wrap 對 .t3）。Blob MERGE-SAFE（3 pin 重算 / rotation double-negation byte-parity / routing 零數學節點 / sampler=TiXL default 非 fork）。**ValueRaster BLOCK（port 對、golden 空洞）→ fixer 修**：refuter 獨立重算證 center alpha 34=正確 TiXL 值（build agent 手算 7 錯）、但 `>10` 閾值騎 fwidth-driven 半衰減 minor line=「有東西畫」非「對 TiXL」（柏為 Cut47）→ fixer 重設計成 **fwidth-independent 確定性 golden**：major-line pixel (0,0) d=0→Grid1D=1→alpha 123±3（fixer 獨立重算更正 refuter 估的 177=LineColor.a 折進 blend weight=0.695²，Python+GPU 兩路一致）+ off-grid plateau (96,96)==0 + injectBug RED。順手 sample(uv)→sample(uv,level(0))對 TiXL SampleLevel(...,0)。具名 fork GenerateMips=true 未建模（無 LOD 消費者非阻塞）。

**★方法論教訓（reusable）**：image golden **絕不**用 fwidth-dependent / smoothstep 半衰減 pixel 當 exact 斷言（跨硬體 LSB 漂移）→ 挑 **d=0 飽和 plateau pixel**（major line / 中心填充 / 背景遠點），對 TiXL 公式手算確定值。寫進開採工單。

**Resume — next（Phase C 並行進行中，HEAD 45003aa）**:
1. **Batch 5 = 更多 image READY-LEAF**（備援 FraserGrid/RoundedRect/FractalNoise/Grain/WorleyNoise，注意 Grain/noise 可能要 temporal-random seam→STEP-0 閘擋）isolation:worktree 並行 + generator-dummy + **golden 用 d=0 plateau pixel（Cut 62 教訓）** + Opus refuter。
2. **value-op 自登記基建**（仿 imageFilterSpecSink，解鎖 ~129 numbers/string TRIVIAL 並行）。
3. **Phase B point-buffer blueprint**（~90 解鎖）。
4. 排修同前。strays 3 檔仍留工作樹（無害歷史/研究，方向疑問已解=memory）。

## Cut 63 — Phase C 第三批：RoundedRect / FraserGrid / FractalNoise（3 generator 葉子）✅ (2026-06-16 晚,/sw-batch)
**承重達成**：再 3 顆 image generator（**Phase C 至此 9 顆 image generator 全 refuter 過**）。HEAD `9c052a6`（RoundedRect `8dd0fd1` / FraserGrid `0b85cbc` / FractalNoise `911f9b5`+fix`9c052a6`）。--bite **154** NO-BITE:[]/check-arch OK。isolation:worktree 乾淨（Cut61 教訓持續）→cherry-pick conflict-free。Cut62 golden 鐵律持續（RoundedRect d=0 飽和、FraserGrid plateau）。

**refuter（3 波 Opus 唯讀獨立重算）**：RoundedRect MERGE-SAFE（sdBox d=0 飽和 golden / routing 零數學節點 / sampler Repeat 對 .t3 / GradientBias 負分支 verbatim）。FraserGrid MERGE-SAFE（plateau golden / fg_mod 全 floor 無混 % / asin clamp safe / **GenerateMips 已正確註冊 mippedOutput=true 非 gap** / 揪出 2 處 TiXL 死碼我方正確處理）。**FractalNoise BLOCK（kernel byte-correct、golden 空洞）→ fixer 修 + rule-3 清理波**。

**★FractalNoise 雙重教訓**：
①**golden 空洞（最尖的一次）**：原 golden 用 Scale=0→pos=(0,0,0) 讓可手算，但 `hash33` 做 `frac(0×MOD3)=0` **不管 MOD3 都回同值**→golden 完全咬不到 MOD3 噪聲常數（parity 最關鍵）。refuter 證:擾動任一 MOD3 digit 在 (0,0,0) 全得 R=174。修=加 **Sub-test B 在非零 coord（Phase=5→pos=(0,0,0.5)，MOD3 活）**斷言 R=114±6 窗 [108,120]，天然咬 MOD3（正確 114 IN、typo 0.1031→0.1131→R=101 OUT 低窗底 7）。**★方法論（進工單）：procedural/noise op 的 golden 必在 parity-critical input（hash/permutation）活的 coord 斷言，不可挑退化點（Scale=0 把 hash 歸零）。**
②**rule-3 清潔**：第一版 fixer 為了 RED-證 MOD3 在 **production `.metal` shader 內加 MOD3_INJECTED 測試分支 + 假 cbuffer 欄位 InjectBugMOD3**（驗證肉塞進 GPU 業務碼，違鐵律3）→ orchestrator 攔下，派**清理波拔 seam**：production shader 還原 verbatim（git diff 無 .metal），改用斷言窗本身咬 MOD3、injectBug 回 Iterations=2。**★教訓（進工單）：禁止把測試 seam（bug-injection 分支/假欄位）塞進 production shader；golden 靠斷言窗 inherent 咬常數，RED-can-fail 用真 param injectBug。**

**Resume — next（Phase C 並行進行中，HEAD 9c052a6，9 image generator 已 ship）**:
1. **Batch 6 = 更多 image READY-LEAF**（Grain/WorleyNoise 注意 temporal-random/worley seam→STEP-0 擋；或其他 ops-image.md READY-LEAF）isolation:worktree+generator-dummy+**golden d=0 plateau 或 noise-live-coord（Cut62/63 教訓）**+refuter。
2. **value-op 自登記基建**（仿 imageFilterSpecSink→解鎖 ~129 numbers/string TRIVIAL 並行；承重 seam 全工法）——**考慮提前做，槓桿遠大於逐批 3 image 葉子**。
3. **Phase B point-buffer blueprint**（~90 解鎖）。
4. 排修同前。image generator 已證可大量並行開採，乾淨葉子池見底前可持續；之後轉 value-op infra / Phase B 大 seam。

## Cut 64 — value-op 自登記 seam（valueOpSpecSink）+ Sin/PickFloat 驗證 ✅ (2026-06-16 晚,/sw-batch,承重 seam 全工法)
**承重達成**：建 value-op 自登記 seam（Phase-B enabler）→ **解鎖 ~129 numbers/string TRIVIAL 平行開採**（原卡共享 `node_registry_math.cpp`）。HEAD `f80cfaa`。--bite **156** NO-BITE:[]/check-arch OK。全工法：Plan 藍圖→Opus build(worktree)→獨立 Opus refuter→orchestrator cherry-pick 合流。

**seam**：鏡像 edaff22（image-filter 自登記），但更簡單——value op 無 GPU cook，stateless eval fn **就在 NodeSpec.evaluate**→sink 只收 NodeSpec+selftest pair。**並存/增量**（非 big-bang）：ADD `valueOpSpecSink`/`valueOpSelfTests` 旁於既有中央 registry，新 leaf 自登記，**既有 ~70 顆 value op 一行未動=零回歸**。新增 4 檔（value_op_registry.{h,cpp}+value_op_sin.cpp+value_op_pickfloat.cpp）+ 改 3 處各 1-2 行（node_registry findSpec/specTypes、selftests list/dispatch、CMake glob `value_op*.cpp`）。

**驗證 op（refuter 獨立重算 parity）**：Sin（`Sin.cs:16` `Math.Sin(Input/Period+Phase)*Amplitude+Offset`，3/0/1 pin 對；**fork-sin-period-zero**=Period==0→回 Offset 避 NaN=press-pass 類修無歧義 NaN，Period≠0 byte-identical，同已 ship Div B==0→0 慣例）。PickFloat（`PickFloat.cs` index `Mod`(floor-mod) select；fork index-int-trunc/empty→0；**golden 走 resident 路徑 buildEvalGraph+evalResidentFloat=multiInput value op 真產線路徑**，flat evalFloat 不展 multiInput，refuter 證對）。

**refuter MERGE-SAFE**（承重命脈兩條全站住）：①零回歸=git diff 對 node_registry_math.cpp/math_ops_selftest.cpp 全空、consumer 迴圈純 append ②**init-order 比註解更強**=`doc::g_lib` pre-main 圖（defaultParticleGraph）零 value 型別→沒人在 sink 填好前查 Sin/PickFloat（不是「剛好填好」是「根本沒人查」）。牙真咬、soundtrack 解耦、check-arch 乾淨。

**★INCIDENT（雙觸發污染主樹）**：**20:40 我排的 ScheduleWakeup 與柏為手動 /sw-batch 同時觸發**→疑似第二個 orchestrator run 並行在**主樹**蓋了第二份 partial value-op seam（選 MultiplyInt，未驗證、未 commit，20:46-47）。cherry-pick 我的 verified cedfea3 時撞 conflict 才發現。處理=查無 live process（24min stale）→stray 備份 `/tmp/sw-parallel-strays/`（MultiplyInt 可救）→revert 主樹 M 編輯→cherry-pick verified cedfea3。**★教訓：柏為手動驅動時不要有未取消的 auto-wakeup（雙 driver→並行 run 污染共享樹）。柏為在場手動 fire = 我不 auto-schedule；柏為不在 = auto-schedule 自走。二選一單一 driver。**

**Resume — next（HEAD f80cfaa；value-op seam 已開）**:
1. **★Batch 7 = Phase C 平行開採 numbers/string TRIVIAL**（~129 顆現在不撞檔了，value_op_*.cpp 各自 leaf 自登記；isolation:worktree 大量並行；golden 對 TiXL 公式手算、d=0/確定值 Cut62-63 鐵律）。最大產能解鎖。
2. 更多 image READY-LEAF 尾 / Phase B point-buffer(~90)/shader-graph(~64) blueprint。
3. 排修同前。stray MultiplyInt 在 /tmp 待評估是否重港（用乾淨 seam）。
