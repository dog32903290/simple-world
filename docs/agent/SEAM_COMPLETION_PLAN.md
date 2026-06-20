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
point-buffer(~44/90 採) / shader-graph·field-SDF(~29/60) / context-var / cpu-upload-texture(**僅 1/4**：ValuesToTexture 活，Gradients/Curves/Values2 未) / Layer2d+dx11-render-graph(**~70%**：DrawScreenQuad/Clear/Layer2d 核心活，blend/postfx 殘) / camera3d + mesh-pipeline(CPU 幾何 output 活，mesh-input 未)。

### 1.2 現可採乾淨葉子＝18 顆（階段 0 立採，census 逐顆證）
| 家族 | clean 數 | 葉子 |
|------|---------|------|
| field | 5 | RepeatPolar, TranslateUV, StairCombineSDF, NoiseDisplaceSDF, SpatialDisplaceSDF |
| image-filter | 6 | WorleyNoise, ShardNoise, TileableNoise, MunchingSquares2, Raster, ZollnerPattern |
| mesh | 5 | CubeMesh, SphereMesh, TorusMesh, CylinderMesh, IcosahedronMesh |
| numbers | 1 | OKLChToColor |
| point | 1 | RepeatAtPoints |
| string | 0 | （全卡 string-rail）|

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

### 階段 1｜R1 高解鎖燃料（先解最多、最低風險、可機械並行）
| seam | 解鎖約 | 風險 | 視覺 | 內容 / 藍本 / precedent |
|------|-------|------|------|------|
| **string-value-rail** ✅ | ~34 | R1 | 弱 | **✅ 補完 commit `b247602`（2026-06-20，7 agent 承重戰）。** string host-channel rail（第六條 cook flow 鏡像 FloatList，非 evalString 避核心風險）。3 葉子 StringLength/FloatToString/CombineStrings。★FloatToString C# 格式化深坑挖到 .NET 版本 ground-truth（TiXL net10.0→F8 暴露 IEEE-754 noise，非 .NET Framework 補零）。fork-6: StringLength.length 存 floatListBuf,下游橋延後 list-routing。實際解鎖待 Phase C 採(Layer A ~13 + Layer B StringList ~7)。|
| **list-routing**（floatlist/intlist/colorlist） 🔨 | ~26 | R1-R2 | 弱 | 🔨 build 中（worktree aa899fba9）。FloatList→Float 橋=推廣 AudioReaction outCache 逃生口(blueprint `_BLUEPRINT_list_routing.md`)，同解鎖 string-rail fork-6 的 StringLength 下游接線。IntList Float-fold/ColorList 層3延後。precedent：floatlist cook-rail + string-rail。|
| **cpu-point-list** | ~7 | R1 | 中 | 純 CPU StructuredList↔buffer 橋。解鎖 point _cpu 族。precedent：FloatList host-rail。|
| **cpu-point-list** | ~7 | R1 | 中 | 純 CPU StructuredList↔buffer 橋。解鎖 point _cpu 族。precedent：FloatList host-rail。|

### 階段 2｜mesh 島解鎖（3D，單塊大解鎖）
| seam | 解鎖約 | 風險 | 視覺 | 內容 |
|------|-------|------|------|------|
| **mesh-input** | ~29 | R1-R2 | 中(3D) | MeshBuffers 作為 cook **輸入** currency + walker gather（mesh-pipeline 已有 output／generate，缺 input 消費）。解鎖 mesh transform/displace/combine/boolean 族 ~29 顆。precedent：仿 point-buffer 的 input gather（Cut90 mesh cook flow 已建 output 端）。|

### 階段 3｜視覺核心 seam（R2-R3，對「視覺一致」北極星貢獻直接）
| seam | 解鎖約 | 風險 | 依賴 | 內容 |
|------|-------|------|------|------|
| **feedback (ping-pong)** | ~16 | R3 | — | KeepPreviousFrame（89 行 CopyResource）最簡入口 → AfterGlow/Bloom-feedback/FluidFeedback。視覺強。|
| **cpu-upload-texture 補完** | ~5 | R2 | — | GradientsToTexture/CurvesToTexture/ValuesToTexture2（seam 1/4 已活，沿 rail 補 3 顆）。是 gradient 的**硬前置**。precedent：照抄 point_ops_valuestotexture.cpp。|
| **gradient-widget** | ~13 | R2 | ↑cpu-upload | 建 `Gradient` host 型別（**目前不存在**＝真成本）+ ~14 漸層 op（Linear/Radial/Box/NGon Gradient/Steps/SDFToColor…）。視覺強。precedent：圖案 shader 仿 point_ops_checkerboard.cpp；上傳仿 ↑。|
| **vec-color-field-output** | ~7 | R2 | — | field f.xyz/material/color readback（SDFToColor/SetSDFMaterial/SdfToVector）。解鎖 field 島上色。|
| **multi-image** | ~16 | R2 | — | ⚠ Cut55 routing trap：每顆消費 _multiImageFxSetup 必先 .t3 backward-trace（非 1:1）。Blend/Combine/Mask image。seam 本身已建（Displace/DistortAndShade 證），逐顆 trace。|
| **draw-pipeline (point draw)** | ~13 | R2-R3 | Layer2d | DrawLines/Ribbons/Tubes/ConnectionLines/Shaded… 點繪製管線擴充。|

### 階段 4｜進階 / readback（R2-R3，中視覺）
| seam | 解鎖約 | 風險 | 內容 |
|------|-------|------|------|
| texture-into-points | ~14 | R2 | image→points（PointsOnImage/AttributesFromImageChannels/SamplePointColor）。Cut68 Resume 提過的 Texture2D-into-Points infra。|
| compute-readback | ~9-13 | R2-R3 | GPU→CPU staging（JumpFloodFill/SortPoints/PointsToCPU/SortPixelGlitch）。|
| texture-array | ~6 | R2 | image 多紋理陣列。|
| curve | ~7 | R2 | Curve port 型別 + Animator（SampleCurve/CurvesToTexture）。|
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
