# Census: point (135 ops)

> Subdirs scanned: generate / modify / draw / sim / transform / combine / io / helper / _cpu / _internal / _experimental / _obsolete / usse
> Legacy prefixed ops (_legacy / _obsolete / draw/legacy) included but marked SKIP.
> "point-buffer seam" = the ability to allocate, bind, and dispatch GPU StructuredBuffer<Point> (BufferWithViews: SRV+UAV) inside a compute pass, then pass it downstream. This is the universal currency of this entire category — every non-CPU, non-Command, non-legacy op touches it.

## Seam key used in this file

| short name | meaning |
|---|---|
| `point-buffer` | GPU BufferWithViews (Point structured buffer) alloc + SRV/UAV bind + compute dispatch seam |
| `Layer2d+Execute` | Command / render-target compositing seam (already known) |
| `mesh-geom` | MeshBuffers input (mesh pipeline seam, already known-unknown) |
| `sdf-field` | ShaderGraphNode SDF/field node connection (shader graph seam) |
| `particle-system` | GPU particle simulation context (already-known seam, already exists?) |
| `feedback` | ping-pong / KeepPreviousBuffer temporal accumulation |
| `cpu-point-list` | CPU-side StructuredList<Point> (read/write on CPU, no GPU dispatch needed) |
| `svg-loader` | SVG parsing + System.Drawing (platform/C# dep) |
| `font-line` | SVG-font glyph-to-polyline pipeline |
| `gradient-widget` | Gradient input slot (already known-unknown) |
| `curve-widget` | Curve input slot (value-graph already has Curve? — marked separately to be safe) |
| `camera3d` | ICamera / ICameraPropertiesProvider interface for camera ops |
| `readback-cpu` | GPU→CPU buffer readback (async staging) |

---

## generate (19 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| APoint | 單一 Point 純值包裝（CPU compound） | cpu-point-list | COMPOUND | R1 | .t3 only，無 .cs；doc 說「use JoinLists + ListToBuffer to convert」；children=[] 純值 |
| BoundingBoxPoints | 從點群計算 AABB 並輸出邊框點 | point-buffer | BLOCKED:point-buffer | R2 | BufferWithViews in+out；compute |
| CommonPointSets | 預設點集合（GPU + CPU 雙輸出） | point-buffer | BLOCKED:point-buffer | R1 | 同時輸出 BufferWithViews + StructuredList |
| DoyleSpiralPoints2 | Doyle 螺旋點分佈 | point-buffer | BLOCKED:point-buffer | R1 | BufferWithViews out |
| GridPoints | 3D 網格點陣 | point-buffer | BLOCKED:point-buffer | R1 | 標準 compute 點生成 |
| HexGridPoints | 六角網格點陣 | point-buffer | BLOCKED:point-buffer | R1 | 同 GridPoints |
| LinePoints | 線段採樣點 | point-buffer | BLOCKED:point-buffer | R1 | ITransformable；BufferWithViews out |
| MeshVerticesToPoints | 網格頂點→點群 | point-buffer + mesh-geom | BLOCKED:point-buffer | R2 | 次要 seam: mesh-geom（MeshBuffers 輸入） |
| PointInfoLines | 點群 debug 可視化線段 | point-buffer | BLOCKED:point-buffer | R2 | MultiInputSlot<BufferWithViews>；debug vis |
| PointsOnImage | 按圖像亮度散佈點 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入（image-filter seam 已有）+ compute |
| PointsOnMesh | 在網格面上散佈點 | point-buffer + mesh-geom | BLOCKED:point-buffer | R2 | MeshBuffers + Texture2D 輸入 |
| PointTrail | 點拖尾（有 feedback 特性） | point-buffer | BLOCKED:point-buffer | R3 | Reset + TrailLength；需 temporal buffer |
| PointTrailFast | 快速點拖尾 | point-buffer | BLOCKED:point-buffer | R2 | 同 PointTrail 簡化版 |
| RadialPoints | 放射狀點圓 | point-buffer | BLOCKED:point-buffer | R1 | 純數學 compute |
| RepeatAtPoints | 在目標點位重複來源點群 | point-buffer | BLOCKED:point-buffer | R2 | 兩個 BufferWithViews 輸入 |
| RepetitionPoints | 複製點群 | point-buffer | BLOCKED:point-buffer | R1 | StructuredList out (CPU 側) |
| SpherePoints | 球面點分佈 | point-buffer | BLOCKED:point-buffer | R1 | 純數學 compute |
| SubdivideLinePoints | 細分線段點 | point-buffer | BLOCKED:point-buffer | R1 | BufferWithViews in+out |
| _GridPoints_Old | 舊版 GridPoints | point-buffer | BLOCKED:point-buffer | R1 | 前綴 _ = 舊版，可跳 |
| _DoyleSpiralRoot | Doyle 螺旋內部運算根 | point-buffer | BLOCKED:point-buffer | R1 | 內部 helper |

---

## modify (21 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AddNoise | 對點群加 noise 擾動 | point-buffer | BLOCKED:point-buffer | R1 | BufferWithViews in+out；標準 compute |
| AttributesFromImageChannels | 從圖像通道採樣設定點屬性 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入；通道映射 |
| ClearSomePoints | 隨機清除部分點 | point-buffer | BLOCKED:point-buffer | R1 | 簡單 compute |
| CustomPointShader | 自訂點 compute shader + Gradient/Field | point-buffer + sdf-field + gradient-widget | BLOCKED:point-buffer | R3 | 同時要 ShaderGraphNode Field + Gradient；次要 seam: sdf-field, gradient-widget |
| FilterPoints | 按 index/count 篩選點 | point-buffer | BLOCKED:point-buffer | R1 | 純篩選 compute |
| LinearSamplePointAttributes | 線性採樣圖像設定點屬性 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入 |
| MapPointAttributes | 按 Gradient/Curve/Texture 重映射點屬性 | point-buffer + gradient-widget + curve-widget | BLOCKED:point-buffer | R2 | Gradient + Curve + Texture2D 三種映射；次要: gradient-widget, curve-widget |
| MoveToSDF | 把點推向 SDF 表面 | point-buffer + sdf-field | BLOCKED:point-buffer | R3 | ShaderGraphNode SDF 輸入；次要: sdf-field |
| PointAttributeFromNoise | 用 noise 設定點屬性 | point-buffer | BLOCKED:point-buffer | R1 | Gradient 可選（RemapNoise）；次要: gradient-widget |
| PointColorWithField | 用 SDF field 著色點 | point-buffer + sdf-field | BLOCKED:point-buffer | R2 | ShaderGraphNode SDF；次要: sdf-field |
| RandomizePoints | 隨機化點位置/屬性 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| ResampleLinePoints | 重採樣線段點 | point-buffer | BLOCKED:point-buffer | R2 | BufferWithViews in+out |
| SamplePointAttributes_v1 | (v1) 從圖像採樣設定點屬性 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入 |
| SamplePointColorAttributes | 從圖像採樣設定點顏色屬性 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入 |
| SamplePointsByCameraDistance | 依相機距離採樣點 | point-buffer + camera3d | BLOCKED:point-buffer | R2 | Curve + CameraReference；次要: camera3d |
| SelectPoints | 按條件選擇/標記點 | point-buffer | BLOCKED:point-buffer | R1 | ITransformable |
| SelectPointsWithSDF | 用 SDF 選擇點 | point-buffer + sdf-field | BLOCKED:point-buffer | R2 | ShaderGraphNode SDF；次要: sdf-field |
| SetAttributesWithPointFields | 用 Field + Gradient + Curve 設定屬性 | point-buffer + sdf-field + gradient-widget + curve-widget | BLOCKED:point-buffer | R3 | 三種 seam 組合 |
| SetPointAttributes | 直接設定點屬性值 | point-buffer | BLOCKED:point-buffer | R1 | 最基本的屬性設定 |
| SortPoints | 依相機距離排序點 | point-buffer + camera3d | BLOCKED:point-buffer | R2 | CameraReference (Object) + DebugView Texture2D out |
| TransformWithImage | 用圖像 displacement 變形點 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入；ITransformable |
| _RandomizePoints_Legacy1 | 舊版 RandomizePoints | point-buffer | BLOCKED:point-buffer | R1 | _ 前綴舊版 |

---

## draw (16 ops + 3 legacy)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| DrawBillboards | 在點位繪製 billboard 四邊形 | point-buffer + Layer2d+Execute + gradient-widget + curve-widget | BLOCKED:Layer2d+Execute | R3 | Command out；BufferWithViews + Gradient + Curve + Texture2D |
| DrawClosedLines | 繪製閉合線段 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out；BufferWithViews in |
| DrawConnectionLines | 繪製連接線（帶 Gradient） | point-buffer + Layer2d+Execute + gradient-widget | BLOCKED:Layer2d+Execute | R2 | Command out；雙 Gradient 輸入 |
| DrawLines | 繪製線段（帶 Texture） | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out；Texture2D 可選 |
| DrawLinesBuildup | 累積線段繪製 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out |
| DrawLinesShaded | 帶光照的線段 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | Command out；Texture2D |
| DrawMeshAtPoints | 在點位繪製 mesh（帶 Gradient/Curve） | point-buffer + Layer2d+Execute + mesh-geom + gradient-widget + curve-widget | BLOCKED:Layer2d+Execute | R3 | Command out；MeshBuffers + Gradient + Curve |
| DrawMovingPoints | 繪製移動中的點（motion blur 風格） | point-buffer + Layer2d+Execute + sdf-field | BLOCKED:Layer2d+Execute | R3 | Command out；ShaderGraphNode ColorField |
| DrawPoints | 繪製點（帶 ShaderGraphNode ColorField） | point-buffer + Layer2d+Execute + sdf-field | BLOCKED:Layer2d+Execute | R2 | Command out；ShaderGraphNode 可選 |
| DrawPoints2 | DrawPoints 第二版 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out |
| DrawPointsDOF | 帶景深的點渲染 | point-buffer + Layer2d+Execute + sdf-field | BLOCKED:Layer2d+Execute | R3 | Command out；ShaderGraphNode InputField |
| DrawPointsShaded | 帶光照的點 | point-buffer + Layer2d+Execute + sdf-field | BLOCKED:Layer2d+Execute | R3 | Command out；ShaderGraphNode ColorField |
| DrawRayLines | 繪製 ray 線段 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out；Texture2D |
| DrawRibbons | 繪製 ribbon 帶狀線 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | Command out；複雜幾何生成 |
| DrawTubes | 繪製管狀幾何 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | Command out；3D 幾何 |
| VisualizePoints | 點群 debug 可視化 | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R1 | Command out；debug 用途 |
| _DrawBillboardsOld (legacy) | 舊版 DrawBillboards | — | SKIP | — | draw/legacy；廢棄 |
| _DrawQuads (legacy) | 舊版四邊形繪製 | — | SKIP | — | draw/legacy；廢棄 |
| _DrawVaryingQuads (legacy) | 舊版可變四邊形 | — | SKIP | — | draw/legacy；廢棄 |

---

## sim (12 ops including legacy/experimental)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| PointSimulation | 點模擬主節點（ping-pong buffer） | point-buffer + feedback | BLOCKED:point-buffer | R3 | BufferWithViews in+out；Reset/Update；需 temporal buffer |
| SamplePointSimAttributes | 從圖像採樣模擬點屬性 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D + BufferWithViews；TransformCallbackSlot |
| SimCentricalOffset | 中心力場偏移 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute；GizmoVisibility |
| SimDirectionalOffset | 方向力場偏移 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| SimDisplacePoints2d | 2D 圖像 displacement 模擬 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入 |
| SimForceOffset | 力場偏移（重力） | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| SimNoiseOffset | Noise 模擬偏移 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| ApplyRandomWalk (experimental) | 隨機漫步模擬 | point-buffer + feedback | BLOCKED:point-buffer | R3 | TriggerStep；有 temporal 性質 |
| GrowStrains (experimental) | 生長線段模擬 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews (GPoints + GTargets) |
| SimBlendTo (experimental) | 混合到目標點群 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews |
| SimFollowMeshSurface (experimental) | 沿網格表面移動 | point-buffer + mesh-geom | BLOCKED:point-buffer | R3 | MeshBuffers；次要: mesh-geom |
| SimPointMeshCollisions (experimental) | 與網格碰撞模擬 | point-buffer + mesh-geom | BLOCKED:point-buffer | R3 | MeshBuffers；次要: mesh-geom |
| LegacyParticleSimulation (_legacy) | 舊版粒子模擬 | — | SKIP | — | _legacy；廢棄 |
| _LegacySimForwardMovement (_legacy) | 舊版向前移動 | — | SKIP | — | _legacy；廢棄 |

---

## transform (15 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BoundPoints | 把點限制在包圍盒內 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| FindClosestPointsOnMesh | 找網格上最近點 | point-buffer + mesh-geam | BLOCKED:point-buffer | R2 | MeshBuffers 輸入；次要: mesh-geom |
| IkChain | 逆向運動學鏈 | point-buffer | BLOCKED:point-buffer | R3 | 複雜迭代算法；多 BufferWithViews |
| MovePointsToCurveSpace | 把點移動到曲線局部空間 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews |
| OrientPoints | 設定點的朝向 | point-buffer | BLOCKED:point-buffer | R2 | LookAtCamera 模式；次要: camera3d |
| PolarTransformPoints | 極座標變換點 | point-buffer | BLOCKED:point-buffer | R1 | ITransformable |
| ReorientLinePoints | 重新設定線段點朝向 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| SnapPointsToGrid | 吸附到網格 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| SnapToPoints | 吸附到另一組點群 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews |
| SoftTransformPoints | 軟性體積變換 | point-buffer | BLOCKED:point-buffer | R2 | ITransformable；體積範圍 |
| TransformFromClipSpace | 從裁剪空間變換 | point-buffer + camera3d | BLOCKED:point-buffer | R2 | 需要相機矩陣；次要: camera3d |
| TransformPoints | 標準 TRS 變換 | point-buffer | BLOCKED:point-buffer | R1 | 基礎 compute |
| TransformSomePoints | 選擇性 TRS 變換 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| WrapPointPosition | 環繞包裹位置 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| WrapPoints | 點群環繞 | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |

---

## combine (8 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BlendPoints | 線性混合兩組點群 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews |
| CombineBuffers | 合併多個點 buffer | point-buffer | BLOCKED:point-buffer | R1 | MultiInputSlot<BufferWithViews> |
| PairPointsForGridWalkLines | 為網格走線配對點 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews |
| PairPointsForLines | 配對點成線段 | point-buffer | BLOCKED:point-buffer | R1 | 雙 BufferWithViews |
| PairPointsForSplines | 配對點成樣條線 | point-buffer | BLOCKED:point-buffer | R2 | 雙 BufferWithViews + StructuredList |
| PickPointList | 從多個 buffer 選一個 | point-buffer | BLOCKED:point-buffer | R1 | MultiInputSlot<BufferWithViews> |
| SplinePoints | CPU 樣條插值（GPU + CPU 雙輸出） | point-buffer + cpu-point-list | BLOCKED:point-buffer | R2 | 同時輸出 BufferWithViews + StructuredList；MultiInputSlot<StructuredList> |
| _ExecuteCombineBuffers | 用 compute shader 合併 buffer | point-buffer | BLOCKED:point-buffer | R2 | ComputeShader 輸入；這是 multi-pass seam 的一種，但 compute 面向 |

---

## io (5 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| DataPointConverter | CSV/JSON → 點資料轉換 | cpu-point-list | BLOCKED:cpu-point-list | R2 | CPU 側 StructuredList；需檔案 I/O |
| DataPointImportExport | 點 buffer 匯入/匯出（JSON/binary） | point-buffer + readback-cpu | BLOCKED:point-buffer | R3 | GPU readback + 檔案 I/O；BufferWithViews in+out |
| LineTextPoints | SVG 字型 → 線段點 | font-line + svg-loader + cpu-point-list | BLOCKED:NEW-SEAM:font-line | R3 | 依賴 SVG 字型解析；StructuredList out；複雜 |
| LoadSvg | SVG 路徑 → 線段點（CPU） | svg-loader + cpu-point-list | BLOCKED:NEW-SEAM:svg-loader | R2 | System.Drawing.Drawing2D + Svg.NET；StructuredList out |
| PrepareSvgLineTransition | SVG 路徑過渡動畫準備 | svg-loader + cpu-point-list | BLOCKED:NEW-SEAM:svg-loader | R2 | SVG 輸入 + StructuredList in+out |

---

## helper (7 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| CpuPointToCamera | CPU 點群 → 相機 reference（ICamera） | cpu-point-list + camera3d | BLOCKED:camera3d | R2 | ICamera + ICameraPropertiesProvider；StructuredList 輸入 |
| LoadObjAsPoints | OBJ 檔案 → CPU 點群 | cpu-point-list + NEW-SEAM:obj-loader | BLOCKED:NEW-SEAM:obj-loader | R2 | OBJ parsing CPU side；StructuredList out |
| PointToMatrix | 點 → 相機矩陣（ICamera） | cpu-point-list + camera3d | BLOCKED:camera3d | R2 | ICamera + ICameraPropertiesProvider；Matrix4x4[] out |
| PointsToCPU | GPU 點 buffer → CPU StructuredList | point-buffer + readback-cpu | BLOCKED:readback-cpu | R2 | GPU readback；Async 模式 |
| ReadPointColors | 從 GPU 點 buffer 讀取顏色到 CPU | point-buffer + readback-cpu | BLOCKED:readback-cpu | R2 | Async readback；List<Vector4> out |
| SampleCpuPoints | 在 CPU StructuredList 上插值取樣 | cpu-point-list | BLOCKED:cpu-point-list | R1 | 純 CPU 計算 |
| _VisualizePointFields | 可視化點 field 向量（Command 輸出） | point-buffer + Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | Command out；MultiInputSlot<BufferWithViews> |

---

## _cpu (6 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| LinePointsCpu | CPU 線段點生成 | cpu-point-list | BLOCKED:cpu-point-list | R1 | StructuredList out；純 CPU |
| LinearPointsCpu | CPU 線性點列 | cpu-point-list | BLOCKED:cpu-point-list | R1 | StructuredList out；純 CPU |
| RadialPointsCpu | CPU 放射狀點 | cpu-point-list | BLOCKED:cpu-point-list | R1 | StructuredList out；純 CPU |
| RepeatAtPointsCpu | CPU 版在目標點重複 | cpu-point-list | BLOCKED:cpu-point-list | R1 | 雙 StructuredList in；純 CPU |
| SampleSplinePoint | 在樣條上取樣單點（Command 型） | cpu-point-list + Layer2d+Execute | BLOCKED:cpu-point-list | R2 | Command in+out；StructuredList in；需 SubTree 執行 |
| TransformCpuPoint | CPU 點 TRS 變換 | cpu-point-list | BLOCKED:cpu-point-list | R1 | StructuredList in+out |

---

## _internal (10 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AnalyzeBuffers | 分析/選擇多個 buffer | point-buffer | BLOCKED:point-buffer | R1 | MultiInputSlot<BufferWithViews> |
| GetParticleComponents | 從 context 提取粒子系統組件 | particle-system | BLOCKED:particle-system | R2 | context.ParticleSystem；Slot<UnorderedAccessView> |
| MultiUpdatePoints | 合併多個點 buffer | point-buffer | BLOCKED:point-buffer | R1 | MultiInputSlot<BufferWithViews> |
| NoisePoints | Noise 生成 CPU Point[] | NEW-SEAM:cpu-point-array | BLOCKED:NEW-SEAM:cpu-point-array | R1 | Slot<Point[]>；CPU array（非 StructuredList 也非 BufferWithViews） |
| _AppendPoints | 把一組點附加到另一組 | point-buffer | BLOCKED:point-buffer | R1 | 雙 BufferWithViews |
| _BuildSpatialHashMap | 建立空間哈希表（Command 輸出） | point-buffer + Layer2d+Execute | BLOCKED:point-buffer | R3 | Command out；空間加速結構 |
| _ExecuteParticleUpdate | 執行粒子更新 Command 序列 | particle-system + Layer2d+Execute | BLOCKED:particle-system | R3 | MultiInputSlot<Command> + Slot<ParticleSystem> |
| _MixPoints | 混合 CPU Point[] | NEW-SEAM:cpu-point-array | BLOCKED:NEW-SEAM:cpu-point-array | R1 | Slot<Point[]>；CPU array |
| _OffsetPoints | 偏移點 buffer | point-buffer | BLOCKED:point-buffer | R1 | 純 compute |
| _SetParticleSystemComponents | 設定粒子系統到 context | particle-system | BLOCKED:particle-system | R3 | context.ParticleSystem；MultiInputSlot<ParticleSystem> Forces |

---

## _experimental (8 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| KeepBufferReference | 保持 buffer reference（實驗性） | point-buffer | BLOCKED:point-buffer | R2 | BufferWithViews + Object reference |
| NumberLinePoints | 數字標籤點生成 | point-buffer | BLOCKED:point-buffer | R2 | List<int> 輸入；特殊數字視覺化 |
| PointsFromMeshData | 從 ShaderResourceView 生成點 | NEW-SEAM:raw-srv | BLOCKED:NEW-SEAM:raw-srv | R3 | InputSlot<ShaderResourceView>；直接 SRV 輸入 |
| RecycleBuffer | Buffer reference 回收管理 | point-buffer | BLOCKED:point-buffer | R2 | Object + BufferWithViews |
| ReflectionLines | 反射線段計算 | point-buffer + mesh-geom | BLOCKED:point-buffer | R2 | MeshBuffers；次要: mesh-geom |
| TraceContourLines | 追蹤等高線 | point-buffer | BLOCKED:point-buffer | R2 | Texture2D 輸入；TransformCallbackSlot |
| _GetSketchPoints | 取得草圖點（從 SketchImpl context） | NEW-SEAM:sketch-context | BLOCKED:NEW-SEAM:sketch-context | R3 | InputSlot<object> Pages；高度實驗性 |
| _SketchImpl | 草圖繪製實作（極實驗性） | NEW-SEAM:sketch-context | BLOCKED:NEW-SEAM:sketch-context | R3 | 使用 clipSpaceToWorld；完整 sketch 子系統 |

---

## _obsolete (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| __OBSOLETEFollowMeshSurface | 已廢棄網格表面跟隨 | — | SKIP | — | _obsolete；MeshBuffers；廢棄 |

---

## usse (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| KeepPreviousPointBuffer | ping-pong GPU 點 buffer（時間回饋） | point-buffer + feedback | BLOCKED:feedback | R3 | 雙 BufferWithViews out (A/B)；ResourceOptionFlags.BufferStructured；CopyResource；這是 feedback seam 的具體實作 |

---

## 摘要

- **總 op 數**：135（含 3 _legacy + 3 draw/legacy + 1 _obsolete = 7 SKIP）
- **有效 op 數**：128
- **READY-LEAF**：0（這是整類最大的發現——point 類別**全員被 point-buffer seam 擋住**，或被 Layer2d+Execute 擋住）
- **COMPOUND**：1（APoint，.t3 only）
- **SKIP**：7（legacy/obsolete）
- **BLOCKED 分佈**：

| seam | 擋住的 op 數（主要） | 備註 |
|------|---------------------|------|
| `point-buffer` | ~90 | 整個 generate/modify/sim/transform/combine/部分 _internal 的通用貨幣 |
| `Layer2d+Execute` | ~16 | 全部 draw op（Command 輸出） |
| `point-buffer` + `Layer2d+Execute`（雙擋） | ~3 | _internal/_BuildSpatialHashMap, _VisualizePointFields, SampleSplinePoint |
| `cpu-point-list` | ~12 | _cpu/* + helper/SampleCpuPoints + io/DataPointConverter + combine/SplinePoints 部分 |
| `feedback` | 1 (主要) | KeepPreviousPointBuffer (usse)；PointTrail/ApplyRandomWalk 次要 |
| `particle-system` | 3 | _ExecuteParticleUpdate / _SetParticleSystemComponents / GetParticleComponents |
| `readback-cpu` | 3 | PointsToCPU, ReadPointColors, DataPointImportExport |
| `camera3d` | 2 (主要) | CpuPointToCamera, PointToMatrix |
| `mesh-geom` | 0 (主要，次要約 8) | 次要 seam：MeshVerticesToPoints / PointsOnMesh / FindClosestPointsOnMesh / SimFollowMeshSurface / SimPointMeshCollisions / ReflectionLines 等 |

- **解鎖最多的 seam**：`point-buffer`（解鎖後立刻開放 ~90 顆 op 進 Phase C）——這是 point 類別的核心地基；`Layer2d+Execute` 是次要大門（解鎖全部 draw op）

---

## 新冒出的 NEW-SEAM

| 短名 | 一句描述 | 擋住顆數（主要） |
|------|---------|----------------|
| `cpu-point-list` | CPU 側 StructuredList<Point> 讀寫：無 GPU dispatch，純 C# 計算後可選 Upload | ~12 |
| `svg-loader` | SVG 解析（依賴 Svg.NET + System.Drawing）→ GraphicsPath → Point[] | 3 |
| `font-line` | SVG 字型字形 → 折線點管線（LineTextPoints 專用） | 1 |
| `readback-cpu` | GPU structured buffer → CPU staging readback（Async 支援） | 3 |
| `camera3d` | ICamera / ICameraPropertiesProvider 相機 reference 系統 | 2 |
| `obj-loader` | OBJ 格式解析 → CPU 點群（LoadObjAsPoints） | 1 |
| `cpu-point-array` | CPU Point[] 原生陣列（非 StructuredList 也非 BufferWithViews）——_MixPoints / NoisePoints 特用 | 2 |
| `raw-srv` | 直接 ShaderResourceView 輸入（PointsFromMeshData）——比 point-buffer 更低層 | 1 |
| `sketch-context` | 草圖 Pages context 物件（_GetSketchPoints / _SketchImpl）——高度實驗性子系統 | 2 |

---

## 意外 / 盲區

1. **point-buffer seam 是 128 顆 op 的通用前提**——不只是「blocked」，而是整個 point 類別的引擎地基完全不存在。蓋好這一塊 = 解鎖比任何其他類別更大量的 op。
2. **cpu-point-list 是第二大分支**：_cpu/ 和部分 io/helper 走 CPU StructuredList<Point>，不需 GPU dispatch，但需要 CPU→GPU Upload（ListToBuffer）才能接 GPU 消費者。這個 seam 比 point-buffer 容易，值得早建。
3. **KeepPreviousPointBuffer（usse）是 feedback seam 的具體 Metal 實作挑戰**：它直接用 DX11 `CopyResource` + ResourceOptionFlags.BufferStructured + UAV——Metal 端對應 `MTLBuffer` blit + MTLBuffer as `[[ buffer(n) ]]`。實作並不複雜，但必須有 point-buffer 先行。
4. **sdf-field（ShaderGraphNode）次要出現約 6 顆 op**（CustomPointShader / MoveToSDF / SelectPointsWithSDF / PointColorWithField / DrawPoints / DrawMovingPoints / DrawPointsDOF / DrawPointsShaded）——這條 seam 不在已知清單中，是 shader graph / inline field 子系統。
5. **LineTextPoints 是複雜度最高的 io op**：依賴 SVG 字型解析 → 字形 → 折線，完全 CPU 側，整條路和 GPU 無關；但在 Mac 上 System.Drawing 可能要換成 CoreGraphics / SkiaSharp。標 `NEW-SEAM:font-line` 但警示 platform 層需要驗證。
6. **_SketchImpl 幾乎是一個完整的子系統**（草圖、相機反投影、clipSpaceToWorld 矩陣），與其他 op 完全隔離。建議保持 SKIP 直到需求明確。
7. **gradient-widget 在 point 類別出現次數**：DrawBillboards / DrawConnectionLines / DrawMeshAtPoints2 / CustomPointShader / MapPointAttributes / SetAttributesWithPointFields / PointAttributeFromNoise（7 顆）。這些全部同時被 point-buffer 擋，不是 gradient-widget 的主擋，但解鎖時需要注意。
