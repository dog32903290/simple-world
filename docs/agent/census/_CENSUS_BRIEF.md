# Phase A 普查 — 共用簡報（所有 census agent 第零份讀物）

目標：把 TiXL 整本 op 目錄掃成「**接縫依賴圖** + **開採 backlog**」兩張表的原料。
你負責一個 op 類別，產出該類別的 per-op 分類檔。**不要動程式碼、不 port、不 build** ——
這是純分析掃描（read-only sweep）。orchestrator 只收你的短摘要；細節寫進你的輸出檔。

## 北極星（為什麼做這件事）
Mac 版 simple_world 要完整 clone TiXL（功能/行為/UI 節點視覺）。完成定義=對 TiXL 機器驗證。
舊工法「一顆 op 撞一塊接縫」太碎 → 改地基先行：先掃出**所有 op 各需要哪些引擎接縫（seam）**，
才能排「先蓋哪塊大接縫」。你的分類就是這張地基圖的原料。

## 什麼是「接縫（seam）」
seam = 一顆 op 要能忠實運作，simple_world 引擎**必須先具備的底層能力**。
不是 op 本身的數學，是它腳下踩的地基。判斷一顆 op 的 seam，看它的 .cs operator 類別簽章 +
有沒有配對的 shader（.hlsl/.fx/compute）+ .t3 圖檔結構。

### 已建成的 seam（simple_world 已有，踩這些的 op = 乾淨葉子可開採）
- `value-graph` — 常駐增量求值圖：Float/Int/Vec slot、Curve 動畫、transport 時間。多數 numbers/ 純值運算踩這個。
- `compound` — Symbol/Child/Connection 巢狀、combine/copy/paste。純 compound op（.t3 無 .cs 邏輯，只接子節點）踩這個。
- `transport` — 播放頭、fxTime、bars/bpm、soundtrack。
- `particle-system` — GPU 粒子模擬：emit/cycle/pool。
- `image-filter` — 單輸入 _ImageFx：一張 Texture2D 進→出，fragment shader。image/ 主力。
- `multi-pass` — 自訂 executor 多趟渲染（如 FastBlur 的 _ExecuteFastBlurPasses）。
- `mip` — 輸入貼圖 mip 生成 + LOD 取樣。
- `asset-texture` — 載入 PNG 資產綁到貼圖 slot（如 RgbTV 的 perlin 噪聲圖綁 t1）。
- `png-decode` — 原生 ImageIO PNG→RGBA8（platform 層）。
- `multi-image` — gather 多張 Texture2D 輸入（t0/t1，經 point_graph）。如 Displace / DistortAndShade。

### 已知未建的大 seam（命名固定，請用這些字）
- `Layer2d+Execute` — Layer2d render-target 合成 + Command/Execute 繪製 op。解鎖 Glow/Bloom/ScreenCloseUp 等。最大塊。
- `gradient-widget` — 漸層/色帶 authoring 小工具（GradientType 輸入）。解鎖 BubbleZoom/SubdivisionStretch/Steps 等。
- `feedback` — 影格回饋/時間累積（ping-pong buffer）。
- `RWStructuredBuffer` — 可讀寫 structured compute buffer。
- `source-op` — 來源/產生器 op（如 LoadImage 當 source 而非 filter；產生 texture/points 從無到有）。
- `temporal-random` — 影格相干隨機。

### 沒列到的 seam
若某 op 踩的底層能力不在上面任何一條 → 標 `NEW-SEAM:<短名> — <一句描述它要什麼引擎能力>`。
請盡量複用同一個短名（例如多顆 op 都要 3D 相機 → 都標 `NEW-SEAM:camera3d`），別每顆自創。
常見會冒出來的新 seam 範例（自行判斷命名）：mesh 幾何管線、SDF/field 取樣、3D 相機/transform、
文字渲染、音訊分析輸入、video/相機輸入、compute-dispatch（泛用 compute 非粒子）、blend-modes、
structured buffer 讀回 CPU、noise field、raymarch…

## 分類方法（效率優先 — 這是 sweep 不是逐行讀）
1. 先用 grep/glob 把類別內 op 分桶：找 base class / 介面 / 輸出型別的關鍵字
   （`: _ImageFx`、`Command`、`Texture2D`、`MeshBuffers`、`BufferWithViews`、`StructuredList`、
   `GradientType`、`Curve`、`PointList`/`Point[]`、`Field`、`Camera`、shader 檔副檔名…）。
2. 純 compound op（只有 .t3、無 .cs 或 .cs 不含 shader/buffer 邏輯）= 標 `compound`，
   並記它內部接了哪些子 op（這決定它真正的 seam = 子 op seam 的聯集）。
3. 對每桶取代表性樣本確認，不必逐顆深讀；數學細節**不是**這次的事，seam 才是。
4. 判斷每顆 op 的**主要阻擋 seam**（要忠實 port 之前，最關鍵那塊還沒有的地基）。

## 每顆 op 的狀態欄（你只負責前 4 欄；ported 與否由 synthesis 階段 join，不用你查）
- `READY-LEAF` — 只踩已建成 seam，可直接進 Phase C 開採。
- `BLOCKED:<seam>` — 需要某未建 seam（已知或 NEW-SEAM）。列**主要**那塊；次要的放備註。
- `COMPOUND` — 純 compound，真正 seam = 內部子 op 聯集（在備註列子 op）。
- `TRIVIAL` — 純值/工具 op，踩 value-graph，幾乎零風險（numbers/string 多屬此）。

## 風險分級（給 Phase C 排序用）
`R1` 低（單 shader/純值，數學直白）/ `R2` 中（多參數路由、FloatsToBuffer .t3 注意 Cut 55 routing trap）/
`R3` 高（多 pass、視覺判斷、brittle blend、時間相干）。

## 輸出檔格式（寫到 docs/agent/census/ops-<你的類別>.md）
Markdown 表，一顆 op 一列：

```
# Census: <類別> (<n> ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註(shader/子op/次要seam/fork疑慮) |
|----|---------|----------|------|------|----------------------------------|
| Blur | 高斯模糊 | image-filter | READY-LEAF | R1 | Blur.hlsl 單pass |
| GlowVer2 | ... | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 內部 2 render-target |
...

## 摘要
- 總 op 數 / READY-LEAF 數 / 各 BLOCKED seam 分佈 / 冒出的 NEW-SEAM 清單 / 意外發現
```

## 回報給 orchestrator（只回這個，別貼全表）
1. 類別 + 總 op 數 + 已寫檔路徑。
2. READY-LEAF 數量 + 前 5 顆最值得開採的乾淨葉子（名字）。
3. 各 BLOCKED seam 的 op 數分佈（哪塊 seam 解鎖最多顆）。
4. 你新增的 NEW-SEAM 清單（短名 + 一句 + 各擋幾顆）。
5. 意外/盲區（看不懂的 op、分不清 seam 的、像 TiXL WIP 斷線的）。
