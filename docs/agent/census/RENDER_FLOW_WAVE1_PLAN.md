# render/flow island op-mining — WAVE 1 plan

> HEAD 428430b. The render/Execute (155) + flow (35) islands unlocked by S2/S3. Scout finding: the inventory is THINNER than 155+35 — most "unlocked" render ops still need a value-TYPE sub-seam (Int2/Int3/Vector4[]/BufferWithViews) or a context-var-channel / feedback / blend / trigger sub-seam. **~6 ops are genuinely clean WAVE-1 fuel.** Two registrars with opposite parallelism:
> - Command-rail (render/Execute/draw): `point_ops_register_draw.cpp` + `node_registry_draw.cpp` = central edit-on-add → **SERIAL-MERGE**.
> - value-rail context-var (math): `node_registry_math_contextvar.cpp` MathOp self-reg → parallel.
> - selftest: `REGISTER_SELFTESTS` Meyers-singleton, parallel-safe.

## WAVE 1 = Lane A (Command-rail transform-context leaves, SERIAL, one owner-locked builder)
Ride the proven S2 group-stamp (`groupObjectToWorld`/`hasGroup` per-item stamp, `render_command.h:195-208`) — the EXACT mechanism Group/RotateAroundAxis/Shear/Transform use. **Zero new infra, zero shader, zero seam.** Idiom to copy = `point_ops_shear.cpp` (copy subtree items, build a Mat4, accumulate-stamp onto each `it.groupObjectToWorld`). Matrix helpers exist in `field_camera.h:38-78` (mat4Mul, mat4Identity, lookAtRH, row-vector convention).

| Op | TiXL source | What | Golden | Forks |
|---|---|---|---|---|
| **RotateTowards** | `Lib/render/transform/RotateTowards.cs` | LookAt-style rotation pushing ObjectToWorld so subtree faces a target point | closed-form: build expected lookAtRH(pos,target,up)-derived Mat4, assert stamped `it.groupObjectToWorld[0..15]` element-wise (eps 1e-4); -bug drops stamp → never reorients | up-vector degenerate (target colinear with up) — name like Shear/Group |
| **SpreadIntoGrid** | `Lib/render/transform/SpreadIntoGrid.cs` | replicate subtree into N×M grid, each cell a translated ObjectToWorld | state: Count=rows×cols items, cell (r,c) translate=(c*sx, r*sy, 0); mirror --selftest-loop per-iteration-translate golden; -bug(a) emit-once→count RED, -bug(b) replicate-without-offset→distinct-translate RED | **GATE FIRST: static fan-out vs re-cook.** It replicates ONE cooked subtree (static fan-out) — VERIFY it does NOT need a cook-core re-cook like Loop (`point_graph.cpp:13`). If it needs re-cook → it COLLIDES with S4's point_graph split → STOP+flag, defer. If static (copy+stamp, like Group's multi-item) → clean. |

**Census staleness note:** census marks RotateTowards/SpreadIntoGrid as camera3d-BLOCKED (`ops-render.md:152,155`) — that's STALE (same staleness that mis-listed Group as blocked; S2 proved the transform-context push is independent of full camera3d).

## Batch shape
SERIAL one builder: RotateTowards → SpreadIntoGrid. Both edit `point_ops_register_draw.cpp:27-52` (one `registerXxxOp()` line) + `node_registry_draw.cpp` (one NodeSpec) → two builders = guaranteed conflict. Single-thread them. Each ~90 lines (Shear template). **No overlap with S4 point_graph split** as long as the builder stays in `point_ops_*.cpp` leaf files + the two registrars (Shear precedent is self-contained) — EXCEPT the SpreadIntoGrid re-cook gate above.

## Harness (machine-verifiable, no 柏為)
- `REGISTER_SELFTESTS(orderBase=N, {"<name>", run<Name>SelfTest})` self-registering.
- **Two-leg golden (flat AND resident, S2c blood lesson)**: cook through PointGraph both paths; resident-only miss = prod-only black-hole.
- Transform-context closed-form Mat4 (build expected from field_camera.h, assert groupObjectToWorld[0..15] eps 1e-4). SpreadIntoGrid per-cell translate golden (like --selftest-loop).
- -bug RED both legs; self-check asserts injectBug trips a tooth (point_ops_loop.cpp:327 pattern).

## After WAVE 1 (the honest next moves — sub-seams, not leaves)
- **WAVE 2 = value-type sub-seam** (Int2/Int3/Vector4[] value outputs) → unlocks Lane C (~10: CalcDispatchCount/RequestedResolution/GetTextureSize/TransformMatrix + buffer-select ops need BufferWithViews value type).
- **WAVE 3 = context-var channel sub-seam** (String/Vec3/Matrix channels in `ContextVarMap` `stateful_value_ops.h:71` + dual-rail resolvers) → unlocks the String/Vec3/Matrix var Get/Set family (~10). Bool var ops (GetBoolVar/SetBoolVar+Cmd) are clean NOW (bool fits the intVars channel) — a small Lane-B follow-up.
- Remaining flow-Command ops (BlendScenes/TimeClip/ResetSubtreeTrigger/GetScreenPos) each carry a real sub-seam (multi-RT compose / normalizedTime context-var / trigger-dirty / camera3d matrix read).

## Critical files
- point_ops_register_draw.cpp (SERIAL Command-rail registrar — owner-lock), node_registry_draw.cpp (shared NodeSpec table)
- point_ops_shear.cpp (the transform-context leaf IDIOM to copy)
- render_command.h:195-208 (group-stamp), field_camera.h:38-78 (mat4 helpers)
- external/tixl/Operators/Lib/render/transform/{RotateTowards,SpreadIntoGrid}.cs (port targets)
