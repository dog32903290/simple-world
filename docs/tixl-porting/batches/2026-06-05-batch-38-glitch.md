# Batch 38 Image FX Glitch Acceptance Matrix

Scope: finish `Lib.image.fx.glitch`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.fx.glitch.GlitchDisplace` | C bounded shader adapter | `my_GlitchDisplace` | `vuo-nodes/my.image.fx.glitch.glitchDisplace.c` | C# `external/tixl/Operators/Lib/image/fx/glitch/GlitchDisplace.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/glitch/GlitchDisplace.t3`; shader `Operators/Lib/Assets/shaders/points/draw/GlitchDisplace.hlsl` | Batch 38 tests | `vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo` | done |
| `Lib.image.fx.glitch.RgbTV` | C bounded shader adapter | `my_RgbTV` | `vuo-nodes/my.image.fx.glitch.rgbTv.c` | C# `external/tixl/Operators/Lib/image/fx/glitch/RgbTV.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/glitch/RgbTV.t3`; shader `Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl` | Batch 38 tests | `vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo` | done |
| `Lib.image.fx.glitch.SortPixelGlitch` | D compute/state-heavy bounded adapter | `my_SortPixelGlitch` | `vuo-nodes/my.image.fx.glitch.sortPixelGlitch.c` | C# `external/tixl/Operators/Lib/image/fx/glitch/SortPixelGlitch.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/glitch/SortPixelGlitch.t3`; shader `Operators/Lib/Assets/shaders/img/fx/SortPixelsGlitch-cs.hlsl` | Batch 38 tests | `vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo` | done |
| `Lib.image.fx.glitch.SubdivisionStretch` | C bounded shader adapter | `my_SubdivisionStretch` | `vuo-nodes/my.image.fx.glitch.subdivisionStretch.c` | C# `external/tixl/Operators/Lib/image/fx/glitch/SubdivisionStretch.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/glitch/SubdivisionStretch.t3`; shader `Operators/Lib/Assets/shaders/img/fx/StretchSubdivide.hlsl` | Batch 38 tests | `vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo` | done |

## Proof Notes

- Primary output type is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- `my_GlitchDisplace`, `my_RgbTV`, and `my_SubdivisionStretch` are bounded single-pass shader adapters of their TiXL image effects.
- `my_SortPixelGlitch` is accepted as a bounded visual adapter only: TiXL uses DX11 render targets plus `SortPixelsGlitch-cs.hlsl`, so exact compute-buffer pixel sorting is outside this Vuo body-layer proof.
- Vuo CLI proof target: `batch-38-glitch-proof`.
