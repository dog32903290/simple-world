# Batch 17 Float List Transform Acceptance Matrix

Scope: two `Lib.numbers.floats.process` transform nodes: `CombineFloatLists` and `RemapFloatList`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `List<float>` maps to `VuoList_VuoReal`. `CombineFloatLists` uses a bounded three-list Vuo adapter for TiXL `MultiInputSlot<List<float>>`; `inputCount` declares how many leading ports are treated as connected.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.floats.process.CombineFloatLists` | B bounded adapter | `my_CombineFloatLists` | `vuo-nodes/my.numbers.floats.process.combineFloatLists.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/CombineFloatLists.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/CombineFloatLists.t3` | `tests/tixl_batch17_float_list_transform_semantics.test.js`; `tests/tixl_batch17_float_list_transform_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-17-float-list-transform-proof.vuo` -> `my_Batch17FloatListTransformProof.combinedList`; Vuo-saved image `artifacts/vuo_cli/batch-17-float-list-transform-vuo-save.png` | done |
| `Lib.numbers.floats.process.RemapFloatList` | B adapter | `my_RemapFloatList` | `vuo-nodes/my.numbers.floats.process.remapFloatList.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` | `tests/tixl_batch17_float_list_transform_semantics.test.js`; `tests/tixl_batch17_float_list_transform_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-17-float-list-transform-proof.vuo` -> `my_Batch17FloatListTransformProof.remappedList`; Vuo-saved image `artifacts/vuo_cli/batch-17-float-list-transform-vuo-save.png` | done |

## Batch Notes

- `my_CombineFloatLists` preserves TiXL input order and skips null/empty lists. With no connected lists declared, it returns an empty list.
- `my_RemapFloatList` returns an empty list for empty/null input and fills the output list with `RangeOutMin` when the input range is effectively zero (`abs(inRange) < 0.00001`).
- `my_RemapFloatList` ports TiXL `ApplyGainAndBias` for values strictly inside `(0,1)`, then supports Normal, Clamped, and Modulo modes. Modulo uses `min + fmod(v - min, max - min)`, matching the TiXL list source.
- `MergeFloatLists` and `AmplifyValues` were not included. Both carry persistent state/history and need their own stateful batch instead of being hidden inside a stateless list-transform proof.
- Vuo CLI proof `batch-17-float-list-transform-proof` compiled and linked with return code `0`, loaded all three custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-17-float-list-transform-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.213076`, bright ratio `0.624412`, `mostlyBlack=false`.
