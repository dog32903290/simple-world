# SEAM_COMPLETION_PLAN — 補縫施作計劃（從現在到所有縫補完）

> 柏為 2026-06-20 19:12 下令：「寫一個到我們全部的縫都補起來的施作計劃」。
> 這是 directive 工法③「補縫到位→進 Phase C 大規模並行」的完整 roadmap。
> 可追蹤、可跨 session 接手、可被無人值守 watcher + sw-node-batch / sw-batch 照著一塊塊自走。
>
> **資料來源 + 可信度**：
> - **census（逐顆 grep 兩套註冊，wj5srhwi5，2026-06-20）= 硬數據**：6 家族（numbers/string/field/image/mesh/point）的現可採 clean leaf + blocked seam，逐顆驗證，最可信。
> - **seam 盤點（a33ed，估算 + 全局）= 補充**：render/io/flow/data/animation 家族 census 沒掃，用此估（標「約」）。
> - ⚠ **OP_BACKLOG/SEAM_GRAPH（2026-06-16）的「ready-leaf ~200」是樂觀幻覺**，已被 census 戳破（真實現可採 18）。凡與 census 衝突，以 census 為準。

---

## 0. 一句話戰略

**現成彈藥只有 18 顆乾淨葉子；800 顆裡絕大多數卡在 ~20 塊未建 seam 後面 → 補縫是主路徑，不是次要。**
補縫（改引擎底層）與採葉子（改 registry）**踩不同檔、可同跑不同 lane 不互撞**：補一塊縫解鎖一批葉子 → sw-node-batch 並行採掉 → 補下一塊。

> **姊妹帳：[DEBT_LEDGER](DEBT_LEDGER.md)（債線）。** 本檔＝**產能線**（往前織網：補縫+採葉子）。DEBT_LEDGER＝**債線**（回頭補洞：架構債 26 檔破 400 行 + 排修/parity 真債）。兩條踩不同檔、並行不互撞，但**產能線跑越快、架構債長越多**＝對沖。從 chip 落地進度表後，債第一次有狀態（queued/active/closed）可被撿。動 `node_registry_*` / `point_graph.cpp` / `point_ops.h` 等共享檔的還債 lane 與產能線**不可同跑同檔**（見 DEBT_LEDGER §E/§F）。

---

## 1. 現狀（census 校準）

### 1.1 已建地基（六塊視覺承重根，Cut 85-99 編完，可扣除）
point-buffer(~44/90 採) / shader-graph·field-SDF(~29/60) / context-var / cpu-upload-texture(**4/4 ✅**：ValuesToTexture+GradientsToTexture+CurvesToTexture+ValuesToTexture2 全活，`d468c16`) / Layer2d+dx11-render-graph(**~70%**：DrawScreenQuad/Clear/Layer2d 核心活，blend/postfx 殘) / camera3d + mesh-pipeline(CPU 幾何 output 活，mesh-input 未)。

### 1.2 現可採乾淨葉子＝18 顆（階段 0 立採，census 逐顆證）
| 家族 | clean 數 | 葉子 |
|------|---------|------|
| field | 5 | RepeatPolar, TranslateUV, StairCombineSDF, NoiseDisplaceSDF, SpatialDisplaceSDF |
| image-filter | 6 | WorleyNoise, ShardNoise, TileableNoise, MunchingSquares2, Raster, ZollnerPattern |
| mesh | 5 | CubeMesh, SphereMesh, TorusMesh, CylinderMesh, IcosahedronMesh |
| numbers | 1 | OKLChToColor |
| point | 1 | RepeatAtPoints |
| string | 0 | （全卡 string-rail）|

> **★2026-06-21 校正（sw-node-batch vec-math 採收）**：上表 numbers「1」嚴重低估——census 漏掉 `numbers/{vec2,vec3,int2}` 一整條純值向量數學脈（~25 顆 clean leaf，TiXL 源碼 diff 對已港葉子證實未採）。
> - **wave 1 ✅ commit `8454fd8`（15 顆）**：AddVec3/SubVec3/ScaleVector3/CrossVec3/RoundVec3/NormalizeVector3/DotVec3/Magnitude/Vec2Magnitude/Vec3Distance/AddVec2/ScaleVector2/DotVec2/Vec2ToVec3/Vector2Components。另 `0971ef9` 收 SampleGradient（晨批 orphan leaf，RED flip-the-expected 反模式修正後 land）。
> - **wave 2 🔧 在製（~10 顆）**：LerpVec3/RotateVector3/Vector3Components/EulerToAxisAngle/DivideVector2/RemapVec2/MaxInt2/MakeResolution/ScaleResolution/ScaleSize。
> - **採完此脈＝clean-leaf runway 實質見底**→下批必轉 seam（draw-pipeline 14 / list-host 10 / gpu-buffer 10 / file-io 10 解鎖排序）。
> - **工法定論**：value/CPU 葉子用「寫-leaf-no-build → orchestrator 中央一次 build」（Cut65），**勿並行各自 build**（wave 1 漏設 isolation:worktree→主樹並行 build 互撞→假 RED `doylespiral/clearsomepoints/reorientlinepoints`，clean 中央 build 全綠戳破）。NO-BITE:[] 即每顆 RED 真咬之證。

### 1.3 全局桶（800 顆，數字標「約」）
- 已港：~200（六塊前 ~112 + Phase C 已採 field29/point44/numbers122/image38/mesh3…，census 確認）
- **現可採未採（階段 0）：18（硬數據）**
- 卡 seam：~500-600（分階段 1-6）
- SKIP/obsolete/WIP：~40-50

---

## 2. 補縫順序（解鎖數 × 風險 × 視覺相關 × 依賴鏈）

> 解鎖數＝census 跨家族聚合（6 家族硬數據）+ seam 盤點估（render/io，標約）。風險 R1 最低 R3 最高。

### 階段 0｜採 18 現可採（並行，與補縫同跑，不阻塞）
sw-node-batch 一批 fan-out 18 顆（跨 5 家族，寫-leaf 不撞檔，orchestrator 合流統一加共享）。**這不是補縫，是清現貨**；隨後每補一塊縫，可採池增大再採。

### 階段 1｜R1 高解鎖燃料 ✅✅✅ 三塊全完（string-rail b247602 / list-routing 70406e1 / cpu-point-list ee4a99f，2026-06-20）
| seam | 解鎖約 | 風險 | 視覺 | 內容 / 藍本 / precedent |
|------|-------|------|------|------|
| **string-value-rail** ✅ | ~34 | R1 | 弱 | **✅ 補完 commit `b247602`（2026-06-20，7 agent 承重戰）。** string host-channel rail（第六條 cook flow 鏡像 FloatList，非 evalString 避核心風險）。3 葉子 StringLength/FloatToString/CombineStrings。★FloatToString C# 格式化深坑挖到 .NET 版本 ground-truth（TiXL net10.0→F8 暴露 IEEE-754 noise，非 .NET Framework 補零）。fork-6: StringLength.length 存 floatListBuf,下游橋延後 list-routing。實際解鎖待 Phase C 採(Layer A ~13 + Layer B StringList ~7)。**🚧 P0 結構閘（柏為 2026-06-21）：string Phase C 開採在 `task_32b5b6e5`（resident string-wire）close 之前不准開**——b247602 是 flat-only，production 走 resident（`document.h:58`）接不出，綠燈測死路。見 DEBT_LEDGER §B / 記憶 sw-string-rail-resident-gate。|
| **list-routing**（floatlist/intlist/colorlist） ✅ | ~26 | R1-R2 | 弱 | **✅ 補完 commit `70406e1`（含 R-2 production 真 fix）。** FloatList→Float 橋=推廣 AudioReaction outCache 逃生口(blueprint)，同解鎖 string-rail fork-6 StringLength 下游(flat)。★refuter 抓 R-2 自欺(橋 flat-only,production resident 接不出=lane Cut47 自欺)→真 fix:resident_host_scalar_cook.cpp(cookHostScalarNodes per-frame resident pass)+frame_cook 接線,golden 6 resident-path leg 證 production 活(FloatListLength resident=6 非 0)。IntList Float-fold/ColorList 層3延後。★追溯發現 string transport 整個 flat-only(**task_32b5b6e5 補 resident string-wire，已升 P0 結構閘**：見 DEBT_LEDGER §B，string Phase C 開採前必 close)。實際解鎖待 Phase C 採。|
| **cpu-point-list** ✅ | ~7(真 R1 4-6) | R1 | 中 | **✅ 補完 commit `ee4a99f`。** CPU point-list host-rail(第7 cook flow,SwPoint currency)+ListToBuffer 上傳橋(memcpy)。第一批 RadialPointsCpu/TransformCpuPoint/ListToBuffer。★build agent 吸收 R-2 教訓自建 resident pass(point_graph_resident.cpp cookResidentPointList),LEG4 production cookResident ringLit=904 真上螢幕。refuter MERGE-SAFE(R-2 code+pixels 雙證)。NIT: LEG2/3 軟牙強化 task_d139fa7b。下傳 readback 卡 compute-readback 延後。|
| **cpu-point-list** | ~7 | R1 | 中 | 純 CPU StructuredList↔buffer 橋。解鎖 point _cpu 族。precedent：FloatList host-rail。|

### 階段 2｜mesh 島解鎖（3D，單塊大解鎖）
| seam | 解鎖約 | 風險 | 視覺 | 內容 |
|------|-------|------|------|------|
| **mesh-input** ✅ | ~29(真 R1 6-10) | R1-R2 | 中(3D) | **✅ seam 補完 commit `d81d705`。** MeshCookCtx 加 SwMeshView/inputMeshes + MeshCountFn 升級吃 input counts(消費型 count 取決上游) + flat cookMeshNode + ★resident cookMeshNode。第一批 TransformMesh/CombineMeshes。★★**順手修 DrawMeshUnlit(bbe9feb)既有 production 黑洞**(resident 對 mesh 全盲,新增 resident cookCommand Mesh 分支)。refuter MERGE-SAFE(R-2 neutralize 鐵證+rotation/pivot 手算)。census ~29 樂觀=真 R1 6-10 純 mesh→mesh,其餘疊副 seam(texture/field/point/loader)。**Phase C 採:TransformMesh/CombineMeshes(seam 自帶)+✅ FlipNormals/RecomputeNormals/TransformMeshUVs commit `3c65ae2`**(refuter MERGE-SAFE×3,★R-2 unlit-blindness 裁定 ACCEPTABLE=resident 證 mesh gather 活,primary 算術 flat readback 機器驗,兩真非重疊牙)。剩 ~3-7(SplitMeshVertices/CollapseVertices count 變需確認可前算等)。NIT task_5d154518(meshInjectBug per-node scope,refuter 再確認)。|

### 階段 3｜視覺核心 seam（R2-R3，對「視覺一致」北極星貢獻直接）
| seam | 解鎖約 | 風險 | 依賴 | 內容 |
|------|-------|------|------|------|
| **feedback (ping-pong)** ✅seam | ~16 | R3 | — | **✅ seam 建成 commit `5385e6b`**（承重 seam #6）。承重線=**multi-tex-output**：`cookTexNode` 教會按 output pin 回不同 texture（KeepPreviousFrame 兩輸出 CurrentFrame/PreviousFrame），flat 走 `(fromPin-1)%100`→`texOutputOrdinal`、resident 走 `srcSlotId`→ordinal；單輸出 ~30 tex op 在 ordinal 0 **byte-identical**（refuter 確認非發明:graph.h:117 pinId=nodeId*100+portIndex+1）。per-node FeedbackPair{texA,texB}+realloc key(w,h,fmt)+toggle 仿 ensureOwnedTex;blit=copyFromTexture(RgbTV precedent);realloc+~PointGraph 釋放零 leak/UAF。`feedbackCooked` per-cook memo=blit+toggle 每 node 每 frame EXACTLY ONCE（雙輸出都拉也只一次,對齊 Slot Update dirty-clear）。**deliverable=KeepPreviousFrame+SwapTextures**;2-frame golden(frameN red→N+1 PreviousFrame==red)flat+resident+realloc subcase;injectBug RED×2;--bite PASS=297 NO-BITE:[];**Opus refuter MERGE-SAFE 6/6 對 TiXL .cs/.t3 源碼**。fork(具名,非當下可實現):realloc key 收窄 TiXL 全 Texture2DDescription→(w,h,fmt),引擎 frame tex 恒 non-mipped single-sample RGBA8Unorm。**★更正(2026-06-22 scout a1b941e)**：feedback folder ~11 op **全 heavy-compound(10-45 pass)0 clean-leaf**——需 multi-pass-executor(✅下行 Bloom 已建)+ cross-frame-pair 組合才解;ping-pong seam 本身活但 TiXL 無 naive consumer。|
| **multi-pass-executor** ✅seam | ~5 feed-forward(+~6 待 feedback 組合) | R2 | feedback½ | **✅ seam 建成 commit `15161e3`（承重 seam #12，2026-06-22）。** 一 op 跑成有序多 pass 過 cached-scratch 中繼（仿 point_ops_blur 2-pass-into-scratch 放大成 4N+2 pass + N scratch）。proving op **Bloom 全 parity**（brightpass→per-level downsample/blurVH→copy→reverse additive upsample；CalculateDistribution bisection verbatim）。新共享 infra=`cachedTexPSOBlendAdd`(additive PSO 獨立 cache table,refuter 證無 cross-contamination)。registerTexOp→R-2 auto(standard cookTexNode branch flat+resident)。Opus refuter MERGE-SAFE 7 向量 neutralize。fork=GlowGradient per-level tint 略(忠實 colorGradient==null 白,optional gradient input 待補)。**unlock ~5 feed-forward 多-pass 兄弟(GlowNoBlur/multi-tap Glow/streak-lens variants)=executor 已建後便宜 fan-out（Phase-C 小礦重開）**；cross-frame feedback 半(~6 AfterGlow/FluidFeedback)需 multi-pass×feedback-pair 組合 follow-on。|
| **cpu-upload-texture 補完** ✅**4/4** | ~5 | R2 | — | **✅ 全採盡**：GradientsToTexture `a0ae2c6` / CurvesToTexture `29abced`(seam #9) / **ValuesToTexture2 `d468c16`(4/4，附 FloatList resident own-tex seam，順手修 VT1 flat-only-resident pre-existing R-2 gap)**。precedent：照抄 point_ops_valuestotexture.cpp。**★VT1 selftest resident leg 缺口=task_8afef36c（spawn，非阻塞，VT2 transitively 覆蓋）。**|
| **gradient-widget** ✅seam | ~13 | R2 | ↑cpu-upload | **✅ gradient-host seam（第 8 cook flow）建成 commit `a0ae2c6`（2026-06-21，~20 解鎖最大承重）。** `Gradient` host 型別（`sw_gradient.h`，原本不存在＝真成本已付）+ DefineGradient + GradientsToTexture 落地。**剩 ~14 漸層 op（Linear/Radial/Box/NGon Gradient/Steps/SDFToColor…）待 Phase C 採**。precedent：圖案 shader 仿 point_ops_checkerboard.cpp；上傳仿 ↑。|
| **vec-color-field-output** | ~7 | ~~R2~~ **真 R3** | G3 bridge | ⚠**scout a22f5156 翻轉假設**：R-2 production 路徑**不存在**（無 Field cook 流,`renderField2d` 全 golden 呼叫,render template 丟 `f.xyz` 只寫 `f.w` 進 R32Float）→在 field 島採葉子=大規模 golden-綠/production-黑 自欺。**延後**：須先建獨立 G3「field render bridge」seam（Render2dField terminal+RGBA target+template 寫 `f.xyz`,依賴 camera3d/Layer2d,census 自標）。|
| **multi-image** ★階段3 最大塊 | ~16 | R2 | — | **scout a779d8ea → `census/_BLUEPRINT_multi_image.md`**（17 op 盤）。seam 已建（_multiImageFxSetup/Static 2-image，Displace/DistortAndShade 證）+R-2 cookTexNode 已活。**R2-ready 乾淨 4**（Blend/BlendWithMask/Combine3Images/CombineMaterialChannels2，**clean 1:1 cbuffer 無 Cut55 trap**）。**✅ 第一批 Blend+BlendWithMask commit `7969339`**（refuter 8 攻擊全清 MERGE-SAFE×2,★承重發現:3-image gather positional-by-spec=point_graph.cpp:524-538 按 spec port order 非 connection order,wire-order-independent,**無需 _trippleImageFxSetup seam**,multi-image 對 2/3-image 都通）→**✅ Combine3Images/CombineMaterialChannels2 commit `7a24c8c`（R2-ready 4 顆採盡）**。DistortAndShade Cut55 trap defer;OpticalFlow/CombineMaterialChannels(curve)延後;KeepPreviousFrame **✅ 已由 feedback seam `5385e6b` 收**;RgbTV(已港)。逐顆 .t3 backward-trace。|
| **draw-pipeline (point draw)** 🔧進行中 | ~13 | R2-R3 | — | **scout a22f5156 → `census/_BLUEPRINT_draw_pipeline.md`**。`cookCommand` resident 流已活+DrawMeshUnlit 黑洞已修（d81d705）→新 cmd 葉子**零接線上 resident 螢幕**,零 Cut55 trap,DrawLines 整檔 precedent。**✅ 第一葉 DrawClosedLines commit `1c74d3e`**（ConnectionLines backward-trace 揭真名 DrawConnectionLines=R3 spatial-hash-map+Gradient host 型別+cross-frame state→build agent 自擋,切真 R1-R2 DrawClosedLines;shared draw core additive defaulted 零回歸=DrawLines/Billboards live PASS;flat+★resident R-2 golden;refuter MERGE-SAFE 六線全攻不破）。**✅ 第二批 DrawPoints2+DrawLinesBuildup commit `6189670`**（reuse draw_points/draw_lines,Cut55 .t3 Add(-0.01) routing 防呆,shared core append-only defaulted 零回歸,refuter MERGE-SAFE）。draw 第三批 DrawMovingPoints/DrawRayLines **退單**（DrawMovingPoints=prev-frame point-buffer state R3 跨幀;DrawRayLines=camera-facing geometry camera3d 島延伸,build agent 翻轉 blueprint「ray dir」誤判→實際相鄰點+camera-space 叉積）→**純 line/point draw 採盡**(3 採+9 已港)。剩全卡 seam:Ribbons/Tubes/DrawConnectionLines/DrawPointsDOF=R3;DrawPointsShaded/DrawLinesShaded 卡 ColorField host-eval seam。下一塊 draw 大 seam=camera-facing line/ColorField host-eval。|

### 階段 4｜進階 / readback（R2-R3，中視覺）
| seam | 解鎖約 | 風險 | 內容 |
|------|-------|------|------|
| **texture-into-points** ✅seam | ~14（scope 校正：clean 2，餘拆出） | R2 | **✅ seam 建成 commit `443574e`（2026-06-21，承重 seam #10）。** PointCookCtx 加 `inputTextures[kMaxTexInputs]`+count（仿 TexCookCtx），flat+resident 雙 driver gather Texture2D SRV 進 point compute kernel（cookTexNode fwd-decl reorder above cookNode 雙檔；Texture2D 不計入 Points count）。**proving op SamplePointColorAttributes 全 parity**（transformSampleSpace Scale=2/Stretch/TextureRotate Y·X·Z/Center/Aspect 在 shader 從 scalar 組=float4x4 align-safe；sampler Wrap+Nearest）。4-leg golden（uniform/non-uniform 2×2/flat-driver PointGraph::cook/resident cookResident）。**Opus refuter MERGE-WITH-FOLLOWUP→當場補全**（matrix+sampler+non-uniform golden+flat test-gap）→re-refute clean。**★scope 校正**：scout ~14 過樂觀；clean 2 全採（SamplePointColorAttributes ✅ `443574e` + **AttributesFromImageChannels ✅ `32e559b`**，13-slot factors[] routing table，refuter MERGE-SAFE 全 slot+quirk 驗）；**MapPointAttributes ✅ commit `b808336`（承重 seam #11，bake-into-point 子 seam）**：op 內部 bake Curve→256×1 R32 + Gradient→512×1 RGBA32 scratch tex（in-cook alloc/release leak-free）綁 t1/t2；PointCookCtx 加 inputCurves/inputGradients，flat+resident 雙 driver gather。refuter MERGE-SAFE（parity 全對/lifetime leak-free/bake load-bearing），**coverage gap=task_e25ba64a**（driver gather 無自身牙，需 Curve producer，gradient 半可先補）；**PointsOnImage** = 4-dispatch CDF pipeline + intermediate-tex alloc（拆成獨立 seam，重）。|
| compute-readback | ~9-13 | R2-R3 | GPU→CPU staging（JumpFloodFill/SortPoints/PointsToCPU/SortPixelGlitch）。|
| texture-array | ~6 | R2 | image 多紋理陣列。|
| **curve / curve-host** ✅seam | ~7（剩 ~5 採） | R2 | **✅ seam 建成 commit `29abced`（2026-06-21，承重 seam #9，完成 cpu-upload 3/4）。★`sw::Curve` host 型別早已存在（`curve.{h,cpp}` 1:1 TiXL，Animator 在用）→重用不重寫**。deliverable=CurvesToTexture（own-tex R32F，rail 仿 GradientsToTexture）+ SampleCurve（value-rail，仿 SampleGradient）。core 改：`point_graph_resident.cpp` resident own-tex gate 拓寬 `(hasGradientInput\|\|hasCurveInput)`；`point_graph.h` TexCookCtx::inputCurves。Opus refuter MERGE-SAFE 7 向量（gate neutralize 雙證 load-bearing+零回歸；resident 非 theater）。**剩 ~5 curve consumer 待 Phase C 採**（CombineMaterialChannels/SetAttributesWithPointFields/SamplePointsByCameraDistance…；多需先有 Curve **producer** op，現無 wire 載 curve→延後或建 producer）。fork：r32-only（grayscale RGBA32 toggle 延後）/embedded-default-curve（無 producer，cook .t3 default，refuter 證 resident 真活）。|
| stateful-value 擴 | ~4 | R2 | per-instance 跨幀 buffer（CountInt/FlipBool 等，seam 已建升風險）。|
| source-op | ~3 | R1 | LoadImage（decoder 已建，差 path-watcher）。|
| RWStructuredBuffer | ~7 | R2-R3 | Verlet/Reconstructive force。|

### 階段 5｜render 進階（C*，視覺但深前置，全卡 camera3d 下游）— **需柏為拍板範圍**
依賴鏈：camera3d(已建) → lighting(~8,R1) → pbr-material(~10,R2) → depth-buffer(~8,R3) → shadow-map(~5,R3)；bitmapfont(~7,R3,需 BmFont asset)；lens-flare(~9,R2)；raymarch3D-PBR(~3,R3)；gltf/obj loader(~8,R3,廠商 SDK)。
柏為 lane-state 自定「延後清單」。視覺相關但前置深+高風險。

### 階段 6｜柏為域（C，硬體/演出，偏離視覺 clone 北極星）— **需柏為拍板範圍**
network-io(UDP 底層,~9+14) → osc/artnet/camera-tracking；midi(~10,CoreMIDI)；video-input(~9,AVFoundation)；serial(~3)；audio-playback-op(~5)。
**這些不是「視覺 clone」，是 VJ/演出/硬體整合**。柏為要的「全部縫補完」字面含這些，但建議放最後 + 柏為現身定範圍（多需實體裝置驗證）。

---

## 3. 里程碑（補一塊解鎖一批 → 並行採）

| 補完到 | 累計解鎖可採 | Phase C 並行批 |
|--------|------------|---------------|
| 階段 0（清現貨） | 18 | 1 批 |
| 階段 1（string+list+cpu-point） | +~67 ≈ 85 | string 族 + list 族 + point cpu 族 |
| 階段 2（mesh-input） | +~29 ≈ 114 | mesh transform 島 |
| 階段 3（feedback+gradient+vec-color+multi-image+draw） | +~65 ≈ 180 | 視覺 fx 大批 |
| 階段 4（readback+texture-into-points+curve…） | +~50 ≈ 230 | 進階 fx |
| 階段 5（render 進階，若納入） | +~55 ≈ 285 | 3D render 島 |
| 階段 6（柏為域，若納入） | +~60 ≈ 345 | IO/硬體 |

> 「345」遠少於「800」：差額＝已港 ~200 + SKIP ~50 + 估算誤差。**全部縫補完≈800 顆全可採／已採。**

---

## 4. 每塊 seam 的工法（承重，不可省）
照 sw-batch 承重工法：**Plan scout 必 backward-trace `.t3`（確認真實依賴/編譯哪個 template）→ subagent build（worktree）→ 獨立 Opus refuter（對 TiXL 逐行）→ fixer（Sonnet 兩次不過升 Opus）→ orchestrator 親手合 build + --bite + check-arch + scenario → commit → 結帳**。
- **每塊縫蓋的當下帶 2-3 顆驗證消費葉子**（防 orphan，mip seam 空轉血證）就收手，其餘延後。
- 完成定義＝對 TiXL 機器驗證（golden 對手算公式/源碼常數 + refuter + scenario），視覺 op 用 closed-form pixel-readback golden（render 島 Cut96-99 已證），過閘即 merge 主線不等眼睛。

## 5. 採葉子的工法（Phase C，補縫之後/並行）
sw-node-batch 家族並行：寫-leaf（每顆 op 自己的檔，不撞共享）→ refuter → orchestrator 合流統一加共享檔（registrar/node_registry/selftests/point_ops.h）+ 一次合 build。
- ★**scout 鐵律（vec2 全廢血證）**：判 op 已港必 grep **兩套註冊**（舊式 node_registry_*.cpp+value_eval_ops.cpp / 新式自登記 *_op_*.cpp），只看一套會誤判整批重複港。
- value/CPU 自登記葉子 conflict-free，最適合大規模並行；point/mesh/field 動家族 registrar，合流中央接線。

## 6. 盲區 / 待確認（誠實）
1. census 只掃 6 家族；render/io/flow/data/animation 的解鎖數是 seam 盤點估（±誤差大）。
2. 「已港 ~200」無權威 tally（git log + 檔數推估，±30）。建議跑 selftest harness 數註冊 NodeSpec 取硬數。
3. mesh-input ×29、list-routing ×26 是 census 分類，實際每塊是否單一 seam 需開工前 Plan scout backward-trace 確認（可能再細分）。
4. gradient-widget 真成本是建 `Gradient` host 型別（grep 確認不存在），比估的 R2 略重。
5. multi-image 每顆有 Cut55 routing trap，逐顆 trace（DirectionalBlur 已因此丟棄）。
6. dynamic-shader（_ImageFxShaderSetup2 runtime hlsl）：部分 gradient/image op 用，需確認 sw static metallib 路徑可覆蓋。

## 7. 範圍決策（柏為拍板）
- 階段 0-4＝視覺 clone 主線，orchestrator 自走可做（機器驗證閘）。
- **階段 5（render 進階）+ 階段 6（IO/硬體）需柏為定要不要納入**——偏離「視覺 clone」北極星 or 需實體裝置。柏為說「全部縫」字面含這些，故列入計劃，但標清楚等柏為現身確認範圍。

## 8. 接手指南
- 本檔＝補縫 roadmap SSOT。每塊 seam 開工＝一個 sw-batch 工單；每批採葉子＝一個 sw-node-batch 工單。
- 狀態追蹤：每塊 seam 補完後在 §2 該行標 ✅+commit；現可採池隨之更新。
- 跨 session 接手：讀本檔 §0-2 拿全局，§4-5 拿工法，lane-state [[simple-world-compound-lane-state]] 拿當前進度頭。
- 無人值守：watcher 跑 /sw-node-batch 照本檔順序，補縫塊（承重）建議柏為在場或 attended（裁決品質受重 context 影響）；採葉子（Phase C）重 context 仍 OK。
