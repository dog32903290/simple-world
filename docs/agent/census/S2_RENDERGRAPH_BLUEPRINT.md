# S2 RENDER-GRAPH / Layer2d / Execute — BUILD BLUEPRINT

> Branch sw-parity-lane, base HEAD bfaee74. S1 (`44234aa`) landed the context-carried push/pop pattern. S2 is the highest-leverage spine seam (unlocks the ~155-node render island).
> Authored by read-only Plan pass (a89b00775b76ccd3b, 2026-06-23). file:line cited against base bfaee74.

## 0. One-line verdict

The S2 seam is **NOT** a new draw pipeline — `DrawKind::Layer2d` and `DrawScreenQuad` already composite a *single* layer (`point_ops_layer2d.cpp`, `render_command.h:Layer2d=5`). **The actual gap is the MultiInput Command collector** — TiXL's `Command.CollectedInputs` loop that lets **Execute / Group / VisibleGizmos** concatenate N Command chains *in order* into one `RenderCommand`. Today `cookCommand` gathers **exactly one** Command input (`!haveInCmd`, `point_graph.cpp:449`). That single-input ceiling blocks the ~155 render nodes: nearly every render op outputs `Slot<Command>` and they only compose through a MultiInput `Group`/`Execute`. The byte-faithful precedent already exists in-tree (the `ListToBuffer` MultiInput gather, `point_graph.cpp:194-204`).

## 1. TiXL ground truth (cited)

**Command** (`external/tixl/Core/DataTypes/Command.cs:6-10`): a trivial pair of optional callbacks — `PrepareAction` + `RestoreAction`. A Command is a deferred side-effecting draw; "value" = running it.

**Execute** (`external/tixl/Operators/Lib/flow/Execute.cs:14-39`) — the sequencer:
```
var commands = Command.CollectedInputs;          // MultiInput, wire-declaration order
if (IsEnabled) {
  for i: commands[i].Value?.PrepareAction(ctx)   // 1) prepare ALL, in order
  for i: commands[i].GetValue(ctx)               // 2) execute ALL, in order  ← the draws
  for i: commands[i].Value?.RestoreAction(ctx)   // 3) restore ALL, in order
}
Command.DirtyFlag.Clear();
```
**Ordering semantics:** three sequential passes (prepare-all → execute-all → restore-all), each iterating `CollectedInputs` in wire order. The execute pass IS the draw-command ordering — items append to the render target in input order.

**Group** (`render/transform/Group.cs`) and **VisibleGizmos** (`render/gizmo/VisibleGizmos.cs`) both use `MultiInputSlot<Command>` — same collect-and-run; Group additionally wraps with an SRT `ObjectToWorld` push/pop (the S1/camera pattern). **VisibleGizmos = Group without transform** = the minimal Execute twin.

**Layer2d** (`render/basic/Layer2d.cs:8-66`): `TransformCallbackSlot<Command>` out, `Texture2D` in. Composites one textured quad. SRT math in **`_ProcessLayer2d.cs:18-118`**: `ObjectToWorld = CreateTransformationMatrix(scale=S·Stretch, rotZ, translate=PosXY,PosZ)`, transposed (HLSL row-major, `:110-111`), with ScaleMode aspect coupling (`:55-101`, viewAspect = `CameraToClipSpace.M22/M11`). **Already ported** in `point_ops_layer2d.cpp` (raw-SRT stamp; executor composes). The "render graph" = a tree of Command ops; ordering = depth-first wire order an Execute/Group imposes.

## 2. simple_world current state (post-S1)

**Present cook-core machinery:**
- `cookCommand` (`point_graph.cpp:410-472`) — recursive Command cook; handles Points/Mesh/Texture2D/**single-Command** inputs. S1's RAII guard at `:453-458` (save `requestedResolution` → set → cook subtree → restore).
- `execIntoTarget` (`point_graph.cpp:477-486`) — runs a `RenderCommand` chain via the named texture executor (`cookRenderTarget`, `point_ops_rendertarget.cpp:87-163`, already loops `chain.items` in order).
- Resident mirror (`point_graph_resident.cpp:435-505`) — same shape, same S1 guard (`:485-492`).
- `RenderCommand{vector<RenderDrawItem>}` (`render_command.h`) — ordered item list; executor draws sequentially = ordering faithful **within** one chain.
- `PortSpec.multiInput` flag exists (`graph.h:38`); MultiInput gather precedent in `ListToBuffer` (`point_graph.cpp:194-204`: iterate `g.connections` matching `toPin`, wire order, don't `break` if multiInput).

**What's missing for Execute/Group:** a MultiInput Command branch in `cookCommand` that, for a `Command`-typed **multiInput** port, iterates **all** wired sources (wire-declaration order), cooks each via `cookCommand`, and **concatenates** their `RenderCommand.items` into one chain. (`PrepareAction`/`RestoreAction` collapse into the existing per-subtree push/pop since sw is retained-mode — no immediate-mode ctx to prepare/restore.) Plus the Group SRT push (reuse Layer2d/camera ObjectToWorld composition).

**Cook-core files that must change (owner-locked sequential):**
- `point_graph.cpp` — flat `cookCommand` MultiInput Command branch.
- `point_graph_resident.cpp` — resident mirror (production runs resident; S1's NIT was a missing resident tooth — do NOT skip).
- `point_graph.h` — `CmdCookCtx` gains `inputCommands` (vector/array + count), mirroring `inputTexture(s)`.
- `render_command.h` — possibly an append helper (or just concatenate items; no struct change if Group reuses existing transform fields).
- New leaf files: `point_ops_execute.cpp`, `point_ops_group.cpp` (registrar, like `point_ops_camera.cpp`).

## 3. The ~155 unlocked nodes (grouped, from ops-render.md / SEAM_GRAPH)

`render/` = **155 ops**; all output `Command`. S2 (MultiInput Command compositor) is the keystone.

| Group | Count | Members (representative) | Note |
|---|---|---|---|
| **flow/Execute family** | ~16 | Execute, Group-like sequencers (`SEAM_GRAPH:110`) | direct S2 consumers |
| **transform/** | 8 | Transform, Group, RotateAroundAxis, Shear, SpreadIntoGrid, SpreadLayout | Group IS S2; rest = ObjectToWorld push (S1 pattern) |
| **camera/** | 11 | Camera, OrbitCamera, Orthographic, BlendCameras, ShiftCamera | WorldToCamera/Clip push around subtree |
| **gizmo/** | 15 | DrawLineGrid, GridPlane, Locator, VisibleGizmos (MultiInput!) | VisibleGizmos = Execute twin |
| **basic/** | 8 | Layer2d ✅, DrawScreenQuad ✅, Text, ShadowPlane | core compositors (Layer2d single-layer already live) |
| **shading/** | 22 | SetMaterial, UseMaterial, SetPointLight, SetFog | context-injection Commands (S3-adjacent) |
| **postfx/** | 7 | GodRays, SSAO, DoF, MotionBlur, TemporalAccumulation | need depth/feedback seams downstream |
| **_dx11/api+buffer+fxsetup** | 42 | Draw, ClearRenderTarget ✅, OutputMergerStage… | mostly subsumed by retained-mode executor (NOT ported standalone) |
| **_/ + sprite + utils + scene + analyze** | ~26 | ApplyTransformMatrix, GetScreenPos, DrawAsSplitView | CPU/matrix leaves + draw dispatchers |

**S2 keystone directly unblocks transform(8)+camera(11)+gizmo(15)+basic(8)+flow(16) ≈ 58 ops** otherwise un-composable; the rest cascade as per-op math/context seams land (camera3d, shading=S3-context-var, depth/feedback). "~155" = the whole render island that becomes *reachable* once N-Command composition exists.

## 4. Minimal faithful change — staged S2a/S2b/S2c

**S2a — MultiInput Command collector (THE keystone, smallest).**
- `cookCommand`: add a `Command`-typed **multiInput** port branch — iterate all `g.connections` with `toPin==inPin` (wire order), cook each via `cookCommand`, concatenate `items` into the output chain. Single-input Command branch (Camera/SetRequestedResolution) **untouched**.
- `CmdCookCtx.inputCommands[]` so an op-side Execute can also see them (faithful, though concat can be driver-side).
- Register **Execute** (and **VisibleGizmos** as proving twin — no transform). Flat + resident both.
- Golden: `--selftest-execute` (S2a). **Fork S2a-driver-concat vs op-concat**: prefer driver-side concat (mirrors cookCommand owning subtree cooking + S1 guard), op = thin. FLAG.

**S2b — Group (Execute + SRT push).**
- Reuse `_ProcessLayer2d`/camera ObjectToWorld composition (`field_camera.h` + `point_ops_layer2d.cpp`). Group pushes its SRT around the collected subtree (S1 RAII guard pattern, but for transform context not resolution).
- **Depends on a transform-context push** — if no host ObjectToWorld push/pop stack exists yet (S1 only added resolution), S2b forks into needing a generic context-push (shared with camera3d). FLAG: S2b may merge with the camera3d seam. Golden `--selftest-group` (two layers, second offset by Group translate → known pixel shift).

**S2c — multi-layer Layer2d composition golden + blend ordering.**
- No new core; proves S2a end-to-end: two Layer2d ops → Execute → one target, **blend order = wire order** (Normal over: top layer wins center; Additive: sum). Golden `--selftest-layercompose`.

Recommended landing order: **S2a → S2c → S2b** (S2c needs only S2a; S2b's transform-push may defer/merge with camera3d).

## 5. Harness (harness-first gate)

**Closed-form golden — `--selftest-layercompose` (load-bearing):**
- Two known solid source textures via `runRenderTargetSelfTest` precedent (`point_ops_rendertarget.cpp:194`): layer A = solid red (1,0,0,1), layer B = solid green (0,1,0,1), both full-frame at default camera Scale=1 ScaleMode=Stretch.
- Wire: `LayerA → Execute.Command[0]`, `LayerB → Execute.Command[1]` → RenderTarget executor.
- **Normal blend:** center pixel = **green** (B drawn second = on top, opaque alpha-over). Far-corner = green too. Swap wire order → center = **red** (proves ordering is wire-order).
- **Additive blend:** both Additive → center = clamp(red+green) = (1,1,0,1) **yellow**.
- **Refuter/injectBug:** (a) collapse MultiInput loop to first-wire-only (the `break` bug) → only A draws → center red when expecting green-on-top → **RED**. (b) reverse execute order → center flips → **RED**. (c) resident path with collector NOT mirrored → resident center = single-layer → **RED** (closes the S1 resident-NIT preemptively).
- **Discipline:** assert deep interior + far corner only (never quad edges); single-sample, no depth (Layer2d EnableDepthTest=false, pin it). Compute expected via the same blend equation the executor uses — don't hardcode.

**Secondary `--selftest-execute` (S2a structural):** 3 stub Command ops each appending one distinguishable DrawPoints item; assert resulting `RenderCommand.items` in wire order, count==3. injectBug: drop one wire / reorder → mismatch RED.

## 6. Collision map (serialization)

**Cook-core files that serialize against ALL L4 node-mining and S1's merged changes:**
- `point_graph.cpp` ⚠ (cookCommand — S1 touched `:453-458`, same function)
- `point_graph_resident.cpp` ⚠ (resident cookCommand — S1 touched `:485-492`)
- `point_graph.h` ⚠ (CmdCookCtx — additive field, shared header)
- `render_command.h` (append-only convention; low risk)
- `point_graph_internal.h` (only if Impl state needed)

**Owner-lock (MASTER_PLAN §並行紀律, DEBT_LEDGER §E/§F):** S2 = single sequential spine worker. CANNOT run concurrently with L4 node-mining touching `point_graph.cpp`/registrars, nor S4 file-split debt. New leaf files + `--selftest-*` registration are conflict-free IF orchestrator adds the shared-header field once.

**Safe to parallelize against S2:** L1 (Variation, new subsystem), L3 (document/asset app/), L5 (platform/ IO), L6 perf/backup (ui/), L2 (ui/ except output-window). Disjoint domains.

## 7. Risk / ambiguity (flag for real-TiXL resolution)

1. **Prepare/Restore collapse** — Execute runs three passes (prepare-all → execute-all → restore-all). sw is retained-mode (no immediate DX11 ctx); collapse to per-subtree push/pop already in cookCommand. **But** if any Command op relies on a Prepare side-effect visible to a *later sibling* in the same Execute (cross-command state), the three-pass split matters. **Confirm no render op uses PrepareAction for cross-sibling state** before collapsing.
2. **S2b transform-push dependency** — Group needs an ObjectToWorld context push that may not exist independent of camera3d. **Decide: generic transform-context stack (shared w/ camera3d) or defer Group to camera3d seam.**
3. **MultiInput wire-order authority** — assumes `g.connections` iteration order == wire-declaration order (TiXL `CollectedInputs`). `ListToBuffer` precedent relies on this. **Confirm sw's connection vector preserves insertion order** (`graph.h:102` suggests yes), else add explicit sort key to the golden's order-assertion.
4. **Texture-ptr lifetime** — collected Command items borrow PointGraph-owned textures; must not outlive one cook. Same single-frame contract as existing FORK#1 — keep, don't retain.

## Critical Files
- `app/src/runtime/point_graph.cpp` (flat cookCommand `:410-472`; MultiInput Command branch + S1 guard `:449-459`)
- `app/src/runtime/point_graph_resident.cpp` (resident mirror `:435-505`)
- `app/src/runtime/point_graph.h` (CmdCookCtx — add `inputCommands`)
- `app/src/runtime/render_command.h` (RenderCommand item-chain concat)
- ref-only: `external/tixl/Operators/Lib/flow/Execute.cs:14-39` (CollectedInputs prepare/execute/restore ground truth)
