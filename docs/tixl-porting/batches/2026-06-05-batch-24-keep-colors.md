# Batch 24 Keep Colors Acceptance Matrix

Scope: one stateful color-list memory node: `KeepColors`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `KeepColors` owns a private `_list`. The Vuo node is a stateful `nodeInstanceEvent` adapter with an instance-owned `VuoList_VuoColor`.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.KeepColors` | B adapter | `my_KeepColors` | `vuo-nodes/my.numbers.color.keepColors.c` | C# `external/tixl/Operators/Lib/numbers/color/KeepColors.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/KeepColors.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` | `tests/tixl_batch24_keep_colors_semantics.test.js`; `tests/tixl_batch24_keep_colors_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-24-keep-colors-proof.vuo` -> `my_Batch24KeepColorsProof.keptColors`; Vuo-saved image `artifacts/vuo_cli/batch-24-keep-colors-vuo-save.png` | done |

## Batch Notes

- `my_KeepColors` preserves TiXL defaults: `Color=white`, `AddColorToList=true`, `MaxLength=100`, and `Reset=false`.
- `Reset` clears the internal list before the optional add, matching TiXL source order. If `Reset=true` and `AddColorToList=true`, the current color remains as the first item after that cook.
- `AddColorToList=true` inserts the new color at the front of the list.
- `MaxLength` is clamped to `[1, 100000]`, then the list is trimmed from the tail.
- `AddColorToList=false` preserves the previous list except for reset/trim pressure.
- Vuo CLI proof `batch-24-keep-colors-proof` compiled and linked with return code `0`, loaded `my.numbers.color.keepColors` and `my.numbers.batch.batch24KeepColorsProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-24-keep-colors-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.640669`, bright ratio `1.0`, `mostlyBlack=false`.
