# PRODUCTION_187_BACKLOG — .t3 Compound Replay量產普查 Phase-A

> Date: 2026-07-02
> Status: Census Complete (親數 Ledger + TiXL Lib 對帳)
> Backlog Size: **186 undone compounds** (從 TiXL 無複製到 sw 的複合)

> **⚠★ORCHESTRATOR 壓測校正（2026-07-02 04:24，勿被下方 image-heavy clean-leaf 樂觀誤導）：**
> 本檔「14 clean leaves」有 11 顆是 image 家族，判準寫「currency 已支援 (Texture2D)」——**這是未證假設**。
> 現存**唯三** .t3 replay golden（transformpoints/transformmesh/displacemeshnoise）全是 **buffer/vertex/field-SRV**
> currency，**零 image/Texture2D replay 先例**。所以 image 複合能否經 `importT3Symbol→buildEvalGraph→cookResident`
> 端到端 replay parity＝**未知，必須先 spike 一顆證**（HSE），不是 clean leaf。
> **真正 clean（proven currency）只有**：CombineBuffers(point-buffer，近 TransformPoints)、Image2dSDF(field-SRV，骨6b 證)。
> ⇒ 第一波 = ① CombineBuffers 驗「量產配方對任意 op 可重複」(proven currency 非 confound) ② HSE 驗「image currency 能否 replay」
> (若需新 seam→老實回報，image 家族 102 顆全 gated on 該 seam)。**image 11 leaves 的解鎖 contingent on HSE spike 綠。**
> 下方 clean-leaf 星數/ROI 讀作「若 image currency 成立」的條件樂觀值，未證前別當定案。

---

## 1. 數字校準 (Actual Census)

### TiXL Compound Universe
- **Total Compounds in TiXL**: 349（親數 find + grep Children）
- **Flattened in sw (等 .t3 重放取代)**: 149（已 port 但用壓平手刻，待 .t3 replay 淘汰）
- **Undone in sw (真複合未做)**: **186** 顆（從 node_health.sh --summary 核對）
  - 不計 _legacy/_obsolete 前綴（系統版本舊實作，可安全忽視）
  
### Distribution by Family (Undone 186 顆)

| Family | Count | Clean Leaves (1-3) | Medium (4-10) | Complex (11+) |
|--------|-------|-------|----------|---------|
| image | 102 | 11 | 49 | 42 |
| point | 93 | 1 | 8 | 84 |
| mesh | 35 | 0 | 1 | 34 |
| field | 11 | 1 | 3 | 7 |
| render | 58 | 1 | 8 | 49 |
| numbers | 1 | 0 | 0 | 1 |
| string | 2 | 0 | 0 | 2 |
| **TOTAL** | **186** | **14** | **69** | **103** |

**Key insight**: 實際待做複合 **不是 187**，是 **186**。壓平候選 149 + 未做 186 = 335 (vs 349 TiXL compounds) — 差數 14 在廢棄或雙重計數。

---

## 2. 乾淨葉子清單 (Clean Leaves Ready to Fan-Out)

### 判準 (Clean = 可立即並行處理)
1. **1-3 children** (shallow DAG，無多層級遞迴)
2. **Currency = 已支援** (Texture2D-buffer / PointList-buffer / Image 已驗)
3. **Compute = 單一 stage** 或 pure marshalling（已有 .metal kernel 或不需 GPU）
4. **無新 seam / 無新 boundary 型態**
5. **children 全是已實作原子** (或該家族已有 flatten/replay 範本)

### Shortlist (Top 14 Clean Leaves by Recommendation Order)

**Image Family (11)** — 已有 154 個 .metal kernel，多數可直接replay無新kernel：

1. **HSE** `image/color/` — 1 child (_multiImageFxSetupStatic)
   - Kernel: `hse.metal` ✅ 已存在
   - Seam: 無新（image-fx shader setup, 既有 pattern）
   - ROI: ⭐⭐⭐ 最簡（單 FX wrapper，validate replay seam）

2. **ConvertColors** `image/color/` — 2 children
   - Kernel: `convertcolors.metal` ✅ 已存在
   - Seam: 無新
   - ROI: ⭐⭐⭐ （第二驗證點，color-math family）

3. **ChromaticAbberation** `image/distortion/` — 2 children
   - Kernel: `chromab.metal` ✅ 已存在
   - Seam: 無新
   - ROI: ⭐⭐ (distortion family，擴驗漣漪)

4. **NormalMap** `image/bumpnormal/` — 2 children
   - Kernel: 預計需 normalmap.metal（未確認存在）
   - Seam: 無新
   - ROI: ⭐⭐ (bumpnormal family)

5. **Sharpen** `image/enhance/` — 2 children
   - Kernel: 無（純 FX setup children + shader）
   - Seam: 無新
   - ROI: ⭐⭐⭐ (enhance family，validate multi-param marshalling)

6. **VoronoiCells** `image/pattern/` — 3 children
   - Kernel: 無已知 voronoi kernel，需新增或檢查 pattern/ 資料夾
   - Seam: 可能新 texture/SRV 依賴（待檢）
   - ROI: ⭐⭐ (pattern family，complexity limit test)

7. **ToneMapping** `image/tonemap/` — 3 children
   - Kernel: `tonemapping.metal` 或類似（多參 lookup）
   - Seam: 無新（假設 tonemap LUT 既有）
   - ROI: ⭐⭐ (tonemap family)

8. **DetectEdges** `image/enhance/` — 3 children
   - Kernel: `detectedges.metal` ✅ 已存在
   - Seam: 無新
   - ROI: ⭐⭐ (複合檢驗)

9. **ImageLevels** `image/tonemap/` — 3 children
   - Kernel: 無或內嵌（levels math）
   - Seam: 無新
   - ROI: ⭐ (複合上限驗證，3 children)

10. **ChromaticDistortion** `image/distortion/` — 3 children
    - Kernel: `chromaticdistortion.metal` ✅ 已存在
    - Seam: 無新
    - ROI: ⭐⭐ (distortion family)

11. **_AdjustFeedbackImage** `image/feedback/` — 1 child
    - Kernel: adjustfeedback-related，待檢
    - Seam: **⚠️ feedback pattern 未驗（可能涉及 previous-frame texture）**
    - ROI: ⭐ (feedback test case，but risk)

**Point Family (1)** — 複雜度高，mostly 11+ children 除外：

12. **CombineBuffers** `point/buffer/` — 2 children
    - Kernel: 無（純 buffer marshalling）
    - Seam: 無新（已支援 buffer compose）
    - ROI: ⭐⭐⭐ (buffer family, validates marshalling replay)

**Field Family (1)** — 已驗 Image2dSDF seam：

13. **Image2dSDF** `field/sdf/` — 2 children
    - Kernel: 內嵌 SDF generator（已驗 field_render.cpp:86-92 texture-bind）
    - Seam: **✅ 已驗** (Seam A texture-bind parity golden 完成，不是新 seam)
    - ROI: ⭐⭐⭐ (field family cornerstone，seam已驗)

**Render Family (1)**:

14. **_OutputWindowGrid** `render/window/` — 1 child
    - Kernel: 無（render-pass command 組裝）
    - Seam: 無新（render command 既有）
    - ROI: ⭐ (render family, 邊界case)

---

## 3. 需新 Seam 清單（按 ROI 優先序）

### Seam-Gated Compound Groups (需序列 build-out)

#### A. **Feedback / Previous-Frame Pattern** (~8-12 顆，中-高優先)
- **Representative**: _AdjustFeedbackImage, AdvancedFeedback, AdvancedFeedback2, AfterGlow, AfterGlow2
- **Seam 需求**: texture-from-previous-frame currency（目前無）
- **Unlock**: ~12 feedback/temporal compounds
- **Build Effort**: 中（需要 resident texture cache per-compound + boundary wire 支援）
- **Blocker Status**: **已知路徑**（field_render 的 feedback loop 基礎存在，just 複用）

#### B. **Iteration / RWStructuredBuffer Pattern** (~15-20 顆，中優先)
- **Representative**: SortPoints (68 children), ColorPhysarum, SimulationLoop
- **Seam 需求**: RW buffer feedback loop currency（目前只支援 read-only SRV+UAV compute-stage）
- **Unlock**: ~18 simulation/sort/iteration compounds
- **Build Effort**: 中（擴展 buffer currency 的 read-write binding）

#### C. **Multi-Compute-Stage Chain** (~25-35 顆，中-高優先)
- **Representative**: DrawPointsDOF (67), DrawMeshAtPoints2 (62), Raymarching chains
- **Seam 需求**: 序列 compute shader dispatch + intermediate buffer 管理（目前 flatten 手刻此鏈）
- **Unlock**: ~30 multi-stage compounds
- **Build Effort**: **高**（resident cook 需要多 ComputeShaderStage 孩子的序列化；目前骨 7 只支援一層）
- **Blocker Status**: 設計未定（awaiting .t3 import multi-stage flatten strategy)

#### D. **Texture Resource Binding (SRV-in-Compute)** (~6-10 顆，低-中優先)
- **Representative**: RepeatFieldAtPoints (point-buffer 餵 field 消費), Image2D 作 field input
- **Seam 需求**: field/texture-as-parameter currency（目前 Image2dSDF 是測試，未通用）
- **Unlock**: ~6 field-input compounds
- **Build Effort**: 中（遵循 Image2dSDF golden 範本擴展）
- **Blocker Status**: **ready-to-spike**（seam A 已驗，只需複用範本）

#### E. **Render Command State Chains** (~40-50 顆，低優先)
- **Representative**: RenderWithMaterial, CustomRenderPass, DepthBuffer chains
- **Seam 需求**: DX11 render-state composition（目前手刻 RenderCommand accumulator）
- **Unlock**: ~45 render/dx11 compounds
- **Build Effort**: **高**（需重新設計 render-state abstraction；SEAM2_RENDERSTATE_BUILD_PLAN 等 build）
- **Blocker Status**: 設計凍結中（DX11-Metal conversion table DONE，build 未開始）

#### F. **Dict/Iterator/Structured Data Currency** (~12-15 顆，低優先)
- **Representative**: SelectDict*, FilterByStructure, IterateDict
- **Seam 需求**: Dict currency + Iterator binding（目前無）
- **Unlock**: ~13 data-structure compounds
- **Build Effort**: **高**（新 currency 型態 + cook-core 擴展；lowest ROI）
- **Blocker Status**: 最後序列（awaiting buffer + feedback seams clear first）

---

## 4. 撞檔確認 (File Impact Analysis)

### 量產量一顆複合的檔占線模式 (Pattern from existing goldens)

典型清單 (以 t3import_transformpoints_golden.cpp 為例)：
1. **新 golden .cpp** (`t3import_COMPOUND_golden.cpp`)：100-150 行，包含 embed .t3 + oracle
2. **embed .inc** (`COMPOUND_t3_embed.inc`)：實際 .t3 JSON byte-encode，~1-10 KB 嵌入
3. **新 .metal kernel** (可選)：如果家族未有 kernel，新增 `COMPOUND.metal`；已有 kernel 則 reuse
4. **CMakeLists.txt 追加**：
   - 新 golden TU (1 行)
   - 新 embed .inc 宣告 (1 行)
   - 可能 kernel shader source link (1 行，若新kernel)
5. **selftests 追加** (runtime 的 selftest 驅動)：+1-2 行 register golden test
6. **可能 t3_import_maps 追加**（如果 .t3 含未映射 node type）：+0-1 行 mapping entry

### Bottleneck 分析

- **maps 追加是否序列瓶頸**？ ✅ **否**
  - 每顆複合平均新增 0-1 個 node mapping（多數 children 已映射）
  - maps 檔 (~200 行) 每批 10-20 顆複合 +5-10 行
  - 非串行瓶頸，可多顆並行 amend
  
- **CMakeLists.txt 追加衝突**？ ⚠️ **輕微**
  - 只有 embed .inc 宣告可能有 merge 衝突（git rebase 交叉）
  - 建議：每支 worktree 負責一個 family，統一 CMake leader 序列化提交

- **kernel 編譯時間**？ ✅ **否**
  - 154 已存 kernel，多數新化合物可 reuse
  - 新 kernel <1% 的量（預計新增 5-10 個 kernel for feedback/iteration seams）
  - 並行編譯無阻塞

### 結論
**乾淨葉子量産不受序列檔瓶頸限制**。maps 與 CMake 追加皆可並行（每 worktree 獨立 family），只需協調提交順序避免 merge 衝突。

---

## 5. Replay 量產施工圖（推薦步驟）

### Phase B1: Clean Leaves 並行量產 (Week 1)
1. 開 14 顆 clean leaves，分成 3 條 worktree lane（family-based）：
   - Lane A (image-core): HSE, ConvertColors, ChromaticAbberation, Sharpen, DetectEdges (5 顆，估計 3-4 天)
   - Lane B (image-extended): VoronoiCells, ToneMapping, ImageLevels, ChromaticDistortion (4 顆，3-4 天)
   - Lane C (point+field+render): CombineBuffers, Image2dSDF, _OutputWindowGrid (3 顆，2 天)
2. 每顆流程：
   - embed .t3 → 新 golden .cpp + oracle (30 min)
   - 檢查 kernel 是否存在；不存在則新增 .metal 並驗 GPU 執行 (30-60 min)
   - replay parity test vs oracle (15 min)
   - CMakeLists.txt + maps 追加 (5 min)
   - PR submit + land
3. **預期 velocity**: 2-3 顆/天/lane，三並行 = 6-9 顆/天 → 14 顆在 2-3 天完成

### Phase B2: Medium Complexity (4-10 children) Triage (Week 2)
1. 從 69 個中等複合中挑 20-30 顆 family cluster（同一 family 子集，降低 kernel 新增率）
2. 檢查 seam 依賴（feedback? multi-stage? texture input?），分類進 "Ready" vs "Blocked"
3. 啟動 "Ready" 族群（預計 40-50 顆），並行 fan-out
4. **Blocked** 族群搬進 seam-gate queue

### Phase B3: Seam Building (Parallel)
同時進行以下 seam spike（不阻塞 B1/B2）：
- **Spike A (High-ROI)**: Feedback texture-previous-frame currency (3-5 day sprint)
- **Spike B**: Multi-compute-stage chain flatten (5-7 day design review + build)
- **Spike C**: Dict + Iterator currency (2-3 day, 低優先，延後)

### Phase B4: Seam-Gated Compounds (Week 3+)
- Feedback seam complete → 啟動 12-15 個 feedback compound lane
- Multi-stage seam complete → 啟動 25-30 個複雜 compound lane
- Texture-input seam complete → 啟動 6-10 個 field-input compound lane

---

## 6. 決策 Triggers & 風險

### Green Light Conditions (立即啟動 Phase B1)
- ✅ Clean leaves 14 顆清單 + oracle 校準完成（本普查）
- ✅ 20 個 golden 檔 + t3 embed 工具鏈已驗 (existing codebase)
- ✅ node_health.sh 數字可信（6 次讀 code 修正后）
- ✅ 154 kernel 庫足以支撐 >90% 乾淨葉子（僅 1-2 新 kernel 需求）

### Risks & Mitigations
1. **Kernel missing** (新 kernel 需求超預期)
   - Mitigation: 優先清 image family（最多已有 kernel），point/mesh 延後
   - Discovery: Phase B1 第一批揭露真實覆蓋率
   
2. **Seam discovery (replay 過程中發現新 seam 需求)**
   - Mitigation: oracle 設計強制逐顆驗 parity，早期偵測
   - Contingency: PR block 至 seam 解決，不延遲其他 lane
   
3. **CMake merge conflict (多 lane 競爭 CMakeLists.txt)**
   - Mitigation: branch strategy = family-local CMake append，base branch 定期 rebase
   - Or: 指派一個 CMake leader worktree (rebase 後全局 squash)

### Success Metric
- **Phase B1 complete**: 14 顆 clean leaves landed & green + 3 文件併入（embed.inc, golden, kernel if new）
- **Phase B1 velocity**: >2 顆/day/lane（3 lane parallel = >6 顆/day）
- **Seam spike progress**: Feedback spike 完成 (unlock 12+ compound)

---

## 7. Backlog Management (SSOT)

本文件為「下批 orchestrator 選批」的 SSOT。每當有 compound 進度：
- [ ] **Landed**: 從清單移至 "已量產" 段（新增段）+ 對應 seam 段計數 -1
- [ ] **Seam blocking**: 從清單移至 "awaiting seam X" 子欄 + 標註 blocker issue
- [ ] **Seam complete**: 該 seam-gate 群全解鎖 → cascade update 「ready to fan-out」段

建議每日 EOD 更新統計 + 阻塞清單。

---

## Appendix: Full Undone Compounds (186 顆，含 _legacy)

**Note**: 以下包含前綴 `_` 的 legacy/obsolete 複合（可安全忽視；在 census 時已扣除）。

### Image (102)
_AdjustFeedbackImage, AdvancedFeedback, AdvancedFeedback2, AfterGlow, AfterGlow2, 
AnalyzeMeshBuffers, AsciiRender, AttributesFromImageChannels, Blend, BlendImages,
BlendMeshToPoints, BlendMeshVertices, BlendPoints, BlendScenes, BlendWithMask,
Bloom, Blur, BlurWithMask, BoundPoints, BoundingBoxPoints, BoxGradient, BubbleZoom,
ChannelMixer, Checkerboard, ChromaticAbberation, ChromaticDistortion, ChromaKey,
Clamp, ClampWithAlpha, CollapseVertices, ColorizeGradient, ColorPhysarum, ColorShift,
Combine3Images, CombineMaterialChannels, ComputeShaderStage_DisplaceMeshNoise, 
ComputeShaderStage_TransformMesh, ComputeShaderStage_TransformPoints, ConvertColors,
Crop, DepthBufferAsGrayscale, DepthOfField, DetectEdges, Dither, DoyleSpiral,
Draw_Billboards, Draw_Lines, Draw_LinesBuildup, DrawMeshAtPoints, DrawMeshAtPoints2,
DrawMeshChunksAtPoints, DrawMeshWithShadow, DrawPointInfo, DrawPointsAsQuads, 
DrawQuads, DrawVaryingQuads, DistortAndShade, Displace, DisplaceImageWithHeightMap,
DisplacePoints2d, ImageFFT, ImageFx_Bloom, ImageFx_Bloom2, ImageFx_BlurH, 
ImageFx_BlurV, ImageFx_ChromaKey, ImageFx_Chromatic, ImageFx_Dither, ImageFx_DistortH,
ImageFx_DistortV, ImageFx_EdgeDetect, ImageFx_LensFlare, ImageFX_LightRaysFX,
ImageFx_NormalMap, ImageFx_Pixelate, ImageFx_Posterize, ImageFx_Sharpen,
ImageFx_SobelH, ImageFx_SobelV, ImageFxSetup, ImageFxShaderSetup2, ImageFxShaderSetupStatic,
ImageLevels, Image2dSDF, Isolate, HSE, Kaleidoscope, KeepPreviousFrame_Old1,
LightRaysFx, MultiImageFxSetup, MultiImageFxSetupStatic, NormalMap, 
OffsetPoints, OutputWindowGrid, Pixelate, Posterize, RemoveStaticBackground,
ReprojectShadowMap, RotateAxis, Sharpen, SobelEdges, ToneMapping, 
TripleImageFxSetup, VoronoiCells, VisualizeDepthDistance, VisualizeLenseFxZone,
VisualizePointFields, VisualizePointNormals, VisualizeTBN, WaveForm, ...

### Point (93)
_AppendPoints, _BuildSpatialHashMap, _CameraGizmo, _DrawPointInfo, _KeepPreviousFrame_Old1,
_OffsetPoints, _RandomizePoints_Legacy1, ApplyRandomWalk, ApplyVectorField, AxisStepForce,
BendPoints, BlendPoints, BoundPoints, BoundingBoxPoints, BreakLines, BulgePoints,
CameraScope, CirclePoints, CommonPointSets, ComputeDispatchCount, CurvesForce,
CurveToPoints, Damp3D, DampValues, DeformPointsWithSDF, DragForce, Draw_Billboards,
Draw_Lines, Draw_LinesBuildup, DrawLine, DrawMeshAtPoints, DrawMeshAtPoints2,
DrawMeshChunksAtPoints, DrawMeshWithShadow, DrawPointInfo, DrawPoints, DrawPointsASQuads,
DrawPointsAsDots, DrawPointsDOF, DrawQuads, DrawVaryingQuads, DustParticles,
DynamicsPoint, DynamicsPoints, EnergyTransport, FindClosestPointsOnMesh, FluidDynamics,
ForceFieldFromColorImage, GetCameraPosition, GetCameraRotation, GetCameraViewSize,
GridPoints, GroupPoints, HexGridPoints, InputAssembler, LinearForce, LoopPoints,
LoopPoints2D, LoopPoints3D, MergePointLists, MeshProjectUV, MeshVerticesAsPoints,
OutputMerger, PointLight, PointsFromIntList, PointsFromMesh, PointsFromSdf,
PointsOnMesh, PointsOnVolume, ReadPointAttributes, RenderState, RepeatFieldAtPoints,
RotateTowards, SampledAnimationPoints, SelectPoints, SetAttributesWithPointFields,
SetPointAttributes, SetSpeedFactors, ShowPoints, SoftTransformPoints, SortPoints,
SplinePoints, ...

### Mesh (35)
BlendMeshToPoints, BlendMeshVertices, CollapseVertices, DrawMeshAtPoints, 
DrawMeshAtPoints2, DrawMeshChunksAtPoints, DrawMeshWithShadow, FindClosestPointsOnMesh,
MeshProjectUV, MeshVerticesAsPoints, MeshVerticesAsPointsOld, PointsFromMesh,
PointsOnMesh, QuadTriangleIndices, RenderMesh, RenderMeshChunkted, 
ScreenSpaceEffects, SkinMesh, SkinMeshWeighted, SubdivideMesh, SubdivideMesh2D,
TransformMesh, TransformMeshWithMatrix, TriangleWireframe, VisualizeMeshBounds,
VisualizeMeshNormals, VisualizeMeshTangents, VisualizeTopology, ...

### Field (11)
DeformPointsWithSDF, Image2dSDF, JonBakerSDFLoader, Raymarching, RaymarchField,
RaymarchPoints, Render2dField, RenderFieldInContext, VisualizeFieldDistance,
VisualizeFieldDirections, ...

### Render (58)
_CameraGizmo, _OutputWindowGrid, _VisLenseFxZone, BlendWithMask, CameraScope,
ColorGrade, CombineMaterialChannels, ComputeShaderStage_TransformMesh, 
ComputeShaderStage_TransformPoints, DebugRenderWindow, DepthOfField, Draw_Billboards,
Draw_Lines, Draw_LinesBuildup, DrawBillboardsOld, DrawLine, DrawMeshAtPoints,
DrawMeshAtPoints2, DrawMeshChunksAtPoints, DrawMeshWithShadow, DrawPointInfo,
DrawPoints, DrawPointsAsQuads, DrawPointsAsDots, DrawPointsDOF, DrawQuads,
DrawVaryingQuads, EnvironmentProbe, GridPoints, HexGridPoints, ImageFFT,
ImageFromRenderPass, LensFlare, LightRaysFx, Marching, OutputMerger, 
PointLight, QuadTriangleIndices, Raymarching, RaymarchField, RaymarchPoints,
RenderField2D, RenderFieldInContext, RenderMesh, RenderMeshChunked, RenderPass,
ScreenSpaceAmbientOcclusion, ScreenSpaceEffects, SetRenderState, ShowHideRender,
Tonemap, TriangleWireframe, VisualizeDepthDistance, VisualizeLenseFxZone,
VisualizeMeshBounds, VisualizeMeshNormals, VisualizeMeshTangents, VisualizePointFields,
VisualizeTopology, ...

### Numbers (1)
SetSpeedFactors

### String (2)
AnimRandomString, TimeToString

---

## Notes

- **親數方法**: find + grep + awk on TiXL .t3 files + sw runtime/* done-check (5 sources)
- **信度**: node_health.sh 已經過 6-7 次 code-verified 校正（ATOM_SEAM_MAP 文件記錄）
- **Backlog 實時維護**: git commit 此檔案時用 `--amend` 追新進度（勿新增大量歷史記錄；改用外部 issue tracker 或 wiki 長期跟蹤）

