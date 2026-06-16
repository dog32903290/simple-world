# PHASE_BC_KICKOFF — 下一批可立刻派工的具體計畫

> Phase B/C kickoff 偵察（2026-06-16）。唯讀分析，源碼證據逐項到 `app/src/` + `external/tixl`。
> 取代「一顆 op 撞一塊接縫」：把 SEAM_GRAPH/OP_BACKLOG 地基圖變成下一批 worktree 並行 lane 派工單。
> SSOT 接續 SEAM_GRAPH.md（seam 排序）+ OP_BACKLOG.md（READY-LEAF 清單）。

---

## A. 預檢結論表（5 項 + 證據）

| # | 預檢 | 結論 | 證據（檔:行 / 原文） |
|---|------|------|--------------------|
| ① | `Gradient` 色帶型別已實作？ | **NO** | 全 source grep「Gradient」僅 3 處且全無關：`displace_params.h:9`（DisplaceMode 0=IntensityGradient 列舉名）、`pointattributefromnoise_params.h:23/27`（FORK note：TiXL 的 RemapNoise(Gradient) port **baked off** as no-op）、`node_registry_point_modify.cpp:540`（同 fork note）。**無 `struct/class Gradient`、無 GradientStop、無 SampleGradient/DefineGradient**。→ numbers census line 177「value-graph 已有 Gradient」**誤判**，須更正。 |
| ② | 支援 dynamic HLSL/MSL（runtime shader source string）？ | **NO（但不擋 port）** | `app/CMakeLists.txt:33-41`：「**Precompiled (not runtime-compiled)** so each .metal can #include shared headers... `xcrun -sdk macosx metal -c`」+ glob `CONFIGURE_DEPENDS shaders/*.metal`。源碼**零** `newLibraryWithSource/MTLCompileOptions/compileShader`。→ TiXL 的 `_ImageFxShaderSetup2`（dynamic）vs `_ImageFxShaderSetupStatic` 區別**對 port 無意義**：兩者都 port 成預編譯 `.metal`。**已證**：census 標 dynamic 的 mirrorrepeat/chromaticdistortion/voronoicells/kochkaleidoscope/tint **全已 ship 為 `app/shaders/<name>.metal`**（`ls` 確認）。 |
| ③ | `AdsrCalculator` / ADSR envelope 已存在？ | **NO** | 全 source grep「adsr」零命中（`app/src` + `runtime`）。AdsrEnvelope 是 sw 自加 op 但其依賴 `AdsrCalculator`（T3.Core.Audio）**未移植**。→ AdsrEnvelope 開採前須先補 AdsrCalculator（小型 DSP helper，非 seam），暫移出 READY-LEAF。 |
| ④ | PolarCoordinates/EdgeRepeat 是真孤兒，還是補一行就救活？ | **真孤兒，但便宜可救（R1）** | `point_ops_polarcoordinates.cpp:113` / `point_ops_edgerepeat.cpp:122`：用**舊式** `void register*Op() { registerTexOp(...); }` free function，**主樹無 caller**（grep 確認只有自身定義+selftest 註解）、**無 `ImageFilterOp` registrar**、**無 NodeSpec**、**不在任何 kTable/menu**。對比 live op `point_ops_adjustcolors.cpp:116` 用 `static const ImageFilterOp _reg_adjustcolors{...}`（自動推 NodeSpec 進 menu sink）。`.metal`（`app/shaders/polarcoordinates.metal`+`edgerepeat.metal`）、cook、params、selftest **全已存在**。→ **救活 = 把 free function 換成 `ImageFilterOp` registrar literal**（cook + NodeSpec + selftest pair），單一 leaf 檔內編輯，零共享檔，R1。 |
| ⑤ | multi-image op 每顆開採前必做 .t3 backward-trace（硬閘）？ | **YES — 記入計畫硬閘** | Cut 55（DirectionalBlur 丟棄）/Cut 58（RgbTV perlin 誤判）教訓。`_multiImageFxSetup*` 用 FloatsToBuffer 以 connection-order 填 cbuffer，中間可夾數學節點（非 1:1）。**本計畫 D/E 隊形：multi-image op 一律不進第一批 Phase C**（全列 BLOCKED-pending-trace）；要採須先單顆 STEP-0 backward-trace .t3 + task_258d9510 audit 結論。 |

---

## B. 並行就緒度表

| 候選家族 | 現在可並行? | 機制證據 | 需什麼基建 | 基建大小 |
|---------|-----------|---------|-----------|---------|
| **image-filter 葉子** | **✅ YES（conflict-free-now）** | `image_filter_op_registry.h`：每 op 一個 leaf `point_ops_<name>.cpp`，file-scope `ImageFilterOp` registrar（自動 registerTexOp + push NodeSpec 進 `imageFilterSpecSink()` + selftest）。CMake `SW_POINT_OP_SRCS = GLOB CONFIGURE_DEPENDS point_ops*.cpp`（`CMakeLists.txt:99`）→ 加 op 零 CMake 編輯。sink 是 Meyers singleton，`findSpec`/`specTypes`（`node_registry.cpp:84-98`）已 live 讀 sink。Cut 48 已證多 worktree 平行合流零衝突。 | 無 | — |
| **numbers / math / vec / int / bool TRIVIAL（~129）** | **❌ NO（共享檔撞點）** | 全灌單一 `node_registry_math.cpp`（**717 行**）的一個 `static std::vector<NodeSpec> specs = {...}`（`mathSpecs()`）。`node_registry_math.cpp` 在 CMake **明列非 glob**（`CMakeLists.txt:167`）。同理 generators/point_modify/point_combine/draw（162-166 全明列）。多 lane 加 op = 全撞同一 vector literal → 每 lane merge 衝突。 | **value-op 自登記 seam**：仿 `imageFilterSpecSink()` 建 `valueOpSpecSink()` Meyers singleton + `ValueOp` RAII registrar（NodeSpec + evaluate fn）；`findSpec`/`specTypes` 加讀此 sink（~3 行，與 image-filter 同模式已存在）。每顆 op 改成自身 leaf `value_ops_<name>.cpp`（或先做 sink、舊 ops 留原檔，新 ops 走 leaf）。 | **小**（header + sink + 3 consumer 行；image-filter 已是現成藍本，無新概念） |
| **string TRIVIAL（34）** | **❌ NO（檔根本還沒有）** | source 無任何 string-family registry 檔（grep 確認）。string 型別 slot 是否已實作未查（本批盲區）。 | 同 value-op 自登記 seam（共用）+ 確認 String value-slot 型別已存在 | **小～中**（共用 value-op seam；若 String slot 未建則 +1 小型型別） |
| **transport / anim（~19）** | **❌ NO（共享檔，但量小）** | 同走 `node_registry_math.cpp` 或 `node_registry.cpp` 共享表。 | 同 value-op 自登記 seam（共用） | **小**（共用上面 seam） |

**結論**：image-filter 是**唯一**現在就能多 lane 平行的家族。numbers/string/transport 全卡共享 `node_registry_*.cpp` 表 → Phase C 要大量並行開採這 ~129+34 顆 TRIVIAL **前**，需先做**一塊小型 value-op 自登記基建**（仿 edaff22 image-filter 模式）。基建本身是 R1 小工，且有現成藍本。

---

## C. 立刻可派的第一批 Phase C 開採 lane（image READY-LEAF，conflict-free-now）

挑通過預檢①②、走已建 image-filter seam、單輸入 fragment、數學直白、**排除已 port**（對照 A1 桶 A 的 25 顆）。全部走 `point_ops_<name>.cpp` 自登記葉子 → 多 worktree 平行零衝突。

| # | op | TiXL 源碼指針 | 風險 | golden 對手算策略（一句） | fork 疑慮 |
|---|----|--------------|------|------------------------|----------|
| **C-1** | **CheckerBoard** | `Operators/Lib/image/generate/basic/CheckerBoard.cs` + `_ImageFxShaderSetupStatic` + `Resources/lib/img/generate/CheckerBoard.hlsl` | R1 | 棋盤是 UV→floor/mod 的硬邊界：選格心+格邊各取 4 pin，黑白交替必中；injectBug 反相格子→RED。 | 無（pure UV pattern，無 sampler wrap 歧義） |
| **C-2** | **Rings** | `image/generate/pattern/Rings.cs` + `Rings.hlsl`（static） | R1 | 同心圓 = `length(uv-center)` 帶入 sin/step；沿半徑取等距 pin 比對亮環暗環；injectBug 改頻率→RED。 | 無 |
| **C-3** | **SinForm** | `image/generate/pattern/SinForm.cs` + `_ImageFxShaderSetup2`（dynamic→**port 成 .metal**，預檢②證無礙）+ `SinForm.hlsl` | R1 | 正弦波形 = `sin(uv*freq+phase)`；固定 freq/phase 取波峰波谷 pin；injectBug 移相→RED。 | 無 |
| **C-4** | **ValueRaster** | `image/generate/pattern/ValueRaster.cs` + `ValueRaster.hlsl`（static） | R1 | 灰階柵格 = 規則網格亮度量化；取格內/格間 pin 灰階值；injectBug 改 raster 間距→RED。 | 無 |
| **C-5** | **Blob** | `image/generate/basic/Blob.cs` + `Blob.hlsl`（static） | R1 | metaball/SDF 有機形，中心高邊緣低；中心+四角徑向衰減 pin；injectBug 改半徑→RED。注意：source-like（無輸入貼圖），但走 image-filter（Crop 已證 ImageFilterComputeOp 可無上游）。 | 無 |

**備援同批可加（同 seam 同模式，若 lane 容量夠）**：FraserGrid / ZollnerPattern / NGon / RoundedRect / FractalNoise / Grain / WorleyNoise（全 `_ImageFxShaderSetupStatic`，R1，純 pattern/noise generator，無 multi-image、無 gradient、無 asset）。

**第一批刻意排除**：
- **已 port（A1 桶 A，25 顆）**：AdjustColors/Blur/ChannelMixer/ChromaB/ChromaKey/ChromaticDistortion/ColorGrade/ConvertColors/Crop/DetectEdges/Displace/DistortAndShade/Dither/FastBlur/KochKaleidoscope/MirrorRepeat/NormalMap/Pixelate/RgbTV/Sharpen/StarGlowStreaks/Tint/ToneMapping/TransformImage/VoronoiCells。
- **gradient-widget BLOCKED**：所有 LinearGradient/BoxGradient/RadialGradient/NGonGradient/Steps/SubdivisionStretch/MandelbrotFractal/BubbleZoom（預檢① Gradient 型別未建 → 全擋）。
- **multi-image BLOCKED-pending-trace**（預檢⑤硬閘）：Blend/HSE/RemapColor/Fxaa/FakeLight/MosiacTiling/HoneyCombTiles/DirectionalBlur 等，第一批不採。
- **孤兒救活（PolarCoordinates/EdgeRepeat）**：見下方「免費 op」——可作第一批的 0.5 顆 warm-up（只改 registrar，比新 op 更快），但獨立成 chip/小 lane，不混進 C-1..C-5 計時。

---

## D. Phase B 第一塊 seam 建議：point-buffer vs shader-graph（建設複雜度評估）

評估的是**蓋這塊 seam 本身的工程量**，不是它解鎖的 op 複雜度。

| 維度 | `point-buffer`（解 ~90） | `shader-graph`（解 ~64） |
|------|------------------------|------------------------|
| 已有可複用基建 | **大量**。`particle_system.h` 已證 sw 有：`Point` struct（`tixl_point.h`，64B，對齊已處理）、`MTL::Buffer` of Point（emitPoints/particles/resultPoints）、compute PSO（psoEmit/psoTurb/psoSim）、`dispatchThreadgroups`、result-buffer readback。point-buffer = **把粒子系統私有的 Point-buffer plumbing 一般化成共享 seam**。 | **幾乎零**。需要 ShaderGraphNode inline-HLSL→MSL 的 graph-codegen（snippet 組合成完整 kernel）。sw 是**預編譯 only**（預檢②）→ 要嘛建 runtime MSL 編譯子系統（重），要嘛建一套 codegen→寫 .metal→reconfigure 的 build-time pipeline（也重、且與 hot-edit 衝突）。 |
| 本質新工程量 | **中**。StructuredBuffer alloc/bind/dispatch 的「通用貨幣」抽象 + CPU readback 統一路徑。GPU 同步/對齊是本質複雜但**已被 particle 系統馴服一次**。 | **大**。inline shader 圖組裝是全新子系統，且撞 sw 的預編譯哲學（架構憲法 platform 區的根設計）。 |
| 前置 | 無（自足葉子地基） | 無（自足），但下游 raymarch 才需 camera3d |
| 配驗證 op | **RadialPoints / GridPoints**（TiXL GPU 原版；sw 現有的是 value-graph CPU-fill fork，正好可拿 GPU 版對照 CPU fork 做 golden 交叉驗證） | SphereSDF / BoxSDF + Render2dField |
| 風險 | R1 驗證 op，seam 本身 R2（有藍本） | R1 驗證 op，但 seam 本身 **R3**（無藍本+撞預編譯哲學） |

**推薦（一個）：先蓋 `point-buffer`。**

理由：①建設成本明顯低於 shader-graph——粒子系統已把 Point-buffer 的本質複雜（64B 對齊、compute dispatch、readback）馴服過一次，point-buffer 是「一般化既有 plumbing」而非從零發明；shader-graph 要的 inline-HLSL codegen 直接撞 sw「預編譯 not runtime-compiled」的根哲學（預檢②），是真正的新子系統。②解鎖數更大（~90 vs ~64）。③驗證 op 有現成交叉對照（sw 已有 RadialPoints/GridPoints 的 CPU-fill fork，GPU 原版 golden 可與 CPU fork 互證）。④兩者皆自足無前置、互不依賴，shader-graph 不會因為晚一批而被卡。

---

## E. 下批隊形建議（一個，不並列）

**推薦：第一批先純 Phase C image-mining 暖身（證並行模型 + 同步起跑 value-op 自登記基建），Phase B point-buffer blueprint 排下下批。**

理由（為何不是「Phase C ∥ Phase B point-buffer 同批」）：
1. **並行模型尚未在本工法下實跑過**。Cut 48 證過 image-filter 多 lane 合流，但那是舊節奏；新「三階段地基先行」工法的第一個並行批，應先用**最安全燃料（image-filter conflict-free-now 葉子，R1）**證明 orchestrator 的多 worktree 編排+合流+驗證閘在新工法下跑得順，再壓上 R2-R3 的 point-buffer 承重工。先放 R3 重工進第一個並行批 = 把工法風險和工程風險疊在一起。
2. **value-op 自登記基建是 Phase C 真正放大的前提**。第一批 image 葉子（C-1..C-5）能跑是因為 image-filter 已 conflict-free；但 backlog 最大燃料是 ~129+34 顆 numbers/string TRIVIAL，那些**現在會撞共享檔**（必答2）。第一批 image 並行進行的**同時**，用一條 Opus lane 蓋 value-op 自登記 seam（小、有藍本）——這樣下批就能把 ~163 顆 TRIVIAL 真正大量並行，而不是被 `node_registry_math.cpp` 序列化。
3. **point-buffer 是承重 R3 工，值得獨立一批專注**，不在「證並行+鋪小基建」的第一批分心。它沒有前置、不會因晚一批受損（D 已述）。

**第一批具體編隊**：
- **Lane 1-5（Sonnet 機械並行，worktree）**：C-1 CheckerBoard / C-2 Rings / C-3 SinForm / C-4 ValueRaster / C-5 Blob（image-filter 自登記葉子，零共享檔）。
- **Lane 6（Opus 承重，worktree 或主 checkout 序列）**：value-op 自登記 seam 基建（`valueOpSpecSink()` + `ValueOp` registrar + node_registry 3 行 + 1-2 顆新 value op 當驗證，例 numbers TRIVIAL 的 `All`/`Xor` 走新 leaf 證 sink）。
- **Chip / 小 warm-up**：PolarCoordinates + EdgeRepeat 孤兒救活（改 registrar，R1，可塞任一 image lane 尾段或獨立 chip）。
- **下下批**：value-op seam 完工後 → numbers/string ~163 顆 TRIVIAL 大量並行（Sonnet）∥ point-buffer blueprint（Opus，配 RadialPoints/GridPoints GPU 原版驗證）。

---

## 盲區 / 存疑

1. **String value-slot 型別**：string TRIVIAL 開採需 sw 已有 String 值 slot 型別（不只 NodeSpec，是 graph.h 的 slot 型別系統認得 String）。本批未深查 graph.h slot 型別清單，標盲區——value-op seam 蓋之前須確認 String slot 是否已存在（若無，+1 小型型別工）。
2. **value-op 自登記 seam 的 evaluate-fn 分檔**：`value_eval_ops.cpp`（740 行）目前是集中 evalXxx 函數庫；自登記化時，新 op 的 eval fn 放各自 leaf 還是續用集中庫，是品味/工法決策（不影響 NodeSpec sink，但影響「真零共享檔」程度）。建議：sink 先做、新 op eval 放各自 leaf；舊 op 不動（incremental，不重織既有承重線）。
3. **AdsrCalculator 工作量**：預檢③確認未建。AdsrEnvelope 依賴的 ADSR DSP helper 是否大，本批未估（TiXL `T3.Core.Audio.AdsrCalculator` 行數未查）。暫移出 READY-LEAF，需要時單獨評估。
4. **numbers census line 177 誤判須更正**：「value-graph 已有 Gradient」與源碼矛盾（預檢①）。下次校 census 時修正 ops-numbers.md 的 DefineGradient/BuildGradient/SampleGradient 等「READY-LEAF」標記——它們其實 BLOCKED:gradient-type（一個比 gradient-widget 更底層的未建型別）。
5. **第一批 C-3 SinForm 用 dynamic shader**：預檢②證 dynamic/static 對 port 無礙（都成 .metal），但 SinForm 是本計畫第一顆「TiXL 標 dynamic」的新採 op，第一個 lane 可順帶確認 dynamic-source op 的 .hlsl→.metal 移植無隱藏陷阱（理論上與 static 同，但首採值得留意）。
