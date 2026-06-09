# 設計：TiXL 點 operator 圖 runtime（Mac/Metal）

> 北極星 = Mac 版 TiXL。本 lane 把現行**焊死的單一 ParticleSystem** 拆成 TiXL 那樣的**點 operator 圖**，
> 再照 TiXL `Operators/Lib/point/` 逐批港節點。設計權威 = `external/tixl`（不自創，不確定查 TiXL）。
> 接在 audio M1（聲音→參數 + AudioReaction 對齊）之後；時間/交互層 M2–M4 退為 queued。

## 為什麼（問題）

編輯器裡 `RadialPoints → ParticleSystem → DrawPoints` 看似一條鏈，實則四個名牌貼在**一顆** C++ `ParticleSystem`
類別上（`main.cpp:340-436` 驗：`new ParticleSystem(...)` 一顆，`generate/update/render` 跑焊死的 emit+turbulence+sim+draw）。
圖的連線現在只驅動**參數**（值脊椎，`evalParam`），**不驅動 buffer 流**。要長出 TiXL 的點詞彙（生成器 / 變換 / modify /
combine / 各種 draw，光「點」就 90+ 顆）就得先有真的點 operator 圖：連線真的把一袋點從一顆交到下一顆。

## 設計權威（TiXL 點系統，已爬）

- **原子 = `Point`（64 bytes）**：`external/tixl/Core/DataTypes/Point.cs` + shader 端 `Operators/Lib/Assets/shaders/shared/point.hlsl`。
  欄位序：`float3 Position; float F1; float4 Orientation(quat); float4 Color; float3 Scale; float F2;`。
  **我們的 `app/src/runtime/tixl_point.h` 已是它的逐欄移植（64B），契約原子已對齊。**
- **連線型別 = 一袋點**：TiXL 是 `BufferWithViews`（DX11 structured buffer + SRV/UAV）。Metal 對應 = 一個
  `MTL::Buffer` of `Point[count]`（Metal 同一 buffer 可讀可讀寫，不需分 view）。
- **operator 模式單一**：C# 類別宣告 `InputSlot<BufferWithViews>` 輸入 + `Slot<BufferWithViews>` 輸出，配對一支
  `.hlsl` compute kernel（讀 `StructuredBuffer<Point>` → 寫 `RWStructuredBuffer<Point>`）。
  **生成器**無點輸入（無中生點）；**變換/modify**讀一袋→寫一袋；**draw** 吃一袋 → 吐一個 render command（鏈終點）。
- **分類**：`Operators/Lib/point/{generate,transform,modify,combine,draw}`；官方語意文件在
  `external/tixl/.help/docs/operators/lib/point/{...}/*.md`（每顆一份：語意 + 參數表 + 「需要一個 DrawX 才看得到」）。
- **真實命名校正**（我先前的猜測 → TiXL 實名）：`DrawMeshAtPoints` → **`DrawMeshAtPoints2`**；
  `RepeatAtPoints` 在 `generate/`（非 modify）；其餘 RadialPoints/LinePoints/GridPoints/TransformPoints/DrawPoints/DrawLines/CombineBuffers 命名正確。

## 契約（A.0，順序鎖，Opus 自己做、禁 fan-out）

1. **點 buffer 鏈 API**：定義一顆點 operator 在 Metal 上怎麼產出/消費一袋點。
   - 一條連線扛 `MTL::Buffer*`（Point[count]）+ count。
   - operator 介面三型：`Generator`（無輸入→產出 buffer）、`Modifier`（輸入 buffer→輸出 buffer）、`Drawer`（輸入 buffer→render）。
   - cook：從 draw 節點往回走 buffer 相依，照拓撲序逐顆 dispatch compute，各寫各的輸出 buffer，末端餵 draw。
   - buffer 擁有權 / 生命週期 / realloc（複用既有契約 golden：allocate→reuse→reallocate 序列）。
2. **節點登記資料驅動（鐵律 7，並行 fan-out 的前提）**：把「加一顆節點」收斂成
   **加一行登記 + 一個葉檔（kernel+spec）+ 一個 `--selftest-<node>`**，避免並行 agent 撞 `graph.cpp` / `main.cpp` selftest 派發 / `CMakeLists`。
   現況：node spec 已是 `graph.cpp` 的一張 NodeSpec 表（半資料驅動）；A.0 要把表 + selftest 派發 + CMake 收到「加一行不撞」的形狀（參考 `app/src/app/menu.cpp` 既有資料驅動樣板）。
3. **現有粒子怪物折回模型**：把 `ParticleSystem` 拆成 TiXL 對應節點——emit=生成器（RadialPoints 已有 kernel）、
   sim=`PointSimulation` 風格 modifier（吃點→更新點，含力）、`TurbulenceForce`=力、draw=`DrawPoints`。

## 三拍

- **A.0 鎖契約 + 資料驅動登記**（上方；順序、我做）。
- **A.1 第一刀 = 最小真鏈**（能 demo 的地基）：`RadialPoints`(生) → `TransformPoints`(變換) → `DrawPoints`(畫)，
  連線真的扛那袋點；在編輯器改接線/繞過/換生成器，**畫面跟著變**。證明鏈是真的（不是新長相，是讓新長相變便宜的地基）。
- **A.2+ 一批一批並行 fan-out**：照 TiXL 順序（generate → transform → modify → combine → draw）逐批港。
  每批用 workflow 派 N 隻 agent，各認領一顆 operator，跑下方「每顆固定流程」。一整類一起做。
  招牌 `DrawMeshAtPoints2`（每點長 mesh，需 mesh 資源型別 + instanced draw）排在 draw 批。

## 每顆節點固定流程（柏為定，不可跳；fan-out 不偷工）

1. **爬 TiXL**：`Operators/Lib/point/<cat>/<Name>.cs`（port/參數/語意/失敗行為）+ 配對 `.hlsl`（真實數學）+ `.help/docs/.../<Name>.md`。
2. **port**：metal-cpp/MSL 重實作，葉檔 `runtime/<name>.{h,cpp}` + `.metal` kernel。
3. **golden selftest**：抓 TiXL 的數學公式 → 已知輸入→已知輸出斷言（TiXL 在 Win/DX11 跑不動，**用它的公式當標準答案**，非跑兩邊比）。`injectBug` 變體真退化→FAIL。
4. **驗證一樣**：port/參數/預設/enum 逐項對齊 `.cs`；render 出來用 eye 比對 `.md` 文件長相；每顆 selftest 後派 subagent 對抗審查「漏測什麼 / golden 是否真的對上 TiXL .hlsl」。

## 驗證 parity（TiXL 跑不動的因應）

無法 Win/DX11 跑 TiXL 雙邊比 → parity = ①**公式 golden**（compute readback 斷言，TiXL 公式為準）+ ②**port/參數對齊**（vs `.cs`）+ ③**eye 比對長相**（vs `.help` 文件 / 截圖）。三者齊才算「一樣」。

## commit / 律法閘（柏為 2026-06-09 定）

每做完一大步 commit；**commit 前對照 `ARCHITECTURE.md` 自檢動到的碼**（五區分區 / 依賴單向 / verify 一行 hook / 單檔≤400行 / `--selftest-*` / 資料驅動），有違反先改完再 commit。law debt 不過夜。見 memory `simple-world-commit-law-check-ritual`。

## 地基現況（2026-06-09 驗）

- **律法乾淨**：`graph.cpp` 333 行（<400 ✓）、`graph_selftest.cpp` 已拆、`app/audio_monitor` 接 DSP（platform→runtime 已修）、`audio_capture.mm` 零 runtime include。
- **點原子** `tixl_point.h` 64B 已對齊 TiXL `point.hlsl`。
- **現行節點**：RadialPoints / ParticleSystem / TurbulenceForce / DrawPoints（+ 值節點 Time/Const/Multiply/Sine/Remap/AudioReaction）。RadialPoints/DrawPoints 現為「只有臉」，buffer 流焊在 ParticleSystem 內。
- `radial_points.cpp` / `transform_points.cpp` 現為 selftest-only 證明（kernel 在、未組成 runtime 鏈）= A.1 可複用。

## 範圍（YAGNI）

- **不**一次港全部 90+ 顆；分批，先 generate/transform/draw 的核心幾顆把鏈跑通。
- sim/CPU operator、SDF/field/mesh 取樣類、進階 draw（tubes/ribbons/DOF）= 後批。
- mesh 資源型別只在 `DrawMeshAtPoints2` 批才建。
- compound（子圖群組）、通用型別檢查 = 本 lane 外（queued）。

## 風險 / 待釐清

- A.1 的「現有粒子怪物拆解」是本 lane 最大刀（動 `ParticleSystem` 內臟）——拆時保持 emit/sim/force/draw 各自可單測，避免一次大爆。
- buffer 生命週期在動態加/刪節點時的 realloc（複用 RESOURCE_LIFETIME golden）。
- 並行 fan-out 的 build：CMake 加源是否要 glob 或每節點一行（A.0 決）。
