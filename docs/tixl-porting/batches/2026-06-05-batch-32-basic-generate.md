# Batch 32 Basic Image Generate Acceptance Matrix

Scope: `Lib.image.generate.basic` procedural Texture2D nodes that can be shown through a bounded Vuo image body layer. This batch skips already-built `ConstantImage` / `RenderTarget` and leaves more complex shape generators for the next batch.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.generate.basic.CheckerBoard` | C bounded shader adapter | `my_CheckerBoard` | `vuo-nodes/my.image.generate.basic.checkerBoard.c` | C# `external/tixl/Operators/Lib/image/generate/basic/CheckerBoard.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/CheckerBoard.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/CheckerBoard.hlsl` | `tests/tixl_batch32_basic_generate_semantics.test.js`; `tests/tixl_batch32_basic_generate_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-32-basic-generate-proof.vuo` -> first band shows alternating checker cells | done |
| `Lib.image.generate.basic.LinearGradient` | C bounded shader adapter | `my_LinearGradient` | `vuo-nodes/my.image.generate.basic.linearGradient.c` | C# `external/tixl/Operators/Lib/image/generate/basic/LinearGradient.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/LinearGradient.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/LinearGradient.hlsl` | same tests | same proof -> second band shows rotated black-to-white gradient | done |
| `Lib.image.generate.basic.RadialGradient` | C bounded shader adapter | `my_RadialGradient` | `vuo-nodes/my.image.generate.basic.radialGradient.c` | C# `external/tixl/Operators/Lib/image/generate/basic/RadialGradient.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/RadialGradient.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl` | same tests | same proof -> third band shows radial white-to-black falloff | done |
| `Lib.image.generate.basic.RoundedRect` | C bounded shader adapter | `my_RoundedRect` | `vuo-nodes/my.image.generate.basic.roundedRect.c` | C# `external/tixl/Operators/Lib/image/generate/basic/RoundedRect.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/RoundedRect.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl` | same tests | same proof -> fourth band shows rounded/stroked rectangle SDF | done |

## Proof Notes

- Primary output type for all four manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- `LinearGradient` and `RadialGradient` do not yet carry TiXL's full `Gradient` datatype; this Vuo body layer exposes the default two-stop gradient as `colorA/colorB` and marks that boundary in node metadata.
- Mipmap, DXGI texture format, blend-over-source image, and TiXL sampler details are bounded adapter limits in this batch.
- Vuo CLI proof `batch-32-basic-generate-proof` compiled and linked with return code `0`, loaded all four manufactured nodes plus proof-only `my.image.batch.batch32BasicGenerateProof`, opened an onscreen runner window, and captured screenshot evidence at `artifacts/vuo_cli/batch-32-basic-generate-proof.run.png`.
- Screenshot visual stats: `mostlyBlack=false`, average luma `0.371621`, bright ratio `0.65749`. Non-fatal warnings were limited to missing Vuo Pro license, module/cache/runtime state noise, and a logged zero-texture warning while the proof still rendered non-black output.
