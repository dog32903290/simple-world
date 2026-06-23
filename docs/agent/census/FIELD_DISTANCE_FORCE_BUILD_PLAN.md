# FieldDistanceForce Build Plan — first clean ride on the PF-a bridge

> Verified against HEAD 92b886f on `sw-parity-lane` (2026-06-24). PF-a bridge (0da88d3) live. Plan-agent output, orchestrator-persisted for the Opus builder.

## Why clean ride
Structurally identical to VectorFieldForce: samples the wired field at raw `Particle.Position`, integrates into `Velocity`. The only addition over VFF is calling `GetField().w` four times for a finite-difference normal. The PF-a bridge already delivers `GetField` with real SDF `.w` (`field_ops_spheresdf.cpp:56` emits `f.w = length(p-Center)-Radius`); the `GetFieldNormal` helper (TiXL `FieldDistanceForce.hlsl:65-72`) is pure shader math, no new seam. **Reuses `cachedSourceComputePSO` as-is — NO tex_op_cache edits** (smaller owner-lock than PF-a).

GUID `42394232-51fa-4e75-851b-c2bca39de71a`. TiXL: `external/tixl/Operators/Lib/particle/force/FieldDistanceForce.cs:3`; `external/tixl/Operators/Lib/Assets/shaders/particles/FieldDistanceForce.hlsl:74-101` (READ-ONLY).

## Force template (NEW) `app/shaders/templates/field_distance_force_template.metal`
Mirror `vector_field_force_template.metal` (inline Particle struct byte-identical to tixl_point.h — runtime newLibrary has no -I path):
- Same hooks `{GLOBALS}`/`{FLOAT_PARAMS}`/`{FIELD_CALL}` + 3 `{TEXTURE*}` hooks (collapse-empty for SDF leaves).
- `constant FieldDistForceParams& fp [[buffer(FORCE_Params)]]` carrying `Amount, Attraction, Repulsion, NormalSamplingDistance, DecayWithDistance, uint Count`.
- `constant FieldParams& P [[buffer(FORCE_FieldParams)]]`.
- Kernel body = port of `FieldDistanceForce.hlsl:74-101`:
```
float3 n = GetFieldNormal(pos);   // 4x GetField().w finite diff, NormalSamplingDistance
float d  = GetField(float4(pos,0)).w;
if (isnan(d)||isnan(n.x)) return;        // KEEP NaN guards
if (d>0) vel -= n*Attraction*Amount*pow(d+1, -DecayWithDistance);
else     vel += n*Repulsion*Amount;
```

## Closed-form golden (NEW) `app/src/fielddistanceforce_field_golden.cpp`, `--selftest-fielddistanceforce-field`
Mirror `vectorfieldforce_field_golden.cpp` (incl. `setFieldSourceCompiler` + `clearTexOpCache` registration — CRITICAL, else null PSO → silent baked fallback).
- Wire one **SphereSDF** leaf (`field_ops_spheresdf`, Center=0, Radius=0.5). Seed one particle at `(1,0,0)`, V=0, Amount=A, Attraction=1, Repulsion=0, DecayWithDistance=0, NormalSamplingDistance small.
- `d = |(1,0,0)| - 0.5 = 0.5 > 0` → attract. Sphere-SDF normal at (1,0,0) = (1,0,0) (finite-diff converges). `decay = pow(0.5+1, -0) = 1`.
- `velocity -= n*Attraction*Amount*decay` → **`Velocity = (-A, 0, 0)`** first frame. Pick a concrete A (e.g. A=4 → (-4,0,0)).
- **-bug RED:** sever the field wire → `GetField` baked to 1 → `.w=1`, normal degenerate → assertion `≈(-A,0,0)` fails.

## Cook-core files touched (owner-lock, SERIAL within particle lane; file-disjoint from S4)
- `app/src/runtime/particle_params.h` — append `FORCE_KIND_FIELDDISTANCE=6` (APPEND-ONLY, never insert — pre-existing .swproj _ForceKind overrides keep meaning, `:288` discipline) + new `FieldDistForceParams` struct. **Enum edit forces metallib rebuild — front-load it.**
- `app/src/runtime/point_ops.cpp` — new `else if (forceKind==FORCE_KIND_FIELDDISTANCE)` branch in `cookParticleSim` (`:227-344`), modeled on the VECTORFIELD branch (`:237-285`): if `c.inputFieldTree` assemble field_distance template→cachedSourceComputePSO→bind FieldDistForceParams@FORCE_Params + field@FORCE_FieldParams→dispatch; else baked no-op/fallback (mirror fork-VFF). New PSO member in `SimState` (`:116-127`) if a baked fallback PSO is needed; makeComputePSO/release in simStateNew/simStateFree (`:136-156`).
- `app/src/runtime/node_registry_particle.cpp` — new `_ForceKind` spec row (mirror `:23-122`).
- `app/CMakeLists.txt` — `SW_FIELD_DISTANCE_TEMPLATE` define (mirror SW_VFF_TEMPLATE) + add fielddistanceforce_field_golden.cpp to target.
- `app/src/selftests_decls.h` + `selftests_point.cpp` — register the new selftest.

cookParticleSim is the single shared cook fn → flat+resident auto-share (PF-a structural win). No point_graph touch.

## Forks
- baked-fallback fork (no field wired → no-op; mirror fork-VFF). Inherits **fork-VFF-singlefield** (`point_graph.h:102`, single inputFieldTree slot). **No new fork.**

## Refuter focus (next turn)
1. Source compiler registered in the golden (else null PSO → silent baked); compile failure → RED not silent.
2. Inline Particle struct byte-layout matches tixl_point.h offset-for-offset (Velocity@48, total 64, packed_float3 12B).
3. Closed-form ground truth re-derived from TiXL FieldDistanceForce.hlsl:74-101 + SphereSDF math — is (-A,0,0) actually correct (normal direction sign, decay exponent)?
4. NaN guards preserved (isnan(d), isnan(n)); sample point avoids degenerate.
5. fork-VFF + VectorFieldForce goldens still green (FORCE_KIND append didn't shift existing kinds).
6. node_registry _ForceKind row matches TiXL FieldDistanceForce.cs params.

## Build size / sequencing
Small, mostly mirror-of-VFF. Front-load particle_params.h enum + template + CMake define → trigger metallib rebuild → iterate cook/golden warm. On warm main checkout ~15-20min total. run_all target PASS=413 (412 + new golden).

## Critical files (ref)
- clone template: `app/shaders/templates/vector_field_force_template.metal`
- clone golden: `app/src/vectorfieldforce_field_golden.cpp`
- branch precedent: `point_ops.cpp:237-285` (VECTORFIELD branch)
- SDF .w source: `field_ops_spheresdf.cpp:56`
- TiXL truth: `external/tixl/.../FieldDistanceForce.hlsl:74-101`, `FieldDistanceForce.cs:3`
