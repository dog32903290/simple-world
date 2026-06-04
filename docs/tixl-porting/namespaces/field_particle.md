# Field / Particle TiXL Porting Research

Scope: every TiXL node whose namespace starts with `Lib.field` or `Lib.particle`, from `external/tixl-spec/TIXL_CLONE_SPEC_20260604`, cross-checked against `external/tixl` source paths and Vuo shader/image/scene affordances under `external/vuo`.

## Preliminary Namespace Counts

| namespace | nodes | A | B | C | D | note |
|---|---:|---:|---:|---:|---:|---|
| `Lib.field.adjust` | 7 | 0 | 0 | 7 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.adjust._` | 3 | 0 | 0 | 0 | 3 | ShaderGraphNode / SDF graph |
| `Lib.field.analyze` | 1 | 0 | 0 | 1 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.combine` | 4 | 0 | 0 | 4 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.generate.sdf` | 17 | 0 | 0 | 17 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.generate.sdf._` | 3 | 0 | 0 | 0 | 3 | ShaderGraphNode / SDF graph |
| `Lib.field.generate.texture` | 2 | 0 | 0 | 2 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.generate.vec3` | 1 | 0 | 0 | 1 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.render` | 3 | 0 | 0 | 3 | 0 | ShaderGraphNode / SDF graph |
| `Lib.field.render._` | 1 | 0 | 0 | 0 | 1 | ShaderGraphNode / SDF graph |
| `Lib.field.space` | 12 | 0 | 0 | 11 | 1 | ShaderGraphNode / SDF graph |
| `Lib.field.space._` | 1 | 0 | 0 | 0 | 1 | ShaderGraphNode / SDF graph |
| `Lib.field.use` | 5 | 0 | 0 | 2 | 3 | ShaderGraphNode / SDF graph |
| `Lib.particle` | 1 | 0 | 0 | 0 | 1 | Particle buffer/state / GPU force |
| `Lib.particle.force` | 18 | 0 | 0 | 0 | 18 | Particle buffer/state / GPU force |

Total nodes covered: **79**. Initial grades: **A 0 / B 0 / C 48 / D 31**.

## Grade Rule Used Here

- `C`: important shader/image/scene work. Most `Lib.field` nodes are `C` because their real artifact is TiXL `ShaderGraphNode`, not a scalar value.
- `D`: internal codegen/helper, DX/compute/buffer dependency, or particle state/force behavior. Most `Lib.particle` nodes are `D` until My World owns a particle buffer/state runtime.
- `A/B`: none in this slice so far. These namespaces mostly avoid pure value/list/control nodes.

## Vuo Fit Summary

- Vuo can approximate final raster or scene outputs with `external/vuo/node/vuo.image/vuo.image.make.shadertoy2.cc`, `external/vuo/node/vuo.shader/*`, `external/vuo/node/vuo.scene/vuo.scene.render.image2.c`, and mesh/scene primitives such as sphere/cube/torus/points.
- Vuo does not appear to provide a stock equivalent to TiXL `ShaderGraphNode` DAG generation, HLSL template injection, raymarch SDF material graph, or GPU particle force chain.
- Therefore the first My World承重線 is shader graph runtime + particle buffer/state runtime; Vuo is useful as an output/render host or partial visual approximation, not as the semantic owner.

## Behavior Markers

| behavior | TiXL evidence | Vuo approximation | Needs My World runtime |
|---|---|---|---|
| `ShaderGraphNode` | Field generators/transforms/combiners expose `Slot<ShaderGraphNode>` / `InputSlot<ShaderGraphNode>` in C# specs. | Vuo shaders can render a final image/material, but not preserve TiXL's composable field DAG. | Yes: shader graph node model, dependency invalidation, code/parameter/resource assembly. |
| SDF primitives | `Lib.field.generate.sdf.*` nodes output SDF fields consumed by combine/space/render/use nodes. | Vuo scene primitives can visually mimic sphere/box/cylinder/torus geometry, and Shadertoy/image nodes can render isolated GLSL SDF experiments. | Yes for semantic TiXL SDF composition, material injection, and distance-field sampling. |
| raymarch | `RaymarchField`, `RaymarchPoints`, `VisualizeFieldDistance` use generated shader code/templates and SDF sampling. | Vuo can host a custom image shader or render a scene image, but not TiXL raymarch graph resources directly. | Yes: raymarch template injection, depth/write behavior, field params, point-buffer raymarching. |
| particle buffer/state | `ParticleSystem` emits/updates `BufferWithViews`; force nodes output `ParticleSystem`. | Vuo can draw points/meshes or random point scenes, but these are render approximations. | Yes: persistent particle state, emit/update lifecycle, force chaining. |
| GPU force behavior | Force `.t3` graphs reference `ComputeShader`, `ComputeShaderStage`, and HLSL under `Lib:shaders/particles/*`. | No stock Vuo particle-force compute chain found in `external/vuo/node`. | Yes: compute dispatch, `RWStructuredBuffer<Particle>`, mesh/image/SDF force resource binding. |

## Compact Node Rows

| full_path | purpose | I/O summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.field.adjust.AbsoluteSDF` | Returns the absolute value of the signed distance field Helpful combination: CombineSDF AbsoluteSDF 處理 SDF / shader field 資料。 | in InputField:ShaderGraphNode; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/AbsoluteSDF.cs`; .t3 `Operators/Lib/field/adjust/AbsoluteSDF.t3`; docs `.help/docs/operators/lib/field/adjust/AbsoluteSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.InvertSDF` | Inverts the incoming SDF field. Example: A cube in infinite empty space thus becomes a cut-out space in infinite matter. Helpful combination: Combi... | in InputField:ShaderGraphNode; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/InvertSDF.cs`; .t3 `Operators/Lib/field/adjust/InvertSDF.t3`; docs `.help/docs/operators/lib/field/adjust/InvertSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.NoiseDisplaceSDF` | Displaces the distance of an SDF with a Perlin-like noise offset. NOTE: this operator will break the Lipschitz continuity of your field and will ca... | in Amount:float, InputField:ShaderGraphNode, Offset:Vector3, Scale:float, StepFactor:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/NoiseDisplaceSDF.cs`; .t3 `Operators/Lib/field/adjust/NoiseDisplaceSDF.t3`; docs `.help/docs/operators/lib/field/adjust/NoiseDisplaceSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.PushPullSDF` | Makes the incoming SDF volumes thicker or thinner by pushing or pulling the surface by adding a constant value to the distance. PushPullSDF 處理 SDF... | in Amount:float, AmountField:ShaderGraphNode, SdfField:ShaderGraphNode, StepScale:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/PushPullSDF.cs`; .t3 `Operators/Lib/field/adjust/PushPullSDF.t3`; docs `.help/docs/operators/lib/field/adjust/PushPullSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.SetSDFMaterial` | Injects a Color provider into the shader graph. There can be multiple colors which can be mixed with CombineSDF SetSDFMaterial 用來調整、轉換或重新映射影像色彩。 | in Color:Vector4, ColorField:ShaderGraphNode, SdfField:ShaderGraphNode; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/SetSDFMaterial.cs`; .t3 `Operators/Lib/field/adjust/SetSDFMaterial.t3`; docs `.help/docs/operators/lib/field/adjust/SetSDFMaterial.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.SpatialDisplaceSDF` | 3D spatial distortion of an SDF Samples simplex noise at 3 positions to apply spatial warping SpatialDisplaceSDF 處理 SDF / shader field 資料。 | in Amount:float, InputField:ShaderGraphNode, Offset:Vector3, SamplePos:Vector3, Scale:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/SpatialDisplaceSDF.cs`; .t3 `Operators/Lib/field/adjust/SpatialDisplaceSDF.t3`; docs `.help/docs/operators/lib/field/adjust/SpatialDisplaceSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust.TranslateUV` | Moves the UV coordinates of the incoming SDF field along the X, Y, and Z axes. To scale the UV coordinates, use ‘Texture Scale’ in RaymarchField. T... | in InputField:ShaderGraphNode, Translation:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/TranslateUV.cs`; .t3 `Operators/Lib/field/adjust/TranslateUV.t3`; docs `.help/docs/operators/lib/field/adjust/TranslateUV.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.adjust._._ExecuteSdfToColor` | _官方摘要缺失。_ | in GainAndBias:Vector2, GradientSrv:ShaderResourceView, InputField:ShaderGraphNode, Mapping:int, Offset:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/_/_ExecuteSdfToColor.cs`; .t3 `Operators/Lib/field/adjust/_/_ExecuteSdfToColor.t3`; docs `Unknown` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.adjust._._ExecuteSdfToColor_Old` | _官方摘要缺失。_ | in GradientSrv:ShaderResourceView, InputField:ShaderGraphNode, Range:Vector2; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/_/_ExecuteSdfToColor_Old.cs`; .t3 `Operators/Lib/field/adjust/_/_ExecuteSdfToColor_Old.t3`; docs `Unknown` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.adjust._._SDFToColor_Old` | _官方摘要缺失。_ | in Gradient:Gradient, InputField:ShaderGraphNode, Range:Vector2; out Result:ShaderGraphNode | C# `Operators/Lib/field/adjust/_/_SDFToColor_Old.cs`; .t3 `Operators/Lib/field/adjust/_/_SDFToColor_Old.t3`; docs `Unknown` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.analyze.VisualizeFieldDistance` | Visualizes the distance a field by drawing a plane with contour lines. This plane can be rotated or moved through your field. This can be very usef... | in Background:Vector4, Center:Vector3, EnableZTest:bool, EnableZWrite:bool, LineColor:Vector4, +3 more; out DrawCommand:Command | C# `Operators/Lib/field/analyze/VisualizeFieldDistance.cs`; .t3 `Operators/Lib/field/analyze/VisualizeFieldDistance.t3`; docs `.help/docs/operators/lib/field/analyze/VisualizeFieldDistance.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.combine.BlendSDFWithSDF` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* BlendSDFWithSDF 位在「Fields / SDF / shader graph」的 c... | in FieldA:ShaderGraphNode, FieldB:ShaderGraphNode, Offset:float, Range:float, WeightField:ShaderGraphNode; out Result:ShaderGraphNode | C# `Operators/Lib/field/combine/BlendSDFWithSDF.cs`; .t3 `Operators/Lib/field/combine/BlendSDFWithSDF.t3`; docs `.help/docs/operators/lib/field/combine/BlendSDFWithSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.combine.CombineFieldColor` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* CombineFieldColor 位在「Fields / SDF / shader graph」的... | in CombineMethod:int, InputFields:ShaderGraphNode, K:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/combine/CombineFieldColor.cs`; .t3 `Operators/Lib/field/combine/CombineFieldColor.t3`; docs `.help/docs/operators/lib/field/combine/CombineFieldColor.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.combine.CombineSDF` | Combines two or more connected fields with the provided blend method. CombineSDF 處理 SDF / shader field 資料。 | in CombineMethod:int, InputFields:ShaderGraphNode, K:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/combine/CombineSDF.cs`; .t3 `Operators/Lib/field/combine/CombineSDF.t3`; docs `.help/docs/operators/lib/field/combine/CombineSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.combine.StairCombineSDF` | A set of sdf combine operations that will turn intersections into stairs or bumps. StairCombineSDF 處理 SDF / shader field 資料。 | in CombineMethod:int, InputFields:ShaderGraphNode, K:float, Steps:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/combine/StairCombineSDF.cs`; .t3 `Operators/Lib/field/combine/StairCombineSDF.t3`; docs `.help/docs/operators/lib/field/combine/StairCombineSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.BoxFrameSDF` | Generates a procedural Box Frame field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Can be used with Repeat... | in Center:Vector3, Size:Vector3, Thickness:float, UniformScale:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/BoxFrameSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/BoxFrameSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/BoxFrameSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.BoxSDF` | Generates a procedural box field with rounded edges which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Also known... | in Center:Vector3, EdgeRadius:float, Size:Vector3, UniformScale:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/BoxSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/BoxSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/BoxSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.CappedTorusSDF` | Generates a procedural torus field which can be capped (with rounded edges). It can be rendered with RaymarchField and visualized with VisualizeFie... | in Axis:int, Center:Vector3, Fill:float, Radius:float, Thickness:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/CappedTorusSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/CappedTorusSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/CappedTorusSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.CapsuleLineSDF` | Generates a procedural capsule field by connecting two points. It can be rendered with RaymarchField and visualized with VisualizeFieldDistance. It... | in Center:Vector3, EndPoint:Vector3, StartingPoint:Vector3, Thickness:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/CapsuleLineSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/CapsuleLineSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/CapsuleLineSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.ChainLinkSDF` | Generates a procedural chain link field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Can be used with Repea... | in Center:Vector3, Length:float, Size:float, Thickness:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/ChainLinkSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/ChainLinkSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/ChainLinkSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.CustomSDF` | Creates a custom SDF with optional parameters. The default code generates a procedural sphere field which can be rendered with RaymarchField and vi... | in A:float, AdditionalDefines:string, B:float, C:float, DistanceFunction:string, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/CustomSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/CustomSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/CustomSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.CylinderSDF` | Generates a procedural cylinder field with rounded edges which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. It ca... | in Axis:int, Center:Vector3, Height:float, Radius:float, Rounding:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/CylinderSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/CylinderSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/CylinderSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.FractalSDF` | Generates a procedural MandelBox field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. For more Fractals check... | in Clamping:Vector3, Fold:Vector2, Increment:Vector3, Iterations:int, Minrad:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/FractalSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/FractalSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/FractalSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.HeightMapSdf` | Generates an SDF from a texture HeightMapSdf 生成一個 SDF / 距離場，通常接到 RaymarchField 或其他 field 節點繼續變形、合併、渲染。 | in DisplacementHeight:float, MaxHeight:float, MaxSlope:float, SdfImage:Texture2D, UvOffset:Vector2, +2 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/HeightMapSdf.cs`; .t3 `Operators/Lib/field/generate/sdf/HeightMapSdf.t3`; docs `.help/docs/operators/lib/field/generate/sdf/HeightMapSdf.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.Image2dSDF` | Uses the grayscale information of a texture as (signed) distance data. This works nicely with JumpFloodFill Image2dSDF 生成一個 SDF / 距離場，通常接到 Raymarch... | in ImageSize:Vector2, Offset:float, SdfImage:Texture2D, SdfScale:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/Image2dSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/Image2dSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/Image2dSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.OctahedronSDF` | Generates a octahedron distande field. BoxSDF PlaneSDF CombineSDF RaymarchField OctahedronSDF 生成一個 SDF / 距離場，通常接到 RaymarchField 或其他 field 節點繼續變形、合併... | in Center:Vector3, EdgeRadius:float, Size:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/OctahedronSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/OctahedronSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/OctahedronSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.PlaneSDF` | Creates a flat surface as an SDF field. Generates a procedural flat surface field which can be rendered with RaymarchField and visualized with Visu... | in Axis:int, Center:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/PlaneSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/PlaneSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/PlaneSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.PrismSDF` | Generates a prism SDF (i.e. an n-sided cylinder). NOTE: the triangular prism does not support rounding. PrismSDF 生成一個 SDF / 距離場，通常接到 RaymarchField... | in Axis:int, Center:Vector3, EdgeRadius:float, Length:float, Radius:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/PrismSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/PrismSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/PrismSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.PyramidSDF` | Generates a pyramid distande field. BoxSDF PlaneSDF CombineSDF RaymarchField PyramidSDF 生成一個 SDF / 距離場，通常接到 RaymarchField 或其他 field 節點繼續變形、合併、渲染。 | in Axis:int, Center:Vector3, Rounding:float, Scale:Vector3, UniformScale:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/PyramidSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/PyramidSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/PyramidSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.RotatedPlaneSDF` | A plane defined by center and normal vector. This is similar to PlaneSDF but allows finer control. RotatedPlaneSDF 生成一個 SDF / 距離場，通常接到 RaymarchFiel... | in Center:Vector3, Normal:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/RotatedPlaneSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/RotatedPlaneSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/RotatedPlaneSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.SphereSDF` | Generates a procedural sphere field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. It can be modified with Be... | in Center:Vector3, Radius:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/SphereSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/SphereSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/SphereSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf.TorusSDF` | Generates a procedural torus field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Also known as: Donut SDF It... | in Axis:int, Center:Vector3, Radius:float, Thickness:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/TorusSDF.cs`; .t3 `Operators/Lib/field/generate/sdf/TorusSDF.t3`; docs `.help/docs/operators/lib/field/generate/sdf/TorusSDF.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.sdf._.ExecuteHeightmapSdf` | _官方摘要缺失。_ | in MaxHeight:float, MaxSlope:float, Scale:float, SdfImageSrv:ShaderResourceView, UvOffset:Vector2, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/_/ExecuteHeightmapSdf.cs`; .t3 `Operators/Lib/field/generate/sdf/_/ExecuteHeightmapSdf.t3`; docs `Unknown` | D | Internal image/SDF helper with `ShaderResourceView`; keep document-only until caller/runtime contract is pinned. |
| `Lib.field.generate.sdf._.ExecuteImage2dSdf` | _官方摘要缺失。_ | in Offset:float, Scale:float, SdfImageSrv:ShaderResourceView, Size:Vector2; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/_/ExecuteImage2dSdf.cs`; .t3 `Operators/Lib/field/generate/sdf/_/ExecuteImage2dSdf.t3`; docs `Unknown` | D | Internal image/SDF helper with `ShaderResourceView`; keep document-only until caller/runtime contract is pinned. |
| `Lib.field.generate.sdf._.JonBakerSDFLoader` | _官方摘要缺失。_ | in A:float, B:float, C:float, ListSelect:int, Offset:Vector3, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/sdf/_/JonBakerSDFLoader.cs`; .t3 `Operators/Lib/field/generate/sdf/_/JonBakerSDFLoader.t3`; docs `Unknown` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.generate.texture.Raster3dField` | Uses TiXL's Raymarching integration to generate a procedural raster 3D Texture that can be mapped onto meshes via DrawMesh. Connects into the 'Frag... | in ColorA:Vector4, ColorB:Vector4, Feather:float, LineWidth:float, Offset:Vector3, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/texture/Raster3dField.cs`; .t3 `Operators/Lib/field/generate/texture/Raster3dField.t3`; docs `.help/docs/operators/lib/field/generate/texture/Raster3dField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.texture.SubDivPattern3d` | Generates a 3D pattern similar to SubDivisionStretch. SubDivPattern3d：Generates a 3D pattern similar to SubDivisionStretch. | in ColorA:Vector4, ColorB:Vector4, ColorMode:int, Feather:float, GapColor:Vector4, +7 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/texture/SubDivPattern3d.cs`; .t3 `Operators/Lib/field/generate/texture/SubDivPattern3d.t3`; docs `.help/docs/operators/lib/field/generate/texture/SubDivPattern3d.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.generate.vec3.ToroidalVortexField` | Generates a torus like vector field. ToroidalVortexField：Generates a torus-like vector field. | in Axis:int, Center:Vector3, FallOffRate:float, RadialGain:float, Radius:float, +2 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/generate/vec3/ToroidalVortexField.cs`; .t3 `Operators/Lib/field/generate/vec3/ToroidalVortexField.t3`; docs `.help/docs/operators/lib/field/generate/vec3/ToroidalVortexField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.render.RaymarchField` | Renders the connected shader graph SDF. It uses the following SetMaterial, SetFog and PointLight override. It will correctly initialize the depth b... | in AmbientOcclusion:Vector4, AoDistance:float, Color:Vector4, DistToColor:float, MaxDistance:float, +9 more; out DrawCommand:Command, ShaderCode:string | C# `Operators/Lib/field/render/RaymarchField.cs`; .t3 `Operators/Lib/field/render/RaymarchField.t3`; docs `.help/docs/operators/lib/field/render/RaymarchField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.render.Render2dField` | Renders a 2d color field into a texture. Render2dField 用來調整、轉換或重新映射影像色彩。 | in AmbientOcclusion:Vector4, AoDistance:float, Background:Vector4, ColorField:ShaderGraphNode, DistToColor:float, +9 more; out DrawCommand:Command | C# `Operators/Lib/field/render/Render2dField.cs`; .t3 `Operators/Lib/field/render/Render2dField.t3`; docs `.help/docs/operators/lib/field/render/Render2dField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.render.SampleFieldPoints` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* SampleFieldPoints 位在「Fields / SDF / shader graph」的... | in Field:ShaderGraphNode, Points:BufferWithViews, WriteTo:int; out Result2:BufferWithViews | C# `Operators/Lib/field/render/SampleFieldPoints.cs`; .t3 `Operators/Lib/field/render/SampleFieldPoints.t3`; docs `.help/docs/operators/lib/field/render/SampleFieldPoints.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.render._.GenerateShaderGraphCode` | _官方摘要缺失。_ | in AdditionalDefines:string, Field:ShaderGraphNode, TemplateFilePath:string; out FloatParams:Buffer, Resources:Object, ShaderCode:string | C# `Operators/Lib/field/render/_/GenerateShaderGraphCode.cs`; .t3 `Operators/Lib/field/render/_/GenerateShaderGraphCode.t3`; docs `Unknown` | D | Internal ShaderGraphNode codegen and parameter/resource assembly; should become My World shader graph runtime, not a first-pass Vuo node. |
| `Lib.field.space.BendField` | Bends / curves the incoming field along the given axis. This works best for small source volumes within a limited unit range. It can be rendered wi... | in Amount:float, Axis:int, InputField:ShaderGraphNode, StepFactor:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/BendField.cs`; .t3 `Operators/Lib/field/space/BendField.t3`; docs `.help/docs/operators/lib/field/space/BendField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.ReflectField` | Folds / kinks / reflects the incoming field through a plane. It can be rendered with RaymarchField and visualized with VisualizeFieldDistance. It n... | in InputField:ShaderGraphNode, Offset:float, PlaneNormal:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/ReflectField.cs`; .t3 `Operators/Lib/field/space/ReflectField.t3`; docs `.help/docs/operators/lib/field/space/ReflectField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RepeatAxis` | Infinitely mirrors and repeats the incoming field along the defined axis. Similar node: RepeatFieldLimit, RepeatField3 It can be rendered with Raym... | in Axis:int, InputField:ShaderGraphNode, Mirror:bool, Size:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RepeatAxis.cs`; .t3 `Operators/Lib/field/space/RepeatAxis.t3`; docs `.help/docs/operators/lib/field/space/RepeatAxis.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RepeatField3` | Infinitely repeats the incoming field in every direction. Similar node: RepeatFieldLimit It can be rendered with RaymarchField and visualized with... | in InputField:ShaderGraphNode, Size:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RepeatField3.cs`; .t3 `Operators/Lib/field/space/RepeatField3.t3`; docs `.help/docs/operators/lib/field/space/RepeatField3.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RepeatFieldAtPoints` | Repeats the incoming field at the connected points. Warning: This operator is extremely slow especially when used with RayMarchField. Therefore the... | in CombineMethod:int, InputField:ShaderGraphNode, K:float, Points:BufferWithViews; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RepeatFieldAtPoints.cs`; .t3 `Operators/Lib/field/space/RepeatFieldAtPoints.t3`; docs `.help/docs/operators/lib/field/space/RepeatFieldAtPoints.md` | D | Field repetition depends on point transforms / structured buffers and is marked slow/clamped in TiXL docs. |
| `Lib.field.space.RepeatFieldLimit` | Repeats the incoming field in the defined direction. It can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Other nodes:... | in Axis:int, InputField:ShaderGraphNode, Size:float, Start:float, Stop:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RepeatFieldLimit.cs`; .t3 `Operators/Lib/field/space/RepeatFieldLimit.t3`; docs `.help/docs/operators/lib/field/space/RepeatFieldLimit.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RepeatPolar` | Mirrors and rotates the incoming field in the desired number around a central axis. It can be rendered with RaymarchField and visualized with Visua... | in Axis:int, InputField:ShaderGraphNode, Mirror:bool, Offset:float, Repetitions:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RepeatPolar.cs`; .t3 `Operators/Lib/field/space/RepeatPolar.t3`; docs `.help/docs/operators/lib/field/space/RepeatPolar.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RotateAxis` | Rotates the incoming field in 3D space. Similar node with more options: TransformField It can be rendered with RaymarchField and visualized with Vi... | in Axis:int, InputField:ShaderGraphNode, Rotation:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RotateAxis.cs`; .t3 `Operators/Lib/field/space/RotateAxis.t3`; docs `.help/docs/operators/lib/field/space/RotateAxis.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.RotateField` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* RotateField 位在「Fields / SDF / shader graph」的 space... | in InputField:ShaderGraphNode, Rotation:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/RotateField.cs`; .t3 `Operators/Lib/field/space/RotateField.t3`; docs `.help/docs/operators/lib/field/space/RotateField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.TransformField` | Transforms, rotates and scales the incoming field in 3D space. Similar node with fewer options: Translate It can be rendered with RaymarchField and... | in InputField:ShaderGraphNode, Pivot:Vector3, RotateFieldVecs:bool, Rotation:Vector3, Scale:Vector3, +3 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/TransformField.cs`; .t3 `Operators/Lib/field/space/TransformField.t3`; docs `.help/docs/operators/lib/field/space/TransformField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.Translate` | Transforms the position of the field in 3D space. Similar node with more options: TransformField It can be rendered with RaymarchField and visualiz... | in InputField:ShaderGraphNode, Translation:Vector3; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/Translate.cs`; .t3 `Operators/Lib/field/space/Translate.t3`; docs `.help/docs/operators/lib/field/space/Translate.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space.TwistField` | Twists the input field along the given axis. This works best for small source volumes within unit range. It can be rendered with RaymarchField and... | in Amount:float, Axis:int, InputField:ShaderGraphNode, StepFactor:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/TwistField.cs`; .t3 `Operators/Lib/field/space/TwistField.t3`; docs `.help/docs/operators/lib/field/space/TwistField.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.space._.ExecuteRepeatFieldAtPoints` | _官方摘要缺失。_ | in CombineMethod:int, InputField:ShaderGraphNode, K:float, Points:BufferWithViews; out Result:ShaderGraphNode | C# `Operators/Lib/field/space/_/ExecuteRepeatFieldAtPoints.cs`; .t3 `Operators/Lib/field/space/_/ExecuteRepeatFieldAtPoints.t3`; docs `Unknown` | D | Field repetition depends on point transforms / structured buffers and is marked slow/clamped in TiXL docs. |
| `Lib.field.use.ApplyVectorField` | Applies a signed XYZ vector field by setting the rotation and F1 to a set of points. Ideally this requires a _vector_ field, not a color or SDF, wi... | in ClampLength:float, Normalize:bool, Points:BufferWithViews, ScaleLength:float, SetFx1To:int, +5 more; out Result2:BufferWithViews | C# `Operators/Lib/field/use/ApplyVectorField.cs`; .t3 `Operators/Lib/field/use/ApplyVectorField.t3`; docs `.help/docs/operators/lib/field/use/ApplyVectorField.md` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.use.RaymarchPoints` | Moves points along their Z-axis until they hit an SDF surface. This can be used for visualizing raymarching or simlurate rays being reflected from... | in Field:ShaderGraphNode, MaxDistance:float, MaxReflectionCount:int, MaxSteps:int, MinDistance:float, +6 more; out Result2:BufferWithViews | C# `Operators/Lib/field/use/RaymarchPoints.cs`; .t3 `Operators/Lib/field/use/RaymarchPoints.t3`; docs `.help/docs/operators/lib/field/use/RaymarchPoints.md` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.use.SDFToColor` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* SDFToColor 位在「Fields / SDF / shader graph」的 use 類節... | in GainAndBias:Vector2, Gradient:Gradient, InputField:ShaderGraphNode, Mapping:int, Offset:float, +1 more; out Result:ShaderGraphNode | C# `Operators/Lib/field/use/SDFToColor.cs`; .t3 `Operators/Lib/field/use/SDFToColor.t3`; docs `.help/docs/operators/lib/field/use/SDFToColor.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.field.use.SdfReflectionLinePoints` | Moves points along their Z-axis until they hit an SDF surface. This can be used for visualizing raymarching or simlurate rays being reflected from... | in Field:ShaderGraphNode, MaxDistance:float, MaxReflectionCount:int, MaxSteps:int, MinDistance:float, +5 more; out Result2:BufferWithViews | C# `Operators/Lib/field/use/SdfReflectionLinePoints.cs`; .t3 `Operators/Lib/field/use/SdfReflectionLinePoints.t3`; docs `.help/docs/operators/lib/field/use/SdfReflectionLinePoints.md` | D | Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime. |
| `Lib.field.use.SdfToVector` | Converts an SDF to a direction vector field by sampling the gradient. SdfToVector 處理 SDF / shader field 資料。 | in InputField:ShaderGraphNode, LookUpDistance:float; out Result:ShaderGraphNode | C# `Operators/Lib/field/use/SdfToVector.cs`; .t3 `Operators/Lib/field/use/SdfToVector.t3`; docs `.help/docs/operators/lib/field/use/SdfToVector.md` | C | ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly. |
| `Lib.particle.ParticleSystem` | Emits particles on emit points and applies the connected forces. Please check the how-to linked below HowToUseParticles. ParticleSystem：Emits parti... | in Drag:float, Emit:bool, EmitMode:int, EmitPoints:BufferWithViews, EmitVelocity:float, +11 more; out OutBuffer:BufferWithViews | C# `Operators/Lib/particle/ParticleSystem.cs`; .t3 `Operators/Lib/particle/ParticleSystem.t3`; docs `.help/docs/operators/lib/particle/ParticleSystem.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.AxisStepForce` | A force for the ParticleSystem that applies random accelerations toward an axis direction. This can lead to interesting results in motion design. I... | in AddOriginalVelocity:float, ApplyTrigger:bool, AxisDistribution:Vector3, AxisSpace:int, RandomizeStrength:float, +4 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/AxisStepForce.cs`; .t3 `Operators/Lib/particle/force/AxisStepForce.t3`; docs `.help/docs/operators/lib/particle/force/AxisStepForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.CollisionForce` | A simple simulation of sphere collision between points. The radius of the points is defined on emit by the ParticleSystem's PointRadiusW factor. So... | in Attraction:float, AttractionDecay:float, Bounciness:float, CellSize:float, CollistionResolve:float, +1 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/CollisionForce.cs`; .t3 `Operators/Lib/particle/force/CollisionForce.t3`; docs `.help/docs/operators/lib/particle/force/CollisionForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.CustomForce` | Allows to write small shaders to create custom particle force effects. CustomForce：Allows writing small shaders for custom particle forces. | in A:float, AdditionalDefines:string, Amount:float, B:float, C:float, +10 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/CustomForce.cs`; .t3 `Operators/Lib/particle/force/CustomForce.t3`; docs `.help/docs/operators/lib/particle/force/CustomForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.DirectionalForce` | Generates a force acting on particles in a defined direction in space. Useful for simulating gravity, wind or attractive forces. DirectionalForce：G... | in Amount:float, Direction:Vector3, RandomAmount:float, ShowGizmo:GizmoVisibility; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/DirectionalForce.cs`; .t3 `Operators/Lib/particle/force/DirectionalForce.t3`; docs `.help/docs/operators/lib/particle/force/DirectionalForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.FieldDistanceForce` | Tries to keep particles within an SDF distance range. FieldDistanceForce 處理 SDF / shader field 資料。 | in Amount:float, Attraction:float, DecayWithDistance:float, Field:ShaderGraphNode, NormalSamplingDistance:float, +1 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/FieldDistanceForce.cs`; .t3 `Operators/Lib/particle/force/FieldDistanceForce.t3`; docs `.help/docs/operators/lib/particle/force/FieldDistanceForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.FieldVolumeForce` | Allows the use of a signed distance field as a force to manipulate a particle system. Works as an input for ParticleSystem. Needs an SDF like Cylin... | in Amount:float, ApplyColorOnCollision:bool, Attraction:float, AttractionDecay:float, Bounciness:float, +7 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/FieldVolumeForce.cs`; .t3 `Operators/Lib/particle/force/FieldVolumeForce.t3`; docs `.help/docs/operators/lib/particle/force/FieldVolumeForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.FollowMeshSurfaceForce` | Contains particles close to a surface of a simple(!) mesh geometry. FollowMeshSurfaceForce：Contains particles close to surface of a simple mesh geo... | in Amount:float, MeshBuffers:MeshBuffers, RandomizeSpeed:float, RandomPhase:float, RandomSpin:float, +4 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/FollowMeshSurfaceForce.cs`; .t3 `Operators/Lib/particle/force/FollowMeshSurfaceForce.t3`; docs `.help/docs/operators/lib/particle/force/FollowMeshSurfaceForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.MeshVolumeForce` | Experimental force that moves particles along a low poly mesh. MeshVolumeForce：Experimental force moving particles along a low-poly mesh. | in Amount:float, ApplyColorOnCollision:bool, Attraction:float, AttractionDecay:float, Bounciness:float, +7 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/MeshVolumeForce.cs`; .t3 `Operators/Lib/particle/force/MeshVolumeForce.t3`; docs `.help/docs/operators/lib/particle/force/MeshVolumeForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.RandomJumpForce` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* RandomJumpForce 位在「Particles / forces」的 force 類節點；... | in Amount:float, AmountFromVelocity:float, DirectionDistribution:Vector3, Frequency:float, Phase:float, +2 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/RandomJumpForce.cs`; .t3 `Operators/Lib/particle/force/RandomJumpForce.t3`; docs `.help/docs/operators/lib/particle/force/RandomJumpForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.ReconstructiveForce` | Blends the simulation particle points toward the original matching emit points. Note: The effect is likely to produce unpredictable results if the... | in Bias:float, DistanceMode:int, FallOff:float, GizmoVisibility:T3.Core.Operator.GizmoVisibility, Strength:float, +5 more; out ParticleSystem:ParticleSystem | C# `Operators/Lib/particle/force/ReconstructiveForce.cs`; .t3 `Operators/Lib/particle/force/ReconstructiveForce.t3`; docs `.help/docs/operators/lib/particle/force/ReconstructiveForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.SnapToAnglesForce` | Slowly align particle velocity with repeated angle steps on the xy-plane. It works well when combined with TurbulenceForce and PointTrail. Note: Th... | in Amount:float, AngleCount:float, KeepPlanar:float, Mode:int, Twist:float, +2 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/SnapToAnglesForce.cs`; .t3 `Operators/Lib/particle/force/SnapToAnglesForce.t3`; docs `.help/docs/operators/lib/particle/force/SnapToAnglesForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.SwitchParticleForce` | Switches between connected forces for the ParticleSystem. Can be used to switch between forces or deactivate all. The index starts with 0 for the f... | in Index:int, Input:ParticleSystem; out Selected:ParticleSystem | C# `Operators/Lib/particle/force/SwitchParticleForce.cs`; .t3 `Operators/Lib/particle/force/SwitchParticleForce.t3`; docs `.help/docs/operators/lib/particle/force/SwitchParticleForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.TextureMapForce` | Accelerate particles from a signed normal map. The map is stretched to the camera clip space. This force can be very powerful in creating effects,... | in Amount:float, AmountVariation:float, AmountXY:Vector2, CenterDepth:float, Colorization:Vector4, +10 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/TextureMapForce.cs`; .t3 `Operators/Lib/particle/force/TextureMapForce.t3`; docs `.help/docs/operators/lib/particle/force/TextureMapForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.TurbulenceForce` | Adds a turbulence force to a Particle Simulation. Also see SimNoiseOffset and AddNoise TurbulenceForce：Adds turbulence force to particle simulation. | in Amount:float, Frequency:float, Phase:float, ValueField:ShaderGraphNode, Variation:float, +1 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/TurbulenceForce.cs`; .t3 `Operators/Lib/particle/force/TurbulenceForce.t3`; docs `.help/docs/operators/lib/particle/force/TurbulenceForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.VectorFieldForce` | Applies a vector field to the particle velocity. Note: This will constantly pump more energy into your particle velocity. You might need to adjust... | in Amount:float, Randomize:float, VectorField:ShaderGraphNode; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/VectorFieldForce.cs`; .t3 `Operators/Lib/particle/force/VectorFieldForce.t3`; docs `.help/docs/operators/lib/particle/force/VectorFieldForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.VelocityForce` | Controls particle speeds by increasing, damping or limiting the particle velocit.y This can be useful for "pumping" and syncing effects. VelocityFo... | in Accelerate:float, Amount:float, MaxSpeed:float, MinSpeed:float, Variation:float, +1 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/VelocityForce.cs`; .t3 `Operators/Lib/particle/force/VelocityForce.t3`; docs `.help/docs/operators/lib/particle/force/VelocityForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.VerletRibbonForce` | An experimental ribbon force simulation. VerletRibbonForce：Experimental ribbon force simulation. | in ConstrainPasses:int, Damping:float, Direction:Vector3, ReferencePoints:BufferWithViews, RestoreFactor:float, +2 more; out Particles:ParticleSystem | C# `Operators/Lib/particle/force/VerletRibbonForce.cs`; .t3 `Operators/Lib/particle/force/VerletRibbonForce.t3`; docs `.help/docs/operators/lib/particle/force/VerletRibbonForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |
| `Lib.particle.force.VolumeForce` | Repels particles from primitive volumes. This force uses the particle's radius in world units as defined on emit by ParticleSystem. Please check th... | in Amount:float, Attraction:float, AttractionDecay:float, Bounciness:float, GizmoVisibility:T3.Core.Operator.GizmoVisibility, +9 more; out ParticleSystem:ParticleSystem | C# `Operators/Lib/particle/force/VolumeForce.cs`; .t3 `Operators/Lib/particle/force/VolumeForce.t3`; docs `.help/docs/operators/lib/particle/force/VolumeForce.md` | D | ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port. |

## Core Node Cards

## SphereSDF

- TiXL full path: `Lib.field.generate.sdf.SphereSDF`
- Namespace: `Lib.field.generate.sdf`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/generate/sdf/SphereSDF.md`
  - related shader / helper source: Unknown direct HLSL file; C# constructs ShaderGraphNode code fragment.
- Purpose: Generates a procedural sphere field which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. It can be modified with Be...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Center`: `Vector3`, default `Unknown`, role Unknown
  - `Radius`: `float`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.generate.sdf.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## BoxSDF

- TiXL full path: `Lib.field.generate.sdf.BoxSDF`
- Namespace: `Lib.field.generate.sdf`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/generate/sdf/BoxSDF.md`
  - related shader / helper source: Unknown direct HLSL file; C# constructs ShaderGraphNode code fragment.
- Purpose: Generates a procedural box field with rounded edges which can be rendered with RaymarchField and visualized with VisualizeFieldDistance. Also known...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Center`: `Vector3`, default `{X=0.0, Y=0.0, Z=0.0}`, role Unknown
  - `EdgeRadius`: `float`, default `0.05`, role Unknown
  - `Size`: `Vector3`, default `Unknown`, role Unknown
  - `UniformScale`: `float`, default `1.0`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.generate.sdf.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## CustomSDF

- TiXL full path: `Lib.field.generate.sdf.CustomSDF`
- Namespace: `Lib.field.generate.sdf`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/generate/sdf/CustomSDF.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/generate/sdf/CustomSDF.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/generate/sdf/CustomSDF.md`
  - related shader / helper source: `DistanceFunction` string input plus `AdditionalDefines`; no separate helper found beyond ShaderGraphNode codegen.
- Purpose: Creates a custom SDF with optional parameters. The default code generates a procedural sphere field which can be rendered with RaymarchField and vi...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `A`: `float`, default `1.0`, role Unknown
  - `AdditionalDefines`: `string`, default `Unknown`, role Unknown
  - `B`: `float`, default `0.0`, role Unknown
  - `C`: `float`, default `0.0`, role Unknown
  - `DistanceFunction`: `string`, default `Unknown`, role Unknown
  - `Offset`: `Vector3`, default `{X=0.0, Y=0.0, Z=0.0}`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.generate.sdf.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## CombineSDF

- TiXL full path: `Lib.field.combine.CombineSDF`
- Namespace: `Lib.field.combine`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/combine/CombineSDF.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/combine/CombineSDF.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/combine/CombineSDF.md`
  - related shader / helper source: Mercury hg SDF combine logic mentioned in `Operators/Lib/field/combine/CombineSDF.t3ui`; helper file Unknown.
- Purpose: Combines two or more connected fields with the provided blend method. CombineSDF 處理 SDF / shader field 資料。
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `CombineMethod`: `int`, default `Unknown`, role Unknown
  - `InputFields`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `K`: `float`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.combine.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## SetSDFMaterial

- TiXL full path: `Lib.field.adjust.SetSDFMaterial`
- Namespace: `Lib.field.adjust`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/adjust/SetSDFMaterial.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/adjust/SetSDFMaterial.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/adjust/SetSDFMaterial.md`
  - related shader / helper source: ShaderGraphNode color/material injection; helper file Unknown.
- Purpose: Injects a Color provider into the shader graph. There can be multiple colors which can be mixed with CombineSDF SetSDFMaterial 用來調整、轉換或重新映射影像色彩。
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Color`: `Vector4`, default `Unknown`, role Unknown
  - `ColorField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `SdfField`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.adjust.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## NoiseDisplaceSDF

- TiXL full path: `Lib.field.adjust.NoiseDisplaceSDF`
- Namespace: `Lib.field.adjust`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/adjust/NoiseDisplaceSDF.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/adjust/NoiseDisplaceSDF.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/adjust/NoiseDisplaceSDF.md`
  - related shader / helper source: ShaderGraphNode noise displacement; docs warn it breaks Lipschitz continuity; helper file Unknown.
- Purpose: Displaces the distance of an SDF with a Perlin-like noise offset. NOTE: this operator will break the Lipschitz continuity of your field and will ca...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Amount`: `float`, default `0.5`, role Unknown
  - `InputField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Offset`: `Vector3`, default `Unknown`, role Unknown
  - `Scale`: `float`, default `Unknown`, role Unknown
  - `StepFactor`: `float`, default `Unknown`, role Unknown
  - `UseLocalSpace`: `bool`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.adjust.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## TransformField

- TiXL full path: `Lib.field.space.TransformField`
- Namespace: `Lib.field.space`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/space/TransformField.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/space/TransformField.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/space/TransformField.md`
  - related shader / helper source: C# assembles transform matrix parameters in `Operators/Lib/field/space/TransformField.cs`; helper file Unknown.
- Purpose: Transforms, rotates and scales the incoming field in 3D space. Similar node with fewer options: Translate It can be rendered with RaymarchField and...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `InputField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Pivot`: `Vector3`, default `Unknown`, role Unknown
  - `RotateFieldVecs`: `bool`, default `Unknown`, role Unknown
  - `Rotation`: `Vector3`, default `Unknown`, role Unknown
  - `Scale`: `Vector3`, default `Unknown`, role Unknown
  - `Shear`: `Vector3`, default `Unknown`, role Unknown
  - `Translation`: `Vector3`, default `Unknown`, role Unknown
  - `UniformScale`: `float`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.space.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## RepeatField3

- TiXL full path: `Lib.field.space.RepeatField3`
- Namespace: `Lib.field.space`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/space/RepeatField3.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/space/RepeatField3.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/space/RepeatField3.md`
  - related shader / helper source: ShaderGraphNode repeat/modulo space op; helper file Unknown.
- Purpose: Infinitely repeats the incoming field in every direction. Similar node: RepeatFieldLimit It can be rendered with RaymarchField and visualized with...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `InputField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Size`: `Vector3`, default `Unknown`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.space.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## RepeatPolar

- TiXL full path: `Lib.field.space.RepeatPolar`
- Namespace: `Lib.field.space`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/space/RepeatPolar.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/space/RepeatPolar.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/space/RepeatPolar.md`
  - related shader / helper source: ShaderGraphNode mirror/rotate repeat op; helper file Unknown.
- Purpose: Mirrors and rotates the incoming field in the desired number around a central axis. It can be rendered with RaymarchField and visualized with Visua...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Axis`: `int`, default `Unknown`, role Unknown
  - `InputField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Mirror`: `bool`, default `Unknown`, role Unknown
  - `Offset`: `float`, default `Unknown`, role Unknown
  - `Repetitions`: `float`, default `8.0`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.space.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## RaymarchField

- TiXL full path: `Lib.field.render.RaymarchField`
- Namespace: `Lib.field.render`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/render/RaymarchField.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/render/RaymarchField.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/render/RaymarchField.md`
  - related shader / helper source: `Lib:shaders/img/generate/RaymarchSDFFieldWithMatTemplate.hlsl`, `Lib:shaders/img/generate/RaymarchSDFFieldTemplate.hlsl`, `Operators/Lib/field/render/_/GenerateShaderGraphCode.cs`.
- Purpose: Renders the connected shader graph SDF. It uses the following SetMaterial, SetFog and PointLight override. It will correctly initialize the depth b...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `AmbientOcclusion`: `Vector4`, default `{X=1e-06, Y=9.9999e-07, Z=9.9999e-07, W=1.0}`, role Unknown
  - `AoDistance`: `float`, default `1.0`, role Unknown
  - `Color`: `Vector4`, default `{X=1.0, Y=1.0, Z=1.0, W=1.0}`, role Unknown
  - `DistToColor`: `float`, default `0.15`, role Unknown
  - `MaxDistance`: `float`, default `300.0`, role Unknown
  - `MaxSteps`: `float`, default `100.0`, role Unknown
  - `MinDistance`: `float`, default `0.002`, role Unknown
  - `NormalSamplingD`: `float`, default `0.002`, role Unknown
  - `SdfField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `SpecularAA`: `float`, default `0.5`, role Unknown
  - `StepSize`: `float`, default `1.0`, role Unknown
  - `TextureScale`: `float`, default `1.0`, role Unknown
  - `UVMapping`: `int`, default `1`, role Unknown
  - `WriteDepth`: `bool`, default `True`, role Unknown
- Outputs:
  - `DrawCommand`: `Command`, default `Unknown`, role Unknown
  - `ShaderCode`: `string`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.render.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## SDFToColor

- TiXL full path: `Lib.field.use.SDFToColor`
- Namespace: `Lib.field.use`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/use/SDFToColor.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/use/SDFToColor.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/use/SDFToColor.md`
  - related shader / helper source: Uses SDF color mapping graph; internal older helpers under `Operators/Lib/field/adjust/_`; exact active shader helper Unknown.
- Purpose: *No description yet. Edit this operator's description in the TiXL editor to populate this page.* SDFToColor 位在「Fields / SDF / shader graph」的 use 類節...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `GainAndBias`: `Vector2`, default `{X=0.5, Y=0.5}`, role Unknown
  - `Gradient`: `Gradient`, default `<Gradient default>`, role Unknown
  - `InputField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Mapping`: `int`, default `0`, role Unknown
  - `Offset`: `float`, default `0.0`, role Unknown
  - `Range`: `float`, default `1.0`, role Unknown
- Outputs:
  - `Result`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.use.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - C: ShaderGraphNode/SDF field composition; Vuo can approximate final image/scene output, but not TiXL composable field graph directly.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## RaymarchPoints

- TiXL full path: `Lib.field.use.RaymarchPoints`
- Namespace: `Lib.field.use`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/field/use/RaymarchPoints.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/field/use/RaymarchPoints.t3`
  - docs: `external/tixl/.help/docs/operators/lib/field/use/RaymarchPoints.md`
  - related shader / helper source: `Lib:shaders/points/modify/MovePointsForwardToSDF.hlsl`, `GenerateShaderGraphCode`, `ComputeShaderStage`, `StructuredBufferWithViews`.
- Purpose: Moves points along their Z-axis until they hit an SDF surface. This can be used for visualizing raymarching or simlurate rays being reflected from...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `shader graph / image-scene runtime`.
- Inputs:
  - `Field`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `MaxDistance`: `float`, default `100.0`, role Unknown
  - `MaxReflectionCount`: `int`, default `0`, role Unknown
  - `MaxSteps`: `int`, default `20`, role Unknown
  - `MinDistance`: `float`, default `0.005`, role Unknown
  - `Mode`: `int`, default `0`, role Unknown
  - `NormalSamplingDistance`: `float`, default `0.01`, role Unknown
  - `Points`: `BufferWithViews`, default `Unknown`, role Unknown
  - `StepDistanceFactor`: `float`, default `1.0`, role Unknown
  - `WriteDistanceTo`: `int`, default `1`, role Unknown
  - `WriteStepCountTo`: `int`, default `2`, role Unknown
- Outputs:
  - `Result2`: `BufferWithViews`, default `Unknown`, role Unknown
- Runtime behavior:
  - Builds or consumes TiXL ShaderGraphNode. Rendering/use nodes call GenerateShaderGraphCode and/or compute/image shader templates where listed.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.field.use.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: Partial: Vuo has shader/image/scene primitives such as `vuo.image.make.shadertoy2`, `vuo.shader.*`, `vuo.scene.render.image2`, `vuo.scene.make.sphere/cube/torus`, but no composable TiXL ShaderGraphNode/SDF graph.
  - missing Vuo support: ShaderGraphNode DAG, HLSL template injection, SDF material/color graph, raymarch parameter/resource assembly.
- Porting grade:
  - D: Internal or point-buffer helper around SDF sampling/raymarching; requires compute/buffer runtime.
- First implementation recommendation: Keep as My World shader graph runtime research first; Vuo can host output-image or mesh approximations after graph semantics are fixed.
- Verification fixture: Sphere/box SDF through transform/combine into raymarch, fixed camera, compare depth/color image and generated shader text/params.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## ParticleSystem

- TiXL full path: `Lib.particle.ParticleSystem`
- Namespace: `Lib.particle`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/ParticleSystem.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/ParticleSystem.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/ParticleSystem.md`
  - related shader / helper source: `Lib:shaders/particles/ParticleSystem.hlsl`.
- Purpose: Emits particles on emit points and applies the connected forces. Please check the how-to linked below HowToUseParticles. ParticleSystem：Emits parti...
- Conversion: TiXL `Points / point attributes` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Drag`: `float`, default `0.005`, role Unknown
  - `Emit`: `bool`, default `True`, role Unknown
  - `EmitMode`: `int`, default `1`, role Unknown
  - `EmitPoints`: `BufferWithViews`, default `Unknown`, role Unknown
  - `EmitVelocity`: `float`, default `1.0`, role Unknown
  - `EmitVelocityFactor`: `int`, default `0`, role Unknown
  - `LifeTime`: `float`, default `-1.0`, role Unknown
  - `MaxParticleCount`: `int`, default `100000`, role Unknown
  - `OrientTowardsVelocity`: `float`, default `0.15`, role Unknown
  - `ParticleForces`: `ParticleSystem`, default `Unknown`, role Unknown
  - `RadiusFactor`: `float`, default `0.01`, role Unknown
  - `Reset`: `bool`, default `False`, role Unknown
  - `SetF1To`: `int`, default `1`, role Unknown
  - `SetF2To`: `int`, default `0`, role Unknown
  - `Speed`: `float`, default `1.0`, role Unknown
  - `Update`: `bool`, default `True`, role Unknown
- Outputs:
  - `OutBuffer`: `BufferWithViews`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## DirectionalForce

- TiXL full path: `Lib.particle.force.DirectionalForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/DirectionalForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/DirectionalForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/DirectionalForce.md`
  - related shader / helper source: `Lib:shaders/particles/DirectionalForce.hlsl`.
- Purpose: Generates a force acting on particles in a defined direction in space. Useful for simulating gravity, wind or attractive forces. DirectionalForce：G...
- Conversion: TiXL `Numbers / vectors / animation values` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `0.007`, role Unknown
  - `Direction`: `Vector3`, default `{X=0.0, Y=-1.0, Z=0.0}`, role Unknown
  - `RandomAmount`: `float`, default `0.0`, role Unknown
  - `ShowGizmo`: `GizmoVisibility`, default `IfSelected`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## VelocityForce

- TiXL full path: `Lib.particle.force.VelocityForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/VelocityForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/VelocityForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/VelocityForce.md`
  - related shader / helper source: `Lib:shaders/particles/VelocityForce.hlsl`.
- Purpose: Controls particle speeds by increasing, damping or limiting the particle velocit.y This can be useful for "pumping" and syncing effects. VelocityFo...
- Conversion: TiXL `Numbers / vectors / animation values` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Accelerate`: `float`, default `1.0`, role Unknown
  - `Amount`: `float`, default `1.0`, role Unknown
  - `MaxSpeed`: `float`, default `1000.0`, role Unknown
  - `MinSpeed`: `float`, default `0.0`, role Unknown
  - `Variation`: `float`, default `0.0`, role Unknown
  - `VariationGainAndBias`: `Vector2`, default `{X=0.5, Y=0.5}`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## TurbulenceForce

- TiXL full path: `Lib.particle.force.TurbulenceForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/TurbulenceForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/TurbulenceForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/TurbulenceForce.md`
  - related shader / helper source: `Lib:shaders/particles/TurbulanceForce.hlsl` plus `GenerateShaderGraphCode`.
- Purpose: Adds a turbulence force to a Particle Simulation. Also see SimNoiseOffset and AddNoise TurbulenceForce：Adds turbulence force to particle simulation.
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `1.0`, role Unknown
  - `Frequency`: `float`, default `1.0`, role Unknown
  - `Phase`: `float`, default `0.0`, role Unknown
  - `ValueField`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `Variation`: `float`, default `0.0`, role Unknown
  - `VariationGroupCount`: `int`, default `0`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## FieldVolumeForce

- TiXL full path: `Lib.particle.force.FieldVolumeForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/FieldVolumeForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/FieldVolumeForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/FieldVolumeForce.md`
  - related shader / helper source: `Lib:shaders/particles/FieldVolumeForce.hlsl`; also references `FieldDistanceForce.hlsl` stage.
- Purpose: Allows the use of a signed distance field as a force to manipulate a particle system. Works as an input for ParticleSystem. Needs an SDF like Cylin...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `1.0`, role Unknown
  - `ApplyColorOnCollision`: `bool`, default `False`, role Unknown
  - `Attraction`: `float`, default `0.2`, role Unknown
  - `AttractionDecay`: `float`, default `0.0`, role Unknown
  - `Bounciness`: `float`, default `1.0`, role Unknown
  - `Field`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `InvertVolume`: `bool`, default `False`, role Unknown
  - `NormalSamplingDistance`: `float`, default `0.1`, role Unknown
  - `RandomizeBounce`: `float`, default `0.0`, role Unknown
  - `RandomizeReflection`: `float`, default `0.0`, role Unknown
  - `ReflectOnCollision`: `bool`, default `True`, role Unknown
  - `Repulsion`: `float`, default `0.1`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## FieldDistanceForce

- TiXL full path: `Lib.particle.force.FieldDistanceForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/FieldDistanceForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/FieldDistanceForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/FieldDistanceForce.md`
  - related shader / helper source: `Lib:shaders/particles/FieldDistanceForce.hlsl` plus `GenerateShaderGraphCode`.
- Purpose: Tries to keep particles within an SDF distance range. FieldDistanceForce 處理 SDF / shader field 資料。
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `1.0`, role Unknown
  - `Attraction`: `float`, default `1.0`, role Unknown
  - `DecayWithDistance`: `float`, default `0.0`, role Unknown
  - `Field`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `NormalSamplingDistance`: `float`, default `0.01`, role Unknown
  - `Repulsion`: `float`, default `1.0`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## VectorFieldForce

- TiXL full path: `Lib.particle.force.VectorFieldForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/VectorFieldForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/VectorFieldForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/VectorFieldForce.md`
  - related shader / helper source: `Lib:shaders/particles/VectorFieldForce-sg.hlsl`; also references `FieldDistanceForce.hlsl` stage.
- Purpose: Applies a vector field to the particle velocity. Note: This will constantly pump more energy into your particle velocity. You might need to adjust...
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `1.0`, role Unknown
  - `Randomize`: `float`, default `0.0`, role Unknown
  - `VectorField`: `ShaderGraphNode`, default `Unknown`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## CollisionForce

- TiXL full path: `Lib.particle.force.CollisionForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/CollisionForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/CollisionForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/CollisionForce.md`
  - related shader / helper source: `Lib:shaders/cs/particle-PointCollisionForce.hlsl`, `Lib:shaders/particles/ParticleCollisionForce.hlsl`, `StructuredBufferWithViews` spatial hash path.
- Purpose: A simple simulation of sphere collision between points. The radius of the points is defined on emit by the ParticleSystem's PointRadiusW factor. So...
- Conversion: TiXL `Numbers / vectors / animation values` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Attraction`: `float`, default `0.0`, role Unknown
  - `AttractionDecay`: `float`, default `1.0`, role Unknown
  - `Bounciness`: `float`, default `0.0`, role Unknown
  - `CellSize`: `float`, default `0.2`, role Unknown
  - `CollistionResolve`: `float`, default `0.5`, role Unknown
  - `IsEnabled`: `bool`, default `True`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## CustomForce

- TiXL full path: `Lib.particle.force.CustomForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/CustomForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/CustomForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/CustomForce.md`
  - related shader / helper source: `Lib:shaders/particles/CustomForce-Template.hlsl` and user `ShaderCode` string.
- Purpose: Allows to write small shaders to create custom particle force effects. CustomForce：Allows writing small shaders for custom particle forces.
- Conversion: TiXL `Field / SDF / shader graph` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `A`: `float`, default `0.0`, role Unknown
  - `AdditionalDefines`: `string`, default `Unknown`, role Unknown
  - `Amount`: `float`, default `1.0`, role Unknown
  - `B`: `float`, default `0.0`, role Unknown
  - `C`: `float`, default `0.0`, role Unknown
  - `Color`: `Vector4`, default `{X=1.0, Y=1.0, Z=1.0, W=1.0}`, role Unknown
  - `D`: `float`, default `0.0`, role Unknown
  - `Field`: `ShaderGraphNode`, default `Unknown`, role Unknown
  - `GainAndBias`: `Vector2`, default `{X=0.5, Y=0.5}`, role Unknown
  - `Gradient`: `Gradient`, default `<Gradient default>`, role Unknown
  - `Image`: `Texture2D`, default `Unknown`, role Unknown
  - `NormalSamplingDistance`: `float`, default `0.1`, role Unknown
  - `Offset`: `Vector3`, default `{X=0.0, Y=0.0, Z=0.0}`, role Unknown
  - `ShaderCode`: `string`, default `// Attributes: vel, col, pos, idx ; ; // Very simple examples:; float3 vp = pos - Offset;; float d = length(vp);; float f = smoothstep(A,B+1,d);; vel-= vp/(d+0.0001);`, role Unknown
  - `TemplateFile`: `string`, default `Lib:shaders/particles/CustomForce-Template.hlsl`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## TextureMapForce

- TiXL full path: `Lib.particle.force.TextureMapForce`
- Namespace: `Lib.particle.force`
- Clone status: source/spec researched; no Vuo code written
- Source evidence:
  - C#: `external/tixl/Operators/Lib/particle/force/TextureMapForce.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/particle/force/TextureMapForce.t3`
  - docs: `external/tixl/.help/docs/operators/lib/particle/force/TextureMapForce.md`
  - related shader / helper source: `Lib:shaders/particles/ImageForce.hlsl`.
- Purpose: Accelerate particles from a signed normal map. The map is stretched to the camera clip space. This force can be very powerful in creating effects,...
- Conversion: TiXL `Image / Texture2D` -> Vuo/My World `particle GPU runtime`.
- Inputs:
  - `Amount`: `float`, default `1.0`, role Unknown
  - `AmountVariation`: `float`, default `0.0`, role Unknown
  - `AmountXY`: `Vector2`, default `{X=1.0, Y=1.0}`, role Unknown
  - `CenterDepth`: `float`, default `3.0`, role Unknown
  - `Colorization`: `Vector4`, default `{X=1.0, Y=1.0, Z=1.0, W=1.0}`, role Unknown
  - `Colorize`: `float`, default `0.0`, role Unknown
  - `DepthConcentration`: `float`, default `0.005`, role Unknown
  - `ShowGizmo`: `GizmoVisibility`, default `IfSelected`, role Unknown
  - `SignedNormalMap`: `Texture2D`, default `Unknown`, role Unknown
  - `Spin`: `float`, default `0.0`, role Unknown
  - `SpinVariation`: `float`, default `0.0`, role Unknown
  - `Twist`: `float`, default `0.0`, role Unknown
  - `TwistVariation`: `float`, default `0.0`, role Unknown
  - `VariationGainAndBias`: `Vector2`, default `{X=0.5, Y=0.5}`, role Unknown
  - `ViewConfinement`: `float`, default `0.03`, role Unknown
- Outputs:
  - `Particles`: `ParticleSystem`, default `Unknown`, role Unknown
- Runtime behavior:
  - Produces or consumes TiXL ParticleSystem force/state. `.t3` evidence shows ComputeShader/ComputeShaderStage and particle HLSL for common forces.
  - Edge cases: Unknown unless stated in TiXL docs; preserve defaults from C#/.t3.
- Observed graph usage:
  - common incoming nodes: see namespace spec adjacency in `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.particle.force.md`
  - common outgoing nodes: same source; use adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: likely `VuoReal`, `VuoPoint3d`, `VuoColor`, `VuoImage`, `VuoShader`, `VuoSceneObject`, depending on final wrapper.
  - Vuo output types: likely `VuoImage` / `VuoSceneObject` for field render; particle nodes require custom state type outside stock Vuo.
  - direct built-in Vuo equivalent, if any: No direct Vuo built-in ParticleSystem/force equivalent found in `external/vuo/node`; scene random points and mesh point nodes are renderable approximations only.
  - missing Vuo support: GPU particle state buffer, force chaining contract, compute shader dispatch, RWStructuredBuffer/BufferWithViews persistence.
- Porting grade:
  - D: ParticleSystem force/state uses GPU compute, RWStructuredBuffer/BufferWithViews, or particle-force composition; document before Vuo port.
- First implementation recommendation: Do not implement as isolated Vuo node first. First define My World particle buffer/state and GPU compute contract; then wrap selected force kernels.
- Verification fixture: Minimal ParticleSystem with one emit point buffer, one force, deterministic seed/time, compare position/velocity/color buffer after N frames.
- Risks / unknowns: Exact HLSL expansion and resource lifetime must be verified from source; Unknown defaults remain Unknown.

## First-Batch Research Targets

1. `Lib.field.render._.GenerateShaderGraphCode`
2. `Lib.field.render.RaymarchField`
3. `Lib.field.generate.sdf.SphereSDF`
4. `Lib.field.generate.sdf.BoxSDF`
5. `Lib.field.combine.CombineSDF`
6. `Lib.field.space.TransformField`
7. `Lib.field.adjust.NoiseDisplaceSDF`
8. `Lib.particle.ParticleSystem`
9. `Lib.particle.force.FieldVolumeForce`
10. `Lib.particle.force.VectorFieldForce`

## Largest Blockers

- `ShaderGraphNode` is semantic, not cosmetic: field generators, space transforms, combine/adjust/use/render depend on code/parameter/resource assembly. Vuo has shader nodes, but not this composable SDF graph contract.
- Particle forces are not standalone values. They are GPU force stages over persistent particle buffers (`ParticleSystem`, `BufferWithViews`, `RWStructuredBuffer<Particle>`), so Vuo scene points are only visual approximations.
- Several high-value nodes are HLSL-template driven (`RaymarchSDFFieldWithMatTemplate`, `FieldVolumeForce`, `VectorFieldForce-sg`, `CustomForce-Template`). Cross-compiling HLSL semantics to Vuo/OpenGL/Metal path is a separate runtime design problem.
- Internal helper namespaces (`Lib.field.*._`) should not become public Vuo nodes until their callers are pinned down.
