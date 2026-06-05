# Batch 25 Pick Color From Image Acceptance Matrix

Scope: one texture/resource readback color node: `PickColorFromImage`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: TiXL `Texture2D` maps to `VuoImage`. This node uses `VuoImage_getBuffer(..., GL_RGBA)` to read a CPU RGBA buffer and samples clamped integer pixel coordinates from normalized `Position`.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.PickColorFromImage` | C resource adapter | `my_PickColorFromImage` | `vuo-nodes/my.numbers.color.pickColorFromImage.c` | C# `external/tixl/Operators/Lib/numbers/color/PickColorFromImage.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/PickColorFromImage.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` | `tests/tixl_batch25_pick_color_from_image_semantics.test.js`; `tests/tixl_batch25_pick_color_from_image_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-25-pick-color-from-image-proof.vuo` -> `my_Batch25PickColorFromImageProof.pickedColor`; Vuo-saved image `artifacts/vuo_cli/batch-25-pick-color-from-image-vuo-save.png` | done |

## Batch Notes

- `my_PickColorFromImage` preserves TiXL defaults: `InputImage=null`, `Position=(0, 0)`, and `AlwaysUpdate=false`.
- Normalized position maps to integer pixels with `(int)(position * dimension)`, then clamps to `[0, dimension - 1]`.
- If `InputImage` is null or CPU readback fails, the Vuo node returns its previous output, matching TiXL's early return behavior.
- The Vuo node keeps a cached CPU RGBA buffer. It refreshes when `AlwaysUpdate=true` or dimensions change, matching TiXL's staging-texture cache boundary as closely as Vuo exposes it.
- This is a C/resource adapter because TiXL supports several Direct3D texture formats. The Vuo node reads RGBA bytes from `VuoImage_getBuffer`; format-specific Direct3D read paths are not one-to-one in Vuo.
- Vuo CLI proof `batch-25-pick-color-from-image-proof` compiled and linked with return code `0`, loaded `my.numbers.color.pickColorFromImage`, `my.numbers.batch.batch25PickColorFromImageSource`, and `my.numbers.batch.batch25PickColorFromImageProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-25-pick-color-from-image-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `960x540`, average luma `0.435736`, bright ratio `1.0`, `mostlyBlack=false`.
