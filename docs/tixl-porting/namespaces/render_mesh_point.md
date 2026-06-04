# Render / Mesh / Point Node Porting Research

Scope: every TiXL node whose full namespace starts with `Lib.render`, `Lib.mesh`, or `Lib.point`. This is research only; no Vuo node code is proposed here.

Evidence order: clone spec namespace markdown, then TiXL C#/.t3 source, then Vuo source for possible target vocabulary. Unknown means not verified.

## Namespace Counts And Initial Grades

| namespace | nodes | A | B | C | D |
|---|---:|---:|---:|---:|---:|
| `Lib.mesh._` | 1 | 0 | 0 | 1 | 0 |
| `Lib.mesh.draw` | 8 | 0 | 0 | 6 | 2 |
| `Lib.mesh.generate` | 11 | 0 | 0 | 11 | 0 |
| `Lib.mesh.modify` | 25 | 0 | 0 | 21 | 4 |
| `Lib.point._cpu` | 6 | 0 | 6 | 0 | 0 |
| `Lib.point._experimental` | 8 | 0 | 0 | 8 | 0 |
| `Lib.point._internal` | 10 | 0 | 0 | 10 | 0 |
| `Lib.point._obsolete` | 1 | 0 | 0 | 1 | 0 |
| `Lib.point.combine` | 8 | 0 | 0 | 8 | 0 |
| `Lib.point.draw` | 17 | 0 | 0 | 13 | 4 |
| `Lib.point.draw.legacy` | 3 | 0 | 0 | 3 | 0 |
| `Lib.point.generate` | 19 | 0 | 0 | 19 | 0 |
| `Lib.point.helper` | 7 | 0 | 0 | 7 | 0 |
| `Lib.point.io` | 5 | 0 | 0 | 5 | 0 |
| `Lib.point.modify` | 22 | 0 | 0 | 18 | 4 |
| `Lib.point.sim` | 7 | 0 | 0 | 7 | 0 |
| `Lib.point.sim._legacy` | 2 | 0 | 0 | 2 | 0 |
| `Lib.point.sim.experimental` | 5 | 0 | 0 | 5 | 0 |
| `Lib.point.transform` | 15 | 0 | 0 | 15 | 0 |
| `Lib.point.usse` | 1 | 0 | 0 | 1 | 0 |
| `Lib.render._` | 1 | 0 | 0 | 1 | 0 |
| `Lib.render._dx11.api` | 23 | 0 | 0 | 0 | 23 |
| `Lib.render._dx11.buffer` | 9 | 0 | 0 | 0 | 9 |
| `Lib.render._dx11.fxsetup` | 11 | 0 | 0 | 0 | 11 |
| `Lib.render.analyze` | 2 | 0 | 0 | 2 | 0 |
| `Lib.render.basic` | 8 | 0 | 0 | 8 | 0 |
| `Lib.render.camera` | 10 | 0 | 0 | 10 | 0 |
| `Lib.render.camera.analyze` | 1 | 0 | 0 | 1 | 0 |
| `Lib.render.gizmo` | 10 | 0 | 0 | 10 | 0 |
| `Lib.render.postfx` | 6 | 0 | 0 | 6 | 0 |
| `Lib.render.scene` | 2 | 0 | 0 | 2 | 0 |
| `Lib.render.shading` | 15 | 0 | 0 | 15 | 0 |
| `Lib.render.shading._` | 2 | 0 | 0 | 2 | 0 |
| `Lib.render.sprite` | 3 | 0 | 0 | 3 | 0 |
| `Lib.render.sprite._experimental` | 1 | 0 | 0 | 1 | 0 |
| `Lib.render.transform` | 8 | 0 | 0 | 8 | 0 |
| `Lib.render.utils` | 4 | 0 | 0 | 4 | 0 |
| **Total** | **297** | **0** | **6** | **234** | **57** |

## Grade Notes

- A: none found in this scope. These namespaces are mostly renderer/mesh/GPU-buffer nodes, not pure values.
- B: only CPU point helpers; useful after a point schema is chosen.
- C: Vuo mapping is plausible but needs a renderer, scene, mesh, point-list, or composition adapter.
- D: DX11 resource/stage nodes or custom shader graph nodes; document for now.

## Vuo Mapping Baseline

| TiXL concept | Evidence | Vuo candidate | Gap |
|---|---|---|---|
| `Command` render subtree | `external/tixl/Core/DataTypes/Command.cs`, render/mesh draw C# slots | Vuo event/dataflow composition using `VuoSceneObject` / `VuoLayer` lists | TiXL evaluates subtrees through `EvaluationContext`; Vuo has no direct Command stack. |
| `Texture2D` image/layer | TiXL `Texture.cs`; Layer2d/Text docs | `VuoImage`, `VuoLayer`, `vuo.layer.render.*`, `vuo.image.make.text` | Depth test/write and TiXL blend/filter enums need mapping. |
| `MeshBuffers` | `external/tixl/Core/DataTypes/MeshBuffers.cs`; mesh generate/draw C# | `VuoMesh`, `VuoSceneObject`, e.g. `external/vuo/node/vuo.scene/vuo.scene.make.cube.1.c`, `vuo.scene.make.sphere.c`, `vuo.scene.make.triangles.c` | TiXL stores vertex/index buffers, UV2, selection, color, tangent/bitangent; Vuo built-ins may not expose all attributes. |
| `BufferWithViews` point buffers | `external/tixl/Core/DataTypes/BufferWithViews.cs`; point nodes use SRV/UAV | `VuoList_VuoPoint3d`, `vuo.scene.make.points.c`, custom Vuo type if needed | TiXL structured GPU point attributes include position, rotation, color, scale, F1/F2/W; Vuo point nodes are simpler. |
| `ShaderGraphNode` / HLSL | `external/tixl/Core/DataTypes/ShaderGraphNode.cs`; DrawMesh/DrawPoints ColorField/FragmentField | `VuoShader`, ISF, `vuo.scene.shader.material.c`, `vuo.shader.*` | No automatic TiXL ShaderGraph/HLSL to Vuo shader graph path. |
| Cameras/lights | TiXL `render/camera/*.cs`, `render/shading/SetPointLight.cs` | `vuo.scene.make.camera.*`, `vuo.scene.make.light.*` | TiXL context matrices, lens shift, material/fog/shadow stacks need adapter semantics. |

Vuo source paths checked for this mapping:

- Layers/text/images: `external/vuo/node/vuo.layer/vuo.layer.make.text2.c`, `external/vuo/node/vuo.layer/vuo.layer.make.stretched.c`, `external/vuo/node/vuo.layer/vuo.layer.render.image.c`, `external/vuo/node/vuo.layer/vuo.layer.render.window2.c`, `external/vuo/node/vuo.image/vuo.image.make.text.c`
- Scene primitives/points: `external/vuo/node/vuo.scene/vuo.scene.make.cube.1.c`, `external/vuo/node/vuo.scene/vuo.scene.make.sphere.c`, `external/vuo/node/vuo.scene/vuo.scene.make.triangles.c`, `external/vuo/node/vuo.scene/vuo.scene.make.points.c`
- Cameras/lights/materials: `external/vuo/node/vuo.scene/vuo.scene.make.camera.perspective.target.c`, `external/vuo/node/vuo.scene/vuo.scene.make.camera.orthographic.target.c`, `external/vuo/node/vuo.scene/vuo.scene.make.light.point.c`, `external/vuo/node/vuo.scene/vuo.scene.make.light.spot.c`, `external/vuo/node/vuo.scene/vuo.scene.shader.material.c`

## DX11-only And High-risk Families

- `Lib.render._dx11.api`, `Lib.render._dx11.buffer`, and `Lib.render._dx11.fxsetup` are DX11-only first-pass D. They expose SRV/UAV/RTV/DSV, stage setup, dispatch counts, blend/rasterizer/viewport, buffer swapping, and execute-update nodes.
- Nodes with `ShaderGraphNode` inputs, including mesh draw/point draw custom fields and mesh custom shader modifiers, are D until shader translation policy exists.
- Nodes using `BufferWithViews` are C unless they are naked DX11 resource helpers. They need a Vuo point-buffer type decision before implementation.
- Mesh/SceneObject mapping is feasible for visible primitives and simple transforms, but not for TiXL exact `MeshBuffers` GPU memory contract.

## Compact Rows For All Nodes

| full_path | purpose | I/O summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.mesh._.UVsViewer` | _官方摘要缺失。_ | in 3 (BlendValue:float, Mesh:MeshBuffers, SwitchUV:bool); out 1 (BlendedMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh._.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.draw.DrawMesh` | Uses PBR rendering to draw incoming geometry and meshnodes according to the desired settings. For convenience Tooll adds a default reflection and two point lights attached to the camera to a RenderTarget. You can overrid | in 15 (AlphaCutOff:float, BlendMode:int, Color:Vector4, Culling:Direct3D11.CullMode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.draw.DrawMeshAtPoints` | Draws PBR shaded instances of a mesh defined by the connected point buffer. It uses the current settings for material, point lights, fog, and environment. It can use the point's F1 and F2 attribute to control the size of | in 17 (AlphaCutOff:float, AtlasMode:int, AtlasSize:Vector.Int2, BlendMode:int, Color:Vector4...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.draw.DrawMeshCelShading` | This is an attempt to create a cel shader. In fact, it's more a gradient-based shading as you need to use a gradient to define what colors will be used to shade your mesh. Make sure to use at least one [PointLight] and a | in 10 (Brightness:float, Color:Vector4, ColorMap:Texture2D, Culling:Direct3D11.CullMode, EdgeColor:Vector4...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.draw.DrawMeshChunksAtPoints` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 17 (AlphaCutOff:float, BlendMode:int, ChunkIndices:BufferWithViews, Color:Vector4, CullMode:CullMode...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.draw.DrawMeshHatched` | Draws incoming geometry and meshnodes in an abstract shading style. An interactive tutorial for the complete TiXL render pipeline can be found at [HowToDrawThings]. The most commonly used render methods are [Drawmesh], [ | in 13 (ColorHighlight:Vector4, ColorMap:Texture2D, ColorShade:Vector4, Culling:CullMode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.draw.DrawMeshUnlit` | Draws incoming geometry and meshnodes without any shading and according to the desired settings. An interactive tutorial for the complete TiXL render pipeline can be found at [HowToDrawThings]. The most commonly used ren | in 13 (AlphaCutOff:float, BlendMode:int, BlurLevel:float, Color:Vector4, Culling:CullMode...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.draw.DrawMeshWithShadow` | Is used together with [SetShadow] and a [SetPointLight] to render incoming geometry with a shadow map. See [SetShadowExample] | in 11 (AlphaCutOff:float, BlendMode:int, Color:Vector4, Culling:Direct3D11.CullMode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.draw.VisualizeUvMap` | A draw helper to check the UV maps of a given mesh | in 4 (Color:Vector4, Mesh:MeshBuffers, SwitchUV:bool, Texture:Texture2D); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.CubeMesh` | Generates a procedural three-dimensional mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. For a simple and interactive tutorial on the TiXL rendering pipeline, see [HowToDra | in 10 (Center:Vector3, Margin:float, Margin2:float, Pivot:Vector3, Rotation:Vector3...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.CylinderMesh` | Generates a procedural three-dimensional mesh which can be rendered with [DrawMesh], [DrawMeshUnlit], and [DrawMeshHatched] among others. This mesh can be generated with or without closing caps. You can use the Height, F | in 12 (BasePivot:float, CapSegments:int, Center:Vector3, Columns:int, Fill:float...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.DelaunayMesh` | Generate a mesh from point list | in 6 (BoundaryPoints:StructuredList, ExtraPoints:StructuredList, FillDensity:float, Seed:int, SubdivideLongEdges:float...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.ExtrudeCurves` | Generates a mesh by extruding a set of points along a set of other points. The extrusion is done along the z-axis. This means that the rail points should be aligned in a way that z is pointing along the rail. Also, the n | in 7 (ProfilePoints:BufferWithViews, RailPoints:BufferWithViews, ScaleFactor:int, UseExtend:bool, UseWAsWidth:bool...); out 1 (Output2:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.generate.IcosahedronMesh` | This generator is expensive, it's not recommended to animate its parameters | in 11 (Center:Vector3, Pivot:Vector3, Rotation:Vector3, Scale:float, Shading:int...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.LoadObj` | Loads a mesh from an obj. file by generating Vertex and Index buffers which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. All surfaces must have valid UVs, otherwise they will appea | in 4 (ClearGPUCache:bool, ScaleFactor:float, SortVertices:int, UseGPUCaching:bool); out 1 (Path:string) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.NGonMesh` | Generates a procedural flat circular mesh with a joined center vertex which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. Also consider using [CylinderMesh]. For a simple and intera | in 6 (Center:Vector3, Radius:float, Rotation:Vector3, Segments:int, Stretch:Vector2...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.QuadMesh` | Generates a procedural three-dimensional tessellated mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. Also known as: Plane, Quad For a simple and interactive tutorial on the | in 6 (Center:Vector3, Pivot:Vector2, Rotation:Vector3, Scale:float, Segments:Int2...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.RepeatMeshAtPoints` | Creates a new mesh that repeats the incoming mesh at each point. Note: Warning: With detailed meshes, or very large scaled meshes and/or a very high number of points, this Operator can take up a lot of resources. Also se | in 7 (ApplyPointScale:bool, InputMesh:MeshBuffers, Points:BufferWithViews, Scale:float, ScaleFactor:int...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.generate.SphereMesh` | Generates a procedural three-dimensional UV sphere mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. For a simple and interactive tutorial on the TiXL rendering pipeline, see | in 2 (Radius:float, Segments:Int2); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.generate.TorusMesh` | Generates a procedural three-dimensional torus mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. Also known as: Donut For a simple and interactive tutorial on the TiXL render | in 6 (Fill:Vector2, Radius:float, Segments:Int2, SmoothAngle:float, Spin:Vector2...); out 1 (Data:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.BlendMeshToPoints` | Allows to deform a mesh with a point buffer, for example you can use [MeshVerticesToPoints] and apply a point modifier. | in 7 (BlendMode:int, BlendValue:float, Mesh:MeshBuffers, Pairing:int, Points:BufferWithViews...); out 1 (BlendedMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.modify.BlendMeshVertices` | Blends between two sets of mesh vertices. This only yields meaningful (i.e., predictable) results for meshes with the same vertex count and topology. | in 7 (BlendMode:int, BlendValue:float, MeshA:MeshBuffers, MeshB:MeshBuffers, Pairing:int...); out 1 (BlendedMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.CollapseVertices` | A simple effect that snaps the position of vertices to a grid. This might look interesting. | in 14 (Amount:float, Center:Vector3, Extend:float, FallOff:float, GridOffset:Vector3...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.ColorVerticesWithField` | Uses a color field to set vertex colors from their position in that field. | in 4 (Mesh:MeshBuffers, SdfField:ShaderGraphNode, Strength:float, StrengthFactor:int); out 1 (OutMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.modify.CombineMeshes` | Combines the connected mesh buffers into a new mesh | in 2 (IsEnabled:bool, Meshes:MeshBuffers); out 1 (CombinedMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.CustomFaceShader` | Uses shader and some parameters to manipulate mesh faces. | in 15 (A:float, AdditionalDefines:string, B:float, C:float, D:float...); out 2 (GeneratedCode:string, MeshBuffers:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_verified_no_csharp_match` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.modify.CustomVertexShader` | Uses custom shader code to manipulate the vertices of a mesh. | in 15 (A:float, AdditionalDefines:string, B:float, C:float, D:float...); out 2 (GeneratedCode:string, MeshBuffers:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_verified_no_csharp_match` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.modify.DeformMesh` | Spherize, Taper and Twist. It works better if your mesh has a high density of vertices. (such as a [CubeMesh] with 64 segments on each axis, for example) | in 12 (AmountPerAxis:Vector2, Mesh:MeshBuffers, Pivot:Vector3, Radius:float, ShowPivots:bool...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.DisplaceMesh` | Distorts the input mesh by displacing its vertices by an amount controlled by the connected input texture. | in 10 (Amount:float, AmountDistribution:Vector3, InputMesh:MeshBuffers, Mode:int, Offset:Vector3...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.DisplaceMeshNoise` | Displaces a Mesh with Perlin noise. | in 12 (Amount:float, AmountDistribution:Vector3, Direction:int, Frequency:float, InputMesh:MeshBuffers...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.DisplaceMeshVAT` | VAT stands for Vertex Animation Texture. The animation is stored in textures. | in 6 (InputMesh:MeshBuffers, Normal:Texture2D, RecomputeNormal:bool, SplitMesh:bool, Texture:Texture2D...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.FlipNormals` | Reverses the normals of the input mesh. This can be useful for drawing shader "inside" surfaces of a mesh. | in 1 (Mesh:MeshBuffers); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.MeshFacesPoints` | Create points at the center, corners or edges of the mesh's faces. The size of each of the faces defines the size of the points and FX1 as the W attribute gets the radius. In center mode uses the "Inscribed Circle Center | in 12 (Color:Vector4, Fx1:float, Fx2:float, InputMesh:MeshBuffers, Mode:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.modify.MeshProjectUV` | Overwrites the existing UV coordinates with planar mapping. Becomes visible with [DrawMesh]. To manipulate existing UV Coordinates see [TransformMeshUVs]. Also see [ReprojectToUV]. | in 6 (Mesh:MeshBuffers, Rotate:Vector3, Scale:float, Stretch:Vector3, ToTexCoord2:bool...); out 1 (OutBuffer:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.MoveMeshToPointLine` | Deforms a mesh in world space origin along a line defined by points. It maps the geometry to the complete range of all points. Use the range parameter to squeeze the geometry to the correct ratio. Use the Offset paramete | in 5 (InputMesh:MeshBuffers, Offset:float, Points:BufferWithViews, Range:float, Scale:float); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.mesh.modify.PickMeshBuffer` | Switch between different mesh buffers | in 2 (Index:int, Input:MeshBuffers); out 1 (Output:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.RecomputeNormals` | Recalculates the normals (smoothing groups) of the surfaces of a mesh. For example, if these are changed by [DeformMesh] and similar operators | in 2 (InputMesh:MeshBuffers, RecomputeIndices:bool); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.ScatterMeshFaces` | This op is similar to [DisplaceMesh]. However, it uses face centers instead of the vertices to "scatter" a mesh while keeping the size of the individual faces. Please note that this effect works best with flat shader mes | in 14 (Amount:float, AmountDistribution:Vector3, Direction:int, Distort:float, InputMesh:MeshBuffers...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.SelectVertices` | Sets the selection property of mesh vertices from a volume. This can later be used to selectively apply manipulations like displace. Also see: [ScatterMeshFaces] | in 12 (Center:Vector3, ClampResult:bool, FallOff:float, InputMesh:MeshBuffers, Mode:int...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.SelectVerticesWithSDF` | Allows selecting and adding values to vertices in 3D space via SDF Objects/Fields. Similar to [SelectPoints]. Needs any SDF such as [BoxSDF], [ChainLinkSDF], [FractalSDF] as an input to select the points. And any form of | in 13 (ClampNegative:bool, DiscardNonSelected:bool, GainAndBias:Vector2, Mapping:int, Mesh:MeshBuffers...); out 1 (ResultMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.mesh.modify.SplitMeshVertices` | This can be relevant to "scatter" smooth shaded geometry or change shading to flat. | in 2 (InputMesh:MeshBuffers, ShadeFlat:float); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.TextureDisplaceMesh` | Uses a projected texture with a normal map to displace the mesh. This is similar to [SamplePointAttributes]. | in 13 (Amount:float, AmountDistribution:Vector3, Center:Vector3, Mesh:MeshBuffers, RotationLookupDistance:float...); out 1 (DisplacedMesh:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.TransformMesh` | Generates a new set of transformed vertices for a mesh. | in 7 (Mesh:MeshBuffers, Pivot:Vector3, Rotation:Vector3, Scale:Vector3, Translation:Vector3...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.TransformMeshUVs` | Manipulates the existing UV coordinates of a mesh to change the mapping of textures. Becomes visible with [DrawMesh]. To overwrite existing UV Coordinates with planar mapping see [MeshProjectUV] Also see [ReprojectToUV]. | in 8 (InputMesh:MeshBuffers, Pivot:Vector3, Rotate:Vector3, Stretch:Vector3, TexCoord2:bool...); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly. |
| `Lib.mesh.modify.Warp2dMesh` | Uses a set of reference points to warp a mesh geometry. This can be useful for adjusting projection mapping. It's working with [QuadMesh] | in 3 (InputMesh:MeshBuffers, Points:BufferWithViews, TargetPoints:BufferWithViews); out 1 (Result:MeshBuffers) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._cpu.LinePointsCpu` | _官方摘要缺失。_ | in 5 (AddSeparator:bool, From:Vector3, To:Vector3, W:float, WOffset:float); out 1 (ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._cpu.LinearPointsCpu` | _官方摘要缺失。_ | in 4 (Count:int, Offset:Vector3, OffsetW:float, StartW:float); out 2 (PointList:StructuredList, Start:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._cpu.RadialPointsCpu` | _官方摘要缺失。_ | in 11 (Axis:Vector3, Center:Vector3, CloseCircle:bool, Count:int, Cycles:float...); out 1 (ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._cpu.RepeatAtPointsCpu` | _官方摘要缺失。_ | in 2 (DestinationsPoints:StructuredList, SourcePoints:StructuredList); out 1 (ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._cpu.SampleSplinePoint` | _官方摘要缺失。_ | in 3 (Points:StructuredList, SubTree:Command, U:float); out 2 (Output:Command, Position:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._cpu.TransformCpuPoint` | _官方摘要缺失。_ | in 4 (Incremental:bool, Lists2:StructuredList, Rotation:Vector3, Space:int); out 3 (Position:Vector3, ResultPoint:StructuredList, Translation:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._cpu.md`; source `C#`; confidence `csharp_verified_no_doc_record` | B | CPU point/list helper; portable with Vuo list/point types once point schema is chosen. |
| `Lib.point._experimental.KeepBufferReference` | _官方摘要缺失。_ | in 2 (Buffer:BufferWithViews, BufferReference:Object); out 1 (Result:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental.NumberLinePoints` | _官方摘要缺失。_ | in 9 (FloatPrecision:int, GTargets:BufferWithViews, Increment:int, LineWidth:float, MaxDigitCount:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental.PointsFromMeshData` | _官方摘要缺失。_ | in 3 (Count:int, Data:ShaderResourceView, Seed:float); out 1 (Points:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental.RecycleBuffer` | _官方摘要缺失。_ | in 0 (); out 2 (Buffer:BufferWithViews, Reference:object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental.ReflectionLines` | _官方摘要缺失。_ | in 7 (DecayW:float, ExtendSteps:float, GPoints:BufferWithViews, Mesh:MeshBuffers, SpreadColor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental.TraceContourLines` | _官方摘要缺失。_ | in 15 (Center:Vector3, Curvature:float, GPoints:BufferWithViews, SampleRadius:float, Smoothness:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._experimental._GetSketchPoints` | _官方摘要缺失。_ | in 3 (GetOnionSkin:bool, PageIndex:int, Pages:object); out 2 (DistanceToCurrentTime:float, PointList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point._experimental._SketchImpl` | _官方摘要缺失。_ | in 8 (ColorMode:int, EnableKeyframeSync:bool, FilePath:string, IsMouseButtonDown:bool, MousePos:Vector2...); out 5 (ActivePageIndexOutput:int, CurrentBrushSize:float, CursorPosInWorld:Vector3, OutPages:object...) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point._internal.AnalyzeBuffers` | _官方摘要缺失。_ | in 2 (Index:int, Input:BufferWithViews); out 5 (BufferCount:int, SelectedBuffer:BufferWithViews, StartPositionForSelected:int, Stride:int...) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal.GetParticleComponents` | _官方摘要缺失。_ | in 0 (); out 4 (IsReset:bool, Length:int, ParticlesUav:UnorderedAccessView, SpeedFactor:float) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal.MultiUpdatePoints` | _官方摘要缺失。_ | in 1 (PointBuffers:BufferWithViews); out 1 (Result:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal.NoisePoints` | _官方摘要缺失。_ | in 5 (Amplitude:float, Count:int, Frequency:float, Phase:float, Thickness:float); out 1 (Scale:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._AppendPoints` | _官方摘要缺失。_ | in 2 (GPoints:BufferWithViews, GTargets:BufferWithViews); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._BuildSpatialHashMap` | _官方摘要缺失。_ | in 2 (CellSize:float, PointsA_:BufferWithViews); out 6 (CellPointCounts:ShaderResourceView, CellPointIndices:ShaderResourceView, CellRangeIndices:ShaderResourceView, HashGridCells:ShaderResourceView...) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._ExecuteParticleUpdate` | _官方摘要缺失。_ | in 2 (Commands:Command, IsEnabled:bool); out 1 (Output2:ParticleSystem) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._MixPoints` | _官方摘要缺失。_ | in 2 (Combination:int, Factor:float); out 1 (Mode:int) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._OffsetPoints` | _官方摘要缺失。_ | in 3 (Direction:Vector3, Distance:float, Points:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._internal._SetParticleSystemComponents` | _官方摘要缺失。_ | in 5 (Forces:ParticleSystem, InitializeVelocityFactor:float, IsReset:bool, PointsSimBuffer:BufferWithViews, SpeedFactor:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._internal.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point._obsolete.__OBSOLETEFollowMeshSurface` | _官方摘要缺失。_ | in 9 (Mesh:MeshBuffers, Points:BufferWithViews, RandomSpeed:float, RandomSpin:float, Reset:bool...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point._obsolete.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.BlendPoints` | Creates different transitions between two different point setups | in 7 (BlendFactor:float, BlendMode:int, Pairing:int, PointsA_:BufferWithViews, PointsB_:BufferWithViews...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.CombineBuffers` | Combines multiple buffers of the same type (like points). | in 1 (Input:BufferWithViews); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.PairPointsForGridWalkLines` | Special effects operator that works nicely with [CollectSpawnPoints]. | in 8 (GridOffset:Vector3, GridSize:Vector3, PhaseOffset:float, RandomizeGrid:Vector3, Speed:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.PairPointsForLines` | Combines two point buffers so they can be drawn with connection lines between points. | in 3 (GPoints:BufferWithViews, GTargets:BufferWithViews, SetWTo01:bool); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.PairPointsForSplines` | Combines two lists of points into a buffer for rendering them as splines. | in 10 (Debug:float, GPoints:BufferWithViews, GTargets:BufferWithViews, Segments:int, SetWTo01:bool...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.PickPointList` | Switches between different point buffers. | in 2 (Index:int, Input:BufferWithViews); out 1 (Selected:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine.SplinePoints` | Samples the incoming points into a set of spline points. | in 5 (Curvature:float, Points:StructuredList, PreSampleSteps:int, SampleCount:int, UpVector:Vector3); out 2 (OutBuffer:BufferWithViews, SampledPoints:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.combine._ExecuteCombineBuffers` | _官方摘要缺失。_ | in 1 (ComputeShader:ComputeShader); out 1 (InputBuffers:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.combine.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.legacy._DrawBillboardsOld` | _官方摘要缺失。_ | in 15 (AlphaCut:float, ApplyPointOrientation:int, BlendMod:int, Color:Vector4, EnableDepthWrite:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.legacy.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.legacy._DrawQuads` | _官方摘要缺失。_ | in 15 (AltasMode:int, BlendMode:int, Color:Vector4, CullMode:CullMode, EnableDepthTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.legacy.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.legacy._DrawVaryingQuads` | _官方摘要缺失。_ | in 17 (AlphaCutOff:float, ApplyFog:bool, ApplyPointOrientation:bool, BlendMod:int, Color:Vector4...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.legacy.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawBillboards` | Draws points and billboards or quads. This operator is very flexible and allows for a wide spectrum of effects. Its parameters are grouped into different sections for various aspects of variations. | in 33 (AlphaCut:float, AtlasMode:int, AtlasSize:Vector.Int2, BlendMode:int, Color:Vector4...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawClosedLines` | _官方摘要缺失。_ | in 16 (BlendMod:int, Color:Vector4, EnableZTest:bool, EnableZWrite:bool, FadeOutTooLong:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawConnectionLines` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 14 (BlendMode:int, CellSize:float, Color:Vector4, ColorOverLifetime:Gradient, EnableZWrite:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawLines` | Draws a point buffer as lines. The lines will be aligned to the camera, but their width will shrink with distance to the camera. You can override this with the ScaleWithDistance parameter. We use the point’s W attribute  | in 14 (BlendMod:int, Color:Vector4, EnableZTest:bool, EnableZWrite:bool, FadeOutTooLong:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawLinesAlt` | Alternative version of [DrawLines] that allow to draw closed shapes by connecting the first and the last points. | in 16 (); out 1 () | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `docs`; confidence `doc_verified_no_csharp_match` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.draw.DrawLinesBuildup` | Renders incoming points as growing strokes. The points' W attribute encodes the U progress of the extension of the strokes. This operator requires a texture with a [LinearGradient] that is transparent at the ends. Anothe | in 10 (BlendMod:int, Color:Vector4, EnableDepthWrite:bool, EnableTest:bool, GPoints:BufferWithViews...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawLinesShaded` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 14 (BlendMod:int, Color:Vector4, EnableZTest:bool, EnableZWrite:bool, FadeOutTooLong:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawMeshAtPoints2` | Similar to [DrawBillboards], this operator draws meshes instead of images. | in 33 (AlphaCut:float, AtlasMode:int, AtlasSize:Int2, Color:Vector4, ColorVariationMode:int...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawMovingPoints` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 15 (AlphaCutOff:float, BlendMode:int, Color:Vector4, ColorField:ShaderGraphNode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.draw.DrawPoints` | Draws a point buffer with the set camera, transform, and fog. The points are drawn as camera-facing billboards, ignoring the point orientation. The W attribute of the points is used for scaling. This can be controlled wi | in 12 (AlphaCutOff:float, BlendMode:int, Color:Vector4, ColorField:ShaderGraphNode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.draw.DrawPoints2` | A new version of [DrawPoints] that uses a Radius parameter instead of Size. It draws a point buffer with the set camera, transform, and fog. The points are drawn as camera-facing billboards, ignoring the point orientatio | in 9 (BlendMode:int, Color:Vector4, EnableZTest:bool, EnableZWrite:bool, FadeNearest:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawPointsDOF` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 15 (AlphaCutOff:float, BlendMode:int, BucketSize:float, Color:Vector4, DofShape:Vector2...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.draw.DrawPointsShaded` | Draws a point buffer as PBR-shaded spheres using the attributes defined by [SetMaterial]. Note: This operator was previously called "DrawMeshAsSpheres". | in 10 (BlendMode:int, Color:Vector4, ColorField:ShaderGraphNode, EnableZTest:bool, EnableZWrite:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.draw.DrawRayLines` | A special line renderer that draws camera-facing 3D geometry lines without corner metering that can intersect the near plane. | in 12 (BlendMod:int, Color:Vector4, EnableDepthWrite:bool, EnableTest:bool, GPoints:BufferWithViews...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawRibbons` | Draws a point buffer as ribbons. The lines will distance to the camera. You can override this with the ScaleWithDistance parameter. We use the points W attribute as a scale factor for the line width. If the W attribute o | in 11 (BlendMod:int, Color:Vector4, Culling:CullMode, EnableDepthWrite:bool, GPoints:BufferWithViews...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.DrawTubes` | Draws a shaded 3D mesh for connected lines points. | in 12 (BlendMod:int, Color:Vector4, Culling:CullMode, EnableDepthWrite:bool, GPoints:BufferWithViews...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.draw.VisualizePoints` | This helper operator visualizes points and their orientation. It is permanently visible, but you can toggle the default settings for these gizmos using the "Gizmo" toggle in the output window. Understanding the size, ord | in 16 (Color:Vector4, LineThickness:float, Points:BufferWithViews, PointSize:float, ShowAttributeList:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.BoundingBoxPoints` | Generates the bounding box containing the points used as input / Indices : Center - 0, Min - 1, Max - 8. In order to access its points' position, use [PointsToCPU] with [GetPointDataFromList]. It iterates through the sou | in 1 (Points:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.CommonPointSets` | Provides a set of useful point lists that can be used to draw shapes, lines and other geometric forms. [RepeatAtPoints] can be used to distribute these shapes. [DrawPoints], [DrawLines] can be used to visualize them. Als | in 1 (Set:int); out 2 (CpuBuffer:StructuredList, GpuBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.DoyleSpiralPoints2` | Generate a set of points with decreasing sizes that can be used to draw a Doyle spiral: In the mathematics of circle packing, a Doyle spiral is a pattern of non-crossing circles in the plane in which each circle is surro | in 13 (Center:Vector3, CenterPositionScale:float, CenterSizeScale:float, Offset:float, OrientationAngle:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.GridPoints` | Creates a buffer of GPU points distributed on a rectangular or hexagonal grid. Tips: - Set any of the counts to 1 to create a plane of points. - Switch the SizeMode to set if the scaling refers to the padding between the | in 16 (Center:Vector3, Color:Vector4, CountX:int, CountY:int, CountZ:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.HexGridPoints` | Creates a buffer of GPU points distributed on a hexagonal grid. Same as [GridPoints] with TilingMode set to 'HoneyCombs' (but with fewer options). Tips: - Set any of the counts to 1 to create a plane of points. Needs a [ | in 11 (Center:Vector3, CountX:int, CountY:int, CountZ:int, OrientationAngle:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.LinePoints` | Define points from a source position to a direction. | in 18 (AddSeparator:bool, Center:Vector3, ColorA:Vector4, ColorB:Vector4, Count:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.MeshVerticesToPoints` | Creates a point at each vertex of the connected mesh. Intended as a helper method to analyze meshes. Useful combinations [LoadObj], [DrawPoints] and grouped with [DrawMesh] with Fillmode set to 'Wireframe' (similar to [V | in 3 (Mesh:MeshBuffers, OffsetByTBN:Vector3, W:float); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.PointInfoLines` | Generates a line point buffer that can visualize numerical point attribute data. The result can be drawn with [DrawLines]. | in 7 (AsBillboards:bool, Digits:int, Input:BufferWithViews, Offset:Vector3, Precision:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.PointTrail` | Same as [PointTrail] with added internal features for [DrawBillboards] Keeps previous copies of points in a cycling buffer that can be used to draw trails and other effects. Can be used with [DrawPoints] [DrawLines] [Dra | in 6 (GPoints:BufferWithViews, IsEnabled:bool, Reset:bool, TrailLength:int, WriteLineSeparators:bool...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.PointTrailFast` | Keeps previous copies of points in a cycling buffer that can be used to draw trails and other effects. Hint: If some features don't work as expected try [PointTrail2] Can be used with [DrawPoints] [DrawLines] [DrawBillbo | in 5 (AddSeperatorThreshold:float, GPoints:BufferWithViews, IsEnabled:bool, Reset:bool, TrailLength:int); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.PointsOnImage` | Uses the image brightness to emit points. WARNING: this is extremely slow for high resolution. We recommend setting fixed resolutions like 512x512px. | in 10 (ApplyColorToPoints:bool, ClampThreshold:float, ColorWeight:Vector4, Count:int, GainAndBias:Vector2...); out 1 (OutputPoints:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.PointsOnMesh` | Get evenly distributed points on a mesh. Note that the initial evaluation of the mesh is extremely slow and should not be done on every frame. Useful combinations [LoadObj] and [DrawPoints] Similar Operators: To create a | in 6 (Count:int, IsEnabled:bool, Mesh:MeshBuffers, Seed:float, Texture:Texture2D...); out 2 (Colors:BufferWithViews, ResultPoints:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.RadialPoints` | A versatile generator of circular point sets that can create a variety of circles, spirals, helixes, etc. Try the presets to get an overview. Needs a [DrawPoints], [DrawLines] or [DrawMeshAtPoints] or similar in order to | in 17 (Axis:Vector3, Center:Vector3, CloseCircleLine:bool, Color:Vector4, Count:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.RepeatAtPoints` | Repeats a list of GPU points at positions provided by another list of points. The orientation of the target points can be applied, so this operator can be used to create point instantiation. If the ApplyTargetScaleW para | in 12 (AddSeparators:bool, ApplyOrientation:bool, ApplyPointScale:bool, ApplyTargetScaleW:bool, CombineMode:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.RepetitionPoints` | Generate a list of points by repeating a transform operation. This can be very useful in combination with [DrawMeshAtPoints]. Also, by driving the Offset parameter with an LFO, you can produce seamlessly animated endless | in 9 (AddSeparator:bool, Count:int, Phase:float, Pivot:Vector3, Rotate:Vector3...); out 1 (ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.generate.SpherePoints` | Generates a sphere with evenly distributed points on its surface. Needs a [DrawPoints], [DrawLines], or [DrawMeshAtPoints] or similar in order to become visible. Similar: [GridPoints], [RadialPoints], [PointsOnMesh] | in 5 (Center:Vector3, Count:int, Radius:float, Scatter:float, StartAngle:float); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate.SubdivideLinePoints` | Inserts additional points between line points. | in 3 (ClosedShape:bool, Count:int, Points:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.generate._DoyleSpiralRoot` | _官方摘要缺失。_ | in 2 (P:float, Q:float); out 3 (A:Vector2, B:Vector2, R:float) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.generate._GridPoints_Old` | _官方摘要缺失。_ | in 16 (Center:Vector3, Color:Vector4, CountX:int, CountY:int, CountZ:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.helper.CpuPointToCamera` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 11 (AlsoOffsetTarget:bool, AspectRatio:float, CamPointBuffer:StructuredList, ClipPlanes:Vector2, FieldOfView:float...); out 1 (CamReference:Object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.helper.LoadObjAsPoints` | Loads an OBJ point cloud file. These files can be generated with Blender or MeshLab and can contain color information. You can use selected operators like [DrawBillboards] that can use the point rotation attribute (XYZW) | in 3 (Mode:int, Path:string, Sorting:int); out 1 (Points:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.helper.PointToMatrix` | Converts a point to transform matrix. | in 10 (AlsoOffsetTarget:bool, AspectRatio:float, ClipPlanes:Vector2, FieldOfView:float, LensShift:Vector2...); out 1 (CamPointBuffer:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.helper.PointsToCPU` | Fetches a point list from GPU to CPU. This can be useful for later exporting to a file (e.g., with [ExportPointList]). | in 6 (Async:bool, MaxCount:int, PointBuffer:BufferWithViews, StartIndex:int, TriggerUpdate:bool...); out 1 (Output:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.helper.ReadPointColors` | _官方摘要缺失。_ | in 4 (Async:bool, MaxCount:int, PointBuffer:BufferWithViews, StartIndex:int); out 1 (Result:List<Vector4>) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.helper.SampleCpuPoints` | Samples a list of line points with cubic bezier spline interpolation. This can useful to animate cameras and other objects. | in 3 (PointList:StructuredList, SamplePos:float, TangentScale:float); out 1 (ResultPoint:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.helper._VisualizePointFields` | _官方摘要缺失。_ | in 4 (Count:int, FieldPointsBuffer:BufferWithViews, Offset:float, Range:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.helper.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.io.DataPointConverter` | Converts point data from CSV or JSON files into a GPU structured buffer of Point objects. It supports custom column mapping for CSV files and can export the converted points back to CSV or JSON. | in 15 (Convert:bool, CsvF1Mapping:string, CsvPosXMapping:string, CsvPosYMapping:string, CsvPosZMapping:string...); out 1 () | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.io.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.io.DataPointImportExport` | Imports a JSON point list into a GPU structured buffer and optionally exports it again. The node keeps the imported data in an internal buffer, so the points remain visible even when the import trigger is released. It al | in 5 (Export:bool, ExportFilePath:string, Import:bool, ImportFilePath:string, PointBufferIn:BufferWithViews); out 1 () | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.io.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.io.LineTextPoints` | Loads a single line SVG font and generates a point buffer for a text. This can then be rendered as lines, particle effects, or mesh. | in 12 (Color:Vector4, CornerWeightBalance:float, FilePath:string, HorizontalAlign:int, LineHeight:float...); out 1 (Text:string) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.io.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.io.LoadSvg` | Loads an SVG file as points so it can be rendered as points or lines. The supported SVG feature set is very basic, and depending on your exporting applications, elements could be missing or not correctly represented. Mis | in 7 (CenterToBounds:bool, FilePath:string, ImportAs:int, ReduceFactor:float, Scale:float...); out 1 (ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.io.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.io.PrepareSvgLineTransition` | This will iterate over the length of all line segments and compute offsets into W. DrawLines can then use this offset for animation effects. Spread: 0 - | in 5 (RandomizeDuration:float, RandomizeStart:float, SourcePoints:StructuredList, Spread:float, SpreadMode:int); out 2 (ResultList:StructuredList, StrokeCount:int) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.io.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Point/mesh/render namespace needs target type design. |
| `Lib.point.modify.AddNoise` | Creates a new buffer by resampling the connected points. This can be useful for increasing resolution or smoothing out hard edges. Also see [SimNoiseOffset] and [TurbulenceForce] | in 9 (AmountDistribution:Vector3, Frequency:float, NoiseOffset:Vector3, Phase:float, Points:BufferWithViews...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.AttributesFromImageChannels` | Some test. | in 26 (Blue:int, BlueFactor:float, BlueOffset:float, Brightness:int, BrightnessFactor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.ClearSomePoints` | Override a fraction of points with separators to insert gaps into lines. | in 5 (Points:BufferWithViews, Ratio:float, Repeat:int, Resolution:int, Seed:int); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.CustomPointShader` | A very fast method of writing simple compute shaders to manipulate the connected points. Each point has the following properties/attributes: p.Position // as float3 p.Rotation // as quaternion p.FX1 // correspond to F1 f | in 17 (A:float, AdditionalDefines:string, B:float, C:float, ConstantBuffers:Direct3D11.Buffer...); out 2 (Output:BufferWithViews, ShaderCode_:string) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.modify.FilterPoints` | Selects (i.e., picks) points based on the given criteria. Can be used to reduce the overall amount of points to increase performance. Useful combination [DrawPoints] [MeshFacesPoints] [MeshVerticesToPoints] Vaguely simil | in 6 (Count:int, Points:BufferWithViews, ScatterSelect:float, Seed:int, StartIndex:int...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.LinearSamplePointAttributes` | A variation of [SamplePointAttributes] that uses the point index instead of texture mapping. | in 19 (Blue:int, BlueFactor:float, BlueOffset:float, Brightness:int, BrightnessFactor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.MapPointAttributes` | Sets the points attribute and color from input attributes. This can be very powerful to remap point attributes. Different modes allow you to... - distribute the curve range along all points - or use different mapping ran | in 13 (Gradient:Gradient, InputMode:int, Mapping:int, MappingCurve:Curve, Phase:float...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.MoveToSDF` | Moves points to the nearest SDF surface. | in 11 (Amount:float, AmountFactor:int, Field:ShaderGraphNode, MaxSteps:int, MinDistance:float...); out 1 (Result2:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.modify.PointAttributeFromNoise` | Changes point attributes with a built-in noise function. | in 20 (Amount:float, Blue:int, BlueFactor:float, BlueOffset:float, Brightness:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.PointColorWithField` | Uses a color field to set point colors from their position in that field. | in 4 (Points:BufferWithViews, SdfField:ShaderGraphNode, Strength:float, StrengthFactor:int); out 1 (Result2:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.modify.RandomizePoints` | Smoothly randomizes various point attributes. It's an extremely versatile operator that provides various options of applying the random modifications and can be smoothly animated. Note: This is an updated version of [_Ra | in 17 (ClampColorsEtc:bool, ColorHSB:Vector4, F1:float, F2:float, GainAndBias:Vector2...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.ResampleLinePoints` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 8 (Count:int, Points:BufferWithViews, RangeMode:int, Rotation:int, RotationUpVector:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SamplePointAttributes_v1` | Samples point attributes from the RGB channels of the connected operator. This version has been deprecated, please use [SetAttrFromImageRgba] | in 26 (Alpha:int, AlphaFactor:float, AlphaOffset:float, Blue:int, BlueFactor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SamplePointColorAttributes` | Use a texture to color the points. Same as [SamplePointAttributes] but for colors only | in 10 (BaseColor:Vector4, BlendMode:int, Center:Vector3, GPoints:BufferWithViews, Scale:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SamplePointsByCameraDistance` | Changes the W value / F value of existing points based on their distance to the active camera. For example, to make distant points appear larger or smaller than nearby points, etc. Needs a point source such as: [GridPoin | in 4 (FarRange:float, NearRange:float, Points:BufferWithViews, WForDistance:Curve); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SelectPoints` | Simulates a selection of points by setting the F1 or F2 attribute. | in 19 (ClampResult:bool, DiscardNonSelected:bool, FallOff:float, GainAndBias:Vector2, Mode:int...); out 1 (Result2:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SelectPointsWithSDF` | Allows selecting and adding values to points in 3D space via SDF Objects/Fields. Similar to [SelectPoints]. Needs any SDF such as [BoxSDF], [ChainLinkSDF], [FractalSDF] as an input to select the points. And any form of p | in 13 (ClampNegative:bool, DiscardNonSelected:bool, GainAndBias:Vector2, Mapping:int, Mode:int...); out 1 (Result2:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | D | Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy. |
| `Lib.point.modify.SetAttributesWithPointFields` | Sets various attribute points from the distance of a 2nd (small) set of points. | in 17 (AffectColor:float, AffectOrientation:float, AffectPosition:float, AffectW:float, Amount:float...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SetPointAttributes` | Sets various attributes of points | in 16 (Amount:float, AmountFactor:int, Color:Vector4, Extend:Vector3, Fx1:float...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.SortPoints` | Sort points by distance to camera, so that the distant sprites can be drawn first, and ones closer to camera get blended correctly | in 4 (Ascending:bool, CameraReference:Object, Points:BufferWithViews, SortingSpeed:float); out 2 (DebugView:Texture2D, Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify.TransformWithImage` | Allows modifying various point attributes from an image. | in 21 (Center:Vector3, Channel:int, GainAndBias:Vector2, GPoints:BufferWithViews, Image:Texture2D...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.modify._RandomizePoints_Legacy1` | _官方摘要缺失。_ | in 11 (Amount:float, Gain:float, Offset:float, Points:BufferWithViews, Position:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim._legacy.LegacyParticleSimulation` | _官方摘要缺失。_ | in 14 (AgingRate:float, ApplyMovement:bool, ClampAtMaxAge:bool, Drag:float, Emit:bool...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim._legacy.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim._legacy._LegacySimForwardMovement` | _官方摘要缺失。_ | in 4 (Drag:float, GPoints:BufferWithViews, IsEnabled:bool, Speed:float); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim._legacy.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.experimental.ApplyRandomWalk` | Applies random steps to the position and rotation of points. | in 12 (AreaCenter:Vector2, AreaEdgeRange:Vector2, GPoints:BufferWithViews, IsEnabled:bool, RandomRotateAngle:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.experimental.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.experimental.GrowStrains` | Uses CollectedSpawnPoints and lines to animate W as weights moving across the strain with the age of the spawn point. We use a look-up texture that defines the growth progression as follows: - Age from left (birth) to ri | in 11 (Frequency:float, GPoints:BufferWithViews, GTargets:BufferWithViews, Length:float, NoiseAmount:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.experimental.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.experimental.SimBlendTo` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 4 (BlendFactor:float, GPoints:BufferWithViews, PairingMethod:int, PointsB:BufferWithViews); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.experimental.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.experimental.SimFollowMeshSurface` | An updated version of [FollowMeshSurface] that applies the movement on the original buffer. | in 10 (IsEnabled:bool, Mesh:MeshBuffers, Phase:float, Points:BufferWithViews, RandomSpeed:float...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.experimental.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.experimental.SimPointMeshCollisions` | Simulates collisions with meshes. Note: this is VERY expensive with large meshes (i.e., with more than 100 faces). | in 6 (Bouncyness:float, ClampAccelleration:float, Damping:float, IsEnabled:bool, Mesh:MeshBuffers...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.experimental.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.PointSimulation` | Creates a simulation buffer for applying simulation-like force fields, curl noise, or flocking simulation. It initially creates a copy of the connected source point buffer. You can then reset to this initial state or con | in 5 (GPoints:BufferWithViews, MinCapacity:int, MixOriginal:float, Reset:bool, Update:bool); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SamplePointSimAttributes` | Affects a point simulation by sampling a 2D texture. | in 19 (Blue:int, BlueFactor:float, BlueOffset:float, Brightness:int, BrightnessFactor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SimCentricalOffset` | Applies a directed force to points (acceleration stored in W). Use [SimForwardMovement] to move them as agents. | in 6 (Amount:float, Center:Vector3, DecayExponent:float, GPoints:BufferWithViews, MaxAcceleration:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SimDirectionalOffset` | Linearly offsets the incoming points along the defined axis. Needs a [DrawPoints], [DrawLines] or [DrawMeshAtPoints] or similar in order to become visible. Needs Points as a base, for example: [RadialPoints], [SpherePoin | in 6 (Amount:float, Direction:Vector3, GPoints:BufferWithViews, Mode:int, RandomAmount:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SimDisplacePoints2d` | Applies a displacement to the current point position. This operator modifies the incoming buffer and thus can be used for simulations with [KeepPoints]. | in 10 (Center:Vector3, DisplaceAmount:float, DisplaceOffset:float, GPoints:BufferWithViews, SampleRadius:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SimForceOffset` | Creates a gizmo with a spherical force that can be used to affect points Needs a [DrawPoints], [DrawLines], or [DrawMeshAtPoints] or similar in order to become visible. Needs Points as a base, for example: [RadialPoints] | in 10 (Center:Vector3, ForceDecayRate:float, GPoints:BufferWithViews, Gravity:Vector3, IsEnabled:bool...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.sim.SimNoiseOffset` | Adds Perlin curl noise to a point buffer. Note: Different from [AddNoise], this effect modifies the original buffer and is therefore not deterministic. Also see [TurbulenceForce] | in 9 (Amount:float, AmountDistribution:Vector3, Frequency:float, GPoints:BufferWithViews, IsEnabled:bool...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.BoundPoints` | Constraints points within a boundary volume It's the opposite of [BoundingBoxPoints] | in 4 (Points:BufferWithViews, Position:Vector3, Size:Vector3, UniformScale:float); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.FindClosestPointsOnMesh` | Combines points and a mesh and finds the nearest neighbors between points and mesh. Useful to visualize 'scan effects' and allows the display of complex details. The example shows the necessary structure. Needs Points su | in 2 (Mesh:MeshBuffers, Points:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.IkChain` | FABRIK inverse kinematic on the GPU. While this operator is still experimental, it works pretty good. Suggestions and help needed. Use [Once] to initialize. | in 11 (AngleConstraint:int, DirectionBias:Vector3, Influence:float, MaxBendAngle:float, MaxIterations:int...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.MovePointsToCurveSpace` | An advanced op that transforms points into a curved space defined by another set of points. To understand what this op does, imagine a set of points within a "source space" of a certain extent and orientation. This opera | in 10 (AtBoundaries:int, Extent:Vector3, Offset:float, Pivot:Vector3, Range:float...); out 1 (ResultPoints:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.OrientPoints` | Orients the rotation of points so that Z points towards a target position. You can also orient the point to the screen or the camera. | in 8 (Amount:float, AmountFactor:int, Flip:bool, OrientationMode:int, Points:BufferWithViews...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.PolarTransformPoints` | Reprojects points from a rectangular space to a radial coordinate system. | in 6 (Mode:int, Points:BufferWithViews, Rotation:Vector3, Scale:Vector3, Translation:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.ReorientLinePoints` | Align the orientation of points so that z- poitns forward. Try to use the current point orientation to avoid relying on up-vector discontinuities. | in 6 (Amount:float, Center:Vector3, Flip:bool, Points:BufferWithViews, UpVector:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.SnapPointsToGrid` | Rounds (floors) the position of points onto a grid, i.e., snaps them to the position of a grid. This can be useful to debug compute shader effects that rely on a spatial grid. | in 11 (Amount:float, AmountFactor:int, BiasAndGain:Vector2, GridScale:float, GridStretch:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.SnapToPoints` | Generate a new point buffer with points snapped to other points if the distance is below a threshold. | in 6 (BlendMode:int, BlendValue:float, Distance:float, MaxAmount:float, PointsA_:BufferWithViews...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.SoftTransformPoints` | Transforms points inside a volume. Experimenting with different FallOff parameters can provide a wide variety of effects. We provide the rotation not as three combined Euler angles to allow multiple revolutions. This Ope | in 19 (Amount:float, Bias:float, Dither:float, FallOff:float, GainAndBias:Vector2...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.TransformFromClipSpace` | Transforms Point positions from world to clip space. | in 1 (Points:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.TransformPoints` | Transforms incoming points. Tips: - Try to activate .WIsWeight and combine this operator with [SelectPoints]. - Changing the Space to Point can be used to offset the points. | in 14 (OffsetW:float, Pivot:Vector3, Points:BufferWithViews, Rotation:Vector3, Scale:float...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.TransformSomePoints` | Transform some points | in 17 (LengthFactor:float, OffsetW:float, OnlyKeepTake:bool, Points:BufferWithViews, Rotation:Vector3...); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.WrapPointPosition` | Can be used to wrap positions around a cubic volume, e.g., around the current camera position. | in 5 (AddLineBreaks:bool, GPoints:BufferWithViews, Position:Vector3, Size:Vector3, UseCameraPosition:bool); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.transform.WrapPoints` | Wraps points within a boundary volume. | in 3 (Points:BufferWithViews, Position:Vector3, Size:Vector3); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.point.usse.KeepPreviousPointBuffer` | Can be used to implement double buffering of points. | in 2 (InputBuffer:BufferWithViews, Keep:bool); out 2 (BufferA:BufferWithViews, BufferB:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.usse.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render._._ComputeLightOcclusions` | _官方摘要缺失。_ | in 3 (InputImage:Texture2D, LightIndex:int, UpdateCommand:Command); out 1 (Output:float) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render._dx11.api.CalcDispatchCount` | _官方摘要缺失。_ | in 2 (Count:int, ThreadGroupSize:Int3); out 1 (DispatchCount:Int3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.CalcInt2DispatchCount` | _官方摘要缺失。_ | in 2 (Size:Int2, ThreadGroups:Int3); out 1 (DispatchCount:Int3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.ClearRenderTarget` | _官方摘要缺失。_ | in 3 (ClearColor:Vector4, DepthStencilView:Direct3D11.DepthStencilView, RenderTarget:RenderTargetView); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.Draw` | _官方摘要缺失。_ | in 2 (VertexCount:int, VertexStartLocation:int); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.DrawInstancedIndirect` | _官方摘要缺失。_ | in 2 (AlignedByteOffsetForArgs:int, Buffer:Buffer); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.DsvFromTexture2d` | _官方摘要缺失。_ | in 1 (Texture:Texture2D); out 1 (DepthStencilView:DepthStencilView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.FloatsToBuffer` | _官方摘要缺失。_ | in 1 (Params:float); out 1 (Buffer:Buffer) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.GenerateMips` | _官方摘要缺失。_ | in 1 (Texture:Texture2D); out 2 (Activate:Command, TextureWithMips:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.GetSRVProperties` | _官方摘要缺失。_ | in 1 (SRV:ShaderResourceView); out 2 (Buffer:Buffer, ElementCount:int) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.GetTextureSize` | _官方摘要缺失。_ | in 2 (OverrideSize:Int2, Texture:Texture2D); out 5 (IsCubeMap:bool, IsTextureValid:bool, Size:Int2, SizeFloat:Vector2...) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.InputAssemblerStage` | _官方摘要缺失。_ | in 4 (IndexBuffer:Buffer, InputLayout:InputLayout, PrimitiveTopology:PrimitiveTopology, VertexBuffers:Buffer); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.OutputMergerStage` | _官方摘要缺失。_ | in 8 (BlendFactor:Vector4, BlendSampleMask:int, BlendState:BlendState, DepthStencilReference:int, DepthStencilState:DepthStencilState...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.Rasterizer` | _官方摘要缺失。_ | in 3 (RasterizerState:RasterizerState, ScissorRectangles:RawRectangle, Viewports:RawViewportF); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.ResolutionConstBuffer` | _官方摘要缺失。_ | in 1 (Resolution:Int2); out 1 (Buffer:Buffer) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.RtvFromTexture2d` | _官方摘要缺失。_ | in 2 (ArrayIndex:int, Texture:Texture2D); out 1 (RenderTargetView:RenderTargetView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.SrvFromStructuredBuffer` | _官方摘要缺失。_ | in 1 (Buffer:Buffer); out 2 (ElementCount:int, ShaderResourceView:ShaderResourceView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.SrvFromTexture2d` | _官方摘要缺失。_ | in 1 (Texture:Texture2D); out 1 (ShaderResourceView:ShaderResourceView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.TimeConstBuffer` | _官方摘要缺失。_ | in 0 (); out 1 (Buffer:Buffer) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.TransformsConstBuffer` | _官方摘要缺失。_ | in 0 (); out 2 (Buffer:Buffer, PrevBuffer:Buffer) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.UavFromBuffer` | _官方摘要缺失。_ | in 1 (Buffer:Buffer); out 1 (UnorderedAccessView:UnorderedAccessView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.UavFromStructuredBuffer` | _官方摘要缺失。_ | in 2 (Buffer:Buffer, BufferFlags:UnorderedAccessViewBufferFlags); out 1 (UnorderedAccessView:UnorderedAccessView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.UavFromTexture2d` | _官方摘要缺失。_ | in 1 (Texture:Texture2D); out 1 (UnorderedAccessView:UnorderedAccessView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.api.Viewport` | _官方摘要缺失。_ | in 6 (Height:float, MaxDepth:float, MinDepth:float, Width:float, X:float...); out 1 (Output:RawViewportF) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.FirstValidBuffer` | _官方摘要缺失。_ | in 1 (Input:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.GetBufferComponents` | _官方摘要缺失。_ | in 2 (BufferWithViews:BufferWithViews, LogWarnings:bool); out 6 (Buffer:Buffer, IsValid:bool, Length:int, ShaderResourceView:ShaderResourceView...) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.IntsToBufferWithViews` | _官方摘要缺失。_ | in 1 (Lists:int); out 3 (Length:int, OutBuffer:BufferWithViews, Srv:ShaderResourceView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.IsBufferDirty` | _官方摘要缺失。_ | in 1 (InputBuffer:BufferWithViews); out 1 (HasChanged:bool) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.ListToBuffer` | _官方摘要缺失。_ | in 1 (Lists:StructuredList); out 2 (Length:int, OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.PickBuffer` | _官方摘要缺失。_ | in 2 (Index:int, Input:BufferWithViews); out 2 (Count:int, Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.SwapBuffers` | _官方摘要缺失。_ | in 3 (BufferAInput:BufferWithViews, BufferBInput:BufferWithViews, EnableSwap:bool); out 2 (BufferA:BufferWithViews, BufferB:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.Texture3dComponents` | _官方摘要缺失。_ | in 1 (Input:Texture3dWithViews); out 4 (RenderTargetView:RenderTargetView, ShaderResourceView:ShaderResourceView, Texture:Texture3D, UnorderedAccessView:UnorderedAccessView) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.buffer.UseFallbackBuffer` | _官方摘要缺失。_ | in 2 (Fallback:BufferWithViews, PrimaryBuffer:BufferWithViews); out 1 (Output:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.buffer.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.ExecuteBufferUpdate` | _官方摘要缺失。_ | in 3 (BufferWithViews:BufferWithViews, IsEnabled:bool, UpdateCommand:Command); out 1 (Output2:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.ExecuteTextureUpdate` | _官方摘要缺失。_ | in 4 (IsEnabled:bool, Texture:Texture2D, TriggerTexture:Texture2D, UpdateCommands:Command); out 1 (Output:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.ExecuteValueUpdate` | _官方摘要缺失。_ | in 3 (InputValue:float, IsEnabled:bool, UpdateCommands:Command); out 1 (Output:float) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.PickBlendMode` | _官方摘要缺失。_ | in 1 (BlendMode:int); out 1 (Result2:BlendState) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.PrefixSum` | _官方摘要缺失。_ | in 2 (InputList2:BufferWithViews, IsInclusive:bool); out 2 (Output:Command, ResultBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.SetPixelAndVertexShaderStage` | _官方摘要缺失。_ | in 6 (ConstantBuffers:Buffer, PixelShader:PixelShader, SamplerStates:SamplerState, ShaderResources:ShaderResourceView, VariousResources:Object...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.ShowTexture2d` | _官方摘要缺失。_ | in 2 (Command:Command, Texture:Texture2D); out 1 (TextureOutput:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.ShowTexture3d` | _官方摘要缺失。_ | in 2 (Command:Command, Texture:Texture3dWithViews); out 1 (TextureOutput:Texture3dWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup.SwitchBlendState` | _官方摘要缺失。_ | in 2 (BlendStates:BlendState, Index:int); out 1 (Output:BlendState) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup._ImageFxShaderSetup2` | _官方摘要缺失。_ | in 10 (BlendMode:int, BufferColor:Vector4, Filter:Direct3D11.Filter, GenerateMipmaps:bool, OutputFormat:Format...); out 1 (TextureOutput:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render._dx11.fxsetup._ImageFxShaderSetupStatic` | _官方摘要缺失。_ | in 10 (BufferColor:Vector4, Filter:Filter, GenerateMips:bool, IntParams:int, OutputFormat:Format...); out 1 (TextureOutput:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.fxsetup.md`; source `C#`; confidence `csharp_verified_no_doc_record` | D | DX11/resource-stage specific; document until Vuo renderer/resource abstraction exists. |
| `Lib.render.analyze.GetScreenPos` | Returns the given position in screen space coordinates. This can be useful to attach labels or annotations to objects rendered by a camera. Please check out the example. | in 2 (LocalPosition:Vector3, SetDepthToZero:bool); out 2 (Position:Vector3, UpdateCommand:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.analyze.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.analyze.GpuMeasure` | Measures the time in milliseconds that the GPU (graphics card) needs to render the current image. Similar to the Performance display top left next to the menu in TiXL's UI. | in 3 (Command:Command, Enabled:bool, LogToConsole:bool); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.analyze.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.DrawScreenQuad` | Renders a rectangle that is locked to the camera view and automatically adjusts to the right image aspect ratio. | in 9 (BlendMode:int, Color:Vector4, EnableDepthTest:bool, EnableDepthWrite:bool, Filter:Filter...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.DrawScreenQuadAdvanced` | Just like DrawScreecQuad but with Depth and Normal buffer support for special FX. | in 10 (BlendMode:int, Color:Vector4, DepthBuffer:Texture2D, EnableDepthTest:bool, EnableDepthWrite:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.DustParticles` | Creates a volume around the camera in which points that behave like dust or snow are spawned and repeated / wrapped infinitely. Tip: This effect can be combined with [SetFog], [DepthOfField], [MotionBlur] among others. | in 18 (BlendMode:int, Color:Vector4, Count:int, EnableZWrite:bool, FadeNearest:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.FadingSlideShow` | Blends smoothly between image files read from a directory. BlendSpeed controls the speed of the transition. | in 11 (BackgroundColor:Vector4, BlendSpeed:float, Color:Vector4, FadeType:int, FolderWithImages:string...); out 2 (Output:Command, TextureOutput:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.Layer2d` | Creates a 2D plane in 3D space onto which the incoming image is rendered. This op automatically adjusts to the correct aspect ratio. A possible alternative [QuadMesh] -> [DrawMesh] -> [SetMaterial] in combination with [R | in 12 (BlendMode:int, Color:Vector4, EnableDepthTest:bool, EnableDepthWrite:bool, Filtering:Direct3D11.Filter...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.ShadowPlane` | Creates a Plane that is transparent except for a realtime rendered Shadowmap. See [ShadowPlaneExample] on how to use it. | in 9 (BlurDistribution:Vector2, BlurRadius:float, Center:Vector3, Color:Vector4, Command:Command...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.Text` | Creates flat 2D Text as an Object in 3D Space from bitmap fonts. For each character a quad mesh is generated. [TextSprites] can be used to manipulate these generated objects. Any String Operator like [AString], [RandomSt | in 14 (BillboardMode:bool, Color:Vector4, CullMode:CullMode, EnableZTest:bool, FontPath:string...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.basic.TextOutlines` | Creates outlined text to use in combination with [Text] | in 16 (BillboardMode:bool, CullMode:Direct3D11.CullMode, EnableZTest:bool, FillColor:Vector4, FontPath:string...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.analyze.VisualizeCamTrail` | Visualize a camera movement by looping over a time range and rendering onion skins of the connected camera reference. | in 3 (CamReference:Object, Steps:int, TimeRange:float); out 1 (Result:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.analyze.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.ActionCamera` | Allows to interactive control a camera starting from a giving reference camera. Also know as: FlyThrough, GameControlled camera, walk through, POV, Point of View. Also see [BlendCameras]. | in 12 (BlendToReferenceCamera:float, Forward:float, FOV:float, Pitch:float, ReferenceCamera:object...); out 1 (Reference:object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.BlendCameras` | Smoothly blends between multiple cameras by using a floating point index. | in 3 (CameraReferences:Object, Command:Command, Index:float); out 2 (CameraReference:object, Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.CamPosition` | Returns the current camera properties. This can be very useful for positioning or emitting things in front of the camera. Please consider looking at the example. | in 0 (); out 4 (AspectRatio:float, Command:Command, Direction:Vector3, Position:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.Camera` | Sets the camera for the subgraph. | in 12 (AlsoOffsetTarget:bool, AspectRatio:float, ClipPlanes:Vector2, Command:Command, FieldOfView:float...); out 2 (Output:Command, Reference:Object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.CameraWithRotation` | An alternative camera operator | in 15 (AlsoOffsetTarget:bool, AspectRatio:float, ClipPlanes:Vector2, Command:Command, FOV:float...); out 2 (Output:Command, Reference:Object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.CurrentCamMatrices` | Returns various matrices of the current context camera. This is used internally for things like rendering shadow maps. | in 0 (); out 1 (Command:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.OrbitCamera` | Provides common camera movement utilities for an easy dynamic camera setup. This operator was previously known as RandomCamera. | in 21 (AimPitchAngleAndWobble:Vector2, AimRollAngleAndWobble:Vector2, AimYawAngleAndWobble:Vector2, AspectRatio:float, CameraTargetPosition:Vector3...); out 2 (Output:Command, Reference:Object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.OrthographicCamera` | Set an orthographic project matrix for the operators following further down (i.e., to the left). Note that this projection uses a special z-buffer depth that might be incompatible with other operators like [DepthBufferAs | in 9 (AspectRatio:float, Command:Command, NearFarClip:Vector2, Position:Vector3, Roll:float...); out 2 (Output:Command, Reference:Object) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.ReuseCamera` | Set the projection matrix to the properties provided by another camera. This can be helpful for reusing an animated [Camera] for several [RenderTarget]s. | in 2 (CameraReference:Object, Command:Command); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.camera.ShiftCamera` | Shifts the clip space of the current context projection matrix. This might be useful for the implementation of obscure rendering tricks. | in 4 (Command:Command, Scale:Vector3, Translation:Vector3, UniformScale:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.ConeGizmo` | Generates points for drawing a wireframe audio cone visualization matching BASS/ManagedBass 3D audio cone behavior. BASS cones are simple conical shapes defined by full angles (0-360 degrees). The cone extends from the o | in 4 (Angle:float, Length:float, RayCount:int, Segments:int); out 1 (Points:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.DrawBoxGizmo` | Creates a 3D wireframe cube that can be used as a bounding box | in 4 (Color:Vector4, Position:Vector3, Scale:float, Stretch:Vector3); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.DrawCamGizmos` | An attempt to draw camera gizmos. | in 2 (Size:float, Visibility:GizmoVisibility); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.DrawLineGrid` | Renders a grid of lines in the given scaling. This might be useful for debugging or to use as content. Also see [GridPlane] | in 7 (BlendMod:int, Color:Vector4, LineWidth:float, Orientation:int, Segments:Int2...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.DrawSpatialAudioGizmos` | Draws gizmos for all spatial audio players in the current composition. Visualizes source and listener positions, attenuation ranges, and directional cones. | in 1 (Visibility:GizmoVisibility); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.DrawSphereGizmo` | Creates a 3D wireframe globe that can be used as a bounding box | in 3 (Color:Vector4, InnerRadius:float, Radius:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.GridPlane` | A helper gizmo that can use a fragment shader to draw smooth grid lines. This is what is visible in the viewport when "show gizmo and floor grid" is activated in an output window. Also known as: Floor Grid, grid, coordin | in 4 (Color:Vector4, Rotation:Vector3, Scale:float, Size:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.Locator` | Can be used to add a visible transform gizmo and its position in a scene (only visible if 'Toggle Gizmos and Floor Grid' is enabled in the Output window). The 'Pos' output can be linked to the 'Translation' input of any  | in 6 (Color:Vector4, Label:string, Position:Vector3, Size:float, Thickness:float...); out 3 (DistanceToCamera:float, Output:Command, Pos:Vector3) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.PlotValueCurve` | Plots a history curve of a value. This can be useful for debugging executables. While using the editor you might want to try [KeepFloatValues] or pinning a value operator to the output window. | in 8 (BufferLength:int, Color:Vector4, DisplayLabel:bool, Label:string, RangeMax:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.gizmo.VisibleGizmos` | Only executes the subgraph if the set visibility matches the visibility of the evaluation context. The user can toggle the gizmo visibility by the gizmo icon in the output window. | in 2 (Commands:Command, Visibility:GizmoVisibility); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.gizmo.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.DepthOfField` | Adds a depth of field effect. Hint: Use a [Locator] and its DistanceToCamera output to use automatic focus distance. Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAberration] [Glow] [Grain] [Blu | in 6 (Amount:float, DepthBuffer:Texture2D, FocusDistance:float, MaxSamples:int, NearFarRange:Vector2...); out 1 (TextureOut:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.GodRays` | Uses the z-buffer to draw god rays. A different approach [LightRaysFx] Useful Ops for a PostFX Pipeline: [MotionBlur] [DepthOfField] [ChromaticAberration] [Glow] [Grain] [Blur] | in 16 (BlurOffset:float, BlurSamples:int, BlurSize:float, CameraReference:Object, CenterIntensity:float...); out 1 (TextureOutput:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.MotionBlur` | Applies a post processing motion blur to a RenderTarget texture and its DepthBuffer. See also [RenderWithMotionBlur] for an operator using multi-pass rendering to achieve high quality at the cost of more resources. Desig | in 7 (CameraReference:Object, ClampEffect:float, DepthMap:Texture2D, Image:Texture2D, SampleCount:int...); out 1 (Output:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.ProjectLight` | Renders light "godrays" from a projected image into a scene casting nice shadows. It's a post render effect rendering an image and a shadow pass and combining it while applying some colors parameters. | in 17 (AmbientColor:Vector4, Image:Texture2D, LightColor:Vector4, Position:Vector3, ProjectorType:int...); out 1 (Output:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.SSAO` | Adds a Screen Space Ambient Occlusion (SSAO) effect to a 3D rendered scene. With this effect, a shading layer can be added that estimates how much a point or surface in the scene is exposed to ambient light. | in 11 (BoostShadows:Vector2, Color:Vector4, DepthBuffer:Texture2D, MixOriginal:float, MultiplyOriginal:float...); out 2 (DepthBuffer2:Texture2D, Output:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.postfx.TemporalAccumulation` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 2 (FeedbackAmount:float, Texture:Texture2D); out 1 (ColorBuffer:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.postfx.md`; source `C#`; confidence `doc_verified_no_csharp_match` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.scene.DrawScene` | Draw the connected SceneSetup. You can use [LoadGltfScene] to load a scene. | in 14 (AlphaCutOff:float, BlendMode:int, Color:Vector4, Culling:Direct3D11.CullMode, EnableZTest:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.scene.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.scene.LoadGltfScene` | Experimental op for loading complex scene setups using the glTF Format. Scenes have to be connected to a [DrawScene]-Op to be rendered. This Operator supports *.glb and *.gltf + .bin + textures. Also see [HowToDrawThings | in 7 (CombineBuffer:bool, MeshChildIndex:int, OffsetMetallic:float, OffsetRoughness:float, Path:string...); out 3 (Material:PbrMaterial, Mesh:MeshBuffers, ResultSetup:SceneSetup) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.scene.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading._._DispatchSceneDraws` | _官方摘要缺失。_ | in 11 (BrdfLookup:ShaderResourceView, FloatParameterBuffer:Buffer, FogParameterBuffer:Buffer, PixelShader:PixelShader, PointLightBuffer:Buffer...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading._.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading._._GetSceneDefinitionPoints` | _官方摘要缺失。_ | in 1 (SceneSetup:SceneSetup); out 3 (ChunkDefsBuffer:BufferWithViews, Points:BufferWithViews, ResultList:StructuredList) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading._.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render.shading.DefineMaterials` | Defines materials for later use when drawing content or scenes. | in 2 (Materials:PbrMaterial, SubGraph:Command); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.Equirectangle` | Renders the input scene as an image with depth with equirectangular mapping (for 360/VR/fulldome video) Also see [PolarCoordinates], [ConvertEquirectangle] | in 2 (Dimension:int, InputCommand:Command); out 2 (OutputColor:Texture2D, OutputDepth:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.GetPointLightOccclusion` | Returns a float list with the visibility of the current point lights. This means: This operator can recognize if a point light is being occluded by another object. It can be used to control the intensity of effects like  | in 4 (Damping:float, DepthMap:Texture2D, LightIndex:int, NearFarRange:Vector2); out 2 (Occlusion:float, Output:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.IntToWrapmode` | Picks a wrap mode with an index. | in 1 (ModeIndex:int); out 1 (Selected:TextureAddressMode) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.LenseFlareSetup` | Pre-made complex light flare setups with various styles. Require at least one [PointLight] on the left side or within the same group / graph. | in 4 (Brightness:float, LightIndex:int, RandomizeColor:Vector4, RandomSeed:int); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.LenseFlareSetupAdvanced` | Same as [LenseFlareSetup] but all elements can be tweaked separately. Note: Some effects can have unpredictable behaviour if the point light position matches the camera look target. | in 12 (Brightness:float, Center:float, ColorEdgeGlow:float, Digital:float, FlareSprites:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.PointLight` | The standard point light with transform gizmo. This can bring light into your scene using a combination of "Color", "Intensity" and "Decay". Beware that [SetEnvironment] also affects the lighting in your scene, even if y | in 9 (Color:Vector4, Command:Command, Decay:float, GizmoSize:float, Intensity:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetEnvironment` | Sets the image-based lighting (IBL) for the current RenderTarget. This texture can then be used by drawing operators for physically based rendering (PBR) further left in the graph. The operators needed are: [LoadImage] - | in 11 (BackgroundBlur:float, BackgroundColor:Vector4, BackgroundDistance:float, Exposure:float, Fallback:int...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetFog` | Adds fog to the incoming scene. ProTip: If large meshes (for example a "quadmesh" as a ground plane) are not properly obscured by the fog, one possible reason could be that the tessellation is not high enough. Useful com | in 4 (Bias:float, Color:Vector4, Command:Command, Distance:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetMaterial` | Sets the Physically Based Rendering (PBR) Material for the current RenderTarget which is then used by [DrawMesh] and other PBR rendering operators. Each of the material properties can be controlled by a color and/or by c | in 11 (BaseColor:Vector4, BaseColorMap:Texture2D, EmissiveColor:Vector4, EmissiveColorMap:Texture2D, MaterialId:string...); out 2 (Output:Command, Reference:PbrMaterial) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetPointLight` | Adds a point light into the scene which illuminates geometry using a combination of "Color", "Intensity" and "Decay". Beware that [SetEnvironment] also affects the lighting in your scene, even if you are not using it, be | in 6 (Color:Vector4, Command:Command, Decay:float, Intensity:float, Position:Vector3...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetRequestedResolution` | Set the requested resolution (similar to the Resolution drop-down of the output windows). Please be sure to understand how Tooll handles resolutions before using this operator. Check the tutorial linked below. Also see:  | in 3 (Resolution:Int2, ScaleResolution:float, Texture:Texture2D); out 1 (Result:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.SetShadow` | Renders a shadow pass for a directional light source. Needs a [DrawMeshWithShadow] and a [SetPointLight] or [PointLight] in order to work. | in 6 (Command:Command, DepthRange:Vector2, LightPosition:Vector3, LightTarget:Vector3, Resolution:int...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.TextureToCubeMap` | Converts a 2d texture ([loaded with [LoadImage]) into a cube map that can then be used by [SetEnvironment] for PBR image-based lighting. Currently Tooll supports Equirectangular Cubemaps. If changing values and settings  | in 3 (Image:Texture2D, Orientation:float, Resolution:int); out 1 (OutputTexture:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.shading.UseMaterial` | Applies the connected material definition to the context and it will be used by Draw operators further down (left) in the graph. | in 2 (MaterialReference:PbrMaterial, SubTree:Command); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.sprite._experimental.SampleSpriteAttributes` | _官方摘要缺失。_ | in 20 (Blue:int, BlueFactor:float, BlueOffset:float, Brightness:int, BrightnessFactor:float...); out 1 (OutBuffer:BufferWithViews) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.sprite._experimental.md`; source `C#`; confidence `csharp_verified_no_doc_record` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render.sprite.DrawPointSprites` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 10 (AlphaCutOff:float, BlendMode:int, Color:Vector4, EnableDepthWrite:bool, Points:BufferWithViews...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.sprite.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render.sprite.DrawPointSpritesShaded` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in 9 (AlphaCutOff:float, BlendMod:int, Color:Vector4, Culling:CullMode, EnableDepthWrite:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.sprite.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render.sprite.TextSprites` | Creates Points and Sprites from text that can be animated and then rendered with [DrawPointSprites] or [DrawPointSpritesShaded]. This operator is an advanced solution that loads a BmFont font definition and the matching  | in 10 (Color:Vector4, Filepath:string, HorizontalAlign:int, LineHeight:float, OffsetBaseLine:float...); out 3 (PointBuffer:BufferWithViews, SpriteBuffer:BufferWithViews, Texture:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.sprite.md`; source `C#`; confidence `doc_and_csharp_verified` | C | GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics. |
| `Lib.render.transform.Group` | Groups a sequence of incoming draw commands. Although similar to [Execute], it also allows to [Transform] and override the color multiply for all operators further down (i.e., left) in the graph. | in 9 (Color:Vector4, Commands:Command, EnableProfiling:bool, ForceColorUpdate:bool, IsEnabled:bool...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.RotateAroundAxis` | Applies a rotation to the transform matrix. | in 3 (Angle:float, Axis:Vector3, Command:Command); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.RotateTowards` | Rotates the incoming scene / objects towards a specified target. Also known as 'LookAt' Useful combination: Using the position output of a [Locator] creates a visible Target in the viewport with an interactive transform  | in 4 (AlternativeTarget:Vector3, Command:Command, LookTowards:int, RotationOffset:Vector3); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.Shear` | Applies a shearing transformation to the transform matrix. | in 2 (Command:Command, Translation:Vector3); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.SliceViewPort` | Modifies the Viewport and projection matrix to help drawing grid cells. In the simplest form this can be used to limit the rendering to a letterbox format (i.e., with black bars on top and bottom). In a more complex setu | in 5 (CellCounts:Int2, CellIndex:int, Mode:int, Stretch:Vector2, SubGraph:Command); out 2 (Count:int, Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.SpreadIntoGrid` | Spatially distributes the incoming graph elements into a grid | in 4 (Commands:Command, GridSize:Int3, Spread:Vector3, SpreadScale:float); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.SpreadLayout` | Spatially distributes the incoming graph elements. | in 10 (Color:Vector4, Commands:Command, ForceColorUpdate:bool, IsEnabled:bool, Pivot:float...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.transform.Transform` | Moves, scales and rotates the sub graph. Transform ops can be chained to add local pivots. Also see [HowToDraw3d] playground. | in 6 (Command:Command, Pivot:Vector3, Rotation:Vector3, Scale:Vector3, Translation:Vector3...); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.utils.ConvertEquirectangle` | Transforms a 360° image that was captured using a mirror ball or a panorama camera via equirectangular mapping so that it can be projected onto a cube without being distorted. Useful combinations: [LoadImage] Also see: [ | in 2 (Image:Texture2D, Resolution:Int2); out 1 (ColorBuffer:Texture2D) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.utils.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.utils.DrawAsSplitView` | Renders the connected draw commands as vertical ViewRect slices with optional labels. This can be quite interesting as an effect or, for example, to show multiple variations. | in 4 (Commands:Command, Labels:string, Mode:int, Stretch:Vector2); out 1 (Output:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.utils.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.utils.RepeatWithMotionBlur` | Repeats the connected subgraph with offset local time. Consider using [RenderWithMotionBlur] for more consistent results. | in 4 (FadeAlpha:float, Passes:int, Strength:float, SubGraph:Command); out 1 (Output2:Command) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.utils.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |
| `Lib.render.utils.RequestedResolution` | Has the function of extracting the resolution from an operator that is initialized later (for example a [Rendertarget]) in order to use it at an earlier point in the node tree. To do this, you can connect the 'Resolution | in 0 (); out 4 (AspectRatio:float, Height:int, Size:Int2, Width:int) | spec `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.utils.md`; source `C#`; confidence `doc_and_csharp_verified` | C | Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design. |

## Full Node Cards

The cards below cover the first high-value render/mesh/point core. For exact slot defaults, read the linked `.t3` and namespace spec block before implementation.

## Layer2d

- TiXL full path: `Lib.render.basic.Layer2d`
- Namespace: `Lib.render.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/basic/Layer2d.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/basic/Layer2d.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/basic/Layer2d.md`
  - related shader / helper source: Unknown
- Purpose: Creates a 2D plane in 3D space onto which the incoming image is rendered. This op automatically adjusts to the correct aspect ratio. A possible alternative [QuadMesh] -> [DrawMesh] -> [SetMaterial] in combination with [R
- Conversion: Map to VuoLayer via vuo.layer.make.image / vuo.layer.make.stretched plus vuo.layer.render.image/window; Z/depth is a gap.
- Inputs:
  - 12 (BlendMode:int, Color:Vector4, EnableDepthTest:bool, EnableDepthWrite:bool, Filtering:Direct3D11.Filter...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to VuoLayer via vuo.layer.make.image / vuo.layer.make.stretched plus vuo.layer.render.image/window; Z/depth is a gap.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## Text

- TiXL full path: `Lib.render.basic.Text`
- Namespace: `Lib.render.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/basic/Text.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/basic/Text.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/basic/Text.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/draw-text-msdf.hlsl; external/tixl/Operators/Lib/Assets/shaders/3d/draw-text-msdf-outlines.hlsl
- Purpose: Creates flat 2D Text as an Object in 3D Space from bitmap fonts. For each character a quad mesh is generated. [TextSprites] can be used to manipulate these generated objects. Any String Operator like [AString], [RandomSt
- Conversion: Map to vuo.layer.make.text2 or vuo.image.make.text; TiXL MSDF sharpness/billboard/Z-test are gaps.
- Inputs:
  - 14 (BillboardMode:bool, Color:Vector4, CullMode:CullMode, EnableZTest:bool, FontPath:string...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to vuo.layer.make.text2 or vuo.image.make.text; TiXL MSDF sharpness/billboard/Z-test are gaps.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DustParticles

- TiXL full path: `Lib.render.basic.DustParticles`
- Namespace: `Lib.render.basic`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/basic/DustParticles.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/basic/DustParticles.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/basic/DustParticles.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; likely compound uses point draw/update shaders; exact internal child shader Unknown
- Purpose: Creates a volume around the camera in which points that behave like dust or snow are spawned and repeated / wrapped infinitely. Tip: This effect can be combined with [SetFog], [DepthOfField], [MotionBlur] among others.
- Conversion: Possible VuoSceneObject points/particles approximation; infinite camera-wrapped GPU dust volume is missing.
- Inputs:
  - 18 (BlendMode:int, Color:Vector4, Count:int, EnableZWrite:bool, FadeNearest:float...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.basic.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Possible VuoSceneObject points/particles approximation; infinite camera-wrapped GPU dust volume is missing.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## Transform

- TiXL full path: `Lib.render.transform.Transform`
- Namespace: `Lib.render.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/transform/Transform.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/transform/Transform.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/transform/Transform.md`
  - related shader / helper source: Unknown
- Purpose: Moves, scales and rotates the sub graph. Transform ops can be chained to add local pivots. Also see [HowToDraw3d] playground.
- Conversion: Map to VuoTransform / vuo.scene.transform.trs; Command subtree semantics are not direct Vuo dataflow.
- Inputs:
  - 6 (Command:Command, Pivot:Vector3, Rotation:Vector3, Scale:Vector3, Translation:Vector3...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to VuoTransform / vuo.scene.transform.trs; Command subtree semantics are not direct Vuo dataflow.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## Group

- TiXL full path: `Lib.render.transform.Group`
- Namespace: `Lib.render.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/transform/Group.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/transform/Group.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/transform/Group.md`
  - related shader / helper source: Unknown
- Purpose: Groups a sequence of incoming draw commands. Although similar to [Execute], it also allows to [Transform] and override the color multiply for all operators further down (i.e., left) in the graph.
- Conversion: Map to vuo.scene.combine/group or list of VuoLayer; TiXL command evaluation order/color stack must be modeled.
- Inputs:
  - 9 (Color:Vector4, Commands:Command, EnableProfiling:bool, ForceColorUpdate:bool, IsEnabled:bool...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.transform.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to vuo.scene.combine/group or list of VuoLayer; TiXL command evaluation order/color stack must be modeled.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## Camera

- TiXL full path: `Lib.render.camera.Camera`
- Namespace: `Lib.render.camera`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/camera/Camera.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/camera/Camera.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/camera/Camera.md`
  - related shader / helper source: Unknown
- Purpose: Sets the camera for the subgraph.
- Conversion: Map to vuo.scene.make.camera.perspective(.target); lens shift/aspect fallback/command subtree need adapter.
- Inputs:
  - 12 (AlsoOffsetTarget:bool, AspectRatio:float, ClipPlanes:Vector2, Command:Command, FieldOfView:float...)
- Outputs:
  - out 2 (Output:Command, Reference:Object)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to vuo.scene.make.camera.perspective(.target); lens shift/aspect fallback/command subtree need adapter.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## OrthographicCamera

- TiXL full path: `Lib.render.camera.OrthographicCamera`
- Namespace: `Lib.render.camera`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/camera/OrthographicCamera.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/camera/OrthographicCamera.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/camera/OrthographicCamera.md`
  - related shader / helper source: Unknown
- Purpose: Set an orthographic project matrix for the operators following further down (i.e., to the left). Note that this projection uses a special z-buffer depth that might be incompatible with other operators like [DepthBufferAs
- Conversion: Map to vuo.scene.make.camera.perspective(.target); lens shift/aspect fallback/command subtree need adapter.
- Inputs:
  - 9 (AspectRatio:float, Command:Command, NearFarClip:Vector2, Position:Vector3, Roll:float...)
- Outputs:
  - out 2 (Output:Command, Reference:Object)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.camera.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to vuo.scene.make.camera.perspective(.target); lens shift/aspect fallback/command subtree need adapter.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SetMaterial

- TiXL full path: `Lib.render.shading.SetMaterial`
- Namespace: `Lib.render.shading`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/shading/SetMaterial.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/shading/SetMaterial.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/shading/SetMaterial.md`
  - related shader / helper source: Unknown
- Purpose: Sets the Physically Based Rendering (PBR) Material for the current RenderTarget which is then used by [DrawMesh] and other PBR rendering operators. Each of the material properties can be controlled by a color and/or by c
- Conversion: Map to VuoShader via vuo.scene.shader.material; TiXL PbrMaterial stack/material id lookup is a gap.
- Inputs:
  - 11 (BaseColor:Vector4, BaseColorMap:Texture2D, EmissiveColor:Vector4, EmissiveColorMap:Texture2D, MaterialId:string...)
- Outputs:
  - out 2 (Output:Command, Reference:PbrMaterial)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to VuoShader via vuo.scene.shader.material; TiXL PbrMaterial stack/material id lookup is a gap.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SetPointLight

- TiXL full path: `Lib.render.shading.SetPointLight`
- Namespace: `Lib.render.shading`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/shading/SetPointLight.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/shading/SetPointLight.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/shading/SetPointLight.md`
  - related shader / helper source: Unknown
- Purpose: Adds a point light into the scene which illuminates geometry using a combination of "Color", "Intensity" and "Decay". Beware that [SetEnvironment] also affects the lighting in your scene, even if you are not using it, be
- Conversion: Map to vuo.scene.make.light.point / spot as SceneObject light; shadow/fog interactions Unknown.
- Inputs:
  - 6 (Color:Vector4, Command:Command, Decay:float, Intensity:float, Position:Vector3...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map to vuo.scene.make.light.point / spot as SceneObject light; shadow/fog interactions Unknown.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SetEnvironment

- TiXL full path: `Lib.render.shading.SetEnvironment`
- Namespace: `Lib.render.shading`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/render/shading/SetEnvironment.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/render/shading/SetEnvironment.t3`
  - docs: `external/tixl/.help/docs/operators/lib/render/shading/SetEnvironment.md`
  - related shader / helper source: Unknown
- Purpose: Sets the image-based lighting (IBL) for the current RenderTarget. This texture can then be used by drawing operators for physically based rendering (PBR) further left in the graph. The operators needed are: [LoadImage] -
- Conversion: Partial VuoShader/environment mapping; cubemap/BRDF lookup workflow must be designed.
- Inputs:
  - 11 (BackgroundBlur:float, BackgroundColor:Vector4, BackgroundDistance:float, Exposure:float, Fallback:int...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render.shading.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoLayer or VuoSceneObject
  - direct built-in Vuo equivalent, if any: Partial VuoShader/environment mapping; cubemap/BRDF lookup workflow must be designed.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: Render Command/Texture2D/scene pipeline; Vuo Layer/SceneObject mapping is plausible but needs composition design.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## CubeMesh

- TiXL full path: `Lib.mesh.generate.CubeMesh`
- Namespace: `Lib.mesh.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/generate/CubeMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/generate/CubeMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/generate/CubeMesh.md`
  - related shader / helper source: Unknown
- Purpose: Generates a procedural three-dimensional mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. For a simple and interactive tutorial on the TiXL rendering pipeline, see [HowToDra
- Conversion: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
- Inputs:
  - 10 (Center:Vector3, Margin:float, Margin2:float, Pivot:Vector3, Rotation:Vector3...)
- Outputs:
  - out 1 (Data:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SphereMesh

- TiXL full path: `Lib.mesh.generate.SphereMesh`
- Namespace: `Lib.mesh.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/generate/SphereMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/generate/SphereMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/generate/SphereMesh.md`
  - related shader / helper source: Unknown
- Purpose: Generates a procedural three-dimensional UV sphere mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. For a simple and interactive tutorial on the TiXL rendering pipeline, see
- Conversion: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
- Inputs:
  - 2 (Radius:float, Segments:Int2)
- Outputs:
  - out 1 (Data:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## QuadMesh

- TiXL full path: `Lib.mesh.generate.QuadMesh`
- Namespace: `Lib.mesh.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/generate/QuadMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/generate/QuadMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/generate/QuadMesh.md`
  - related shader / helper source: Unknown
- Purpose: Generates a procedural three-dimensional tessellated mesh which can be rendered with [DrawMesh], [DrawMeshUnlit] and [DrawMeshHatched] among others. Also known as: Plane, Quad For a simple and interactive tutorial on the
- Conversion: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
- Inputs:
  - 6 (Center:Vector3, Pivot:Vector2, Rotation:Vector3, Scale:float, Segments:Int2...)
- Outputs:
  - out 1 (Data:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Good first-pass Vuo adapter candidate if we accept Vuo-native visual parity instead of TiXL renderer parity.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## RepeatMeshAtPoints

- TiXL full path: `Lib.mesh.generate.RepeatMeshAtPoints`
- Namespace: `Lib.mesh.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/generate/RepeatMeshAtPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/generate/RepeatMeshAtPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/generate/RepeatMeshAtPoints.md`
  - related shader / helper source: Unknown
- Purpose: Creates a new mesh that repeats the incoming mesh at each point. Note: Warning: With detailed meshes, or very large scaled meshes and/or a very high number of points, this Operator can take up a lot of resources. Also se
- Conversion: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
- Inputs:
  - 7 (ApplyPointScale:bool, InputMesh:MeshBuffers, Points:BufferWithViews, Scale:float, ScaleFactor:int...)
- Outputs:
  - out 1 (Result:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map geometry to VuoMesh/VuoSceneObject make nodes when built-in exists; TiXL MeshBuffers vertex attributes/UV2/selection often exceed Vuo built-ins.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawMesh

- TiXL full path: `Lib.mesh.draw.DrawMesh`
- Namespace: `Lib.mesh.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/draw/DrawMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/draw/DrawMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/draw/DrawMesh.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl; external/tixl/Operators/Lib/Assets/shaders/shared/pbr-render.hlsl
- Purpose: Uses PBR rendering to draw incoming geometry and meshnodes according to the desired settings. For convenience Tooll adds a default reflection and two point lights attached to the camera to a RenderTarget. You can overrid
- Conversion: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
- Inputs:
  - 15 (AlphaCutOff:float, BlendMode:int, Color:Vector4, Culling:Direct3D11.CullMode, EnableZTest:bool...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - D: Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawMeshAtPoints

- TiXL full path: `Lib.mesh.draw.DrawMeshAtPoints`
- Namespace: `Lib.mesh.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/draw/DrawMeshAtPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/draw/DrawMeshAtPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/draw/DrawMeshAtPoints.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-DrawAtPoints.hlsl; external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-DrawAtPoints2.hlsl
- Purpose: Draws PBR shaded instances of a mesh defined by the connected point buffer. It uses the current settings for material, point lights, fog, and environment. It can use the point's F1 and F2 attribute to control the size of
- Conversion: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
- Inputs:
  - 17 (AlphaCutOff:float, AtlasMode:int, AtlasSize:Vector.Int2, BlendMode:int, Color:Vector4...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - D: Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawMeshUnlit

- TiXL full path: `Lib.mesh.draw.DrawMeshUnlit`
- Namespace: `Lib.mesh.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/draw/DrawMeshUnlit.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/draw/DrawMeshUnlit.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/draw/DrawMeshUnlit.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-DrawUnlit.hlsl
- Purpose: Draws incoming geometry and meshnodes without any shading and according to the desired settings. An interactive tutorial for the complete TiXL render pipeline can be found at [HowToDrawThings]. The most commonly used ren
- Conversion: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
- Inputs:
  - 13 (AlphaCutOff:float, BlendMode:int, BlurLevel:float, Color:Vector4, Culling:CullMode...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Map MeshBuffers+material to VuoSceneObject with VuoShader; TiXL Command and ShaderGraphNode inputs are gaps.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## TransformMesh

- TiXL full path: `Lib.mesh.modify.TransformMesh`
- Namespace: `Lib.mesh.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/modify/TransformMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/modify/TransformMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/modify/TransformMesh.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-TransformVertices.hlsl
- Purpose: Generates a new set of transformed vertices for a mesh.
- Conversion: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
- Inputs:
  - 7 (Mesh:MeshBuffers, Pivot:Vector3, Rotation:Vector3, Scale:Vector3, Translation:Vector3...)
- Outputs:
  - out 1 (Result:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## CombineMeshes

- TiXL full path: `Lib.mesh.modify.CombineMeshes`
- Namespace: `Lib.mesh.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/modify/CombineMeshes.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/modify/CombineMeshes.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/modify/CombineMeshes.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineVertexBuffers.hlsl; external/tixl/Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineIndexBuffers.hlsl
- Purpose: Combines the connected mesh buffers into a new mesh
- Conversion: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
- Inputs:
  - 2 (IsEnabled:bool, Meshes:MeshBuffers)
- Outputs:
  - out 1 (CombinedMesh:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DisplaceMesh

- TiXL full path: `Lib.mesh.modify.DisplaceMesh`
- Namespace: `Lib.mesh.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/mesh/modify/DisplaceMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/mesh/modify/DisplaceMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/mesh/modify/DisplaceMesh.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Displace.hlsl
- Purpose: Distorts the input mesh by displacing its vertices by an amount controlled by the connected input texture.
- Conversion: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
- Inputs:
  - 10 (Amount:float, AmountDistribution:Vector3, InputMesh:MeshBuffers, Mode:int, Offset:Vector3...)
- Outputs:
  - out 1 (Result:MeshBuffers)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.mesh.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoMesh/VuoSceneObject
  - direct built-in Vuo equivalent, if any: Possible VuoMesh CPU/GPU modifier, but TiXL MeshBuffers+compute-shader mutation has no direct built-in.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: MeshBuffers/mesh renderer design needed; VuoSceneObject/VuoMesh can map some geometry but not TiXL command pipeline directly.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## GridPoints

- TiXL full path: `Lib.point.generate.GridPoints`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/GridPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/GridPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/GridPoints.md`
  - related shader / helper source: Unknown
- Purpose: Creates a buffer of GPU points distributed on a rectangular or hexagonal grid. Tips: - Set any of the counts to 1 to create a plane of points. - Switch the SizeMode to set if the scaling refers to the padding between the
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 16 (Center:Vector3, Color:Vector4, CountX:int, CountY:int, CountZ:int...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## RadialPoints

- TiXL full path: `Lib.point.generate.RadialPoints`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/RadialPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/RadialPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/RadialPoints.md`
  - related shader / helper source: Unknown
- Purpose: A versatile generator of circular point sets that can create a variety of circles, spirals, helixes, etc. Try the presets to get an overview. Needs a [DrawPoints], [DrawLines] or [DrawMeshAtPoints] or similar in order to
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 17 (Axis:Vector3, Center:Vector3, CloseCircleLine:bool, Color:Vector4, Count:int...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## LinePoints

- TiXL full path: `Lib.point.generate.LinePoints`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/LinePoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/LinePoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/LinePoints.md`
  - related shader / helper source: Unknown
- Purpose: Define points from a source position to a direction.
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 18 (AddSeparator:bool, Center:Vector3, ColorA:Vector4, ColorB:Vector4, Count:int...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## PointsOnMesh

- TiXL full path: `Lib.point.generate.PointsOnMesh`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/PointsOnMesh.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/PointsOnMesh.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/PointsOnMesh.md`
  - related shader / helper source: Unknown
- Purpose: Get evenly distributed points on a mesh. Note that the initial evaluation of the mesh is extremely slow and should not be done on every frame. Useful combinations [LoadObj] and [DrawPoints] Similar Operators: To create a
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 6 (Count:int, IsEnabled:bool, Mesh:MeshBuffers, Seed:float, Texture:Texture2D...)
- Outputs:
  - out 2 (Colors:BufferWithViews, ResultPoints:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## MeshVerticesToPoints

- TiXL full path: `Lib.point.generate.MeshVerticesToPoints`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/MeshVerticesToPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/MeshVerticesToPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/MeshVerticesToPoints.md`
  - related shader / helper source: Unknown
- Purpose: Creates a point at each vertex of the connected mesh. Intended as a helper method to analyze meshes. Useful combinations [LoadObj], [DrawPoints] and grouped with [DrawMesh] with Fillmode set to 'Wireframe' (similar to [V
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 3 (Mesh:MeshBuffers, OffsetByTBN:Vector3, W:float)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## RepeatAtPoints

- TiXL full path: `Lib.point.generate.RepeatAtPoints`
- Namespace: `Lib.point.generate`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/generate/RepeatAtPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/generate/RepeatAtPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/generate/RepeatAtPoints.md`
  - related shader / helper source: Unknown
- Purpose: Repeats a list of GPU points at positions provided by another list of points. The orientation of the target points can be applied, so this operator can be used to create point instantiation. If the ApplyTargetScaleW para
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 12 (AddSeparators:bool, ApplyOrientation:bool, ApplyPointScale:bool, ApplyTargetScaleW:bool, CombineMode:int...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.generate.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawPoints

- TiXL full path: `Lib.point.draw.DrawPoints`
- Namespace: `Lib.point.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/draw/DrawPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/draw/DrawPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/draw/DrawPoints.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; draw shader path in .t3 children Unknown
- Purpose: Draws a point buffer with the set camera, transform, and fog. The points are drawn as camera-facing billboards, ignoring the point orientation. The W attribute of the points is used for scaling. This can be controlled wi
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 12 (AlphaCutOff:float, BlendMode:int, Color:Vector4, ColorField:ShaderGraphNode, EnableZTest:bool...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - D: Uses TiXL ShaderGraphNode/HLSL or custom shader path; Vuo ISF/VuoShader mapping is possible only after shader-graph policy.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawLines

- TiXL full path: `Lib.point.draw.DrawLines`
- Namespace: `Lib.point.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/draw/DrawLines.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/draw/DrawLines.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/draw/DrawLines.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; line shader path in .t3 children Unknown
- Purpose: Draws a point buffer as lines. The lines will be aligned to the camera, but their width will shrink with distance to the camera. You can override this with the ScaleWithDistance parameter. We use the point’s W attribute 
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 14 (BlendMod:int, Color:Vector4, EnableZTest:bool, EnableZWrite:bool, FadeOutTooLong:float...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## DrawBillboards

- TiXL full path: `Lib.point.draw.DrawBillboards`
- Namespace: `Lib.point.draw`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/draw/DrawBillboards.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/draw/DrawBillboards.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/draw/DrawBillboards.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; billboard shader path in .t3 children Unknown
- Purpose: Draws points and billboards or quads. This operator is very flexible and allows for a wide spectrum of effects. Its parameters are grouped into different sections for various aspects of variations.
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 33 (AlphaCut:float, AtlasMode:int, AtlasSize:Vector.Int2, BlendMode:int, Color:Vector4...)
- Outputs:
  - out 1 (Output:Command)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.draw.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## AddNoise

- TiXL full path: `Lib.point.modify.AddNoise`
- Namespace: `Lib.point.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/modify/AddNoise.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/modify/AddNoise.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/modify/AddNoise.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/noise-functions.hlsl; compute shader child Unknown
- Purpose: Creates a new buffer by resampling the connected points. This can be useful for increasing resolution or smoothing out hard edges. Also see [SimNoiseOffset] and [TurbulenceForce]
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 9 (AmountDistribution:Vector3, Frequency:float, NoiseOffset:Vector3, Phase:float, Points:BufferWithViews...)
- Outputs:
  - out 1 (Output:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SetPointAttributes

- TiXL full path: `Lib.point.modify.SetPointAttributes`
- Namespace: `Lib.point.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/modify/SetPointAttributes.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/modify/SetPointAttributes.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/modify/SetPointAttributes.md`
  - related shader / helper source: Unknown
- Purpose: Sets various attributes of points
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 16 (Amount:float, AmountFactor:int, Color:Vector4, Extend:Vector3, Fx1:float...)
- Outputs:
  - out 1 (Output:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## RandomizePoints

- TiXL full path: `Lib.point.modify.RandomizePoints`
- Namespace: `Lib.point.modify`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/modify/RandomizePoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/modify/RandomizePoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/modify/RandomizePoints.md`
  - related shader / helper source: Unknown
- Purpose: Smoothly randomizes various point attributes. It's an extremely versatile operator that provides various options of applying the random modifications and can be smoothly animated. Note: This is an updated version of [_Ra
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 17 (ClampColorsEtc:bool, ColorHSB:Vector4, F1:float, F2:float, GainAndBias:Vector2...)
- Outputs:
  - out 1 (Output:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.modify.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## TransformPoints

- TiXL full path: `Lib.point.transform.TransformPoints`
- Namespace: `Lib.point.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/transform/TransformPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/transform/TransformPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/transform/TransformPoints.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/field/ComputePointTransformMatrix.hlsl; .t3 child ComputeShader
- Purpose: Transforms incoming points. Tips: - Try to activate .WIsWeight and combine this operator with [SelectPoints]. - Changing the Space to Point can be used to offset the points.
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 14 (OffsetW:float, Pivot:Vector3, Points:BufferWithViews, Rotation:Vector3, Scale:float...)
- Outputs:
  - out 1 (Output:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SoftTransformPoints

- TiXL full path: `Lib.point.transform.SoftTransformPoints`
- Namespace: `Lib.point.transform`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/transform/SoftTransformPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/transform/SoftTransformPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/transform/SoftTransformPoints.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/field/ComputePointTransformMatrix.hlsl; .t3 child ComputeShader
- Purpose: Transforms points inside a volume. Experimenting with different FallOff parameters can provide a wide variety of effects. We provide the rotation not as three combined Euler angles to allow multiple revolutions. This Ope
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 19 (Amount:float, Bias:float, Dither:float, FallOff:float, GainAndBias:Vector2...)
- Outputs:
  - out 1 (Output:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.transform.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SimDirectionalOffset

- TiXL full path: `Lib.point.sim.SimDirectionalOffset`
- Namespace: `Lib.point.sim`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/sim/SimDirectionalOffset.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/sim/SimDirectionalOffset.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/sim/SimDirectionalOffset.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; sim shader path Unknown
- Purpose: Linearly offsets the incoming points along the defined axis. Needs a [DrawPoints], [DrawLines] or [DrawMeshAtPoints] or similar in order to become visible. Needs Points as a base, for example: [RadialPoints], [SpherePoin
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 6 (Amount:float, Direction:Vector3, GPoints:BufferWithViews, Mode:int, RandomAmount:float...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## SimForceOffset

- TiXL full path: `Lib.point.sim.SimForceOffset`
- Namespace: `Lib.point.sim`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/point/sim/SimForceOffset.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/point/sim/SimForceOffset.t3`
  - docs: `external/tixl/.help/docs/operators/lib/point/sim/SimForceOffset.md`
  - related shader / helper source: external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl; sim shader path Unknown
- Purpose: Creates a gizmo with a spherical force that can be used to affect points Needs a [DrawPoints], [DrawLines], or [DrawMeshAtPoints] or similar in order to become visible. Needs Points as a base, for example: [RadialPoints]
- Conversion: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
- Inputs:
  - 10 (Center:Vector3, ForceDecayRate:float, GPoints:BufferWithViews, Gravity:Vector3, IsEnabled:bool...)
- Outputs:
  - out 1 (OutBuffer:BufferWithViews)
- Runtime behavior:
  - Source-level summary: `C#` source declares slots; spec block records graph adjacency. Detailed implementation is in the C#/.t3 files above.
  - GPU behavior: Uses/depends on BufferWithViews, MeshBuffers, ShaderGraphNode, Texture2D, Command, or HLSL; exact shader child must be inspected before implementation.
  - important edge cases: Unknown unless listed in spec; do not infer missing defaults.
- Observed graph usage:
  - common incoming nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md` node block.
  - common outgoing nodes: see `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.point.sim.md` node block.
- Vuo mapping:
  - Vuo input types: VuoReal/VuoPoint2d/VuoPoint3d/VuoColor/VuoImage/VuoLayer/VuoMesh/VuoSceneObject/VuoShader as applicable; exact ports Unknown.
  - Vuo output types: VuoList_VuoPoint3d or VuoSceneObject points
  - direct built-in Vuo equivalent, if any: Map to VuoPoint3d lists or VuoSceneObject points for simple cases; BufferWithViews attributes F1/F2/W/color/orientation need a custom schema.
  - missing Vuo support: TiXL Command scheduling, DX11 resource views, BufferWithViews point attributes, ShaderGraphNode/HLSL translation, material stack where applicable.
- Porting grade:
  - C: GPU point BufferWithViews/compute pipeline; Vuo has scene points but not TiXL structured point-buffer semantics.
- First implementation recommendation: Document/adapter first; implement only after target Vuo type contract is fixed.
- Verification fixture: Minimal Vuo composition rendering a still frame and comparing transform/size/count/color against a TiXL reference screenshot or captured buffer; exact fixture Unknown.
- Risks / unknowns: Defaults/enums must be confirmed from `.t3`; shader/helper behavior Unknown unless listed above.

## First Batch Recommendation

1. `Lib.render.basic.Layer2d` - VuoLayer image path; good 2D smoke test.
2. `Lib.render.basic.Text` - Vuo text layer/image path; visible and easy to compare.
3. `Lib.render.transform.Transform` - maps to Vuo transform semantics; needed by most scene nodes.
4. `Lib.render.transform.Group` - establishes list/subtree composition semantics.
5. `Lib.render.camera.Camera` - perspective camera baseline.
6. `Lib.mesh.generate.CubeMesh` - primitive geometry and UV fixture.
7. `Lib.mesh.generate.SphereMesh` - primitive geometry with segment/radius fixture.
8. `Lib.mesh.draw.DrawMeshUnlit` - simplest mesh-to-scene draw path.
9. `Lib.point.generate.GridPoints` - point schema/count/position fixture.
10. `Lib.point.draw.DrawPoints` - point visualization, but only after point schema decision.

## Largest Blockers

- `BufferWithViews`: TiXL point buffers are GPU structured buffers with SRV/UAV views and rich point attributes; Vuo needs either a custom point-buffer type or lossy `VuoList_VuoPoint3d` mapping.
- `Command` / `EvaluationContext`: TiXL render nodes mutate context stacks for transforms, cameras, materials, colors, and profiling while evaluating subtrees. Vuo dataflow needs a different adapter shape.
- `ShaderGraphNode` and HLSL: many high-value nodes allow custom fields/fragments; no verified automatic ShaderGraph/HLSL-to-Vuo shader route.
- DX11-only nodes: SRV/UAV/RTV/DSV, compute dispatch, stage setup, and resource views are not first-pass Vuo nodes.
