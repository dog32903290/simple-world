# Batch 42 Lib.image.analyze Acceptance Matrix

Scope: finish `Lib.image.analyze`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.analyze.CompareImages` | bounded body-layer adapter | `my_CompareImages` | `vuo-nodes/my.image.analyze.compareImages.c` | C# `external/tixl/Operators/Lib/image/analyze/CompareImages.cs`; `.t3` `CompareImages.t3`; shader/resource `CompareImages.cs` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.DetectMotion` | bounded body-layer adapter | `my_DetectMotion` | `vuo-nodes/my.image.analyze.detectMotion.c` | C# `external/tixl/Operators/Lib/image/analyze/DetectMotion.cs`; `.t3` `DetectMotion.t3`; shader/resource `DetectMotion.cs` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.GetImageBrightness` | bounded body-layer adapter | `my_GetImageBrightness` | `vuo-nodes/my.image.analyze.getImageBrightness.c` | C# `external/tixl/Operators/Lib/image/analyze/GetImageBrightness.cs`; `.t3` `GetImageBrightness.t3`; shader/resource `cs-GetImageBrightness.hlsl` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.ImageLevels` | bounded body-layer adapter | `my_ImageLevels` | `vuo-nodes/my.image.analyze.imageLevels.c` | C# `external/tixl/Operators/Lib/image/analyze/ImageLevels.cs`; `.t3` `ImageLevels.t3`; shader/resource `ImageLevels.hlsl` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.OpticalFlow` | bounded body-layer adapter | `my_OpticalFlow` | `vuo-nodes/my.image.analyze.opticalFlow.c` | C# `external/tixl/Operators/Lib/image/analyze/OpticalFlow.cs`; `.t3` `OpticalFlow.t3`; shader/resource `OpticalFlowKanade.hlsl` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.RemoveStaticBackground` | bounded body-layer adapter | `my_RemoveStaticBackground` | `vuo-nodes/my.image.analyze.removeStaticBackground.c` | C# `external/tixl/Operators/Lib/image/analyze/RemoveStaticBackground.cs`; `.t3` `RemoveStaticBackground.t3`; shader/resource `remove-static-background-cs1-learning.hlsl` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |
| `Lib.image.analyze.WaveForm` | bounded body-layer adapter | `my_WaveForm` | `vuo-nodes/my.image.analyze.waveForm.c` | C# `external/tixl/Operators/Lib/image/analyze/WaveForm.cs`; `.t3` `WaveForm.t3`; shader/resource `waveform-cs.hlsl` | Batch 42 tests | `vuo-compositions/generated/myworld-batch-42-analyze-proof.vuo` | done |

## Proof Notes

- Texture-producing nodes use TiXL `ColorForTextures #9F008A`.
- Nodes whose TiXL primary force is command, IO, shadergraph field, compute state, history, or secondary GPU resources are accepted as bounded Vuo body-layer adapters only.
- Vuo CLI proof target: `batch-42-analyze-proof`.
