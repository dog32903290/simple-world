# LEAF_ATOMIC_CENSUS — 原子 vs 複合分類(2026-07-04,柏為攔查產出)

**出身**:柏為 2026-07-04 攔查「工單清單是不是都原子節點」→ 揭露 op_census/seam_map 的
**歸縫從來沒切原子/複合這一刀**(歸縫只靠 relpath regex,複合節點也是 `: Instance<>`,直接混進
leaf-ready)。本檔是那一刀的結果 + 根治閘建議。

## 判準(硬信號,直接 .t3 JSON)
- **複合** = 該節點 `.t3` 有**非空 `"Children": [`**(子 SymbolChild)+ 通常有 `"Connections"`。
  body 是子節點連線圖 → 照柏為 2026-07-02 拍板走 **.t3 importer 巢狀重放**,不是手刻。
  memory [[tixl-clone-model-nested-catalog-node]]。
- **原子** = `.t3` 無 Children(或空)。body 在 `.hlsl`/`[numthreads]`/實質 C# `Update()` → **手刻/移植軌**。
- 掃法:`grep -rlE ': *Instance<' external/tixl/Operators/Lib --include=*.cs`(扣 obsolete)→ 對每顆
  同目錄同 stem `.t3` 判 Children。sparse-checkout 須全 11 島(本次已驗完整)。

## 全 749 節點:451 原子 / 298 複合(40% 是複合)
複合高度集中在少數島 —— 這些島「大量產手刻葉子」的假設整個不成立:

| 島 | 總 | 複合 | 原子 | 複合率 | 島 | 總 | 複合 | 原子 |
|---|---|---|---|---|---|---|---|---|
| image | 114 | 103 | 11 | **90%** | render | 69 | 37 | 32 |
| particle | 19 | 18 | 1 | **95%** | field | 52 | 11 | 41 |
| point | 100 | 84 | 16 | **84%** | flow | 32 | 4 | 28 |
| mesh | 44 | 34 | 10 | **77%** | io | 62 | 4 | 58 |
| numbers | 223 | 1 | 222 | 0.4% | string | 33 | 2 | 31 |

**numbers/io/string/flow 幾乎全原子(手刻甜點真在這);image/point/mesh/particle 幾乎全複合(重放軌)。**

## leaf-ready 93 顆 todo:只 40 原子可手刻,53 複合(57%)須重放

### 40 原子(可立即並行手刻 = 真正的第一波)
- **vecmath-leaf (12)**:BuildGradient, CacheBoolean, DelayBoolean, EaseKeys, EaseVec2Keys, EaseVec3Keys, GridPosition, IntListToBuffer, KeepInts, PickColorFromImage, RandomChoiceIndex, ValueToRate ← 唯一 100% 乾淨 lane
- **camera-leaf (9)**:ActionCamera, BlendCameras, CamPosition, CameraWithRotation, CurrentCamMatrices, OrbitCamera, ReuseCamera, ShiftCamera, VisibleGizmos
- **render-leaf (5)**:GetScreenPos, GpuMeasure, SliceViewPort, SpreadIntoGrid, SpreadLayout
- **string-leaf (4)**:CountDown, KeepStrings, NowAsDateTime, StringToDateTime
- **flow-leaf (4)**:LoadSoundtrack, Once, ResetSubtreeTrigger, TimeClip
- **mesh-leaf (2)**:DelaunayMesh, LoadObj
- **point-leaf (1)**:CpuPointToCamera / **field-leaf (1)**:SubDivPattern3d / **data-leaf (1)**:PickObject / **tex-select (1)**:UseTextureReference

### 53 複合(該踢出手刻波 → .t3 importer 重放軌)
- **point-leaf (16)**:CustomPointShader, DrawConnectionLines, DrawLinesShaded, DrawMeshAtPoints2, DrawMovingPoints, DrawPointsDOF, DrawPointsShaded, DrawRayLines, DrawRibbons, DrawTubes, IkChain, MovePointsToCurveSpace, PointInfoLines, PointsOnImage, SamplePointAttributes_v1, VisualizePoints
- **mesh-leaf (15)**:BlendMeshToPoints, ColorVerticesWithField, CustomFaceShader, CustomVertexShader, DisplaceMesh, DisplaceMeshNoise, DisplaceMeshVAT, ExtrudeCurves, MeshFacesPoints, MoveMeshToPointLine, RepeatMeshAtPoints, ScatterMeshFaces, SelectVerticesWithSDF, TextureDisplaceMesh, Warp2dMesh
- **render-leaf (9)**:ConvertEquirectangle, DrawAsSplitView, DrawScreenQuadAdvanced, DustParticles, FadingSlideShow, RepeatWithMotionBlur, ShadowPlane, Text, TextOutlines
- **camera-leaf (5)**:DrawCamGizmos, DrawSpatialAudioGizmos, GridPlane, PlotValueCurve, VisualizeCamTrail
- **flow-leaf (4)**:BlendScenes, DrawQuiz, ImageQuiz, ValueQuiz
- **string-leaf (2)**:AnimRandomString, TimeToString / **field-leaf (1)**:HeightMapSdf / **image-leaf (1)**:DirectionalBlur

### ⚠ caveat:複合 ≠ 一定走 .t3 重放(draw/render 家族逐顆判)
point-leaf 的 `Draw*`(DrawTubes/Ribbons/PointsShaded…)、render-leaf 的 Text/TextOutlines 在 TiXL 是
複合(.t3 組合 primitive+shader setup),但**本質是渲染**;sw 已有 DrawPoints/DrawLines 單一 render
路徑,這批**可能用單一實作達 parity 而非重放整個子圖**——待逐顆對 sw 現有 render 能力判。真正
「場景組合」複合(DustParticles/Quiz/SlideShow/Gizmos)才無疑義走重放。這 caveat 未逐顆驗,別當定論。

## 根治閘(否則每次算 leaf-ready 都重新混入複合)
- **op_census.sh 該加 .t3 Children 判定**:歸 leaf-ready 前先切原子/複合,複合的自動改歸重放桶
  (或標 `leaf-composite`)。現況=純 relpath regex,結構性混複合。memory [[gate-or-it-rots]]。
- 在裝閘前,**任何 leaf-ready 派工單先過本檔的 40 原子白名單**,不直接吃 census 的 93。
