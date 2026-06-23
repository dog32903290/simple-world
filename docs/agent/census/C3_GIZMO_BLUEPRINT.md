# C3 GIZMO BLUEPRINT — the gizmo draw family (camera3d C3 tranche)

> Read-only Plan scout, 2026-06-23. HEAD `a34b2e1` (C2 OrthographicCamera landed). file:line cited at authoring time — re-confirm before editing.
> Mirror-structure: `CAMERA3D_BLUEPRINT.md` / `S2_RENDERGRAPH_BLUEPRINT.md`. This blueprint resolves CAMERA3D_BLUEPRINT.md §2 "C3 = gizmo 15".

## 0. Headline finding (changes the tranche premise)

**The 15 gizmos are NOT a clean leaf fan-out, but they are also NOT a black-box that needs new GPU infra. They are compound symbol ops — hand-ported C++ cooks that COMPOSE already-shipped SW primitives.** Three facts decide everything:

1. **In TiXL, every gizmo is a `.t3` symbol graph, not C# draw code.** Each `.cs` is an I/O shell (`[Guid]` + `Input/Output` slots, 15–43 lines). The real geometry/draw lives in the sibling `.t3` (confirmed: all 10 top-level gizmos have a `.t3`). Example: `DrawLineGrid.t3` child SymbolIds resolve to `DrawLines.cs + TransformPoints.cs + CommonPointSets.cs + Vector3.cs` — a sub-graph wiring 4 primitive ops. Only **ConeGizmo** (129 lines) and **VisibleGizmos** (72 lines) carry real C# Update logic.

2. **Every primitive a gizmo decomposes into already exists in SW.** `point_ops_drawlines.cpp` (DrawKind::Lines, `draw_lines.metal`), `point_ops_drawclosedlines.cpp`, `point_ops_drawpoints2.cpp`, `point_ops_drawbillboards.cpp`, `point_ops_drawmeshunlit.cpp`, `point_ops_commonpointsets.cpp`, `point_ops_transformpoints.cpp`, `value_op_vector3gizmo.cpp` are all shipped and registered. So the gizmo draw infra (line/point screen-space quad rasterizer + the executor terminal) is **the same pipeline C0/C1 already proved**. No new shader, no new DrawKind, no immediate-mode overlay pass is required for the line/point gizmos.

3. **SW has NO `.t3` symbol-graph inliner.** `grep compound/symbolGraph/inlineSubgraph` finds no expander. So each gizmo must be **hand-ported as a C++ cook op** (a `cookX` that builds a `RenderCommand` by composing the primitives' logic), exactly like the existing draw leaves — NOT auto-expanded from `.t3`. The `.t3` is the spec to transcribe, the same way `DrawLines.t3` defaults were transcribed into `point_ops_drawlines.cpp`.

**Therefore C3 = a PORT-AND-COMPOSE fan-out, gated by one small shared-geometry decision (§2), not a build-new-pipeline plan.** The shipped C0/C1/C2 camera stamp means gizmos that want the camera get it for free via the per-item stamp (they emit `RenderDrawItem`s into the same Command rail).

## 1. The 15 gizmo op inventory

TiXL `Operators/Lib/render/gizmo/` top-level (10 ops with `.cs`+`.t3`):

| # | Op | What it draws | Inputs (TiXL slots) | TiXL file:line | Decomposes to (SW primitive) |
|---|---|---|---|---|---|
| 1 | **DrawLineGrid** | wireframe grid plane (XY/XZ/YZ), optional axis | UniformScale, Color, LineWidth, BlendMod, Segments(Int2), Orientation, ShowAxis | `DrawLineGrid.cs:1-30` / `.t3` | CommonPointSets→TransformPoints→**DrawLines** |
| 2 | **GridPlane** | a single oriented grid plane | Color, Size, Scale, Rotation(Vec3) | `GridPlane.cs:1-19` / `.t3` | grid points→transform→**DrawLines** |
| 3 | **Locator** | small 3-axis cross marker + label + distance-to-cam readout | Position, Size, Thickness, Color, Label, Visibility(GizmoVisibility); outputs Pos, DistanceToCamera | `Locator.cs:1-44` (`ITransformable`) / `.t3` | axis line points→**DrawLines** (+ label = bitmapfont, **fork**) |
| 4 | **ConeGizmo** | wireframe audio cone: base circle + apex rays, `-Z` forward, BASS half-angle geometry | Angle, Length, Segments, RayCount; **outputs `StructuredList<Point>`** (NOT Command) | `ConeGizmo.cs:23-129` (real C# Update) | **generator only** → feeds DrawLines downstream |
| 5 | **DrawBoxGizmo** | wireframe box (12 edges) | Color, Stretch(Vec3), Scale, Position(Vec3) | `DrawBoxGizmo.cs:1-21` / `.t3` | box edge points→transform→**DrawClosedLines/DrawLines** |
| 6 | **DrawSphereGizmo** | wireframe sphere (lat/long rings), inner/outer radius | Radius, InnerRadius, Color | `DrawSphereGizmo.cs:1-18` / `.t3` | ring points→**DrawLines/DrawClosedLines** |
| 7 | **DrawCamGizmos** | frustum/camera-icon gizmos for cameras in the comp | Visibility(GizmoVisibility), Size | `DrawCamGizmos.cs:1-15` / `.t3` | **comp-walk** (needs camera enumeration — fork/defer) + DrawLines |
| 8 | **DrawSpatialAudioGizmos** | gizmos for all SpatialAudioPlayer instances (source/listener/cone) | Visibility(GizmoVisibility) | `DrawSpatialAudioGizmos.cs:1-16` / `.t3` | **comp-walk** over audio players + ConeGizmo + DrawLines (heavy fork) |
| 9 | **VisibleGizmos** | visibility GATE: passes child Commands through iff visible (On/IfSelected/Inherit + selection) | Commands(MultiInput Command), Visibility(GizmoVisibility) | `VisibleGizmos.cs:16-72` (real C# Update) | **Command→Command gate**, NOT a draw — Execute-without-transform fork (SW already noted this fork at `node_registry_draw.cpp:264`) |
| 10 | **PlotValueCurve** | scrolling value-vs-time plot overlay (ring buffer of samples) | Value, Color, RangeMin/Max, Reset, BufferLength, Label, DisplayLabel | `PlotValueCurve.cs:1-32` / `.t3` | ring-buffer state→line points→**DrawLines** (+ label fork) |

**That is 10 top-level.** The census "15" is reached by counting the `gizmo/_/` archive + adjacent gizmos (NEED-TIXL-CONFIRM on the exact 15 the census meant). Candidates for the remaining 5:
- `gizmo/_/_CameraGizmo.cs`, `gizmo/_/DrawSphere.cs`, `gizmo/_/_DrawPointInfo.cs`, `gizmo/_/_VisualizeTBN.cs`, `gizmo/_/_OutputWindowGrid.cs` (the `_`-prefixed = deprecated/internal in TiXL; likely **not** production ports — flag as DEFER unless census explicitly includes them).
- `numbers/vec3/Vector3Gizmo.cs` (already partially in SW as `value_op_vector3gizmo.cpp`).
- `io/audio/SpatialAudioPlayerGizmo.cs`.

**ASSUMPTION (needs census cross-check):** the production-relevant set is the **10 top-level** + possibly Vector3Gizmo (done) + the 4 `_`-archived. **Recommend the orchestrator confirm the census's exact 15 GUIDs before tranche-1 dispatch** — porting a `_`-prefixed deprecated op is wasted lane.

## 2. Shared-base 決定 (the one gating seam) — VERDICT: a SMALL shared geometry helper, then fan-out

**Question:** does C3 need a shared gizmo-draw seam (line/handle geometry helper, immediate overlay pass) before fan-out, or does every gizmo just slot into the existing draw pipeline = pure leaf fan-out?

**Verdict: 90% leaf fan-out into the existing DrawKind::Lines pipeline, gated by ONE shared CPU geometry helper that should land FIRST as a thin seam.**

- **No new GPU seam.** The rasterizer (`draw_lines.metal` screen-space quad), the executor terminal (`point_ops_rendertarget.cpp`), the camera stamp (C0/C1/C2) — all shipped and proven. Gizmos emit `RenderDrawItem{kind=Lines}` exactly like DrawLines. No immediate-mode overlay, no new DrawKind, no new shader.
- **One shared CPU helper IS worth seaming first:** every wireframe gizmo (grid/box/sphere/cone/locator/camera-frustum) is "generate a `StructuredList<Point>` of line-segment endpoints with `Point.Separator()` breaks, then hand to the Lines path." TiXL's ConeGizmo (`ConeGizmo.cs:48-107`) is the canonical pattern: build a point list with separators, output it, downstream DrawLines rasterizes. SW should land a small **`gizmo_geometry.{h,cpp}`** leaf (pure CPU, zero camera, zero register touch) with `emitGridLines / emitBoxEdges / emitSphereRings / emitConeLines / emitAxisCross`. This is the analogue of `pointlist_ops_linepointscpu.cpp` — a CPU point-list generator. It is **small, has no shared-register footprint, and unblocks 6 of the 10 in parallel.** Landing it first turns each gizmo into a ~30-line cook that calls one helper + sets color/width.

**So the structure is: seam-first (tiny), then fan-out.** Not "big base then fan", not "pure parallel from frame 0." The seam is one CPU file, ~1 day, owner-locked, and it is NOT on the register collision surface.

## 3. Collision analysis (shared register files)

Three shared files are the collision surface. Same lesson as prior render-leaf batches: **ops that append to a shared register table cannot weave fully in parallel — the appends conflict-merge.**

| Shared file | Role | Gizmo touch | Collision risk |
|---|---|---|---|
| `point_ops_register_draw.cpp:27-52` | `registerDrawPointOps()` — one `registerCmdOp("Name", cook)` line per draw op | **Each gizmo adds 1 line** | **HIGH if parallel** — every lane appends to the same function body → merge conflict on the same 25 lines |
| `node_registry_draw.cpp:9-` | `drawSpecs()` NodeSpec table (UI param widgets) | **Each gizmo adds 1 NodeSpec block** | **HIGH if parallel** — same vector literal, adjacent appends conflict |
| `render_command.h:40-` | `DrawKind` enum + `RenderDrawItem` fields | **Likely ZERO** (gizmos reuse `DrawKind::Lines`) | **LOW** — only touched if a gizmo needs a genuinely new kind (none identified). If one does, that op serializes. |
| `point_ops.h` | declarations | the register file uses local `void registerXOp();` fwd-decls (see `:18-25`) to keep declarations OUT of the god-header | **LOW** if the same convention is followed |

**Parallel formation recommendation (the anti-collision pattern):**

The codebase already solved this — the register split (`point_ops_register_draw.cpp`) and `void registerXOp();` local fwd-decl convention exist precisely so "adding a draw op edits ONLY this file" (file header line 4). But two lanes both editing that one file still conflict. Two options:

- **Option A (recommended, matches existing discipline): serial register merge-point, parallel everything-else.** Each gizmo lives in its own `point_ops_gizmo_<name>.cpp` (cook + selftest, fully parallel, zero shared touch). The TWO append-lines (register + NodeSpec) are batched by ONE owner at the lane merge — N lanes hand back N cook files + a 2-line diff each; the owner concatenates the 2-line diffs serially. Cheap, deterministic, no weave conflict on the body.
- **Option B (heavier, only if N>4 lanes): self-registration.** Convert `registerDrawPointOps` to a static-initializer registry (each `point_ops_gizmo_X.cpp` self-registers via a file-local static), removing the central append entirely. This is the rule-7 "data table not copy-paste" endgame but is a refactor of the register mechanism — out of scope for C3 unless the orchestrator wants it as a separate seam first. **Recommend Option A.**

**Lane count: 2–3 lanes.** Each lane owns a self-contained cook file; the `gizmo_geometry` helper owner-locks first (1 lane, ~1 day) then releases. NodeSpec+register appends collected at a serial merge gate.

## 4. Tranche split (3 batches, value÷risk descending)

**Tranche 0 (seam-first, owner-lock, ~1 day, blocks nothing downstream-of-camera):**
- Land `gizmo_geometry.{h,cpp}` (CPU line-segment generators) + port **ConeGizmo** as the first consumer (it's a pure generator → `StructuredList<Point>`, no camera, no register-draw touch — it registers as a generator, lowest risk, proves the helper). Validation: closed-form — assert the cone base circle has `segments` segments at radius `tan(angle/2)·length`, apex rays from origin, `Separator()` between segments (transcribe `ConeGizmo.cs:48-107` exactly).

**Tranche 1 (cleanest leaf fan-out, unlocks the most — wireframe primitives, ~2–3 lanes parallel):**
- **DrawLineGrid, GridPlane, DrawBoxGizmo, DrawSphereGizmo, Locator(geometry only, drop label).** All five = "emit wireframe points via `gizmo_geometry` → DrawLines/DrawClosedLines." Each its own cook file. These are the first batch because they're the cleanest (pure CPU geometry + existing Lines pipeline) and most-used (grids/boxes/locators are the staple editor gizmos).
- Validation per op: **closed-form draw-pixel golden** (the C3 proving golden from CAMERA3D_BLUEPRINT.md:49) — `DrawLineGrid` line at world x=k projects to NDC `k/(d·tan(fov/2))`; assert two ADJACENT line centers lit (never edges, never just endpoints). Box: assert 12 edges, 8 corners. Sphere: assert ring count. injectBug = LineWidth=0 → nothing between points → RED. Plus **eye-hand screenshot diff vs TiXL** for visual confirmation of the whole gizmo (use `simple-world-eye-hand` clean readback).

**Tranche 2 (the forks / stateful / comp-walking — serialize, harder):**
- **VisibleGizmos** (Command→Command gate, not a draw — port as Execute-without-transform + visibility predicate; SW already flagged the fork at `node_registry_draw.cpp:264`). **PlotValueCurve** (needs a cross-frame ring buffer + label). **DrawCamGizmos / DrawSpatialAudioGizmos** (need composition-walking to enumerate cameras / audio players — SW has no comp-walk-for-gizmos mechanism → these are genuine FORKS, defer or stub. DrawSpatialAudioGizmos additionally depends on the spatial-audio subsystem). **Locator label** (needs bitmapfont, which CAMERA3D_BLUEPRINT.md:87 lists as a downstream-gated separate blueprint).
- Validation: VisibleGizmos = predicate golden (selected vs not → command passes/blocked, no pixels needed). PlotValueCurve = ring-buffer state golden (feed N values, assert plot line tracks). Cam/SpatialAudio gizmos = **flag NEED-TIXL-CONFIRM + likely DEFER** (comp-walk seam not built).

## 5. Parallel safety vs particle-field PF-a

**VERDICT: fully parallel-safe, disjoint file sets, zero collision.**

- **PF-a touches:** `point_ops.cpp` (cookParticleSim FORCE_KIND_VECTORFIELD `~:210`, simState PSO `:115`), `tex_op_cache.{h,cpp}` (new source-compute PSO cache — its device-global state), `field_graph.h` (assembleFieldMSL `:151`), `particle_params.h` (VecFieldForceParams). (Source: `PARTICLE_FIELD_BLUEPRINT.md` §critical files.)
- **C3 gizmo touches:** new `point_ops_gizmo_*.cpp` files, new `gizmo_geometry.{h,cpp}`, and append-only edits to `point_ops_register_draw.cpp` + `node_registry_draw.cpp` (+ possibly `render_command.h` if a new kind, which none need).
- **Intersection = ∅.** PF-a lives in the particle/field compute lane; gizmos live in the draw/render lane. They do not share a file.
- **One watch-point (not a collision):** PF-a's blueprint flags that **S4** (point_graph split, 862/710 lines over cap) may also touch `point_ops.cpp` → PF-a must coordinate with S4 *owner*. C3 does NOT touch `point_ops.cpp` (gizmo cooks are their own files), so **C3 is clear of that S4 entanglement too.** C3's only shared-register concern is internal to C3 (§3), independent of PF-a/S4.

## 6. Risk / essential-vs-accidental complexity

| Risk | Essential? | Note |
|---|---|---|
| **Constant-size-on-screen** (gizmos stay a fixed pixel size regardless of camera distance) | **ESSENTIAL** for editor gizmos in TiXL | Check each `.t3`: if it divides geometry by DistanceToCamera (Locator outputs `DistanceToCamera` `Locator.cs:13` — a tell), SW must replicate via the camera stamp's eye distance. If the v1 gizmos are world-space-sized (no screen-constant), this is a **named fork** — simpler, decide per-op. NEED-TIXL-CONFIRM per `.t3`. |
| **Screen-space projection** | Already solved | gizmos ride the C0/C1/C2 camera stamp; no new projection. |
| **Handle hit-test / interactivity** (`Locator : ITransformable`, `TransformGizmoHandling.cs`) | **ACCIDENTAL for C3 draw scope** | The interactive drag-handle gizmo (`Editor/Gui/Interaction/TransformGizmos/`) is an EDITOR-UI concern, not a render op. C3 = the *draw* family only. Drop TransformCallback (SW already drops it at `node_registry_draw.cpp:361`). Hit-test is a separate UI seam — explicitly OUT of C3 scope. |
| **Comp-walking** (DrawCamGizmos/DrawSpatialAudioGizmos enumerate other instances) | **ESSENTIAL to those 2 ops, but a missing seam** | SW has no "walk the composition for all cameras/audio-players" mechanism. These 2 are genuine forks → Tranche 2 DEFER. |
| **Label rendering** (Locator/PlotValueCurve `Label`) | depends on bitmapfont | bitmapfont is a downstream-gated separate blueprint (CAMERA3D_BLUEPRINT.md:87). Drop labels in C3 v1 (named fork), geometry only. |
| **Cross-frame state** (PlotValueCurve ring buffer) | **ESSENTIAL to that op** | needs persistent buffer like SimState — Tranche 2, more care. |

## 7. golden / eye-hand validation strategy

Gizmos are visual overlays, but **the line/point geometry is closed-form**, so prefer pixel-deterministic goldens over screenshot-diff:

- **Primary = closed-form draw-pixel golden (per gizmo, headless `--selftest-gizmo-X`).** The geometry is analytic: a grid line at world x=k → NDC `k/(d·tan(fov/2))` (CAMERA3D_BLUEPRINT.md:49). Cook the gizmo → RenderTarget → readback → **assert specific pixel columns/rows lit at the computed NDC positions, between line centers (never edges, never just endpoints)**. injectBug (LineWidth=0, or wrong segment count) → assertion fails → RED. This mirrors `runDrawLinesSelfTest` (`point_ops_drawlines.cpp`) exactly — same harness, new geometry. **Route through the RESIDENT terminal** (CAMERA3D_BLUEPRINT.md §3 discipline) so resident dispatch is proven, not bypassed.
- **Generator ops (ConeGizmo)** = assert the output `StructuredList<Point>` element-for-element (positions, separators) vs the transcribed `.cs` formula — no pixels needed.
- **Gate ops (VisibleGizmos)** = predicate golden — feed visible/invisible + selection state, assert child command passes/blocked.
- **Secondary = eye-hand screenshot vs TiXL** (`simple-world-eye-hand` clean readback) for the *whole-gizmo* visual confirmation (does the grid look like TiXL's grid). Use this as the human-facing parity check, not the regression gate (closed-form golden is the gate — screenshots drift on AA/color).
- **Constant-size-on-screen** (if essential per §6): golden = render the same gizmo at eye=5 vs eye=10, assert the screen-pixel extent is IDENTICAL (the discriminator that the distance-divide works). If v1 is world-space-sized fork, assert it SHRINKS with distance instead (and name the fork).

---

### Critical Files for Implementation
- `app/src/runtime/point_ops_drawlines.cpp` — the geometry→DrawKind::Lines pattern + `runDrawLinesSelfTest` golden template every wireframe gizmo clones.
- `app/src/runtime/point_ops_register_draw.cpp` — `registerDrawPointOps()` (the HIGH-collision append point; gizmo register lines land here, serialize per §3).
- `app/src/runtime/node_registry_draw.cpp` — `drawSpecs()` NodeSpec table (the second HIGH-collision append point; UI param widgets per gizmo).
- `app/src/runtime/render_command.h` — `DrawKind` + `RenderDrawItem` (camera stamp already present; verify gizmos need NO new kind).
- `external/tixl/Operators/Lib/render/gizmo/ConeGizmo.cs` (+ each gizmo's `.t3`) — the ground-truth point-list-with-separators pattern to transcribe into `gizmo_geometry.{h,cpp}`.

---

**Two flags for the orchestrator:**
1. The census says "15" but only **10 top-level gizmo ops** exist in `Lib/render/gizmo/`; the other 5 are likely the `_/`-archived (deprecated) set. **Confirm the exact 15 GUIDs the census meant before tranche-1 dispatch** — don't burn a lane on a `_`-prefixed deprecated op.
2. DrawCamGizmos + DrawSpatialAudioGizmos need a composition-walk seam SW doesn't have → recommend **DEFER to a separate mini-blueprint**, not C3.

---

## ★FLAG #1 RESOLVED（2026-06-23 GUID scout）— census 15 = 10 top-level + 5 `_/`-deprecated

掃 `Lib/render/gizmo/`（含 `_/`）+ OP_BACKLOG.md:154 / SEAM_GRAPH.md:104 確認 census「15」=**10 top-level + 5 `_/`-archived**（剛好 15）：
- **Tranche 1 確定可採（5 乾淨 wireframe 葉，現在就採）**：DrawLineGrid `296dddbd` / GridPlane `935e6597` / DrawBoxGizmo `9123651a` / DrawSphereGizmo `1998f949` / Locator `348652c3`（geom only，drop label）。全純 CPU 幾何騎 gizmo_geometry+DrawLines，零 comp-walk/cross-frame。
- **DONE**：ConeGizmo `f7e3c9a4`（T0）/ Vector3Gizmo `e9dc80e1`（Phase C value op）。
- **Tranche 2（fork/state，序列）**：VisibleGizmos `d61d7192`（Command gate）/ PlotValueCurve `92f3193e`（ring-buffer state）。
- **DEFER（需 comp-walk seam，另開 mini-blueprint）**：DrawCamGizmos `cdf5dd6a` / DrawSpatialAudioGizmos `b53e6425`。
- **SKIP（`_`-deprecated，別港）**：_DrawSphere `3b97856c` / _CameraGizmo `b601be85` / _DrawPointInfo `ff5b93e3` / _OutputWindowGrid `e5588101` / _VisualizeTBN `dd353ac7`。
- **跨目錄非 15 內**：SpatialAudioPlayerGizmo `b26e6624`（audio subsystem，DEFER）。

**新 fork flag（Tranche 1 builder 必查）**：`fork-gizmo-screen-constant`——TiXL 部分 gizmo（Locator 輸出 DistanceToCamera）可能除以相機距離保持 fixed-pixel-size。**逐顆查 `.t3` 有無 distance-divide**；CAMERA3D_BLUEPRINT §6 列為 optional/out-of-C3-scope → v1 預設 world-space sizing fork（具名），但 builder 先查再決定。
