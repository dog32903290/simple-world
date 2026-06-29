# deprecation_candidates — 壓平複合廢棄候選清單

> 生成 2026-06-29（`tools/node_health.sh --discard` 依賴掃描固化）。**只標記、不挪 code、不刪、不加註解**（柏為定：先標記別動）。
> 真相源永遠是 `tools/node_health.sh --discard`（跟 code 走、不 stale）；本檔是某時點快照 + 依賴細節。

## 什麼是「壓平複合」

sw 的節點身份 = `sw 做了` × `TiXL .t3 的 Children 空不空`：

- **真原子**（307）：sw 做了 ∧ TiXL `.t3` Children 空 → 留（真積木）。
- **壓平複合**（148）：sw 做了 ∧ TiXL `.t3` Children **非空**（是子圖複合）→ sw 把 TiXL 的子圖**壓平成單一手刻原子**。
  **廢棄候選 = 等 TiXL .t3 重放管線上線後，用「忠實重放子圖」取代手刻單體。** 在那之前它們是活的、能用的——「候選」不是「現在就砍」。

## 依賴語義（這次掃描的關鍵發現）

依賴掃描 = 在「消費者語料」（`.swproj`/`.scn` 場景圖 + 非定義類 `.cpp/.h`，排除節點自己的 leaf/registrar/golden/selftest）裡 grep `"<Type>"` 整字串。

- **safe（135 顆）**：消費者語料**零引用** → 真要廢棄不會斷下游。
- **deps:N（13 顆）**：有 N 處引用，**幾乎全部落在 `runtime/compound_save.cpp` 的 `atomicUuidTable()`**——
  那是 `.swproj` v2 存檔給「已出貨原子算子型別」釘死的固定 UUID 表。**這代表 sw 已經把這些壓平複合當「原子算子」對待並寫進存檔格式**：
  退役它們會破壞既有 `.swproj` 的載入（舊檔案的節點 UUID 對不上）。所以這 13 顆的「依賴」是**持久化合約**，不是隨手引用 → 退役前要連 save/load migration 一起處理。
  （RadialPoints/LinePoints/GridPoints/SpherePoints/ParticleSystem 等 = param-completion fan-out 的活躍生成器；TiXL 端是 .t3 複合、sw 端是有穩定存檔 UUID 的原子。這個雙重身份本身就是 sw-as-scaffold 的張力點。）

## 統計

| 類別 | 顆數 |
|---|---|
| 壓平複合總數 | 148 |
| ├ safe（無下游依賴，可安全廢棄） | 135 |
| └ deps:N（有依賴，挪會壞 save/load） | 13 |

島分布：
- image：64
- point：56
- mesh：11
- particle：10
- render：4
- field：3

## 有依賴的 13 顆（退役前必處理 save/load）

| 算子 | 島 | 引用數 | 引用處 |
|---|---|---|---|
| CombineBuffers | point | 1 | app/src/runtime/compound_save.cpp |
| DirectionalForce | particle | 1 | app/src/runtime/compound_save.cpp |
| GridPoints | point | 1 | app/src/runtime/compound_save.cpp |
| LinePoints | point | 1 | app/src/runtime/compound_save.cpp |
| OrientPoints | point | 1 | app/src/runtime/compound_save.cpp |
| ParticleSystem | particle | 2 | app/src/runtime/compound_save.cpp, app/src/runtime/graph.cpp |
| RadialPoints | point | 4 | app/src/app/document_io.cpp, app/src/runtime/compound_graph.h, app/src/runtime/compound_save.cpp, app/src/runtime/graph.cpp |
| RandomizePoints | point | 1 | app/src/runtime/compound_save.cpp |
| SetPointAttributes | point | 1 | app/src/runtime/compound_save.cpp |
| SpherePoints | point | 1 | app/src/runtime/compound_save.cpp |
| TransformPoints | point | 2 | app/src/runtime/compound_save.cpp, app/src/runtime/point_modify_op_registry.h |
| TurbulenceForce | particle | 2 | app/src/runtime/compound_save.cpp, app/src/runtime/graph.cpp |
| VectorFieldForce | particle | 1 | app/src/runtime/compound_save.cpp |

## 可安全廢棄的 135 顆（無下游依賴）

| 算子 | 島 |
|---|---|
| AddNoise | point |
| AdjustColors | image |
| AdvancedFeedback | image |
| AfterGlow | image |
| AfterGlow2 | image |
| AttributesFromImageChannels | point |
| AxisStepForce | particle |
| Blend | image |
| BlendMeshVertices | mesh |
| BlendPoints | point |
| BlendWithMask | image |
| Blob | image |
| Blur | image |
| BoundPoints | point |
| BoundingBoxPoints | point |
| BoxGradient | image |
| BubbleZoom | image |
| ChannelMixer | image |
| CheckerBoard | image |
| ChromaticAbberation | image |
| ChromaticDistortion | image |
| ClearSomePoints | point |
| CollapseVertices | mesh |
| ColorGrade | image |
| Combine3Images | image |
| CombineMaterialChannels | image |
| CombineMaterialChannels2 | image |
| CombineMeshes | mesh |
| ConvertColors | image |
| Crop | image |
| DeformMesh | mesh |
| DepthBufferAsGrayScale | image |
| DetectEdges | image |
| Displace | image |
| DistortAndShade | image |
| Dither | image |
| DoyleSpiralPoints2 | point |
| DrawBoxGizmo | render |
| DrawLineGrid | render |
| DrawSphereGizmo | render |
| FastBlur | image |
| FieldDistanceForce | particle |
| FieldVolumeForce | particle |
| FilterPoints | point |
| FindClosestPointsOnMesh | point |
| FlipNormals | mesh |
| FractalNoise | image |
| FraserGrid | image |
| Fxaa | image |
| Grain | image |
| GrowStrains | point |
| HSE | image |
| HexGridPoints | point |
| HoneyCombTiles | image |
| Image2dSDF | field |
| ImageLevels | image |
| KeyColor | image |
| KochKaleidoskope | image |
| LinearGradient | image |
| LinearSamplePointAttributes | point |
| Locator | render |
| MandelbrotFractal | image |
| MapPointAttributes | point |
| MeshProjectUV | mesh |
| MeshVerticesToPoints | point |
| MirrorRepeat | image |
| MosiacTiling | image |
| MoveToSDF | point |
| MunchingSquares2 | image |
| NGon | image |
| NGonGradient | image |
| NormalMap | image |
| PairPointsForGridWalkLines | point |
| PairPointsForLines | point |
| PairPointsForSplines | point |
| Pixelate | image |
| PointAttributeFromNoise | point |
| PointColorWithField | point |
| PointTrail | point |
| PointTrailFast | point |
| PointsOnMesh | point |
| PolarTransformPoints | point |
| RadialGradient | image |
| RandomJumpForce | particle |
| Raster | image |
| RaymarchPoints | field |
| RecomputeNormals | mesh |
| RemapColor | image |
| ReorientLinePoints | point |
| RepeatAtPoints | point |
| ResampleLinePoints | point |
| RgbTV | image |
| Rings | image |
| RoundedRect | image |
| RyojiPattern1 | image |
| RyojiPattern2 | image |
| SamplePointColorAttributes | point |
| SamplePointsByCameraDistance | point |
| SdfReflectionLinePoints | field |
| SelectPoints | point |
| SelectPointsWithSDF | point |
| SelectVertices | mesh |
| SetAttributesWithPointFields | point |
| ShardNoise | image |
| Sharpen | image |
| SimCentricalOffset | point |
| SimDirectionalOffset | point |
| SimForceOffset | point |
| SimNoiseOffset | point |
| SinForm | image |
| SnapPointsToGrid | point |
| SnapToAnglesForce | particle |
| SnapToPoints | point |
| SoftTransformPoints | point |
| SortPoints | point |
| SplitMeshVertices | mesh |
| StarGlowStreaks | image |
| SubdivideLinePoints | point |
| TileableNoise | image |
| Tint | image |
| ToneMapping | image |
| TransformFromClipSpace | point |
| TransformImage | image |
| TransformMesh | mesh |
| TransformMeshUVs | mesh |
| TransformSomePoints | point |
| TransformWithImage | point |
| ValueRaster | image |
| VelocityForce | particle |
| VoronoiCells | image |
| WorleyNoise | image |
| WrapPointPosition | point |
| WrapPoints | point |
| ZollnerPattern | image |
| _OffsetPoints | point |
