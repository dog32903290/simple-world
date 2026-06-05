# Batch 4 Remap/Lerp Naming Repair Acceptance Matrix

Scope: repair the two legacy first-pass scalar nodes that violated the current node naming contract. The old `myworld.tixl.remap` and `myworld.tixl.lerp` sources remain in place for compatibility, but the accepted creator-facing nodes are now `my_Remap` and `my_Lerp`.

Gate: each row needs TiXL source/default evidence, semantic edge cases, corrected Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the corrected `my_` node output.

| TiXL node | grade | accepted Vuo title | accepted Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.float.adjust.Remap` | A | `my_Remap` | `vuo-nodes/my.numbers.float.adjust.remap.c` | C# `external/tixl/Operators/Lib/numbers/float/adjust/Remap.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/adjust/Remap.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` for `ApplyGainAndBias`, `Clamp`, `Fmod` | `tests/tixl_remap_lerp_semantics.test.js`; `tests/tixl_remap_lerp_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-4-remap-lerp-proof.vuo` -> `my_Batch4RemapLerpProof.remapValue`; screenshot `artifacts/vuo_cli/batch-4-remap-lerp.run.png` | done |
| `Lib.numbers.float.process.Lerp` | A | `my_Lerp` | `vuo-nodes/my.numbers.float.process.lerp.c` | C# `external/tixl/Operators/Lib/numbers/float/process/Lerp.cs`; `.t3` `external/tixl/Operators/Lib/numbers/float/process/Lerp.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` for `Lerp` and `Clamp` | `tests/tixl_remap_lerp_semantics.test.js`; `tests/tixl_remap_lerp_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-4-remap-lerp-proof.vuo` -> `my_Batch4RemapLerpProof.lerpValue`; screenshot `artifacts/vuo_cli/batch-4-remap-lerp.run.png` | done |

## Batch Notes

- This batch fixes naming and proof acceptance; it does not delete the older `TiXL Remap` / `TiXL Lerp` compatibility nodes.
- `my_Remap` implements TiXL's Normal, Clamped, and Modulo modes. Modulo intentionally follows the TiXL scalar source: `Fmod(v, max - min)`.
- `my_Lerp` preserves TiXL's optional factor clamp: `Clamp=false` extrapolates; `Clamp=true` limits `F` to `0..1`.
