# OP_BACKLOG — 開採 backlog（按狀態分桶）

> **★★ 2026-06-25 STALE 警告（夜批 set-diff 驗證）：桶 B「READY-LEAF-NOW」counts 過時，clean-leaf 已大致採盡。** numbers-TRIVIAL(B1)=**DRY**（84 value-op 已 port，剩全卡 list/dict/matrix/string/keyframe 縫）；string-TRIVIAL(B2)=剩 1（WrapString 已採 `6da350a`，22/33 已 port，餘卡 datetime/stateful/stub）；image-filter(B4)=~42 unported 但多卡 multi-image/feedback/dynamic-HLSL/readback/field 縫，剩 ~8 候選未驗（Steps/HSE/ColorGradeDepth/ConvertFormat/ScreenCloseUp/GetImageBrightness/MandelbrotFractal/NumberPattern）。io/render 同類零頭。**→ 引擎剩餘真工作=承重 SEAMS（桶 C）。下批勿照 B 桶舊 count 派工，先 set-diff 驗。** 詳 MASTER_PLAN Snapshot 2026-06-25。

> Phase A synthesis（2026-06-16）。後續 batch 直接從此 pull。
> 桶：DONE → READY-LEAF-NOW（最重要可行動清單）→ BLOCKED（按擋住的未建 seam 分組）→ SKIP/低優先。
> 數字以類別檔表為準；跨類別加總存疑標「約」。seam 命名見 SEAM_GRAPH.md §0。

---

## 桶 A — DONE（已 port，~112 顆）

引 A1（simple-world-state.md）。分類別列名，不重複細節：

- **Image-filter（25）**：AdjustColors / Blur / ChannelMixer / ChromaB / ChromaKey / ChromaticDistortion / ColorGrade / ConvertColors / Crop / DetectEdges / Displace / DistortAndShade / Dither / FastBlur / KochKaleidoscope / MirrorRepeat / NormalMap / Pixelate / RgbTV / Sharpen / StarGlowStreaks / Tint / ToneMapping / TransformImage / VoronoiCells
  - ＋孤兒 2：PolarCoordinates / EdgeRepeat（cook 存在但**選單不可達**，見 SKIP 桶）
- **Point generator（9）**：RadialPoints / LinePoints / GridPoints / SpherePoints / HexGridPoints / DoyleSpiralPoints / RepetitionPoints / CommonPointSets / BoundingBoxPoints
  - ⚠ 註：這 9 顆是 simple_world 既有 value-graph fork（CPU-fill/readback），**非** TiXL 原版 GPU point-buffer 路徑。TiXL 對應版仍在 point 類別 BLOCKED:point-buffer。
- **Point modify/transform（21）**：TransformPoints / OrientPoints / RandomizePoints / SetPointAttributes / AddNoise / FilterPoints / PolarTransformPoints / WrapPoints / BoundPoints / TransformSomePoints / WrapPointPosition / SnapPointsToGrid / ClearSomePoints / ReorientLinePoints / SelectPoints / SoftTransformPoints / OffsetPoints / PointAttributeFromNoise / ResampleLinePoints / SubdivideLinePoints / PairPointsForLines
- **Point combine（2）**：CombineBuffers / SnapToPoints　**＋ PickPointList（1）**
- **Draw（4）**：DrawPoints / DrawLines / DrawBillboards / RenderTarget
- **Particle（7）**：TurbulenceForce / DirectionalForce / VectorFieldForce（fork-VFF baked）/ VelocityForce / AxisStepForce / SnapToAnglesForce / ParticleSystem
- **Math/value（~51）**：~30 靜態 evalXxx + ~21 stateful（Damp/Spring/Ease/Accumulator…）。詳 A1 §H。

---

## 桶 B — READY-LEAF-NOW（踩已建成 seam、現在就能進 Phase C）

> **這是最重要的可行動清單。** 全部踩已驗證 seam（value-graph/transport/image-filter/multi-image/asset-texture/particle-system/audio-analysis），不需任何新 seam。
> 已扣除桶 A 的已 port op。**總計 ~173 顆全新可採葉子。**

### B1. numbers TRIVIAL — 純值（~95 顆全新，扣已 port ~51 math）

全踩 `value-graph`，R1，零風險。已 port 的 math（Add/Sub/Multiply/Sine/Clamp…約 51）見桶 A；以下為**尚未 port** 的純值 op：

- **bool/（~13）**：All / And / Any / Or / FlipBool / FlipFlop / HasBooleanChanged / Not / PickBool / ToggleBoolean / Xor / CacheBoolean / DelayBoolean / DelayTriggerChange / KeepBoolean / Trigger / WasTrigger
- **int/（~21）**：AddInts / IntAdd / IntDiv / IntToFloat / ModInt / MultiplyInt / MultiplyInts / SubInts / SumInts / CompareInt / CountInt / HasIntChanged / IsIntEven / PickInt / ClampInt / FloatToInt / GetAPrime / KeepInts / MaxInt / MinInt / TryParseInt
- **int2/（6）**：AddInt2 / Int2Components / MakeResolution / MaxInt2 / ScaleResolution / ScaleSize
- **ints/（5）**：IntListLength / IntsToList / MergeIntLists / PickIntFromList / SetIntListValue
- **vec2/vec3/vec4 補漏（~20）**：凡 A1 §H 未列者（GridPosition / Int2ToVector2 / PerlinNoise2/3 / PickVector2/3 / TransformVec3 / MulMatrix / Vector3Gizmo / DotVec4 / PickColor / Vector4Components…）
- **floats/ list（~26）**：FloatListLength / FloatsToList / ColorsToList / SetFloatListValue / FloatListToIntList / IntListToFloatList / ComposeVec3FromList / PickFloatFromList / PickFloatList / AmplifyValues / AnalyzeFloatList / ColorListToInts / CombineFloatLists / CompareFloatLists / DampFloatList / DampPeakDecay / DeltaSinceLastFrame / KeepFloatValues / MergeFloatLists / RemapFloatList / SmoothValues / SumRange / RandomChoiceIndex
- **color/ Gradient ops（~13）**：BlendColors / HSBToColor / HSLToColor / OKLChToColor / PickColorFromList / CombineColorLists / KeepColors / DefineGradient / BuildGradient / BlendGradients / PickGradient / SampleGradient / DefineIqGradient（⚠ Gradient 型別 sw 需確認已實作）
- **curve/**：SampleCurve

### B2. string TRIVIAL（33 顆全新）

全踩 `value-graph`，R1。combine(4) / convert(3) / datetime(6) / list(6) / logic(4) / random(3) / search(3) / transform(2) / buffers(2)：
BlendStrings / CombineStrings / FloatListToString / StringRepeat / FloatToString / IntToString / Vec3ToString / CountDown / DateTimeToFloat / DateTimeToString / NowAsDateTime / StringToDateTime / TimeToString / JoinStringList / KeepStrings / PickStringFromList / SplitString / StringLength / ZipStringList / FilePathParts / HasStringChanged / PickString / PickStringPart / AnimRandomString / BuildRandomString / MockStrings / IndexOf / SearchAndReplace / SubString / ChangeCase / WrapString / StringBuilderToString / StringInsert
**＋ data/ PickObject（1）**

### B3. numbers transport / anim（~19 全新）

踩 `transport`（已建）。
- **anim/time**：Time / ClipTime / RunTime / GetFrameSpeedFactor / LastFrameDuration / HasTimeChanged / ConvertTime / DateTimeInSecs / SetPlaybackTime(R2) / SetPlaybackSpeed(R2) / SetTime(R2) / StopWatch(R2)
- **anim/animators**：AnimValue / AnimVec2 / AnimVec3 / AnimInt / AnimBoolean / AnimFloatList / OscillateVec2 / OscillateVec3 / SequenceAnim(R2) / TriggerAnim(R2) / AdsrEnvelope(R2，⚠ 需確認 sw 有 AdsrCalculator)
- **anim/vj**：GetBpm / SetBpm(R2) / ForwardBeatTaps(R2)

### B4. image-filter 葉子（~30 全新，扣已 port）

踩 `image-filter`（已建）。已 port 見桶 A；以下尚未 port 的乾淨葉子：

- **color**：AdjustColors* / ChannelMixer* / ColorGrade* / ConvertColors* / KeyColor / ToneMapping*　（*若 A1 已列即跳）→ 實際新增：KeyColor、ImageLevels
- **stylize**：ChromaticAbberation / StarGlowStreaks*
- **noise**：FractalNoise / Grain / ShardNoise / TileableNoise / WorleyNoise（5）
- **pattern**：FraserGrid / Raster / Rings / RyojiPattern1(R2) / RyojiPattern2(R2) / SinForm / ValueRaster / ZollnerPattern / NumberPattern→（NumberPattern 實為 multi-image，移 BLOCKED）（8）
- **generate/basic**：Blob / CheckerBoard / NGon / RoundedRect / RenderTarget(generate 版)（5；BoxGradient/LinearGradient/NGonGradient/RadialGradient 是 gradient-widget，移 BLOCKED）
- **generate/misc**：MunchingSquares2（1）
- **transform**：MakeTileableImageAdvanced(R2)（1；MakeTileableImage/MirrorRepeat 注意：MirrorRepeat 已 port）
- **fx/_**：_ExecuteBloomPasses / _ExecuteFastBlurPasses（已 port）→ _ExecuteBloomPasses 是 multi-pass 葉子可採（1）
- **analyze**：ImageLevels（image-filter；1）
- ⚠ **dynamic-shader 注意**：用 `_ImageFxShaderSetup2`（runtime hlsl 載入）者 10 顆（含 Tint/ImageLevels/ChromaticDistortion/VoronoiCells/MirrorRepeat/KochKaleidoskope/RyojiPattern1/2/SinForm/EdgeRepeat）。seam 同為 image-filter，但若 sw 目前只支援 static shader，需一行 platform 差異——**開採前確認 sw 是否支援 dynamic hlsl 載入**。

### B5. image COMPOUND（純 value-graph 邏輯，4 顆）

FirstValidTexture / PickTexture / UseFallbackTexture / UseTextureReference（純 null 檢查/index 選擇，踩 value-graph，R1）

### B6. particle 葉子（2 全新）

踩 `particle-system`（已建）。**SwitchParticleForce**（index 選 force，R1）/ **VolumeForce**（球/立方/橢球體積力，R2，math 直白）

### B7. io 葉子（~11 全新）

- 踩 `audio-analysis`（已建）：**AudioReaction**（已對接 audio_monitor + spectrum_analyzer）/ AudioFrequencies(WIP) / AudioWaveform(WIP) / DetectBeatOffset(R2,WIP) / DetectBpm(R2,WIP) / _SetAudioAnalysis(debug)
- 踩 `transport`：**PlayAudioClip**（已用 AudioEngine.UseSoundtrackClip）
- 踩 `value-graph`（檔案/JSON 純解析）：FilesInFolder / ReadFile / WriteToFile / GetAttributeFromJsonString
- ⚠ WIP 註：audio/_/ 下 AudioFrequencies/AudioWaveform/DetectBeatOffset/DetectBpm 是 TiXL 自標 WIP（`_/` 目錄），優先度可略低。

### B8. render 葉子（~14 全新）

踩 `value-graph`/`transport`/`mip`（已建）：
- **CPU 純計算**：TransformMatrix / _ProcessLayer2d / PickSDXVector4 / GetTextureSize / CalcDispatchCount / CalcInt2DispatchCount / RequestedResolution(utils) / IntToWrapmode(TRIVIAL)
- **已有對應 seam**：GenerateMips（mip 已建）/ ShowTexture2d / ExecuteValueUpdate
- **buffer 條件選取（value-graph）**：FirstValidBuffer / IsBufferDirty / PickBuffer / UseFallbackBuffer
- ⚠ GetScreenPos：純 CPU 矩陣乘，但需讀 camera3d context；若 context 矩陣未建則退化。暫不列入確定可採。

### B9. multi-image 葉子（~18 顆，**開採前需 .t3 backward-trace**）

> ⚠⚠ **Cut 55 routing trap**：消費 `_multiImageFxSetup`/`_multiImageFxSetupStatic` 的 op，.t3 用 FloatsToBuffer 以 connection-order 填 cbuffer，中間可能夾數學節點（非 1:1 op-port→shader-param）。**每顆開採前必做 STEP-0 backward-trace .t3 routing**（DirectionalBlur 已因此丟棄）。seam 本身已建（Displace/DistortAndShade 證），但每顆要單獨 trace。
> task_258d9510 audit 已 ship 的 _multiImageFxSetup op（pixelate/voronoi/koch/displace/mirrorrepeat/sharpen/chromaticdistortion/detectedges/dither）routing 對不對。

候選（image/use + image/color + image/fx）：Blend / BlendImages / Combine3Images / CombineMaterialChannels / CombineMaterialChannels2 / Fxaa / HSE / RemapColor / BlurWithMask / FakeLight(需 asset 法線圖) / HoneyCombTiles / MosiacTiling / Pixelate(已port?) / TimeDisplace(次要 temporal) / BlendWithMask(三輸入,需 _trippleImageFxSetup) / RenderWithMotionBlur(次要 feedback) / DirectionalBlur(**已丟棄,需重 trace**) / particle TextureMapForce
　**註**：DirectionalBlur 是 BLOCKED 而非 READY（Cut 55 證 routing 非 1:1）；BlendWithMask/Combine3Images 需第三輸入 t2（multi-image 目前只到 t0/t1），實為半-blocked。

---

### READY-LEAF-NOW 小計

| 桶 | 全新可採數 | seam | 風險 |
|----|-----------|------|------|
| B1 numbers TRIVIAL | ~95 | value-graph | R1 |
| B2 string TRIVIAL | 34（含 PickObject） | value-graph | R1 |
| B3 numbers transport/anim | ~19 | transport | R1-R2 |
| B4 image-filter | ~30 | image-filter | R1-R2 |
| B5 image COMPOUND | 4 | value-graph | R1 |
| B6 particle | 2 | particle-system | R1-R2 |
| B7 io | ~11 | audio-analysis/transport/value-graph | R1-R2 |
| B8 render | ~14 | value-graph/transport/mip | R1 |
| B9 multi-image（需 trace） | ~12 真可採 | multi-image | R2 |
| **總計** | **~221（含需 trace）／~209 高信心** | | |

> **保守可立刻開工數 ≈ 173**（扣掉 B9 需 trace 的 12 顆、扣掉 WIP/需確認的 ~24 顆：AdsrEnvelope/Gradient 型別/dynamic-shader 群/audio WIP）。
> **最安全並行燃料 = B1+B2 共 ~129 顆 TRIVIAL**（純 value-graph，零 seam 風險，零視覺判斷）。

---

## 桶 C — BLOCKED（按擋住它的未建 seam 分組）

> 每段一個未建 seam，列它解鎖哪些 op（名字 + 風險）。完整依賴鏈見 SEAM_GRAPH.md §3。
>
> **★★ 2026-06-27 GROUND-TRUTH 校正：本桶下列段落 6 塊 seam 標題寫「未建」已過時——`point-buffer`/`shader-graph`/`mesh-pipeline`/`feedback` 已 BUILT，`Layer2d+Execute`/`camera3d` core-BUILT，`dx11-api-wrapper` 在 Metal 上 N/A（render_command.h:40-110 DrawKind 吸收）。這些段落底下的 op 多數已是「葉 fan-out」而非「卡未建 seam」。校正詳 SEAM_GRAPH.md 頂 ground-truth 表 + SEAM_COMPLETION_PLAN.md（標 ✅+commit）。下列保留原文供 op 清單參考，但「擋住=未建 seam」一律以校正表為準。**

### `point-buffer`（~~~90，最大解鎖~~ ✅BUILT 2026-06-27，剩 ~39 葉 fan-out；point_graph_registry.cpp:29）
> **★★ 2026-06-27 GROUND-TRUTH 二次校正（code-cited）：剩 ~39 殘中 clean-leaf ≈ 4-5（只剩部分 Draw* 變體），~85-90% 仍卡 rail-specific 子 seam。剩餘按子 seam 分簇 → cross-frame-sim（PointTrail/PointTrailFast/PointSimulation/SamplePointSimAttributes/Sim*/GrowStrains/ApplyRandomWalk/KeepPreviousPointBuffer，= cook-core sub-seam E；PointSimulation pool-growth ABI = 柏為 call）/ mesh-input（FindClosestPointsOnMesh/DrawMeshAtPoints2/SimFollowMeshSurface/SimPointMeshCollisions）/ shader-graph-Field（PointColorWithField/SelectPointsWithSDF/CustomPointShader）/ CPU-readback（CpuPointToCamera/SampleCpuPoints/DataPointConverter/DataPointImportExport/PointToMatrix）/ camera3d-Draw*（DrawRibbons/DrawTubes/DrawConnectionLines/DrawPointsDOF/DrawMovingPoints/DrawRayLines/DrawPointsShaded/DrawLinesShaded）/ loader（LoadObjAsPoints/LoadSvg/LineTextPoints/PrepareSvgLineTransition）。詳 SEAM_COMPLETION_PLAN §0′-seam-cluster。**
> **★★ 2026-06-27 final-autonomous-seam scout（code-cited，三度校正上行「mesh-input / CPU-readback」二簇=「rail 已建，非待蓋 seam」）：**
> - **mesh-input rail ✅BUILT**（PointCookCtx::meshVtx/meshIdx，point_graph_cook_ctx.h:88 filled point_graph.cpp:355-356；MeshVerticesToPoints/PointsOnMesh 是葉非 seam-gated）→ 此簇 autonomous leaf **FindClosestPointsOnMesh ✅BUILT @504f149**（brute-force tri-distance，無 BVH/camera；point_ops_findclosestpointsonmesh.cpp）；**DrawMeshAtPoints2 = Slot<Command> render（柏為 render tail）/ SimFollowMeshSurface + SimPointMeshCollisions = PointSimulation-gated（柏為 E-hard）**。
> - **CPU-readback rail ✅BUILT（production，非僅 golden 的 debugCookedBuffer）**：upstream op commit+wait，SwPoint bag StorageModeShared，ops 直讀 contents()；**ReadPointColors（colorlist_ops_readpointcolors.cpp）+ PointsToCPU（pointlist_ops_pointstocpu.cpp）已註冊/已建**。此簇 autonomous leaf **SampleCpuPoints ✅BUILT @504f149**（host Bezier+quat；pointlist_ops_samplecpupoints.cpp；fork=samplecpupoints-singleinput / cpupoint-reuses-swpoint）；**PointToMatrix（evaluate==nullptr，卡 value-emit seam＋ICamera）+ CpuPointToCamera（ICamera）= 柏為域**。
> - 兩條 rail 合計的 autonomous leaf 恰 2（FindClosestPointsOnMesh / SampleCpuPoints）現已 ✅全 BUILT @504f149，其餘柏為域。誠實殘渣小，autonomous leaf residue 已觸底。
> **★ PointsOnImage ≠ clean leaf（前 scout 誤列）：它是 4-pass GPU prefix-sum（TiXL `Assets/shaders/points/_internal/PointsOnImage/{0-Clear,1-SumRows,2-SumColumns,3-EmitPoints}.hlsl`）需 GPU-determined-output-count 架構 = 柏為 call，移入「GPU-determined-output-count」子縫。**
generate: BoundingBoxPoints/CommonPointSets/DoyleSpiralPoints2/GridPoints/HexGridPoints/LinePoints/MeshVerticesToPoints/PointInfoLines/PointsOnImage(★4-pass-prefix-sum 非 leaf)/PointsOnMesh/PointTrail(R3)/PointTrailFast/RadialPoints/RepeatAtPoints/RepetitionPoints/SpherePoints/SubdivideLinePoints（TiXL GPU 原版）
modify: AddNoise/AttributesFromImageChannels/ClearSomePoints/CustomPointShader(R3,+sdf+gradient)/FilterPoints/LinearSamplePointAttributes/MapPointAttributes(+gradient+curve)/MoveToSDF(R3,+sdf)/PointAttributeFromNoise/PointColorWithField(+sdf)/RandomizePoints/ResampleLinePoints/Sample*Attributes/SamplePointsByCameraDistance(+camera3d)/SelectPoints/SelectPointsWithSDF(+sdf)/SetAttributesWithPointFields(R3)/SetPointAttributes/SortPoints(+camera3d)/TransformWithImage
sim: PointSimulation(R3,+feedback)/SamplePointSimAttributes/SimCentricalOffset/SimDirectionalOffset/SimDisplacePoints2d/SimForceOffset/SimNoiseOffset/GrowStrains/SimBlendTo/SimFollowMeshSurface(+mesh)/SimPointMeshCollisions(+mesh)/ApplyRandomWalk(+feedback)
transform: BoundPoints/FindClosestPointsOnMesh(+mesh)/IkChain(R3)/MovePointsToCurveSpace/OrientPoints/PolarTransformPoints/ReorientLinePoints/SnapPointsToGrid/SnapToPoints/SoftTransformPoints/TransformFromClipSpace(+camera3d)/TransformPoints/TransformSomePoints/WrapPointPosition/WrapPoints
combine: BlendPoints/CombineBuffers/PairPointsForGridWalkLines/PairPointsForLines/PairPointsForSplines/PickPointList/SplinePoints(+cpu-point-list)/_ExecuteCombineBuffers
_internal: AnalyzeBuffers/MultiUpdatePoints/_AppendPoints/_OffsetPoints/KeepBufferReference/NumberLinePoints/RecycleBuffer/TraceContourLines

### `shader-graph`（~~~64，解鎖整個 field 島~~ ✅BUILT 2026-06-27，剩 ~9 terminal 葉；field_graph_builder.h+42×field_ops_*.cpp+field_render.cpp。無獨立通用 shader-graph 縫）
> **★★ 2026-06-27 GROUND-TRUTH 二次校正（code-cited）：SDF generator 已採盡；殘 9（op_census --undone field）中 clean ≈ 1（Raster3dField 類已採 `field_ops_raster3dfield.cpp`）。其餘全卡子 seam → field-render-output（Render2dField/SDFToColor/VisualizeFieldDistance，executor 已建剩接線）/ vec3-field（SdfToVector/ApplyVectorField）/ sample（SampleFieldPoints/RepeatFieldAtPoints）/ texture-into-field（HeightMapSdf）/ SubDivPattern3d。SDF generator 不再是「待採葉桶」。**
field/ 全 60 顆（SDF generators 16 + modifiers 7 + space transformers 10 + combiners 4 + texture/vec3 field 3 + use/render/analyze + internal helpers）。R1 主力：BoxSDF/SphereSDF/TorusSDF/CylinderSDF/PlaneSDF/OctahedronSDF/Pyramid/Prism/CappedTorus/Capsule/ChainLink/BoxFrame/Bend/Reflect/Repeat*/Rotate*/Translate/Invert/Absolute/TranslateUV…
＋particle force：CustomForce(R3)/FieldDistanceForce(R3)/FieldVolumeForce(R3)/RandomJumpForce(R2)
次要（shader-graph 解鎖後相關）：raymarch（RaymarchField/Render2dField/RaymarchPoints…6）、mesh ColorVerticesWithField/SelectVerticesWithSDF/CustomFaceShader/CustomVertexShader

### `mesh-pipeline`（~~~49，mesh 島~~ ✅BUILT 2026-06-27，mesh-input seam d81d705，31 mesh 檔，meshSpecSink live；剩 Draw* 需 camera3d、loader 需柏為）
> **★★ 2026-06-27 GROUND-TRUTH 二次校正（code-cited）：純 CPU mesh gen/modify rail 已採盡 → 殘 24（op_census --undone mesh）中 clean gen/modify ≈ 0。其餘全卡子 seam → CPU-simplex-noise（DisplaceMeshNoise/ScatterMeshFaces，snoiseVec3 未在 CPU rail）/ texture-into-mesh（TextureDisplaceMesh/DisplaceMeshVAT）/ points-into-mesh（Warp2dMesh/MoveMeshToPointLine/RepeatMeshAtPoints/MeshFacesPoints/BlendMeshToPoints）/ shader-graph-Field（ColorVerticesWithField/SelectVerticesWithSDF/CustomFaceShader/CustomVertexShader）/ camera3d-Draw*（DrawMesh*/VisualizeUvMap）/ loader（LoadObj）/ R3（DelaunayMesh/ExtrudeCurves/DisplaceMesh）。generator 桶下列 R1 多數已採，勿當待採。**
generator R1: QuadMesh/SphereMesh/TorusMesh/CylinderMesh/NGonMesh/CubeMesh/IcosahedronMesh
modifier R1: FlipNormals/RecomputeNormals/CombineMeshes/BlendMeshVertices/SplitMeshVertices/TransformMesh/TransformMeshUVs/DisplaceMeshNoise/PickMeshBuffer/_MeshBufferComponents
R2-R3: Deform/Displace*/Delaunay(R3)/Extrude/Collapse/Scatter/Warp2d/MeshProjectUV/Custom*Shader(+shader-graph)/AnalyzeMeshBuffers/_AssembleMeshBuffers/UVsViewer
draw（次要 +camera3d+Layer2d）: DrawMesh/DrawMeshAtPoints/DrawMeshCelShading/DrawMeshChunksAtPoints/DrawMeshHatched/DrawMeshUnlit/DrawMeshWithShadow/VisualizeMesh/VisualizeUvMap

### `Layer2d+Execute`（~~~37~~ ✅PARTIAL~70% 2026-06-27，core 已建 point_ops_{execute,loop,switch,layer2d}.cpp；剩 ~12 進階 fx 葉。**無 dx11 前置**）
render basic: Layer2d/DrawScreenQuad/DrawScreenQuadAdvanced
image: Glow(R3)/Bloom(已port?)/AfterGlow/AfterGlow2/AdvancedFeedback/AdvancedFeedback2/Sketch/AsciiRender/GlitchDisplace/WaveForm/LightRaysFx/ScreenCloseUp/ColorPhysarum/DetectMotion/FieldToImage(+gradient)
flow: Execute/ExecRepeatedly/ExecuteOnce/LoadSoundtrack/LogMessage/Loop(R3,+context-var)/ResetSubtreeTrigger/Switch/TimeClip(R3)/BlendScenes/GetPosition(+camera3d)/SetRequestedResolutionCmd/ExecuteRawBufferUpdate(+RWStructuredBuffer)/Set*Var 系列(SubGraph 部分)
point draw（雙擋 +point-buffer）: 全 16 draw op + _VisualizePointFields/_BuildSpatialHashMap/SampleSplinePoint

### `camera3d`（~~~50，依賴 dx11-api-wrapper~~ ✅core-BUILT 2026-06-27，**零 dx11 依賴**；field_camera.h Mat4 stack+point_ops_camera.cpp+resident_matrix_output_cook.cpp。剩 ~25-29：gizmo 0/15、camera 變體、value-output Phase2/3=需柏為）
render camera 全 11 / gizmo 全 15 / transform 全 8 / _/ Apply* 系列 / GetScreenPos / TransformsConstBuffer
point: CpuPointToCamera（仍卡 ICamera=柏為域）/PointToMatrix（★emit 半邊 ✅BUILT@b77789e=value-emit pass 已建）
field render: RaymarchField/Render2dField/VisualizeFieldDistance（次要）

### `dx11-api-wrapper`（~~~25，最底層前置~~ ❌N/A 2026-06-27 — Metal 上非真縫）
> **render_command.h:40-110 DrawKind/BlendMode enum 已 1:1 吸收 TiXL D3D11 RTV/DSV/BlendState/Viewport（ScreenQuad/Layer2d/Mesh/Clear 皆 DrawKind）。「camera3d/Layer2d 卡 dx11」這條 keystone 依賴是 FALSE。下列 op 多為 compound 內部子節點或已被 render-command 資料化吸收，不是孤立待蓋葉子。**
render _dx11/api: Draw/DrawInstancedIndirect/ClearRenderTarget/Rtv/Dsv/Uav/SrvFromTexture2d/Srv*/Input/Output/Rasterizer/Viewport/ResolutionConstBuffer/GetSRVProperties/InputAssemblerStage/OutputMergerStage/SetPixelAndVertexShaderStage…
_dx11/buffer: GetBufferComponents/IntsToBufferWithViews/ListToBuffer/Texture3dComponents
_dx11/fxsetup: ShowTexture3d/ExecuteBufferUpdate/PickBlendMode/SwitchBlendState/PrefixSum

### `feedback`（~~~16~~ ✅BUILT 2026-06-27，ping-pong seam 5385e6b + multi-pass executor 15161e3 Bloom parity；剩進階 cross-frame 消費者）
image: AdvancedFeedback/AdvancedFeedback2/AfterGlow/AfterGlow2/FluidFeedback/SimpleLiquid/SimpleLiquid2/KeepPreviousFrame(R2,最簡入口)/SwapTextures/RenderWithMotionBlur/_KeepPreviousFrame_Old1/SlidingHistory
point: KeepPreviousPointBuffer(usse)
render: TemporalAccumulation/SwapBuffers

### `gradient-widget`（~14，柏為 authoring 域）
image: BubbleZoom/LinearGradient/BoxGradient/RadialGradient/NGonGradient/MandelbrotFractal/Steps/SubdivisionStretch/MakeTileableImage/CustomPixelShader(R3)/ColorGradeDepth/RemapColor/CompareImages
field: SDFToColor/_ExecuteSdfToColor（次要）

### `context-var`（15，最便宜中型）
flow: GetBoolVar/GetFloatVar/GetIntVar/GetMatrixVar/GetObjectVar/~~GetStringVar~~ ✅BUILT@b03cddd/GetVec3Var/GetForegroundColor/SetBoolVar/SetFloatVar/SetIntVar/SetMatrixVar/SetObjectVar/~~SetStringVar~~ ✅BUILT@b03cddd/SetVec3Var（Set 系列 SubGraph 部分次要依賴 Layer2d+Execute）
> **★2026-06-27 String 通道 BUILT@b03cddd**：Get/SetStringVar on typed `stringVars` channel（鏡像 vec3Vars 縫）+ writer-first 2-pass string cook + per-frame clear。.t3 defaults VariableName="s"/FallbackDefault=""。named defer `defer-setstringvar-subgraph-command-rail`（SubGraph push/restore scope 未實作，待 SetStringVarCmd Command type，同 float SetFloatVarCmd）；named fork `fork-setstringvar-echo-output`（TiXL Slot<Command> Output→String echo，同 SetFloatVar/SetVec3Var）。

### `network-io`（~9 主擋 + ~14 UDP 共用底層）
io: TcpClient/TcpServer/UDPInput/UDPOutput/WebSocketClient/WebSocketServer/WebServer/RequestUrl
**UDP 底層共用** → osc(2)/artnet-dmx(8)/camera-tracking(4) 蓋 network-io 後工作量大減

### `compute-readback`（~12）
image: GetImageBrightness/RemoveStaticBackground/SortPixelGlitch/DepthBufferAsGrayScale/JumpFloodFill
point: PointsToCPU/ReadPointColors/DataPointImportExport
numbers: PickColorFromImage
render/flow: _ReadIntFromGpuBuffer/_ReadBackImageDifference

### `cpu-upload-texture`（4）
numbers: ValuesToTexture/ValuesToTexture2/GradientsToTexture/CurvesToTexture

### `cpu-point-list` / `StructuredList`（~~~15~~ ✅rail BUILT 2026-06-27 — cpu-point-list 補完 ee4a99f；StructuredList producer = 已建 PointList 第 7 flow，cook driver 已展開 MultiInput PointList port，point_graph_hostvalue_cook.cpp:64-108。非 device-IO / 非 dict-gated）
> **★★ 2026-06-27 final-autonomous-seam scout（code-cited）：StructuredList rail 已建 → 此段 autonomous leaf JoinLists ✅BUILT @504f149（MultiInput concat → Result，Result-only；pointlist_ops_joinlists.cpp；fork=joinlists-length-deferred=其 int `Length` 第 2 output 騎 value rail = deferred 柏為域）。GetPointDataFromList（output Vector3/float/Vector4 到 value rail）撞同一 point-into-frame value-emit 牆 = 柏為域（見下「value-emit」段）。SampleCpuPoints = CPU-readback rail 的 autonomous leaf（已建 rail，host Bezier+quat）✅BUILT @504f149，非此段。**
point _cpu: LinePointsCpu/LinearPointsCpu/RadialPointsCpu/RepeatAtPointsCpu/SampleSplinePoint/TransformCpuPoint
point helper/io: SampleCpuPoints(★CPU-readback rail leaf ✅BUILT@504f149)/DataPointConverter/APoint(COMPOUND)
numbers: GetListItemAttribute/GetPointDataFromList(★✅BUILT@b77789e value-emit pass 已建;fork=fork-getpointdata-vec-as-scalar-ports Vec3/float/Vec4→scalar extOut[8] 3+1+4 值相等)/JoinLists(★autonomous leaf ✅BUILT@504f149,Result-only)

### `point-into-frame value-emit pass`（★✅BUILT @b77789e，一塊解鎖一族 ~10-15）
> ✅BUILT @b77789e（fe35682，--bite 508；named defer `defer-pointtomatrix-needs-point-into-frame-pass` RESOLVED）。**機制**：additive 第二 pass `cookPointValueOutputNodes`（resident_point_value_output_cook.cpp）在 `pg.cookResident` 之後跑（frame_cook.cpp:391），host-read 完成的 StorageModeShared point buffer（`PointGraph::residentCookedPoints(path)`），重用 golden `pointToMatrixRows`；零 point_graph.cpp / EvaluationContext / resident-recursion 改動（純 additive）。
> **已 WIRE @b77789e**：PointToMatrix（emit 半邊,evaluate==nullptr 已翻正）+ GetPointDataFromList。
> **unblocked-but-not-yet-ported 同形 fan-out 葉**（各:resolve Points input→host-index→emit）：GetTextureSize（texture-source 兄弟,DIFFERENT accessor=texture 非 point）/ point-attribute / SamplePointAtList readers。
> **★兩個 NAMED LATENT RISK（今日 inert=0 forward consumer,柏為蓋 PointToMatrix→camera / 任何 in-graph value-from-point consumer rail 前必解，詳 SEAM_COMPLETION_PLAN §0′-latent-risks）**：
> - `latent-pointvalue-emit-one-frame-late`：pass 在 cookResident **之後**寫 extOut/extColorOut（其餘 emit pass 都在之前）→ in-graph consumer 於 cookResident 期間拉值=讀上一幀。sw forward push-cook 本質（TiXL pull-graph 無此延遲）；goldens 用 stub accessor,未覆蓋 multi-frame consumer 路徑。
> - `latent-stale-points-off-display-subtree`：`PointGraph::outBuf` 無 per-frame invalidation + resident cook target-driven（只 cook 顯示節點 upstream subtree）→ Points src 在顯示 subtree 之外=讀 STALE 前一幀 buffer（non-null,錯 count）→ emit stale points 非 identity。
> **★NAMED FORK** `fork-getpointdata-vec-as-scalar-ports`：Vec3/float/Vec4 攤到 scalar extOut[8]（3+1+4）,值相等,同 RequestedResolution Size-as-2-floats 已立 fork。

### `RWStructuredBuffer`（~7）
particle: ReconstructiveForce(R3)/VerletRibbonForce(R3)
render: UavFromStructuredBuffer/_ReadIntFromGpuBuffer
numbers: IntListToBuffer
flow: ExecuteRawBufferUpdate/_ReadBackImageDifference

### `source-op`（3）
image: LoadImage(R1,decoder已建,差path-watcher)/ImageSequenceClip/BuildAsciiFontSorting

### 3D render 後段（依賴 camera3d）
`pbr-material`(~10: SetMaterial/UseMaterial/DefineMaterials/SetEnvironment/Equirectangle…) /
`lighting`(~8: PointLight/SetPointLight/SetFog/FourPointLights…) /
`depth-buffer`(~8: GodRays/SSAO/DepthOfField/MotionBlur…) /
`shadow-map`(~5) / `lens-flare`(~9) / `bitmapfont`(~7: Text/TextOutlines/TextGrid/TextSprites)

### IO 子系統（多數柏為演出域）
`midi`(10) / `video-input`(9,R3) / `artnet-dmx`(8,+network-io) / `audio-playback-op`(5) /
`camera-tracking`(4,+network-io) / `keyboard-mouse`(3) / `serial`(3) / `osc`(2,+network-io) /
`data-recording`(4) / `gamepad`(1) / `beat-timing-details`(1)

### 單顆/小眾 NEW-SEAM（低優先）
`dict-context`(4) / `trigger-dirty`(2: Once/ResetSubtreeTrigger) / `keyframe-edit`(2) /
`cubemap`(2) / `cpu-point-array`(2: NoisePoints/_MixPoints) / `obj-loader`(3) /
`int-cbuffer`(1) / `spatial-hash-grid`(1: CollisionForce) / `gpu-query`(1) /
`texture-format-convert`(1) / `texture-array`(1) / `fft-compute`(1) / `raw-srv`(1) /
`render-state`(~4) / `point-sprite`(3)

---

## 桶 D — SKIP / 低優先

### D1. _Old / _obsolete / WIP skeleton（確認不 port）
- numbers _obsolete（13）：Counter/_Jitter/_Jitter2d/_AnimValueOld/__ObsoletePulse/__ObsoletePulsate/_Time_old/ExportPointList/GetIteratedFloat/GetIteratedVec3/GetIteration/IterateList/Iterator
- point legacy/obsolete（7）：_DrawBillboardsOld/_DrawQuads/_DrawVaryingQuads/LegacyParticleSimulation/_LegacySimForwardMovement/__OBSOLETEFollowMeshSurface/_RandomizePoints_Legacy1/_GridPoints_Old
- image _obsolete（3）：_BlobOld/_FractalNoiseOld/TriangleGridTransition；image use：_KeepPreviousFrame_Old1
- field _obsolete：_ExecuteSdfToColor_Old/_SDFToColor_Old
- render：_DrawLenseFlare_Old
- **WIP skeleton（.cs 無 Update 邏輯）**：SetSpeedFactors（只有欄位無實作，疑 stub）；io audio/_/ WIP 群（DataRecording/GetBeatTimingDetails/GetAllSpatialAudioPlayers）

### D2. 孤兒（A1 標選單不可達 — Phase C 前須確認真孤兒）
- **PolarCoordinates / EdgeRepeat**：cook+metal 存在，用舊式 `registerTexOp`，**主樹 registerBuiltinPointOps 未呼叫**、無 NodeSpec、無 kTable 入口。⚠ **行動項：synthesis 無法確認是否另有 caller；Phase C 開採前需確認是真 orphan（補 NodeSpec+kTable 即可救活）還是廢棄**。

### D3. Windows-only / 廠商 SDK（macOS 無對應或重投資）
- AbletonLinkSync（Windows native DLL）/ SwiftCamDevice（廠商 SDK，可能無 mac 版）/ GpuMeasure（D3D11 DisjointQuery，Metal 需 MTLCounterSampleBuffer 重寫）

### D4. 投資巨大報酬低
- LoadGltfScene/DrawScene/LoadGltf（SharpGLTF + 全新 mesh+PBR dispatch；gltf-scene seam 僅擋 ~5）
- video-input 全族（9，DirectShow/MediaFoundation 深綁，macOS AVFoundation 全替換，R3）
- LoadImageFromUrl(network-fetch)/LoadSvgAsTexture2D(svg-rasterize)/LoadSvg/LineTextPoints（svg-loader+font-line，System.Drawing→CoreGraphics/SkiaSharp 不確定）
- _SpecularPrefilter（CubeMap+GeometryShader，放錯目錄的 3D primitive，優先級最低）
- skillQuest 全族（5：DrawQuiz/ImageQuiz/ValueQuiz/_QuizUp/_ValueQuizGraph/_ReadBackImageDifference — TiXL 教學系統，非產品 clone 必要路徑）
- _sketch-context（_GetSketchPoints/_SketchImpl — 近乎獨立子系統，SKIP 直到需求明確）

---

## 盲區 / 待確認（給 Phase C 前查證）

1. **Gradient 型別**：DefineGradient/BuildGradient/SampleGradient 等列 READY-LEAF，前提是 sw 已實作 `Gradient` 型別。**開採 color/ gradient ops 前須確認**（A1 未明列 Gradient 型別證據）。
2. **dynamic shader（`_ImageFxShaderSetup2`）**：10 顆 image-filter 用 runtime hlsl 載入。sw 若只支援 static shader，需一行 platform 差異。**開採前確認 sw 是否支援動態 hlsl**。
3. **AdsrCalculator**：AdsrEnvelope 是 sw 自加 op，依賴 AdsrCalculator（T3.Core.Audio）。**確認 sw 已有**。
4. **PolarCoordinates/EdgeRepeat 孤兒**（見 D2）。
5. **multi-image unwired-input fallback** 仍 open（task_3fc122a2，lane-wide 黑-fallback convention）：開採新 multi-image op 前需知此 fork 未統一。
6. **B9 multi-image .t3 routing**：每顆開採前必 backward-trace（Cut 55 trap）。task_258d9510 audit 已 ship 的 9 顆 routing。
7. **op 總數**：本 backlog 不重數 931，信任類別檔。各類別總和約：image 127 + render 155 + point 135 + field 60 + mesh 51 + particle 20 + flow 34 + numbers 223(active) + string 33 + io 74 + data 1 ≈ **913 active**（+obsolete ≈ 接近 931）。
