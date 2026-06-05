# Batch 28 Image Use Blend Combine Acceptance Matrix

Scope: three Texture2D blend/combine nodes from `Operators/Lib/image/use`: `BlendImages`, `BlendWithMask`, and `Combine3Images`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: these are shader/body adapters. `BlendImages` maps TiXL `MultiInputSlot<Texture2D>` to three bounded `VuoImage` inputs plus `inputCount`. `BlendWithMask` and `Combine3Images` preserve the visible shader law from TiXL HLSL, while DX11 render-target details, mip generation, sampler address modes, and full aspect-fit behavior are bounded body-layer limits.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.use.BlendImages` | C body adapter | `my_BlendImages` | `vuo-nodes/my.image.use.blendImages.c` | C# `external/tixl/Operators/Lib/image/use/BlendImages.cs`; `.t3` `external/tixl/Operators/Lib/image/use/BlendImages.t3`; docs `external/tixl/.help/docs/operators/lib/image/use/BlendImages.md` | `tests/tixl_batch28_image_use_blend_combine_semantics.test.js`; `tests/tixl_batch28_image_use_blend_combine_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-28-image-use-blend-combine-proof.vuo` -> first band teal from `BlendFraction=1.5` between green and blue | done |
| `Lib.image.use.BlendWithMask` | C shader adapter | `my_BlendWithMask` | `vuo-nodes/my.image.use.blendWithMask.c` | C# `external/tixl/Operators/Lib/image/use/BlendWithMask.cs`; `.t3` `external/tixl/Operators/Lib/image/use/BlendWithMask.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/BlendWithMask.md` | `tests/tixl_batch28_image_use_blend_combine_semantics.test.js`; `tests/tixl_batch28_image_use_blend_combine_vuo_nodes.test.js` | same proof -> second band yellow/olive from mask red channel `0.5` between red and green | done |
| `Lib.image.use.Combine3Images` | C shader adapter | `my_Combine3Images` | `vuo-nodes/my.image.use.combine3Images.c` | C# `external/tixl/Operators/Lib/image/use/Combine3Images.cs`; `.t3` `external/tixl/Operators/Lib/image/use/Combine3Images.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/Combine3Images.md` | `tests/tixl_batch28_image_use_blend_combine_semantics.test.js`; `tests/tixl_batch28_image_use_blend_combine_vuo_nodes.test.js` | same proof -> third band white from default R/G/B channel selection across red/green/blue inputs | done |

## Batch Notes

- `my_BlendImages` preserves TiXL's compound law: clamp `BlendFraction` to `[0,10000]`, use `floor(f)` and `floor(f)+1` with positive modulo over connected images, and crossfade by the fractional part.
- `my_BlendWithMask` preserves TiXL shader law: multiply images by `ColorA` / `ColorB`, sample `mask.r`, then `mix(a, b, maskValue)`.
- `my_Combine3Images` preserves TiXL `img-combine-3.hlsl` channel selections: `0..4` from A, `5..9` from B, `10..14` from C; mode `3` is average and mode `4` is brightness `0.239R + 0.686G + 0.075B`. `SelectAlphaChannel=4` sets alpha to one.
- Vuo CLI proof `batch-28-image-use-blend-combine-proof` compiled and linked with return code `0`, loaded all three manufactured nodes plus `my.image.batch.batch28ImageUseBlendCombineProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-28-image-use-blend-combine-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `480x160`, average luma `0.6184`, bright ratio `1.0`, `mostlyBlack=false`.
