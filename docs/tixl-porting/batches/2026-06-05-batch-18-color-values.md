# Batch 18 Color Values Acceptance Matrix

Scope: three color/value nodes: `HSBToColor`, `HSLToColor`, and `ColorsToList`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `Vector4` color maps to `VuoColor`; TiXL `List<Vector4>` maps to `VuoList_VuoColor`. `ColorsToList` uses a bounded three-color Vuo adapter for TiXL `MultiInputSlot<Vector4>`; `inputCount` declares how many leading ports are treated as connected.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.HSBToColor` | B adapter | `my_HSBToColor` | `vuo-nodes/my.numbers.color.hsbToColor.c` | C# `external/tixl/Operators/Lib/numbers/color/HSBToColor.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/HSBToColor.t3` | `tests/tixl_batch18_color_values_semantics.test.js`; `tests/tixl_batch18_color_values_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-18-color-values-proof.vuo` -> `my_Batch18ColorValuesProof.hsbColor`; Vuo-saved image `artifacts/vuo_cli/batch-18-color-values-vuo-save.png` | done |
| `Lib.numbers.color.HSLToColor` | B adapter | `my_HSLToColor` | `vuo-nodes/my.numbers.color.hslToColor.c` | C# `external/tixl/Operators/Lib/numbers/color/HSLToColor.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/HSLToColor.t3` | `tests/tixl_batch18_color_values_semantics.test.js`; `tests/tixl_batch18_color_values_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-18-color-values-proof.vuo` -> `my_Batch18ColorValuesProof.hslColor`; Vuo-saved image `artifacts/vuo_cli/batch-18-color-values-vuo-save.png` | done |
| `Lib.numbers.floats.basic.ColorsToList` | B bounded adapter | `my_ColorsToList` | `vuo-nodes/my.numbers.floats.basic.colorsToList.c` | C# `external/tixl/Operators/Lib/numbers/floats/basic/ColorsToList.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/basic/ColorsToList.t3` | `tests/tixl_batch18_color_values_semantics.test.js`; `tests/tixl_batch18_color_values_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-18-color-values-proof.vuo` -> `my_Batch18ColorValuesProof.colorList`; Vuo-saved image `artifacts/vuo_cli/batch-18-color-values-vuo-save.png` | done |

## Batch Notes

- `my_HSBToColor` preserves TiXL's hue-in-degrees behavior, including negative hue wrap when saturation is nonzero. Saturation `0` yields grayscale from brightness.
- `my_HSLToColor` preserves TiXL's custom HSL formula and normalized hue cycle. This is not replaced with a platform color library.
- `my_ColorsToList` preserves TiXL input order through a bounded three-port Vuo adapter. With `inputCount=0`, it returns an empty color list.
- The first Batch 18 proof run compiled and linked but produced a nearly black saved PNG because the composition had data cables without a start event to cook the color conversion nodes. This was fixed by adding `FireOnStart -> HSBToColor:hue` and `FireOnStart -> HSLToColor:hue`; the second proof produced a real color-block image.
- Vuo CLI proof `batch-18-color-values-proof` compiled and linked with return code `0`, loaded all four custom nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-18-color-values-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.33981`, bright ratio `0.909807`, `mostlyBlack=false`.
