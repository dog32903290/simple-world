# Batch 29 Image Use Material Channels Acceptance Matrix

Scope: two Texture2D material channel packing nodes from `Operators/Lib/image/use`: `CombineMaterialChannels2` and `CombineMaterialChannels`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: `CombineMaterialChannels2` is the same channel-selector shader law as `Combine3Images`. `CombineMaterialChannels` preserves the current TiXL HLSL packing law for roughness/metallic/occlusion, with the TiXL default identity `RemapRoughness` curve baked in. Full editable Curve input, DX11 sampler behavior, render target details, and mip generation are bounded body-layer limits.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.use.CombineMaterialChannels2` | C shader adapter | `my_CombineMaterialChannels2` | `vuo-nodes/my.image.use.combineMaterialChannels2.c` | C# `external/tixl/Operators/Lib/image/use/CombineMaterialChannels2.cs`; `.t3` `external/tixl/Operators/Lib/image/use/CombineMaterialChannels2.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/CombineMaterialChannels2.md` | `tests/tixl_batch29_image_use_material_channels_semantics.test.js`; `tests/tixl_batch29_image_use_material_channels_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-29-image-use-material-channels-proof.vuo` -> first band white from default R/G/B channel selection across red/green/blue inputs | done |
| `Lib.image.use.CombineMaterialChannels` | C shader adapter | `my_CombineMaterialChannels` | `vuo-nodes/my.image.use.combineMaterialChannels.c` | C# `external/tixl/Operators/Lib/image/use/CombineMaterialChannels.cs`; `.t3` `external/tixl/Operators/Lib/image/use/CombineMaterialChannels.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/use/CombineMaterialChannels.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/CombineMaterialChannels.md` | `tests/tixl_batch29_image_use_material_channels_semantics.test.js`; `tests/tixl_batch29_image_use_material_channels_vuo_nodes.test.js` | same proof -> second band green-turquoise from `roughness.r=0.25`, `metallic.g=0.75`, `occlusion.r=0.5` | done |

## Batch Notes

- `my_CombineMaterialChannels2` preserves TiXL `img-combine-3.hlsl` channel selections and defaults: `SelectChannel_R=0`, `SelectChannel_G=6`, `SelectChannel_B=12`, `SelectAlphaChannel=4`.
- `my_CombineMaterialChannels` preserves TiXL shader packing: roughness comes from `Roughness.r`, metallic from `Metallic.g`, occlusion from `Occlusion.r`, alpha is `1`.
- TiXL fallback values are preserved in the Vuo body: missing roughness -> `0.5`, missing metallic -> `0`, missing occlusion -> `1`.
- TiXL default `RemapRoughness` is an identity curve. The Vuo adapter implements that identity remap and documents editable Curve support as not yet exposed in Vuo.
- Vuo CLI proof `batch-29-image-use-material-channels-proof` compiled and linked with return code `0`, loaded both manufactured nodes plus `my.image.batch.batch29ImageUseMaterialChannelsProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-29-image-use-material-channels-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `320x160`, average luma `0.814999`, bright ratio `1.0`, `mostlyBlack=false`.
