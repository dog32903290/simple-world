# Batch 21 OKLCh Combine Color Lists Acceptance Matrix

Scope: two color value/list nodes: `OKLChToColor` and `CombineColorLists`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `Vector4` color maps to `VuoColor`; TiXL `List<Vector4>` maps to `VuoList_VuoColor`. `CombineColorLists` exposes TiXL collected input behavior through a bounded three-list Vuo adapter.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.OKLChToColor` | B adapter | `my_OKLChToColor` | `vuo-nodes/my.numbers.color.oklchToColor.c` | C# `external/tixl/Operators/Lib/numbers/color/OKLChToColor.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/OKLChToColor.t3`; helper `external/tixl/Core/Utils/OkLab.cs` | `tests/tixl_batch21_oklch_combine_color_lists_semantics.test.js`; `tests/tixl_batch21_oklch_combine_color_lists_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-21-oklch-combine-color-lists-proof.vuo` -> `my_Batch21OklchCombineColorListsProof.oklchColor`; Vuo-saved image `artifacts/vuo_cli/batch-21-oklch-combine-color-lists-vuo-save.png` | done |
| `Lib.numbers.color.CombineColorLists` | B adapter | `my_CombineColorLists` | `vuo-nodes/my.numbers.color.combineColorLists.c` | C# `external/tixl/Operators/Lib/numbers/color/CombineColorLists.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/CombineColorLists.t3` | `tests/tixl_batch21_oklch_combine_color_lists_semantics.test.js`; `tests/tixl_batch21_oklch_combine_color_lists_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-21-oklch-combine-color-lists-proof.vuo` -> `my_Batch21OklchCombineColorListsProof.combinedList`; Vuo-saved image `artifacts/vuo_cli/batch-21-oklch-combine-color-lists-vuo-save.png` | done |

## Batch Notes

- `my_OKLChToColor` preserves TiXL defaults: `Hue=0`, `Saturation=0`, `Brightness=0.50000006`, `Alpha=1`, `UseGamma=false`, and `IntensityBoost=1`.
- `OKLChToColor` computes `OkLab.FromOkLCh(Brightness, Saturation, (Hue % 1) * 360, Alpha, UseGamma)`, then multiplies RGB by `IntensityBoost` while preserving alpha.
- TiXL currently passes `UseGamma` into `OkLab.FromOkLCh`, but `OkLab.FromOkLab` does not branch on that flag and always applies gamma conversion after clamping linear RGB. The Vuo node preserves this current no-branch behavior instead of inventing a new mode.
- `my_CombineColorLists` concatenates connected non-empty color lists in input order. Vuo cannot expose TiXL's dynamic collected-input socket directly, so this port uses a bounded three-list adapter plus `inputCount`.
- `SampleGradient` and related gradient work were not included because TiXL uses a custom `Gradient` type and needs a dedicated body-layer adapter. `KeepColors` remains stateful list memory work, and `PickColorFromImage` remains texture/CPU readback resource work.
- Vuo CLI proof `batch-21-oklch-combine-color-lists-proof` compiled and linked with return code `0`, loaded `my.numbers.color.oklchToColor`, `my.numbers.color.combineColorLists`, `my.numbers.batch.batch21OklchCombineColorListsProof`, and `my.numbers.floats.basic.colorsToList`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-21-oklch-combine-color-lists-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.541961`, bright ratio `1.0`, `mostlyBlack=false`.
