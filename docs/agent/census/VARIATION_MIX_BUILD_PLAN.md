# L1 Variation — N-way per-type Mix + JSON pool round-trip Build Plan

> HEAD f11ac5e. 柏為 queue item 3 (L1/L3/L5) — scope found L1 is the only lane with a core-severity, file-disjoint, closed-form-verifiable next piece. L3 thin (polish), L5 clean half spent (cook-side wire is cook-core owner-locked, OUT). ZERO cook-core touch; rides two proven headless golden patterns. No 柏為 eye/hardware.

## Substrate (done, verified)
springDamp+mixFloat (`variation_mix.h`), VariationPool+VariationCrossfader (`variation_pool.h`/`variation_crossfader.h`), document-override bridge `buildBlendTowardsVariationCommand` (`variation_apply.h:82`, the 2-way cousin). The SetOverrideCommand+MacroCommand substrate exists (`graph_commands.h:121`, `command.h:18`); `variation_apply.cpp` (`--selftest-variation-apply`) proves a HEADLESS no-GPU command golden that reads back through `effectiveInput` and undoes.

## Deliverable A — N-way weighted Mix (per-type), applied via MacroCommand
Lead piece. Currently only single-float `mixFloat` exists.
- `variation_mix.h`: add typed Mix `mixVec2/3/4`, `mixInt` (truncating per TiXL), reusing the MixNeighbour missing-neighbour fallback (`present` flag → currentValue, faithful to ExplorationVariation.cs — a missing neighbour contributes currentValue at its weight, NOT 0). Keep ≤400 (split variation_mix_typed.h if needed).
- `variation_apply.{h,cpp}`: add `buildNWayMixCommand(lib, compositionSymbolId, neighbours+weights, ...)` emitting a MacroCommand of SetOverrideCommand, mirroring buildBlendTowardsVariationCommand.
- TiXL: ExplorationVariation.cs:66-191 (Mix normalize Σ(v·w)/Σw + missing-neighbour fallback), MathUtils.cs:305 (Lerp), ValueUtils.cs:21-58 (per-type BlendMethods — confirms WHICH types branch).

## Deliverable B — on-disk JSON snapshot pool round-trip (same-lane fast-follow)
Closes fork-pool-in-memory (`variation_pool.h:42`).
- NEW `variation_pool_json.h` (+ selftest TU): `ToJson` / `TryLoadVariationFromJson` for the in-memory VariationPool.
- TiXL: Variation.cs ToJson/TryLoadVariationFromJson + ExcludedFromPresets skip.

## Goldens (3 machine-verifiable, no 柏為)
1. **Mix closed-form (per type)** `--selftest-variation-mix`: N=3 neighbours, hand-chosen weights → assert Σ(v·w)/Σw EXACTLY for float/vec2/vec3/vec4 + truncating-int. One leg with a missing neighbour → assert it contributes currentValue at its weight (not 0). -bug = forget the 1/sumWeight normalize OR use 0 for missing → RED.
2. **Doc-apply readback** (fold into the mix golden or variation-apply): apply the Mix as a MacroCommand, read each slot back through `effectiveInput`, assert the weighted result landed as a real override; undo → prior overrides restored (verbatim the variation_apply.cpp shape).
3. **JSON round-trip** `--selftest-variation-pool-json`: populate VariationPool (incl. a CJK title + a vec3 param set) → ToJson → TryLoadVariationFromJson → assert byte-stable/value-equal; skip an ExcludedFromPresets input; missing-input on load tolerated (no crash); tamper a key → RED. Clone asset_ref_roundtrip_selftest.cpp.

## Files (all existing extend + new selftest TU) — ZERO cook-core
variation_mix.h, variation_apply.{h,cpp}, variation_pool.h, NEW variation_pool_json.h + selftest TU, CMake, REGISTER_SELFTESTS (orderBase after 304 per variation_apply.cpp:240). File-disjoint from point_graph/frame_cook/resident_eval (S4) → parallel-safe.

## Forks
- fork-pool-numeric-values (numeric types only; string/bool "hold" + Quaternion Slerp deferred — same line drawn at variation_pool.h:36).
- reuse fork-mix-zero-sumweight-guard (variation_mix.h:80).
- Mix BEFORE scatter: scatter has RNG-divergence risk (must match TiXL random(), R2); land the deterministic Mix spine first (mixFloat signature already wired for scatter at :84-86,97).

## 柏為-residual (OUT, explicitly): the live frame-loop driver (every frame read crossfader→write graph) + UI handler = the "engine ≠ wiring" split (MASTER_PLAN:40,42). Engine golden needs no eye/hardware. Stop at the engine boundary (where variation_apply/pool/crossfader already stop).

## Risk: S-M, R1-R2. Closed-form per-type Mix + JSON round-trip, both proven golden patterns, zero cook-core. Refuter focus: per-type Mix normalize + missing-neighbour=currentValue faithful to ExplorationVariation.cs; JSON round-trip byte-stable + ExcludedFromPresets skip + tamper-RED.

## Critical files
- variation_mix.h (add typed mix), variation_apply.cpp (buildNWayMixCommand, the headless command golden template), variation_pool.h (ToJson source), asset_ref_roundtrip_selftest.cpp (round-trip golden template), external/tixl ExplorationVariation.cs / ValueUtils.cs / Variation.cs.
