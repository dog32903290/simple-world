# Batch 26 Gradients To Texture Acceptance Matrix

Scope: one gradient-to-texture resource node: `GradientsToTexture`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: this batch reuses the Batch 22 bounded gradient adapter where TiXL `Gradient` is carried as `VuoList_VuoColor` + `VuoList_VuoReal` positions + `VuoInteger` interpolation enum. TiXL emits `R32G32B32A32_Float`; the Vuo body emits an 8-bit RGBA `VuoImage` for visible Vuo validation.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.GradientsToTexture` | C resource adapter | `my_GradientsToTexture` | `vuo-nodes/my.numbers.color.gradientsToTexture.c` | C# `external/tixl/Operators/Lib/numbers/color/GradientsToTexture.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/GradientsToTexture.t3`; helper `external/tixl/Core/DataTypes/Gradient.cs` | `tests/tixl_batch26_gradients_to_texture_semantics.test.js`; `tests/tixl_batch26_gradients_to_texture_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-26-gradients-to-texture-proof.vuo` -> `my_GradientsToTexture.GradientsTexture`; Vuo-saved image `artifacts/vuo_cli/batch-26-gradients-to-texture-vuo-save.png` | done |

## Batch Notes

- `my_GradientsToTexture` preserves TiXL defaults: `Resolution=256`, `Direction=0`, and the default single linear gradient.
- `Resolution` is clamped to `[1, 16384]`.
- `Direction=0` maps to TiXL horizontal layout: width = resolution, height = gradient count.
- `Direction=1` maps to TiXL vertical layout: width = gradient count, height = resolution.
- This Vuo adapter supports up to three bounded gradient inputs via `inputCount`.
- TiXL writes `R32G32B32A32_Float`; Vuo proof output is `VuoImageColorDepth_8`. This is a body-layer limit for visible validation, not a claim of float-texture equivalence.
- Vuo CLI proof `batch-26-gradients-to-texture-proof` compiled and linked with return code `0`, loaded `my.numbers.color.gradientsToTexture` and supporting Batch 22 gradient nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-26-gradients-to-texture-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `256x2`, average luma `0.604895`, bright ratio `1.0`, `mostlyBlack=false`.
