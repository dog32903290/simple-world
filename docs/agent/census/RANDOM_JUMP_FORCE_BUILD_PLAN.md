# RandomJumpForce Build Plan — second force-cascade ride (serial after FieldDistance)

> Verified against HEAD 80f6dfa on `sw-parity-lane` (2026-06-24). PF-a bridge + FieldDistanceForce live. From force-cascade Plan-agent scoping (CLEAN-RIDE #2). SERIAL: shares particle_params.h enum / point_ops.cpp / node_registry_particle.cpp with VFF+FieldDistance.

GUID `8342d554-6e83-49b1-91b5-2d4b5b63e726`. TiXL: `external/tixl/Operators/Lib/particle/force/RandomJumpForce.cs:4`; `external/tixl/Operators/Lib/Assets/shaders/particles/RandomJumpForceTemplate.hlsl:52-78` (READ-ONLY).

## Why clean ride (with one named fork)
Samples `GetField` at `Position*0.9`; its drive is `curlNoise(noiseLookup)` — and `curlNoise`/`hash41u`/`hash11u` **already exist** in simple_world MSL (`vector_field_force.metal`, `velocity_force.metal`). The field only MODULATES amount: `fieldAmount = (f.r+f.g+f.b)/3` (`RandomJumpForceTemplate.hlsl:68`). Rides the PF-a bridge with a new template; reuses cachedSourceComputePSO (NO tex_op_cache edits).

**NAMED FORK — fork-RandomJump-position-write:** this op writes **Position** directly (`:77`), NOT Velocity, and applies `qRotateVec3` by `p.Rotation` (`:75`). Different integration target than every ported force so far (all write Velocity). Not a seam (no new bridge capability), but a documented behavioral fork — the golden reads back **Position**, not Velocity. Confirm the runFieldForce lambda (from FieldDistance batch) doesn't assume Velocity-write; if it does, RandomJump's branch must write Position outside the shared lambda or the lambda must be parameterized — VERIFY and keep the lambda's VFF/FieldDistance behavior byte-identical (do NOT regress them).

## Force template (NEW) `app/shaders/templates/random_jump_force_template.metal`
Mirror VFF/FieldDistance template (inline Particle byte-identical tixl_point.h). Hooks `{GLOBALS}`/`{FLOAT_PARAMS}`/`{FIELD_CALL}` + 3 `{TEXTURE*}` (collapse-empty). `constant RandomJumpForceParams& {Amount,Frequency,Phase,Variation,AmountDistribution(packed_float3),uint Count}` @FORCE_Params; FieldParams @FORCE_FieldParams. Body ports `RandomJumpForceTemplate.hlsl:61-77`:
- noiseLookup from Position; `fieldAmount = (f.r+f.g+f.b)/3`; `amount = Amount/100 * fieldAmount`; jump = `curlNoise(lookup)*AmountDistribution`; `qRotateVec3(jump, p.Rotation)`; `p.Position += ...`.
- KEEP any isnan guards present in the hlsl.
- **Param mapping discipline (no invented ports):** `.cs` exposes `DirectionDistribution` (`:28`) → template uses `AmountDistribution` (`:15`): map DirectionDistribution→AmountDistribution. `AmountFromVelocity` (`:25`) has NO template slot in RandomJumpForceTemplate.hlsl (separate non-template variant) → **bake to 0, do not invent a port** (same discipline as SnapAngles RandomSeed at point_ops.cpp:331-334).

## Golden (NEW) `app/src/randomjumpforce_field_golden.cpp`, `--selftest-randomjumpforce-field`
**Option (a) field-gate golden (recommended — curlNoise is NOT a trivial closed form, and the cascade's point is the field-into-force bridge, not curlNoise correctness which is already a ported/verified helper):**
- Wire a field whose `(f.r+f.g+f.b)/3` is known. Seed one particle, Amount large, step 1 frame, read back **Position** (the fork).
- Discriminating assertion: with field driving `fieldAmount`, particle Position MOVES (delta ≠ 0); a field returning rgb=0 → fieldAmount=0 → NO move (Position unchanged). That gate IS the closed-form (`amount = Amount/100 * fieldAmount`), independent of curlNoise's exact value.
- **-bug:** mirror the established closed-form-golden -bug pattern (swap expected, e.g. expect "no move" while the real wired field DOES move) → RED. Confirm it bites.
- If a tighter pin is wanted, additionally assert the move DIRECTION/magnitude scales linearly with fieldAmount across two field values (ratio test) — still curlNoise-agnostic.

## Cook-core files (owner-lock, SERIAL; file-disjoint from S4)
- `particle_params.h` — APPEND `FORCE_KIND_RANDOMJUMP=7` (append-only) + `RandomJumpForceParams` struct (static_assert size).
- `point_ops.cpp` — new FORCE_KIND_RANDOMJUMP branch in cookParticleSim. **Position-write fork**: if the shared runFieldForce lambda assumes Velocity dispatch and that's fine (it just dispatches a compute kernel — the kernel decides what it writes), reuse it; the Position-write happens inside the .metal kernel, not the host. CONFIRM the lambda is write-target-agnostic (it dispatches; the kernel mutates the particle buffer). Likely reusable as-is.
- `node_registry_particle.cpp` — new _ForceKind spec row (Field input + Amount/Frequency/Phase/Variation/AmountDistribution/DirectionDistribution per RandomJumpForce.cs defaults).
- `CMakeLists.txt` — SW_RANDOM_JUMP_TEMPLATE define + golden .cpp.
- `selftests_decls.h` + `selftests_point.cpp` — register.

## Refuter focus
1. fork-RandomJump-position-write faithful (writes Position, qRotateVec3 by Rotation, matches hlsl:75-77).
2. curlNoise/hash helpers are the SAME already-ported functions (not a re-impl that diverges) — and present in the assembled MSL globals.
3. **runFieldForce lambda still byte-identical for VFF + FieldDistance** (this batch reuses/maybe-touches it — do NOT regress PF-a or FieldDistance; their goldens must stay (2.25,0,2.25)/(-4,0,0)).
4. Param mapping: DirectionDistribution→AmountDistribution correct; AmountFromVelocity baked-0 is faithful (TiXL non-template variant).
5. FORCE_KIND_RANDOMJUMP=7 append-only; registry row matches .cs.
6. Golden gate genuinely discriminates (field-driven move vs zero-field no-move); -bug bites.

## Build/sequencing
Front-load particle_params.h enum + template + CMake. Metallib rebuild expected. run_all target PASS=414. point_ops.cpp is at the 400 cap — if the RANDOMJUMP branch pushes it over, extend the shared runFieldForce lambda / peel another helper (do NOT grandfather-bump; gate-or-it-rots).

## Critical files (ref)
- clone: `app/shaders/templates/field_distance_force_template.metal` (latest template), `app/src/fielddistanceforce_field_golden.cpp` (latest golden harness).
- branch precedent + shared lambda: `point_ops.cpp` cookParticleSim (runFieldForce lambda from 04364f9).
- TiXL truth: `external/tixl/.../RandomJumpForceTemplate.hlsl:52-78`, `RandomJumpForce.cs`.
- curlNoise helper location: `vector_field_force.metal` / `velocity_force.metal`.
