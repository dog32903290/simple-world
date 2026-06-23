# PF-a Build Plan — kernel injection / source-compute-PSO bridge

> Verified against HEAD 254ec6c on `sw-parity-lane` (2026-06-24). PF-0a+b+c all landed (67f9007, b4697fc).
> Authored by Plan agent; orchestrator-persisted for the Opus builder. The blueprint (PARTICLE_FIELD_BLUEPRINT.md §3) is PF-0-stale in places — THIS doc supersedes it for PF-a.

## What PF-a is now
PF-0 already delivered the upstream half: graph→FieldNode builder, NodeSpec `VectorField` port, `PointCookCtx.inputFieldTree` channel (flat `point_graph.cpp:357` + resident `point_graph_resident.cpp:345`), and `configureToroidalVortexFieldFromParams`. **PF-a is purely a consumer-side bridge**: take the already-delivered `cc.inputFieldTree`, assemble it into a compute kernel, compile/cache the PSO, bind the field-param buffer, dispatch.

**DO NOT** re-add the Field port, the gather, or the channel — all done in PF-0. **DO NOT** touch the NodeSpec or point_graph gather.

Blueprint §0.5 FLAG ("does resident flatten carry the wired field child") is **RESOLVED** — the builder golden (`--selftest-fieldtree-builder`) proves both legs rebuild the tree with the wired Radius projected.

## 1. Seam design — mirror `renderField2d` (`field_render.cpp:21-90`) but for compute

### 1a. New cache fn `cachedSourceComputePSO` (tex_op_cache.{h,cpp})
Model on `cachedSourcePSO` (`tex_op_cache.cpp:124-160`) but build a COMPUTE pipeline via the `newComputePipelineState(fn,&err)` path from `cachedComputePSO` (`.cpp:170-176`). `tex_op_cache` already has both halves separately — PF-a unions them.
```cpp
MTL::ComputePipelineState* cachedSourceComputePSO(MTL::Device* dev, const char* mslSource,
                                                  uint64_t srcHash, const char* kernelName);
```
- **Cache key = `srcHash`** (FNV-1a of assembled MSL, from `assembleFieldMSL ... out.srcHash`). Param-only edits keep the same srcHash → reuse PSO (TiXL `ChangedFlags.Code` gate). Contract documented `tex_op_cache.h:62-64`.
- New `std::map<uint64_t, SourceComputePSOEntry>` table (entry `{ComputePipelineState*, Library*}`), parallel to `sourcePsoCache()` (`.cpp:36`). Add clear loop in `clearTexOpCache()` (`.cpp:203`) — REQUIRED so per-run selftest devices don't reuse a stale PSO.
- Reuses the same `fieldSourceCompiler()` fn-ptr (`field_graph.cpp:14`, registered `main.cpp:161`). No new leaf seam.
- Device-global process-static map in `tex_op_cache.cpp` anon namespace = the one new device-global owner-lock item; immutable once built.

### 1b. New compute template `app/shaders/templates/vector_field_force_template.metal`
Mirror `field_render_template.metal`. Same hooks: `{GLOBALS}`, `{FLOAT_PARAMS}`, `{FIELD_CALL}`, three `{TEXTURE*}` hooks (collapse empty for ToroidalVortexField). Kernel body = `vector_field_force.metal:40-64` ported, with baked `float4 f = float4(1,1,1,1)` (`.metal:52`) replaced by an `evalField` that runs `{FIELD_CALL}`.

**Critical parity fact (TiXL `VectorFieldForce-sg.hlsl:62-63`):** `GetField(float4(pos,0))` samples the particle's RAW Position — no field-space remap (the render template's uv→p mapping is render-only). Particle world position IS the field sample point. Resolves the blueprint §6 field-space-convention worry.

`assembleFieldMSL` is template-agnostic (`field_graph.cpp:218-290` only string-fills hooks) → works on the compute template byte-for-byte the same. Confirmed.

### 1c. Buffer-slot layout
Current force bindings (`particle_params.h:268-271`): `FORCE_Particles=0`, `FORCE_Params=1`. **Add `FORCE_FieldParams = 2`.** Template declares `constant FieldParams& P [[buffer(FORCE_FieldParams)]]` (the assembled `FieldParams` struct, distinct from `VecFieldForceParams`). Toroidal params carry `ToroidalVortexField_<id>_` prefix (`field_ops_toroidalvortexfield.cpp:150`) → no name collision with force's `Amount`.

### 1d. Cook dispatch (`cookParticleSim` FORCE_KIND_VECTORFIELD branch, `point_ops.cpp:214-220`)
- `if (c.inputFieldTree)`: load compute template (new `SW_VFF_TEMPLATE` compile define mirroring `SW_FIELD_TEMPLATE`) → `assembleFieldMSL(c.inputFieldTree, vffTemplate)` → `cachedSourceComputePSO(dev, msl, srcHash, "vector_field_force")` → upload `asmField.floatParams` to buffer at `FORCE_FieldParams` → dispatch (variant of `runForce` lambda `:193-203` + one extra setBuffer).
- `else`: existing `runForce(s->psoVecField, &vp, sizeof(vp))` — **fork-VFF preserved, byte-identical for every existing baked graph.**

## 2. Files touched
| File | Change | Owner-lock? |
|---|---|---|
| `tex_op_cache.h` | declare `cachedSourceComputePSO` (mirror `:65`) | YES device-global |
| `tex_op_cache.cpp` | `SourceComputePSOEntry` table + fn + clear loop | YES device-global |
| `app/shaders/templates/vector_field_force_template.metal` | NEW compute template | No (templates/ string asset, not glob-compiled) |
| `particle_params.h` | `FORCE_FieldParams=2` in `ForceBinding` (`:268`) | particle-lane local |
| `point_ops.cpp` | `cookParticleSim` VECTORFIELD branch + `#include tex_op_cache.h, field_graph.h`; template via `SW_VFF_TEMPLATE` | YES cookParticleSim cook-core |
| `app/CMakeLists.txt` | `SW_VFF_TEMPLATE="..."` define (mirror `:549`) | No |
| `app/src/particlefield_probe_golden.cpp` | flip both rows to terminal + register source compiler (`#include platform/metal_compile.h` + `setFieldSourceCompiler`, mirror `field_render_golden.cpp:95`) | golden shell |

**Owner-lock set (no concurrent L4 op-mining):** `point_ops.cpp`, `tex_op_cache.cpp`, `tex_op_cache.h`, `particle_params.h`.
**S4 file-disjoint:** S4 splits `point_graph.cpp`/`_resident.cpp`; PF-a's `point_ops.cpp` edit is isolated. PF-0's gather edits (S4-adjacent) already landed.

## 3. flat + resident mirror checklist
`cookParticleSim` is a SINGLE fn shared by both legs → **PF-a consume path auto-shared** (the key structural win; no double-leg consume code). PF-0 delivers `cc.inputFieldTree` identically on both legs (verified by builder golden).
- `cachedSourceComputePSO` device-global cache: MUST add to `clearTexOpCache()`.
- Field source compiler registration: prod `main.cpp:161`. **Probe golden registers NONE currently → MUST add, else `cachedSourceComputePSO` returns nullptr → silent baked fallback → terminal flip silently fails.** (refuter #1 target)
- Probe must assert BOTH `cookFlat` and `cookResidentLeg` reach anisotropy≠0 (already runs both, `:252-253`).

## 4. Golden design
**Primary = flip `--selftest-particlefield-probe` to terminal.** Currently middle state `gapHolds = fieldInputExists && flatIsotropic && resIsotropic` (`:302`).
- **Terminal no-bug (replace `:302`):** `terminal = fieldInputExists && fieldHadEffect` where `fieldHadEffect` (`:266`) = `flatAniso > kAnisoWant && resAniso > kAnisoWant` on both legs.
- **Terminal -bug RED:** SEVER the field wire (drop connection 104) → `gatherForceFieldTree` returns nullptr → baked (1,1,1) → isotropic → the anisotropy assertion fails → RED. Bites the bridge itself.

**Closed-form ground truth (NOT self-consistency).** TiXL `ToroidalVortexField.cs` field math (ported `field_ops_toroidalvortexfield.cpp:86-127`) + `VectorFieldForce-sg.hlsl:62-66`:
- Field params `Radius=0.5, Range=0.5, decayK=2`; particle at field-space `(0.25,0,0)`: `decay=1-(0.25/0.5)²=0.75`, `vSwirl=(0,0,0.75)`, `vRadial=(0.75,0,0)` → `f.xyz=(0.75,0,0.75)`, `f.w=0.75`.
- `velocity = f.xyz * Amount * f.w * variationFactor` (Variation=0 → factor=1) = **`(0.5625·A, 0, 0.5625·A)`** first frame.
- **Fingerprint: `velocity.x ≈ velocity.z`, `velocity.y ≈ 0`** — baked (1,1,1) can never produce this (it gives x=y=z).
- **Add a new `--selftest-vectorfieldforce-field`:** seed ONE particle at `(0.25,0,0)`, step 1 frame, read back `Velocity` (Particle.Velocity @48, `tixl_point.h:51`), assert `≈ (0.5625A,0,0.5625A)` ±eps. Directly verifies f.xyz (closes the VERIFICATION CEILING at `field_ops_toroidalvortexfield.cpp:49-58`). Must register the source compiler.

Cited TiXL: `external/tixl/Operators/Lib/field/generate/vec3/ToroidalVortexField.cs`; `external/tixl/Operators/Lib/Assets/shaders/particles/VectorFieldForce-sg.hlsl:62-66`.

## 5. Refuter focus (make-or-break)
1. **Does the probe golden register a field source compiler?** Currently NO (`:102-105` only loadLib). Without it → nullptr PSO → silent baked → spurious result. #1 target. Verify a compile failure makes golden RED, not silently isotropic.
2. **Resident leg actually consumes the field** (not just flat)? Should be automatic (shared fn) but confirm `cookResidentLeg` reaches anisotropy≠0.
3. **srcHash collision/staleness:** param-only change reuses PSO; code change → different srcHash; two different fields don't alias one PSO.
4. **Buffer-slot aliasing:** `FORCE_FieldParams=2` free; force params @1 + field params @2 no overwrite; assembled `FieldParams` members prefixed (no collision with `VecFieldForceParams.Amount`).
5. **NaN guard preservation:** field early-returns `float4(0)` at `rho<eps` (`field_ops_toroidalvortexfield.cpp:107`); kernel guards `isnan` (`vector_field_force.metal:63`). Keep both; closed-form spot avoids both degenerate points (rho=0 ring, rho=Range center).
6. **fork-VFF byte-identity:** VectorFieldForce with NO wired field → exact same baked (1,1,1) drift. Refuter runs `runVectorFieldForceSelfTest`, confirms GREEN.

## 6. Named forks
- **fork-VFF** (exists, `vector_field_force.metal:7-17`): baked-(1,1,1) static PSO — PRESERVED as no-field fallback, un-forks only when field wired.
- **fork-VFF-singlefield** (PF-0, `point_graph.h:102`): `inputFieldTree` single slot, one VectorFieldForce + one field; multi-force×multi-field deferred. Inherited, no new fork.
- No new fork in PF-a itself (compute template = faithful port; cbuffer→`constant&` / `register(bN)`→`[[buffer(N)]]` is the accepted field-op fork-class, `field_graph.cpp:266`).

## 7. Build size / cold-build
~300-400 lines, mostly mirror-of-existing. **Expect a metallib rebuild** (the `particle_params.h FORCE_FieldParams` enum forces it; the new `.metal` template is NOT glob-compiled so adds nothing). **Front-load the enum + template first** to trigger the rebuild, then iterate cache/cook/golden warm. On a worktree from-scratch this is ~50min (watchdog ≥55min, [[sw-watchdog-cook-core-false-death]]); on the warm main checkout the incremental + metallib rebuild is much shorter.

## Critical files (quick ref)
- `app/src/runtime/tex_op_cache.{h,cpp}` — new `cachedSourceComputePSO`; mirror `cachedSourcePSO` (`:124-160`) with `newComputePipelineState`.
- `app/src/runtime/point_ops.cpp` — `cookParticleSim` VECTORFIELD branch (`:214-220`).
- `app/shaders/templates/vector_field_force_template.metal` — NEW; port `vector_field_force.metal:40-64`.
- `app/src/runtime/particle_params.h` — `FORCE_FieldParams=2` (`:268`).
- `app/src/particlefield_probe_golden.cpp` — flip both rows (`:280-307`) + register compiler; + new `--selftest-vectorfieldforce-field`.
- ref-only: `field_render.cpp:21-90` (dispatch precedent), `external/tixl/.../VectorFieldForce-sg.hlsl` (integrate truth), `field_ops_toroidalvortexfield.cpp:86-127` (field math truth).
