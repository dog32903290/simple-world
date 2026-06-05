# Batch 20 Color Mix Pick Acceptance Matrix

Scope: two color value/list nodes: `BlendColors` and `PickColorFromList`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `Vector4` color maps to `VuoColor`; TiXL `List<Vector4>` maps to `VuoList_VuoColor`. `PickColorFromList` exposes TiXL's empty/null early-return cache behavior through an explicit `previousSelected` input.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.BlendColors` | B adapter | `my_BlendColors` | `vuo-nodes/my.numbers.color.blendColors.c` | C# `external/tixl/Operators/Lib/numbers/color/BlendColors.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/BlendColors.t3` | `tests/tixl_batch20_color_mix_pick_semantics.test.js`; `tests/tixl_batch20_color_mix_pick_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-20-color-mix-pick-proof.vuo` -> `my_Batch20ColorMixPickProof.blendedColor`; Vuo-saved image `artifacts/vuo_cli/batch-20-color-mix-pick-vuo-save.png` | done |
| `Lib.numbers.color.PickColorFromList` | B adapter | `my_PickColorFromList` | `vuo-nodes/my.numbers.color.pickColorFromList.c` | C# `external/tixl/Operators/Lib/numbers/color/PickColorFromList.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/PickColorFromList.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` | `tests/tixl_batch20_color_mix_pick_semantics.test.js`; `tests/tixl_batch20_color_mix_pick_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-20-color-mix-pick-proof.vuo` -> `my_Batch20ColorMixPickProof.pickedColor`; Vuo-saved image `artifacts/vuo_cli/batch-20-color-mix-pick-vuo-save.png` | done |

## Batch Notes

- `my_BlendColors` preserves TiXL modes: `0=Mix`, `1=Multiply`, `2=Add`, `3=Blend`.
- `Blend` mode uses color B alpha as the RGB blend factor, then sets alpha to `aA + aB - aA * aB`, matching TiXL source.
- `my_PickColorFromList` uses TiXL positive modulo indexing. Empty/null input returns `previousSelected` to expose TiXL's unchanged `Selected.Value` behavior.
- `KeepColors` was not included because it carries persistent list state. `PickColorFromImage` was not included because it is texture/CPU readback resource work and belongs in a runtime/resource batch.
- Vuo CLI proof `batch-20-color-mix-pick-proof` compiled and linked with return code `0`, loaded all three new custom nodes plus `my_ColorsToList`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-20-color-mix-pick-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.16041`, bright ratio `1.0`, `mostlyBlack=false`.
