# Batch 40 Lib.image.fx.distort Acceptance Matrix

Scope: finish `Lib.image.fx.distort`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.fx.distort.BubbleZoom` | bounded body-layer adapter | `my_BubbleZoom` | `vuo-nodes/my.image.fx.distort.bubbleZoom.c` | C# `external/tixl/Operators/Lib/image/fx/distort/BubbleZoom.cs`; `.t3` `BubbleZoom.t3`; shader/resource `BubbleZoom.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.ChromaticDistortion` | bounded body-layer adapter | `my_ChromaticDistortion` | `vuo-nodes/my.image.fx.distort.chromaticDistortion.c` | C# `external/tixl/Operators/Lib/image/fx/distort/ChromaticDistortion.cs`; `.t3` `ChromaticDistortion.t3`; shader/resource `ChromaticDistortion.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.Displace` | bounded body-layer adapter | `my_Displace` | `vuo-nodes/my.image.fx.distort.displace.c` | C# `external/tixl/Operators/Lib/image/fx/distort/Displace.cs`; `.t3` `Displace.t3`; shader/resource `Displace.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.DistortAndShade` | bounded body-layer adapter | `my_DistortAndShade` | `vuo-nodes/my.image.fx.distort.distortAndShade.c` | C# `external/tixl/Operators/Lib/image/fx/distort/DistortAndShade.cs`; `.t3` `DistortAndShade.t3`; shader/resource `DistortAndShade.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.EdgeRepeat` | bounded body-layer adapter | `my_EdgeRepeat` | `vuo-nodes/my.image.fx.distort.edgeRepeat.c` | C# `external/tixl/Operators/Lib/image/fx/distort/EdgeRepeat.cs`; `.t3` `EdgeRepeat.t3`; shader/resource `EdgeRepeat.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.FieldToImage` | bounded body-layer adapter | `my_FieldToImage` | `vuo-nodes/my.image.fx.distort.fieldToImage.c` | C# `external/tixl/Operators/Lib/image/fx/distort/FieldToImage.cs`; `.t3` `FieldToImage.t3`; shader/resource `FieldToImageTemplate.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.KochKaleidoskope` | bounded body-layer adapter | `my_KochKaleidoskope` | `vuo-nodes/my.image.fx.distort.kochKaleidoskope.c` | C# `external/tixl/Operators/Lib/image/fx/distort/KochKaleidoskope.cs`; `.t3` `KochKaleidoskope.t3`; shader/resource `KochKaleidoscope.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.PolarCoordinates` | bounded body-layer adapter | `my_PolarCoordinates` | `vuo-nodes/my.image.fx.distort.polarCoordinates.c` | C# `external/tixl/Operators/Lib/image/fx/distort/PolarCoordinates.cs`; `.t3` `PolarCoordinates.t3`; shader/resource `PolarCoordinates.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |
| `Lib.image.fx.distort.TimeDisplace` | bounded body-layer adapter | `my_TimeDisplace` | `vuo-nodes/my.image.fx.distort.timeDisplace.c` | C# `external/tixl/Operators/Lib/image/fx/distort/TimeDisplace.cs`; `.t3` `TimeDisplace.t3`; shader/resource `TimeDisplace.hlsl` | Batch 40 tests | `vuo-compositions/generated/myworld-batch-40-distort-proof.vuo` | done |

## Proof Notes

- Texture-producing nodes use TiXL `ColorForTextures #9F008A`.
- Nodes whose TiXL primary force is command, IO, shadergraph field, compute state, history, or secondary GPU resources are accepted as bounded Vuo body-layer adapters only.
- Vuo CLI proof target: `batch-40-distort-proof`.
