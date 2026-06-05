# Batch 30 Image Use Post-FX Acceptance Matrix

Scope: three post-fx Texture2D nodes from `Operators/Lib/image/use`: `Fxaa`, `NormalMap`, and `DepthBufferAsGrayScale`.

Gate: every row needs TiXL source/default evidence, semantic edge cases, Vuo C source, installation into the Vuo user module folder, and a Vuo-visible proof that consumes the exact `my_<TiXLName>` node output.

Vuo body-layer note: this batch is explicitly bounded. `Fxaa` uses a Vuo local-contrast smoothing approximation rather than a full FXAA 3.11 preset compile path. `NormalMap` ports the visible neighbor-gradient shader law for modes `0..3`. `DepthBufferAsGrayScale` preserves the visible depth-to-gray formula in a fragment shader, not TiXL's compute/UAV pipeline or float texture precision.

| TiXL node | grade | Vuo title | Vuo source | source evidence | semantic/source tests | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.use.Fxaa` | C bounded shader adapter | `my_Fxaa` | `vuo-nodes/my.image.use.fxaa.c` | C# `external/tixl/Operators/Lib/image/use/Fxaa.cs`; `.t3` `external/tixl/Operators/Lib/image/use/Fxaa.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/use/FXAA.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/Fxaa.md` | `tests/tixl_batch30_image_use_postfx_semantics.test.js`; `tests/tixl_batch30_image_use_postfx_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-30-image-use-postfx-proof.vuo` -> first band from generated high-contrast pattern through `my_Fxaa` | done |
| `Lib.image.use.NormalMap` | C shader adapter | `my_NormalMap` | `vuo-nodes/my.image.use.normalMap.c` | C# `external/tixl/Operators/Lib/image/use/NormalMap.cs`; `.t3` `external/tixl/Operators/Lib/image/use/NormalMap.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/NormalMap.md` | `tests/tixl_batch30_image_use_postfx_semantics.test.js`; `tests/tixl_batch30_image_use_postfx_vuo_nodes.test.js` | same proof -> second band blue-purple normal-map output from generated luminance pattern | done |
| `Lib.image.use.DepthBufferAsGrayScale` | C bounded shader adapter | `my_DepthBufferAsGrayScale` | `vuo-nodes/my.image.use.depthBufferAsGrayScale.c` | C# `external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.cs`; `.t3` `external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl`; docs `external/tixl/.help/docs/operators/lib/image/use/DepthBufferAsGrayScale.md` | `tests/tixl_batch30_image_use_postfx_semantics.test.js`; `tests/tixl_batch30_image_use_postfx_vuo_nodes.test.js` | same proof -> third band gray output from synthetic depth gradient | done |

## Batch Notes

- `my_Fxaa` preserves TiXL defaults `Preset=0`, `KeepAlpha=false`, and the `KeepAlpha` output alpha law. Full TiXL FXAA preset parity is not claimed; this Vuo body uses a bounded local-contrast smoothing approximation.
- `my_NormalMap` preserves TiXL defaults `Impact=1`, `SampleRadius=2`, `Resolution=(0,0)`, `Twist=180`, and `Mode=0`. It ports the visible neighbor-gradient normal conversion for modes `0..3`; DX11 texture repeat/output format are bounded limits.
- `my_DepthBufferAsGrayScale` preserves TiXL defaults `NearFarRange=(0.01,1000)`, `OutputRange=(0,5)`, `ClampOutput=false`, `Mode=0`, plus the negative-depth checker sentinel. Vuo output is 8-bit visible image proof, not TiXL `R16G16B16A16_Float` compute/UAV equivalence.
- Vuo CLI proof `batch-30-image-use-postfx-proof` compiled and linked with return code `0`, loaded all three manufactured nodes plus proof-only `my.image.batch.batch30PostfxSource` and `my.image.batch.batch30ImageUsePostfxProof`, opened an onscreen runner window, and produced Vuo-rendered PNG evidence at `artifacts/vuo_cli/batch-30-image-use-postfx-vuo-save.png`.
- Per current acceptance rule, macOS window screenshot capture is skipped as a known permission-layer issue. Vuo-native saved PNG is the visual proof: `480x160`, average luma `0.429468`, bright ratio `1.0`, `mostlyBlack=false`.

## Remaining Image/Use Risk Notes

- `KeepInTextureArray` requires persistent texture-array memory and slice read/write semantics. It should not be treated as a simple VuoImage shader node.
- `UseTextureReference` depends on TiXL `RenderTargetReference` state and feedback handoff. Exact Vuo parity is blocked without a bounded feedback proof contract.
- `CustomPixelShader` requires dynamic HLSL/template compilation and additional shader resources/buffers/samplers. Only the default snippet could be bounded in a first pass.
- `RenderWithMotionBlur` re-evaluates upstream graph time across passes. A texture smear would not prove TiXL behavior.
