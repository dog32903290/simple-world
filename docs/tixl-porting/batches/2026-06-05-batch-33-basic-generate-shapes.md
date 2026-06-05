# Batch 33 Basic Image Generate Shapes Acceptance Matrix

Scope: finish the remaining `Lib.image.generate.basic` procedural shape nodes after Batch 32.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.generate.basic.Blob` | C bounded shader adapter | `my_Blob` | `vuo-nodes/my.image.generate.basic.blob.c` | C# `external/tixl/Operators/Lib/image/generate/basic/Blob.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/Blob.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/Blob.hlsl` | `tests/tixl_batch33_basic_generate_shapes_semantics.test.js`; `tests/tixl_batch33_basic_generate_shapes_vuo_nodes.test.js` | `vuo-compositions/generated/myworld-batch-33-basic-generate-shapes-proof.vuo` -> first band shows blob falloff | done |
| `Lib.image.generate.basic.BoxGradient` | C bounded shader adapter | `my_BoxGradient` | `vuo-nodes/my.image.generate.basic.boxGradient.c` | C# `external/tixl/Operators/Lib/image/generate/basic/BoxGradient.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/BoxGradient.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/BoxGradient.hlsl` | same tests | same proof -> second band shows rounded-box gradient | done |
| `Lib.image.generate.basic.NGon` | C bounded shader adapter | `my_NGon` | `vuo-nodes/my.image.generate.basic.nGon.c` | C# `external/tixl/Operators/Lib/image/generate/basic/NGon.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/NGon.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/NGon.hlsl` | same tests | same proof -> third band shows regular polygon fill | done |
| `Lib.image.generate.basic.NGonGradient` | C bounded shader adapter | `my_NGonGradient` | `vuo-nodes/my.image.generate.basic.nGonGradient.c` | C# `external/tixl/Operators/Lib/image/generate/basic/NGonGradient.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/basic/NGonGradient.t3`; shader `external/tixl/Operators/Lib/Assets/shaders/img/generate/NGonGradient.hlsl` | same tests | same proof -> fourth band shows polygon distance gradient | done |

## Proof Notes

- Primary output type for all four manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- `my_NGon` and `my_NGonGradient` preserve TiXL capitalization exactly in the creator-facing title.
- `BoxGradient` and `NGonGradient` do not yet carry TiXL's full `Gradient` datatype; this Vuo body layer exposes the default two-stop gradient as `colorA/colorB`.
- Source-image blending, DXGI texture format, mip generation, and full TiXL blend modes are bounded adapter limits in this batch.
- Vuo CLI proof target: `batch-33-basic-generate-shapes-proof`.
- Vuo compile/link proof passed: `compile.returncode=0`, `link.returncode=0`; loaded user nodes included `my.image.generate.basic.blob`, `my.image.generate.basic.boxGradient`, `my.image.generate.basic.nGon`, `my.image.generate.basic.nGonGradient`, and `my.image.batch.batch33BasicGenerateShapesProof`.
- The runner window screenshot step failed on this machine with macOS screen-capture permission (`could not create image from window`), but the Vuo-native save node wrote `artifacts/vuo_cli/batch-33-basic-generate-shapes-vuo-save.png`.
- Saved PNG proof stats: `640x160`, mean luma `109.508`, bright ratio `0.604053`, nontransparent ratio `0.715059`, `mostly_black=false`.
- Non-fatal Vuo stderr still includes the local no-Pro-license notice and the existing GL texture warning seen in nearby image batches.
