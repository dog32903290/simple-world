# Batch 19 Color List To Ints Acceptance Matrix

Scope: one color/list conversion node: `ColorListToInts`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `List<Vector4>` maps to `VuoList_VuoColor`; TiXL `List<int>` maps to `VuoList_VuoInteger`. `ColorListToInts` uses a bounded three-color-list Vuo adapter for TiXL `MultiInputSlot<List<Vector4>>`; `inputCount` declares how many leading ports are treated as connected.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.floats.process.ColorListToInts` | B bounded adapter | `my_ColorListToInts` | `vuo-nodes/my.numbers.floats.process.colorListToInts.c` | C# `external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.cs`; `.t3` `external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.t3` | `tests/tixl_batch19_color_list_to_ints_semantics.test.js`; `tests/tixl_batch19_color_list_to_ints_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-19-color-list-to-ints-proof.vuo` -> `my_Batch19ColorListToIntsProof.intList`; Vuo-saved image `artifacts/vuo_cli/batch-19-color-list-to-ints-vuo-save.png` | done |

## Batch Notes

- `my_ColorListToInts` preserves TiXL output modes: `0=RGBA`, `1=ARGB`, `2=RGB`, `3=R`, `4=A`.
- Channel values are converted as `(int)(channel * 255).Clamp(0,255)`: this truncates after clamp and does not round.
- Null or empty input color lists are skipped. With no connected color lists declared, the result is an empty int list.
- The Vuo proof uses Batch 18 `my_ColorsToList` to create a visible color list, then converts it through `my_ColorListToInts` and renders list length/sum/max as bars.
- Vuo CLI proof `batch-19-color-list-to-ints-proof` compiled and linked with return code `0`, loaded both new custom nodes plus `my_ColorsToList`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-19-color-list-to-ints-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.237349`, bright ratio `0.399713`, `mostlyBlack=false`.
