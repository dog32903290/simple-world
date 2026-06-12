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
