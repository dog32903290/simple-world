# Census: field/ + mesh/ (60 + 51 = 111 ops)

---

## field/ (60 ops)

**Sweep method:** All 60 .cs files confirm output `Slot<ShaderGraphNode>` — the field/ namespace is a single coherent subsystem. Every SDF generator/modifier/space-transformer wraps a ShaderGraphNode inline-code graph. The three "consumer" ops (RaymarchField, Render2dField, VisualizeFieldDistance) output `Slot<Command>` and feed the field graph into a raymarch fragment shader. Use-ops (ApplyVectorField, RaymarchPoints, SdfReflectionLinePoints, SampleFieldPoints) output `Slot<BufferWithViews>` and combine field + points via compute.

**Buckets identified:**

1. **SDF generators** (generate/sdf/ — 16 ops, all ShaderGraphNode output): BoxSDF, SphereSDF, TorusSDF, CylinderSDF, PlaneSDF, OctahedronSDF, PyramidSDF, PrismSDF, CappedTorusSDF, CapsuleLineSDF, ChainLinkSDF, BoxFrameSDF, FractalSDF, RotatedPlaneSDF, CustomSDF, JonBakerSDFLoader. All ShaderGraphNode in/out — pure shader-graph seam.

2. **SDF generators with texture input** (generate/sdf/ — 2 ops): HeightMapSdf, Image2dSDF — output ShaderGraphNode but take `InputSlot<Texture2D>` → need asset-texture seam to be useful.

3. **Texture/noise field generators** (generate/texture/ + generate/vec3/ — 3 ops): Raster3dField, SubDivPattern3d, ToroidalVortexField — all ShaderGraphNode output, pure inline HLSL snippet generators. No external textures.

4. **SDF modifiers / adjust** (adjust/ — 7 ops, all ShaderGraphNode in+out): AbsoluteSDF, InvertSDF, PushPullSDF, SetSDFMaterial, SpatialDisplaceSDF, NoiseDisplaceSDF, TranslateUV. Pure shader-graph manipulation.

5. **Field space transformers** (space/ — 10 ops, all ShaderGraphNode in+out): BendField, ReflectField, RepeatAxis, RepeatField3, RepeatFieldLimit, RepeatPolar, RotateAxis, RotateField, TransformField, Translate. Pure shader-graph seam.

6. **Field-at-points space transformer** (space/ — 2 ops): RepeatFieldAtPoints (ShaderGraphNode+BufferWithViews in → ShaderGraphNode out), ExecuteRepeatFieldAtPoints (ShaderGraphNode+BufferWithViews in → ShaderGraphNode out). Need both shader-graph AND particle-system (point buffer seam).

7. **SDF combiners** (combine/ — 4 ops, all ShaderGraphNode in+out): BlendSDFWithSDF, CombineFieldColor, CombineSDF, StairCombineSDF.

8. **SDF color mapping** (use/ — 2 ops): SDFToColor (ShaderGraphNode → ShaderGraphNode, takes Gradient → also needs gradient-widget), SdfToVector (ShaderGraphNode → ShaderGraphNode, pure shader-graph).

9. **Use / points-output** (use/ — 3 ops, output BufferWithViews): ApplyVectorField (ShaderGraphNode+BufferWithViews → BufferWithViews), RaymarchPoints (ShaderGraphNode+BufferWithViews → BufferWithViews), SdfReflectionLinePoints (ShaderGraphNode+BufferWithViews → BufferWithViews). Need shader-graph + raymarch + particle-system (point buffer).

10. **Render ops** (render/ + analyze/ — 4 ops, output Command): RaymarchField (ShaderGraphNode → Command), Render2dField (ShaderGraphNode → Command), VisualizeFieldDistance (ShaderGraphNode → Command), SampleFieldPoints (Field+Points → BufferWithViews). All need shader-graph + raymarch + camera3d (3D Command context).

11. **Internal helpers** (adjust/_ — 3 ops): _ExecuteSdfToColor (IGraphNodeOp, ShaderGraphNode in/out, takes GradientSrv), _ExecuteSdfToColor_Old, _SDFToColor_Old — internal TiXL pipeline nodes, likely COMPOUND/internal.

12. **Internal render helper** (render/_ — 1 op): GenerateShaderGraphCode — code generator utility for the ShaderGraphNode system itself. Internal.

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AbsoluteSDF | SDF 取絕對值（fold 空間） | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out，純 HLSL snippet |
| InvertSDF | SDF 取反 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| NoiseDisplaceSDF | 以 noise 位移 SDF | shader-graph | BLOCKED:shader-graph | R2 | ShaderGraphNode in/out，內建 noise 計算 |
| PushPullSDF | 用另一 field 推拉 SDF | shader-graph | BLOCKED:shader-graph | R1 | 兩個 ShaderGraphNode 輸入 |
| SetSDFMaterial | 設 SDF 材質色 | shader-graph | BLOCKED:shader-graph | R1 | 兩個 ShaderGraphNode 輸入（SDF+Color） |
| SpatialDisplaceSDF | 以空間偏移位移 SDF | shader-graph | BLOCKED:shader-graph | R2 | ShaderGraphNode in/out |
| TranslateUV | 平移 SDF UV 空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| _ExecuteSdfToColor | 內部：SDF→色 IGraphNodeOp | shader-graph | BLOCKED:shader-graph | R2 | 含 GradientSrv，內部節點 |
| _ExecuteSdfToColor_Old | 舊版（廢棄） | shader-graph | BLOCKED:shader-graph | R3 | 廢棄版本，TiXL WIP 疑慮 |
| _SDFToColor_Old | 舊版（廢棄） | shader-graph | BLOCKED:shader-graph | R3 | 廢棄版本 |
| VisualizeFieldDistance | 視覺化 SDF 距離場 | shader-graph | BLOCKED:shader-graph | R2 | 次要 seam: raymarch, camera3d |
| BlendSDFWithSDF | 混合兩個 SDF | shader-graph | BLOCKED:shader-graph | R2 | 三個 ShaderGraphNode 輸入 |
| CombineFieldColor | 合併多個 Color Field | shader-graph | BLOCKED:shader-graph | R2 | MultiInputSlot<ShaderGraphNode> |
| CombineSDF | union/intersection/difference 合併 SDF | shader-graph | BLOCKED:shader-graph | R2 | MultiInputSlot<ShaderGraphNode>，多 op-mode |
| StairCombineSDF | 階梯式平滑 SDF 合併 | shader-graph | BLOCKED:shader-graph | R2 | MultiInputSlot<ShaderGraphNode> |
| BoxFrameSDF | 方框邊框 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 ShaderGraphNode 輸出，無輸入 field |
| BoxSDF | 立方體 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator，ITransformable |
| CappedTorusSDF | 截斷環面 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| CapsuleLineSDF | 線段膠囊 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| ChainLinkSDF | 鏈節 SDF | shader-graph | BLOCKED:shader-graph | R2 | 參數較多 |
| CustomSDF | 自訂 HLSL SDF | shader-graph | BLOCKED:shader-graph | R3 | 動態 shader 程式碼輸入 |
| CylinderSDF | 圓柱 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| FractalSDF | 碎形 SDF | shader-graph | BLOCKED:shader-graph | R2 | 數學較複雜 |
| HeightMapSdf | 高度圖 SDF | shader-graph | BLOCKED:shader-graph | R2 | 次要 seam: asset-texture（SdfImage 輸入） |
| Image2dSDF | 2D image SDF（SDF 圖） | shader-graph | BLOCKED:shader-graph | R2 | 次要 seam: asset-texture（SdfImage 輸入） |
| OctahedronSDF | 八面體 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| PlaneSDF | 無限平面 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| PrismSDF | 三角柱 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| PyramidSDF | 四角錐 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| RotatedPlaneSDF | 旋轉平面 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| SphereSDF | 球體 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator，ITransformable |
| TorusSDF | 環面 SDF | shader-graph | BLOCKED:shader-graph | R1 | 純 generator |
| ExecuteHeightmapSdf | 內部：執行高度圖 SDF | shader-graph | BLOCKED:shader-graph | R2 | 子 executor，對應 HeightMapSdf |
| ExecuteImage2dSdf | 內部：執行 Image2d SDF | shader-graph | BLOCKED:shader-graph | R2 | 子 executor，對應 Image2dSDF |
| JonBakerSDFLoader | 載入 JonBaker SDF 圖書館 | shader-graph | BLOCKED:shader-graph | R2 | 純 ShaderGraphNode，參數選 SDF 公式 |
| Raster3dField | 3D 光柵色場 | shader-graph | BLOCKED:shader-graph | R1 | 純 ShaderGraphNode generator，無外部 tex |
| SubDivPattern3d | 3D 細分格型場 | shader-graph | BLOCKED:shader-graph | R2 | 純 ShaderGraphNode generator |
| ToroidalVortexField | 環形渦旋向量場 | shader-graph | BLOCKED:shader-graph | R2 | 向量場 ShaderGraphNode |
| RaymarchField | Raymarch SDF 場→Command | shader-graph | BLOCKED:shader-graph | R3 | 主要 seam:shader-graph, 次要: raymarch, camera3d, Layer2d+Execute |
| Render2dField | 2D Raymarch SDF→Command | shader-graph | BLOCKED:shader-graph | R3 | 同 RaymarchField，2D 版 |
| SampleFieldPoints | 對 points 取樣 SDF | shader-graph | BLOCKED:shader-graph | R2 | ShaderGraphNode+BufferWithViews 輸入 → BufferWithViews |
| GenerateShaderGraphCode | 生成 GLSL/HLSL 字串（內部） | shader-graph | BLOCKED:shader-graph | R3 | 內部系統節點，ShaderGraphNode→string |
| BendField | 彎曲空間（SDF 空間扭曲） | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| ReflectField | 反射空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RepeatAxis | 沿軸重複空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RepeatField3 | 3 軸重複空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RepeatFieldAtPoints | 在 PointList 位置重複 Field | shader-graph | BLOCKED:shader-graph | R2 | 次要 seam: particle-system（BufferWithViews） |
| RepeatFieldLimit | 有限重複空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RepeatPolar | 極座標重複 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RotateAxis | 繞軸旋轉空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| RotateField | 旋轉 Field 空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| TransformField | TRS 變換 Field 空間 | shader-graph | BLOCKED:shader-graph | R1 | IGraphNodeOp, ITransformable |
| Translate | 平移 Field 空間 | shader-graph | BLOCKED:shader-graph | R1 | ITransformable |
| TwistField | 扭曲空間 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out |
| ExecuteRepeatFieldAtPoints | 內部：執行 RepeatFieldAtPoints | shader-graph | BLOCKED:shader-graph | R2 | 含 BufferWithViews（point buffer） |
| ApplyVectorField | 把向量場套用到 points | shader-graph | BLOCKED:shader-graph | R2 | ShaderGraphNode+BufferWithViews → BufferWithViews |
| RaymarchPoints | 從 SDF 場 raymarch 到 points | shader-graph | BLOCKED:shader-graph | R3 | 次要: raymarch, particle-system |
| SDFToColor | SDF→色場 ShaderGraphNode（含 Gradient） | shader-graph | BLOCKED:shader-graph | R2 | 次要 seam: gradient-widget（Gradient 輸入） |
| SdfReflectionLinePoints | SDF 反射線 points | shader-graph | BLOCKED:shader-graph | R2 | 次要: raymarch, particle-system |
| SdfToVector | SDF→向量場 | shader-graph | BLOCKED:shader-graph | R1 | ShaderGraphNode in/out，純計算 |

## field/ 摘要

- **總 op 數：** 60
- **READY-LEAF：** 0（field/ 整體被 `shader-graph` seam 完全阻擋）
- **BLOCKED 分佈：**
  - `shader-graph` — 60 顆（100%）
  - 次要 seam（不單獨計，shader-graph 解鎖後才相關）：
    - `raymarch` — 約 6 顆（render/use 組）
    - `camera3d` — 約 3 顆（輸出 Command 的 render 組）
    - `particle-system`（point buffer） — 約 5 顆（RepeatFieldAtPoints 組 + use 組）
    - `gradient-widget` — 2 顆（SDFToColor, _ExecuteSdfToColor）
    - `asset-texture` — 2 顆（HeightMapSdf, Image2dSDF）
- **NEW-SEAM：**
  - `shader-graph` — ShaderGraphNode 內嵌 HLSL 程式碼圖，含 IGraphNodeOp 介面、inline snippet 組合、code-assembly context。這是 field/ 的根基，全 60 顆都擋在這裡。
  - `raymarch` — 3D SDF raymarch 執行管線（在 shader 裡跑 sphere-tracing，RaymarchField/Render2dField 消費）。
- **意外：** field/ 是**完全封閉的一個接縫島**——shader-graph seam 解鎖之後，field 內 50+ 顆輕量 op 可同時開採（SDF generators/modifiers/space-transformers 全部 R1/R2，數學直白）。解鎖優先序：shader-graph → raymarch → camera3d/particle-system（後二不影響 shader-graph 主力）。

---

## mesh/ (51 ops)

**Sweep method:** 所有 mesh op 依輸出型別分三桶：
1. 輸出 `MeshBuffers`（generator/modifier） — 需要 `mesh-pipeline` seam（MeshBuffers 資料型別、vertex/index buffer 分配、CPU 幾何計算）
2. 輸出 `Command`（draw ops） — 需要 `mesh-pipeline` + `Layer2d+Execute`（3D Command context）+ `camera3d`
3. 輸出 `BufferWithViews` — 跨接 mesh ↔ point seam

**Key observations:** 多數 generate/modify 是純 CPU MeshBuffers 計算（C# 幾何生成），無 GPU shader。Draw ops 都需要 3D Command 管線。Compound 均無純 .t3 only（所有 51 顆都有 .cs 實作）。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AnalyzeMeshBuffers | 分析 MeshBuffers，輸出 vertex/index BufferWithViews | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 輸出 BufferWithViews（可接 point ops） |
| LoadGltf | 載入 GLTF 檔→MeshBuffers | gltf-scene | BLOCKED:NEW-SEAM:gltf-scene | R3 | 依賴 SharpGLTF.Schema2，file loader |
| LoadObjEdges | 載入 OBJ 邊緣→MeshBuffers | obj-loader | BLOCKED:NEW-SEAM:obj-loader | R2 | IDescriptiveFilename，file loader |
| UVsViewer | UV 空間展平工具（MeshBuffers→MeshBuffers） | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 調試工具，pass-through 含 UV remap |
| VisualizeMesh | 視覺化 mesh（wireframe/normals → Command） | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: camera3d, Layer2d+Execute |
| _AssembleMeshBuffers | 從 BufferWithViews 組裝 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 接受 vertex/index/chunkDefs buffers |
| _MeshBufferComponents | 拆解 MeshBuffers→vertex/index BufferWithViews | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純分解 |
| DrawMesh | PBR 材質繪製 mesh（→Command） | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, shader-graph（FragmentField） |
| DrawMeshAtPoints | 在 points 位置實例化繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, particle-system, shader-graph |
| DrawMeshCelShading | Cel shading 繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, gradient-widget |
| DrawMeshChunksAtPoints | 以 chunk 方式在 points 繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, particle-system |
| DrawMeshHatched | 網格線條風格繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, asset-texture |
| DrawMeshUnlit | 無光照繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: camera3d, Layer2d+Execute |
| DrawMeshWithShadow | 含陰影繪製 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: camera3d, Layer2d+Execute, shadow seam |
| VisualizeUvMap | 視覺化 UV map（→Command） | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: camera3d, Layer2d+Execute |
| CubeMesh | 生成立方體 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 純 CPU 幾何，多 UV mapper 模式 |
| CylinderMesh | 生成圓柱 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 幾何 |
| DelaunayMesh | Delaunay 三角化→MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 輸入 StructuredList（points），需演算法 |
| ExtrudeCurves | 從 curve points 擠出 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 輸入 BufferWithViews（rail+profile points） |
| IcosahedronMesh | 生成二十面體 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 多 UV mapper 模式，細分面 |
| LoadObj | 載入 OBJ 檔→MeshBuffers | obj-loader | BLOCKED:NEW-SEAM:obj-loader | R2 | IDescriptiveFilename, IStatusProvider，file loader |
| NGonMesh | 生成 N 邊形 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 幾何 |
| QuadMesh | 生成四邊形 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 幾何 |
| RepeatMeshAtPoints | 在 points 位置複製 mesh→MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: particle-system（BufferWithViews 輸入） |
| SphereMesh | 生成球體 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 幾何 |
| TorusMesh | 生成環面 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 幾何 |
| BlendMeshToPoints | 把 mesh vertex 移往 points 位置 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: particle-system |
| BlendMeshVertices | 線性插值兩個 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU，兩 MeshBuffers 輸入 |
| CollapseVertices | 依體積形狀折疊頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | ITransformable，多模式 |
| ColorVerticesWithField | 以 SDF field 上色 mesh 頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: shader-graph（ShaderGraphNode 輸入） |
| CombineMeshes | 合併多個 MeshBuffers | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | MultiInputSlot<MeshBuffers> |
| CustomFaceShader | 自訂 face shader（動態 HLSL+ShaderGraph） | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: shader-graph，動態 shader 程式碼 |
| CustomVertexShader | 自訂 vertex shader（動態 HLSL+ShaderGraph） | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: shader-graph，動態 shader 程式碼 |
| DeformMesh | 球化/錐化/扭曲 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | ITransformable，多種幾何變形 |
| DisplaceMesh | 以 SRV 貼圖位移頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: asset-texture |
| DisplaceMeshNoise | 以 noise 位移頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU 計算 |
| DisplaceMeshVAT | Vertex Animation Texture 位移 | mesh-pipeline | BLOCKED:mesh-pipeline | R3 | 次要: asset-texture（兩張 Texture2D 輸入） |
| FlipNormals | 翻轉法線 | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU |
| MeshFacesPoints | 每個面心生成 point→BufferWithViews | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 輸出 BufferWithViews，次要: asset-texture |
| MeshProjectUV | 投影式 UV 展開 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | ITransformable |
| MoveMeshToPointLine | 沿 point line 移動 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: particle-system |
| PickMeshBuffer | 從多個 MeshBuffers 選一個 | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | MultiInputSlot<MeshBuffers> |
| RecomputeNormals | 重新計算法線 | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU |
| ScatterMeshFaces | 面散射生成 points | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 輸出 MeshBuffers，散射複製 |
| SelectVertices | 選擇頂點子集（with gizmo） | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | ITransformable，vertex selection mask |
| SelectVerticesWithSDF | 以 SDF field 選頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: shader-graph（ShaderGraphNode 輸入） |
| SplitMeshVertices | 分裂頂點（每個面獨立） | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU |
| TextureDisplaceMesh | 以貼圖位移 mesh 頂點 | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 次要: asset-texture，ITransformable |
| TransformMesh | TRS 變換 mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | ITransformable |
| TransformMeshUVs | 變換 UV 座標 | mesh-pipeline | BLOCKED:mesh-pipeline | R1 | 純 CPU UV transform |
| Warp2dMesh | 以 point pair 扭曲 2D mesh | mesh-pipeline | BLOCKED:mesh-pipeline | R2 | 兩組 BufferWithViews 輸入 |

## mesh/ 摘要

- **總 op 數：** 51
- **READY-LEAF：** 0（mesh-pipeline seam 全擋）
- **BLOCKED 分佈：**
  - `mesh-pipeline` — 49 顆（主要阻擋）
  - `gltf-scene`（NEW） — 1 顆（LoadGltf）
  - `obj-loader`（NEW-SEAM，但 LoadObj/LoadObjEdges 已被其他 census 用到） — 2 顆（LoadObj, LoadObjEdges）
  - 次要 seam（mesh-pipeline 解鎖後才相關）：
    - `camera3d` + `Layer2d+Execute` — 8 顆（所有 draw/ ops + VisualizeMesh）
    - `shader-graph` — 4 顆（ColorVerticesWithField, SelectVerticesWithSDF, CustomFaceShader, CustomVertexShader）
    - `particle-system` — 5 顆（RepeatMeshAtPoints, BlendMeshToPoints, MoveMeshToPointLine, ExtrudeCurves, Warp2dMesh）
    - `asset-texture` — 4 顆（DisplaceMesh, DisplaceMeshVAT, TextureDisplaceMesh, DrawMeshHatched/MeshFacesPoints）
    - `gradient-widget` — 1 顆（DrawMeshCelShading）
- **NEW-SEAM：**
  - `mesh-pipeline` — MeshBuffers 型別（vertex/index/chunkDef buffer 組合體）的分配、CPU 幾何計算、TransformCallbackSlot 繫結。這是 mesh/ 的根基，49 顆被擋。
  - `gltf-scene` — SharpGLTF 函式庫解析（GLTF 2.0 scene/mesh 載入），1 顆（LoadGltf）。（`obj-loader` 已在其他 census 中列出，不重複宣告 NEW-SEAM。）
- **意外：**
  - `mesh-pipeline` 解鎖後，最快可採的是純 CPU 幾何 generators（QuadMesh/SphereMesh/TorusMesh/CylinderMesh/NGonMesh/FlipNormals/RecomputeNormals/CombineMeshes — R1，無外部依賴）。
  - `DrawMesh` 含 `ShaderGraphNode FragmentField` 輸入，是 shader-graph × mesh-pipeline 交叉點，R3 最後開採。
  - `DelaunayMesh` 輸入是 `StructuredList`（非 BufferWithViews），這個型別需確認是否已建（其他 census 可能有）。
  - LoadGltf 依賴 `SharpGLTF.Schema2`（外部 NuGet），屬獨立接縫，gltf-scene seam 僅擋 1 顆，投資報酬率低。
