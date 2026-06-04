# TiXL Source Inventory

Generated from:

- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/tables/nodes_index.csv`
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/tables/ports_long.csv`
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/FALSE_CLAIMS_AND_GAPS.md`
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/TROUBLESHOOTING_CLONE_NOTES.md`
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/WORKFLOWS_CLONE_NOTES.md`
- source checkouts under `external/tixl` and `external/vuo`

Scope note: this is an inventory over all 935 indexed TiXL nodes. It is not a per-node Node Card pass.

## Global Counts

| metric | count |
|---|---:|
| TiXL nodes in `nodes_index.csv` | 935 |
| Parsed port records in `ports_long.csv` | 12,400 |
| Namespaces including blank namespace | 144 |
| Blank namespace nodes | 56 |
| Nodes with C# source path | 915 |
| Nodes missing C# source path | 20 |
| Nodes with docs source path | 756 |
| Nodes missing docs source path | 179 |
| Nodes with `.t3` path | 915 |
| Nodes missing `.t3` path | 20 |

Source-confidence split:

| confidence | nodes | working meaning |
|---|---:|---|
| `doc_and_csharp_verified` | 579 | Best inventory tier: docs and C# match at node level. Still inspect ports/defaults before writing a Node Card. |
| `csharp_verified_no_doc_record` | 179 | Source interface exists, but no docs record. Treat purpose and user-facing semantics as Unknown unless C# behavior is inspected. |
| `doc_verified_no_csharp_match` | 177 | Docs record exists, but source match is mismatched or missing in the generated index. Do not claim runtime behavior without direct source lookup. |

Top TiXL port types from C# port records:

| type | C# port rows | first-pass Vuo risk |
|---|---:|---|
| `float` | 1,933 | Low; candidate `VuoReal`, but check float/double tolerance and range metadata. |
| `int` | 924 | Low; candidate `VuoInteger`, but enum-like `int` needs allowed values. |
| `bool` | 743 | Low; candidate `VuoBoolean`. |
| `Texture2D` | 417 | Medium/high; candidate `VuoImage`, but TiXL is Direct3D texture-backed while Vuo image is OpenGL-backed. |
| `Vector3` | 399 | Low/medium; candidate `VuoPoint3d`, but semantic role can be point, vector, Euler rotation, scale, or color-like triple. |
| `Vector2` | 356 | Low/medium; candidate `VuoPoint2d`; same semantic-role caveat. |
| `Vector4` | 286 | Medium; candidate `VuoPoint4d` or `VuoColor`. Must decide by port role and docs. |
| `BufferWithViews` | 281 | High; TiXL GPU buffer/point/mesh attribute container. No direct Vuo type. |
| `string` | 272 | Low; candidate `VuoText`. |
| `Command` | 259 | High; TiXL render command graph, not a Vuo value type. Usually maps to `VuoSceneObject`, `VuoLayer`, or a composition structure, not a node-local type. |
| `ShaderGraphNode` | 114 | High; TiXL SDF/field shader graph node. No direct Vuo type. |
| `MeshBuffers` | 90 | Medium/high; candidate `VuoMesh`, but TiXL buffer layout and Vuo mesh semantics differ. |

Direct-port blockers observed in port rows:

| family | C# port rows | examples |
|---|---:|---|
| `Buffer` / `BufferWithViews` / `MeshBuffers` | 403 | `AnalyzeMeshBuffers`, `_AssembleMeshBuffers`, point generators/modifiers/drawers |
| `Command` | 259 | `DrawMesh`, `Group`, `Camera`, `RenderTarget` style render chains |
| `ShaderGraphNode` | 114 | SDF/field nodes such as `AbsoluteSDF`, `SphereSDF`, `RaymarchField` chains |
| Direct3D/DXGI/ShaderResourceView/UAV/RTV/shader-stage types | 112+ | `Lib.render._dx11.api`, `Lib.render._dx11.fxsetup`, internal image FX setup nodes |
| `ParticleSystem` | 22 | `Lib.particle` and `Lib.particle.force` |
| `Texture3dWithViews` | 3 | DX11 3D texture helpers |
| `SceneSetup`, `PbrMaterial`, `RenderTargetReference` | 11 | scene/material/reference state that needs a Vuo-specific design |

## Namespace Inventory

`C#`, `docs`, and `.t3` are node counts with a source path in `nodes_index.csv`. `blank?` means the index namespace field is empty, not that the node is invalid.

| namespace | nodes | C# | docs | .t3 | blank? | source confidence |
|---|---:|---:|---:|---:|---|---|
| `_blank` | 56 | 56 | 0 | 56 | yes | csharp_verified_no_doc_record:56 |
| `Lib.data.object` | 1 | 1 | 1 | 1 | no | doc_verified_no_csharp_match:1 |
| `Lib.field.adjust` | 7 | 7 | 7 | 7 | no | doc_and_csharp_verified:7 |
| `Lib.field.adjust._` | 3 | 3 | 0 | 3 | no | csharp_verified_no_doc_record:3 |
| `Lib.field.analyze` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.field.combine` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.field.generate.sdf` | 17 | 17 | 17 | 17 | no | doc_and_csharp_verified:17 |
| `Lib.field.generate.sdf._` | 3 | 3 | 0 | 3 | no | csharp_verified_no_doc_record:3 |
| `Lib.field.generate.texture` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.field.generate.vec3` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.field.render` | 3 | 3 | 3 | 3 | no | doc_and_csharp_verified:3 |
| `Lib.field.render._` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.field.space` | 12 | 12 | 12 | 12 | no | doc_and_csharp_verified:12 |
| `Lib.field.space._` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.field.use` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:5 |
| `Lib.flow` | 11 | 11 | 11 | 11 | no | doc_and_csharp_verified:11 |
| `Lib.flow.context` | 18 | 18 | 18 | 18 | no | doc_and_csharp_verified:18 |
| `Lib.flow.skillQuest` | 4 | 4 | 3 | 4 | no | doc_verified_no_csharp_match:3, csharp_verified_no_doc_record:1 |
| `Lib.image.analyze` | 7 | 7 | 7 | 7 | no | doc_and_csharp_verified:6, doc_verified_no_csharp_match:1 |
| `Lib.image.color` | 11 | 11 | 11 | 11 | no | doc_and_csharp_verified:11 |
| `Lib.image.fx._` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.image.fx._obsolete` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.image.fx.blur` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:5 |
| `Lib.image.fx.distort` | 9 | 9 | 9 | 9 | no | doc_and_csharp_verified:9 |
| `Lib.image.fx.feedback` | 7 | 7 | 7 | 7 | no | doc_and_csharp_verified:7 |
| `Lib.image.fx.glitch` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.image.fx.stylize` | 15 | 15 | 15 | 15 | no | doc_and_csharp_verified:15 |
| `Lib.image.generate` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.image.generate._obsolete` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.image.generate.basic` | 9 | 9 | 9 | 9 | no | doc_and_csharp_verified:9 |
| `Lib.image.generate.fractal` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.image.generate.load` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.image.generate.misc` | 3 | 3 | 3 | 3 | no | doc_and_csharp_verified:2, doc_verified_no_csharp_match:1 |
| `Lib.image.generate.noise` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:5 |
| `Lib.image.generate.pattern` | 9 | 9 | 9 | 9 | no | doc_and_csharp_verified:9 |
| `Lib.image.transform` | 6 | 6 | 6 | 6 | no | doc_and_csharp_verified:5, doc_verified_no_csharp_match:1 |
| `Lib.image.use` | 19 | 19 | 18 | 19 | no | doc_and_csharp_verified:18, csharp_verified_no_doc_record:1 |
| `Lib.io.audio` | 5 | 5 | 5 | 5 | no | doc_verified_no_csharp_match:3, doc_and_csharp_verified:2 |
| `Lib.io.audio._` | 4 | 4 | 0 | 4 | no | csharp_verified_no_doc_record:4 |
| `Lib.io.audio._obsolete` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.io.data` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.io.dmx` | 6 | 6 | 6 | 6 | no | doc_and_csharp_verified:4, doc_verified_no_csharp_match:2 |
| `Lib.io.dmx.helpers` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:1, doc_verified_no_csharp_match:1 |
| `Lib.io.dmx.obsolete` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.io.file` | 3 | 3 | 3 | 3 | no | doc_and_csharp_verified:3 |
| `Lib.io.freed` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.io.http` | 1 | 0 | 1 | 0 | no | doc_verified_no_csharp_match:1 |
| `Lib.io.input` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.io.json` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.io.midi` | 10 | 10 | 10 | 10 | no | doc_and_csharp_verified:10 |
| `Lib.io.osc` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.io.posistage` | 2 | 2 | 2 | 2 | no | doc_verified_no_csharp_match:2 |
| `Lib.io.ptz` | 2 | 2 | 2 | 2 | no | doc_verified_no_csharp_match:2 |
| `Lib.io.serial` | 3 | 3 | 2 | 3 | no | doc_and_csharp_verified:2, csharp_verified_no_doc_record:1 |
| `Lib.io.tcp` | 2 | 2 | 2 | 2 | no | doc_verified_no_csharp_match:2 |
| `Lib.io.udp` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.io.video` | 13 | 7 | 12 | 7 | no | doc_verified_no_csharp_match:9, doc_and_csharp_verified:3, csharp_verified_no_doc_record:1 |
| `Lib.io.video.mediapipe` | 7 | 0 | 7 | 0 | no | doc_verified_no_csharp_match:7 |
| `Lib.io.websocket` | 2 | 0 | 2 | 0 | no | doc_verified_no_csharp_match:2 |
| `Lib.mesh._` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.mesh.draw` | 8 | 8 | 8 | 8 | no | doc_and_csharp_verified:7, doc_verified_no_csharp_match:1 |
| `Lib.mesh.generate` | 11 | 11 | 11 | 11 | no | doc_and_csharp_verified:11 |
| `Lib.mesh.modify` | 25 | 25 | 25 | 25 | no | doc_and_csharp_verified:23, doc_verified_no_csharp_match:2 |
| `Lib.numbers.anim` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.numbers.anim._obsolete` | 7 | 7 | 0 | 7 | no | csharp_verified_no_doc_record:7 |
| `Lib.numbers.anim.animators` | 10 | 8 | 10 | 8 | no | doc_and_csharp_verified:8, doc_verified_no_csharp_match:2 |
| `Lib.numbers.anim.time` | 13 | 13 | 13 | 13 | no | doc_and_csharp_verified:13 |
| `Lib.numbers.anim.utils` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.numbers.anim.vj` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.numbers.bool.combine` | 4 | 4 | 4 | 4 | no | doc_verified_no_csharp_match:4 |
| `Lib.numbers.bool.convert` | 2 | 2 | 2 | 2 | no | doc_verified_no_csharp_match:2 |
| `Lib.numbers.bool.logic` | 9 | 9 | 9 | 9 | no | doc_verified_no_csharp_match:9 |
| `Lib.numbers.bool.process` | 4 | 4 | 4 | 4 | no | doc_verified_no_csharp_match:4 |
| `Lib.numbers.color` | 14 | 14 | 14 | 14 | no | doc_and_csharp_verified:14 |
| `Lib.numbers.curve` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.numbers.data._obsolete` | 6 | 6 | 0 | 6 | no | csharp_verified_no_doc_record:6 |
| `Lib.numbers.data.utils` | 7 | 7 | 7 | 7 | no | doc_and_csharp_verified:7 |
| `Lib.numbers.float.adjust` | 8 | 8 | 8 | 8 | no | doc_verified_no_csharp_match:8 |
| `Lib.numbers.float.basic` | 9 | 9 | 9 | 9 | no | doc_verified_no_csharp_match:9 |
| `Lib.numbers.float.logic` | 7 | 7 | 7 | 7 | no | doc_verified_no_csharp_match:7 |
| `Lib.numbers.float.process` | 14 | 14 | 14 | 14 | no | doc_verified_no_csharp_match:14 |
| `Lib.numbers.float.random` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:3 |
| `Lib.numbers.float.trigonometry` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:3 |
| `Lib.numbers.floats.basic` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.numbers.floats.conversion` | 3 | 3 | 3 | 3 | no | doc_and_csharp_verified:2, doc_verified_no_csharp_match:1 |
| `Lib.numbers.floats.io` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.numbers.floats.logic` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.numbers.floats.process` | 16 | 16 | 16 | 16 | no | doc_and_csharp_verified:15, doc_verified_no_csharp_match:1 |
| `Lib.numbers.int.basic` | 10 | 10 | 10 | 10 | no | doc_verified_no_csharp_match:10 |
| `Lib.numbers.int.logic` | 5 | 5 | 5 | 5 | no | doc_verified_no_csharp_match:5 |
| `Lib.numbers.int.process` | 9 | 9 | 9 | 9 | no | doc_verified_no_csharp_match:9 |
| `Lib.numbers.int2.basic` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.numbers.int2.process` | 5 | 4 | 5 | 4 | no | doc_and_csharp_verified:4, doc_verified_no_csharp_match:1 |
| `Lib.numbers.ints` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:4, doc_verified_no_csharp_match:1 |
| `Lib.numbers.vec2` | 14 | 14 | 14 | 14 | no | doc_and_csharp_verified:14 |
| `Lib.numbers.vec2.process` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:2, doc_and_csharp_verified:1 |
| `Lib.numbers.vec3` | 22 | 22 | 22 | 22 | no | doc_and_csharp_verified:22 |
| `Lib.numbers.vec3.process` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:2, doc_and_csharp_verified:1 |
| `Lib.numbers.vec4` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.particle` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.particle.force` | 18 | 18 | 18 | 18 | no | doc_and_csharp_verified:18 |
| `Lib.point._cpu` | 6 | 6 | 0 | 6 | no | csharp_verified_no_doc_record:6 |
| `Lib.point._experimental` | 8 | 8 | 0 | 8 | no | csharp_verified_no_doc_record:8 |
| `Lib.point._internal` | 10 | 10 | 0 | 10 | no | csharp_verified_no_doc_record:10 |
| `Lib.point._obsolete` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.point.combine` | 8 | 8 | 7 | 8 | no | doc_and_csharp_verified:7, csharp_verified_no_doc_record:1 |
| `Lib.point.draw` | 17 | 16 | 16 | 16 | no | doc_and_csharp_verified:15, csharp_verified_no_doc_record:1, doc_verified_no_csharp_match:1 |
| `Lib.point.draw.legacy` | 3 | 3 | 0 | 3 | no | csharp_verified_no_doc_record:3 |
| `Lib.point.generate` | 19 | 19 | 17 | 19 | no | doc_and_csharp_verified:16, csharp_verified_no_doc_record:2, doc_verified_no_csharp_match:1 |
| `Lib.point.helper` | 7 | 7 | 5 | 7 | no | doc_and_csharp_verified:5, csharp_verified_no_doc_record:2 |
| `Lib.point.io` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:5 |
| `Lib.point.modify` | 22 | 22 | 21 | 22 | no | doc_and_csharp_verified:20, doc_verified_no_csharp_match:1, csharp_verified_no_doc_record:1 |
| `Lib.point.sim` | 7 | 7 | 7 | 7 | no | doc_and_csharp_verified:7 |
| `Lib.point.sim._legacy` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.point.sim.experimental` | 5 | 5 | 5 | 5 | no | doc_and_csharp_verified:5 |
| `Lib.point.transform` | 15 | 15 | 15 | 15 | no | doc_and_csharp_verified:15 |
| `Lib.point.usse` | 1 | 1 | 1 | 1 | no | doc_and_csharp_verified:1 |
| `Lib.render._` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.render._dx11.api` | 23 | 23 | 0 | 23 | no | csharp_verified_no_doc_record:23 |
| `Lib.render._dx11.buffer` | 9 | 9 | 0 | 9 | no | csharp_verified_no_doc_record:9 |
| `Lib.render._dx11.fxsetup` | 11 | 11 | 0 | 11 | no | csharp_verified_no_doc_record:11 |
| `Lib.render.analyze` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.render.basic` | 8 | 8 | 8 | 8 | no | doc_and_csharp_verified:8 |
| `Lib.render.camera` | 10 | 10 | 10 | 10 | no | doc_and_csharp_verified:10 |
| `Lib.render.camera.analyze` | 1 | 1 | 1 | 1 | no | doc_verified_no_csharp_match:1 |
| `Lib.render.gizmo` | 10 | 10 | 10 | 10 | no | doc_and_csharp_verified:10 |
| `Lib.render.postfx` | 6 | 6 | 6 | 6 | no | doc_and_csharp_verified:4, doc_verified_no_csharp_match:2 |
| `Lib.render.scene` | 2 | 2 | 2 | 2 | no | doc_and_csharp_verified:2 |
| `Lib.render.shading` | 15 | 15 | 15 | 15 | no | doc_and_csharp_verified:15 |
| `Lib.render.shading._` | 2 | 2 | 0 | 2 | no | csharp_verified_no_doc_record:2 |
| `Lib.render.sprite` | 3 | 3 | 3 | 3 | no | doc_and_csharp_verified:3 |
| `Lib.render.sprite._experimental` | 1 | 1 | 0 | 1 | no | csharp_verified_no_doc_record:1 |
| `Lib.render.transform` | 8 | 8 | 8 | 8 | no | doc_and_csharp_verified:8 |
| `Lib.render.utils` | 4 | 4 | 4 | 4 | no | doc_and_csharp_verified:4 |
| `Lib.string.buffers.convert` | 1 | 1 | 1 | 1 | no | doc_verified_no_csharp_match:1 |
| `Lib.string.buffers.transform` | 1 | 1 | 1 | 1 | no | doc_verified_no_csharp_match:1 |
| `Lib.string.combine` | 4 | 4 | 4 | 4 | no | doc_verified_no_csharp_match:4 |
| `Lib.string.convert` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:3 |
| `Lib.string.datetime` | 6 | 6 | 6 | 6 | no | doc_verified_no_csharp_match:6 |
| `Lib.string.list` | 6 | 6 | 6 | 6 | no | doc_verified_no_csharp_match:6 |
| `Lib.string.logic` | 4 | 4 | 4 | 4 | no | doc_verified_no_csharp_match:4 |
| `Lib.string.random` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:3 |
| `Lib.string.search` | 3 | 3 | 3 | 3 | no | doc_verified_no_csharp_match:3 |
| `Lib.string.transform` | 2 | 2 | 2 | 2 | no | doc_verified_no_csharp_match:2 |

## Representative Gaps

These are representative, not exhaustive. Full counts are in the namespace table.

Missing C# and missing `.t3` are the same 20 indexed nodes in this pass:

| node | namespace | evidence available |
|---|---|---|
| `Lib.io.http.WebServer` | `Lib.io.http` | docs only |
| `Lib.io.video.NdiInput` | `Lib.io.video` | docs only |
| `Lib.io.video.NdiOutput` | `Lib.io.video` | docs only |
| `Lib.io.video.PlayVideoClip` | `Lib.io.video` | docs only |
| `Lib.io.video.ScreenCapture` | `Lib.io.video` | docs only |
| `Lib.io.video.SpoutInput` | `Lib.io.video` | docs only |
| `Lib.io.video.SpoutOutput` | `Lib.io.video` | docs only |
| `Lib.io.video.mediapipe.FaceDetection` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.FaceLandmarkDetection` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.GestureRecognition` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.HandLandmarkDetection` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.ImageSegmentation` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.ObjectDetection` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.video.mediapipe.PoseLandmarkDetection` | `Lib.io.video.mediapipe` | docs only |
| `Lib.io.websocket.WebSocketClient` | `Lib.io.websocket` | docs only |
| `Lib.io.websocket.WebSocketServer` | `Lib.io.websocket` | docs only |
| `Lib.numbers.anim.animators.AnimInt` | `Lib.numbers.anim.animators` | docs only |
| `Lib.numbers.anim.animators.AnimValue` | `Lib.numbers.anim.animators` | docs only |
| `Lib.numbers.int2.process.MakeResolution` | `Lib.numbers.int2.process` | docs only |
| `Lib.point.draw.DrawLinesAlt` | `Lib.point.draw` | docs only |

Missing docs examples:

| node | namespace | source evidence |
|---|---|---|
| `AnalyzeMeshBuffers` | `_blank` | C# + `.t3` |
| `ApplyCamMatrices` | `_blank` | C# + `.t3` |
| `ApplyCamTransform` | `_blank` | C# + `.t3` |
| `ApplyTransformMatrix` | `_blank` | C# + `.t3` |
| `BlurWithMask` | `_blank` | C# + `.t3` |
| `BuildAsciiFontSorting` | `_blank` | C# + `.t3` |
| `ContextCBuffers` | `_blank` | C# + `.t3` |
| `LoadGltf` | `_blank` | C# + `.t3` |
| `VisualizeMesh` | `_blank` | C# + `.t3` |
| `Lib.render._dx11.api.*` | `Lib.render._dx11.api` | C# + `.t3`, no docs for all 23 nodes |
| `Lib.render._dx11.buffer.*` | `Lib.render._dx11.buffer` | C# + `.t3`, no docs for all 9 nodes |
| `Lib.render._dx11.fxsetup.*` | `Lib.render._dx11.fxsetup` | C# + `.t3`, no docs for all 11 nodes |
| `Lib.point._internal.*` | `Lib.point._internal` | C# + `.t3`, no docs for all 10 nodes |

Source mismatch / doc-C# mismatch examples:

| node | namespace | mismatch note |
|---|---|---|
| `Lib.data.object.PickObject` | `Lib.data.object` | indexed as `doc_verified_no_csharp_match` despite C# + docs + `.t3` paths existing |
| `Lib.flow.skillQuest.DrawQuiz` | `Lib.flow.skillQuest` | docs path lowercases `skillquest`; C# path uses `skillQuest` |
| `Lib.io.dmx.PointsToDmxLights` | `Lib.io.dmx` | docs path uses `PointsToDmxLights`; C# path uses `PointsToDMXLights` |
| `Lib.io.http.WebServer` | `Lib.io.http` | docs only; source/t3 missing in index |
| `Lib.io.video.NdiInput` | `Lib.io.video` | docs only; source/t3 missing in index |
| `Lib.io.video.mediapipe.FaceDetection` | `Lib.io.video.mediapipe` | docs only; source/t3 missing in index |
| `Lib.io.websocket.WebSocketClient` | `Lib.io.websocket` | docs only; source/t3 missing in index |
| `Lib.numbers.float.process.*` | `Lib.numbers.float.process` | all 14 nodes indexed as doc-C# mismatch; needs targeted path/name audit |
| `Lib.string.*` namespaces | multiple | all string namespaces in this index are doc-C# mismatch despite path coverage |

## Clone Notes: Errors We Should Not Repeat

From `FALSE_CLAIMS_AND_GAPS.md`:

- Do not invent TiXL node names. `PointsOnImage`, `ImageToPoints`, and `GetPosition` are not verified TiXL nodes in this source pack.
- Use `LinearSamplePointAttributes`, plural. The singular `LinearSamplePointAttribute` is a known naming error.
- Do not translate TouchDesigner, vvvv, Houdini, or generic node-graph vocabulary into TiXL node names unless the TiXL source confirms the node.
- Every node/workflow claim needs a source-confidence label: verified, doc verified, source verified, example verified, inferred, or Unknown.

From `TROUBLESHOOTING_CLONE_NOTES.md`:

- Mesh data does not draw itself. `LoadObj` / `CubeMesh` need a draw/render chain such as `DrawMesh -> Group -> Camera -> RenderTarget`.
- Points are not images, textures are not points, and SDF/Field is not mesh. Keep data-flow types separate before proposing a chain.
- `DrawMeshAtPoints` needs both mesh and points; missing either side is a node-card-level behavior risk.
- Audio-driven visuals need a verified timeline/audio source and value remap. Do not assume an idle preview equals timeline playback.
- When a user cannot find a node, first check verified inventory and negative knowledge instead of inventing a replacement.

From `WORKFLOWS_CLONE_NOTES.md`:

- Preserve workflow certainty. A workflow marked `inferred_*` must not become a verified interface claim in a Node Card.
- `Vector3` / vector wrapper nodes may be conceptually necessary even when the named node is missing or unverified in this source pack; mark such nodes Unknown instead of filling them in.
- For SDF/Field, `SphereSDF -> CombineSDF -> RaymarchField -> Camera -> RenderTarget` is listed as inferred/needs validation, not as a fully verified runtime contract.
- Texture material workflows and texture-to-point-sampling workflows are separate. Do not collapse `LoadImage -> SetMaterial -> DrawMesh` into point-cloud generation.
- `.t3` graph adjacency is usage evidence. It is not proof of port types, defaults, or runtime semantics.

## Worker Node Card Checklist

Before a worker writes an individual Node Card:

- Confirm the exact TiXL full path from `nodes_index.csv`; do not use guessed node names.
- Record source evidence separately for C#, `.t3` defaults, docs, and helper/shader/runtime files.
- If `confidence` is `doc_verified_no_csharp_match`, inspect the actual `external/tixl` path before claiming behavior.
- If docs are missing, keep Purpose/Conversion as Unknown unless C# behavior is directly readable.
- If C# is missing, do not infer port shape from docs alone; mark runtime behavior Unknown.
- Separate data-flow type: Float/Int/Bool/Text, Vec2/Vec3/Vec4, Texture2D/Image, MeshBuffers, BufferWithViews/Points, ShaderGraphNode/SDF, Command/Scene, device/API state.
- For `Vector4`, decide whether the semantic role is color (`VuoColor`) or vector (`VuoPoint4d`); do not default blindly.
- For `Command`, `BufferWithViews`, `ShaderGraphNode`, Direct3D/DX11, `ParticleSystem`, or `SceneSetup`, avoid grade A/B until a Vuo-specific design exists.
- Use `.t3` only for defaults and observed graph usage; never as type-law proof.
- Add a verification fixture that can fail: numeric golden cases, list edge cases, image/hash comparison, mesh count/bounds, scene smoke render, device mock, or explicit Unknown.
