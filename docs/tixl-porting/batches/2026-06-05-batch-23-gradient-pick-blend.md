# Batch 23 Gradient Pick Blend Acceptance Matrix

Scope: two gradient routing/mixing nodes: `PickGradient` and `BlendGradients`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: this batch reuses the Batch 22 bounded gradient adapter where TiXL `Gradient` is carried as `VuoList_VuoColor` + `VuoList_VuoReal` positions + `VuoInteger` interpolation enum.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.numbers.color.PickGradient` | B adapter | `my_PickGradient` | `vuo-nodes/my.numbers.color.pickGradient.c` | C# `external/tixl/Operators/Lib/numbers/color/PickGradient.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/PickGradient.t3`; helper `external/tixl/Core/Utils/MathUtils.cs` | `tests/tixl_batch23_gradient_pick_blend_semantics.test.js`; `tests/tixl_batch23_gradient_pick_blend_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-23-gradient-pick-blend-proof.vuo` -> `my_SampleGradient` and `my_Batch23GradientPickBlendProof.pickedSampleColor`; Vuo-saved image `artifacts/vuo_cli/batch-23-gradient-pick-blend-vuo-save.png` | done |
| `Lib.numbers.color.BlendGradients` | B adapter | `my_BlendGradients` | `vuo-nodes/my.numbers.color.blendGradients.c` | C# `external/tixl/Operators/Lib/numbers/color/BlendGradients.cs`; `.t3` `external/tixl/Operators/Lib/numbers/color/BlendGradients.t3`; helper `external/tixl/Core/DataTypes/Gradient.cs` | `tests/tixl_batch23_gradient_pick_blend_semantics.test.js`; `tests/tixl_batch23_gradient_pick_blend_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-23-gradient-pick-blend-proof.vuo` -> `my_SampleGradient` and `my_Batch23GradientPickBlendProof.blendedSampleColor`; Vuo-saved image `artifacts/vuo_cli/batch-23-gradient-pick-blend-vuo-save.png` | done |

## Batch Notes

- `my_PickGradient` preserves TiXL positive modulo indexing. With zero connected gradients, TiXL returns without changing `Selected.Value`; Vuo exposes this cache behavior through explicit `previousSelected*` inputs.
- `PickGradient` uses a bounded three-gradient input adapter for TiXL `MultiInputSlot<Gradient>`, with `inputCount` declaring how many leading gradient payloads are connected.
- `my_BlendGradients` preserves TiXL blend modes: `0=Normal`, `1=Multiply`, `2=Screen`, `3=Mix`.
- `BlendGradients` clamps `MixFactor` to `[0, 1]`, blends over the union of A/B step positions, samples the opposite gradient at each position, sorts the result positions, and sets result interpolation to Linear.
- `BlendGradients` sampling is Linear within this Vuo body-layer because Batch 22's bounded gradient payload does not yet carry TiXL spline cache state; this matches the tested Batch 22 adapter boundary.
- `GradientsToTexture` is still not included because it emits `Texture2D` and belongs in the image/resource batch after gradient data nodes are stable.
- Vuo CLI proof `batch-23-gradient-pick-blend-proof` compiled and linked with return code `0`, loaded `my.numbers.color.pickGradient`, `my.numbers.color.blendGradients`, `my.numbers.batch.batch23GradientPickBlendProof`, and supporting Batch 22 gradient nodes, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-23-gradient-pick-blend-vuo-save.png`.
- Current macOS window capture failed with `could not create image from window`, matching previous batch reruns; this remains classified as a capture-layer issue. The Vuo-native saved image is non-black: `960x540`, average luma `0.555497`, bright ratio `1.0`, `mostlyBlack=false`.
