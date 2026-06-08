# simple_world Master Progress — imgui + Metal Pivot

> **這是 simple_world 目前唯一的進度進入點（single source of truth）。**
> 新 session 必須先讀這份。舊的
> `docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md`（那份宣稱
> 自建 C++ native runtime「100/100」）已被本檔取代，只當歷史與底層知識來源，
> **不要再照它往「繼續蓋自建 graph runtime」的方向走。**

## Current Snapshot

- 日期：2026-06-08。分支：`codex/js-to-cpp-contract-migration`（內容已與新方向脫節）。
- 階段：**step 0 殼 → step 1 粒子線 → step 2 照 TiXL re-arch → step 3 graph 資料模型 → B0 編輯器基本可操作層
  全部 ✅（柏為親手測過）→ B2「命令層/undo-redo」Phase 1 ✅ closed（柏為 7/7 驗收）。**
  北極星 = Mac 版 TiXL（柏為選 B，視覺也追）；完成定義 = 柏為親手測得到（見下〈完成定義〉）；照〈Roadmap〉順序走。
- **節點編輯命令層進度（2026-06-08）**：Phase 1（命令核心 + add/link/delete/move 走命令 + Cmd+Z/Cmd+Shift+Z）
  = **closed，柏為 7/7 親手驗收**。Phase 2（reconnect / replace-on-input）= 程式碼+selftest 已完成但 **parked**
  （GUI 拖接點驗收卡在「runtime 節點種類太少、缺型別相容節點可測」）。Phase 3–5（插入節點 / 框選多選 / 複製貼上）= queued。
  詳見 `2026-06-08-node-editing-commands-phase1.md`（closed）/`-phase2.md`（parked banner）/spec `node-editing-commands-design.md`。
- **下一個 active lane（柏為 2026-06-08 拍板）= 「值脊椎」**：讓 param 能被連線驅動（Time→Sine→Remap→粒子脈動）。
  這同時解掉 Phase 2 parked 的根因（節點太少）。命門 = param/port schema：採 TiXL 式「param = 帶預設值的 input port」
  （連線覆蓋預設），**不是** param/port 兩套互相定址；值線增刪/param 驅動切換**必須走已建好的命令層**（可 undo）。
  下一步先只做這柱 schema 紙上對齊（去 `external/tixl` 核 input slot 真實結構）再碰 code。
- 與柏為長談後確立的核心判斷（四個皮選項都已逐一壓過、由他拍板）：
  - **核心資產 = 自己的 Metal 粒子/3D 引擎。皮是可換外殼。**
  - 放棄整批克隆 TiXL / Vuo runtime（兩者 runtime 都搬不動，證據見 Conflict Register）。
  - **下注：`imgui-node-editor`（C++）當皮 + 自建 Metal runtime。**

## 北極星（2026-06-07 柏為拍板，最高指導原則）

**最終成品 = Mac 版的 TiXL（Tooll3）。** 工程導向：系統性地把 TiXL 搬到 Mac/Metal，**不自創、不即興**。

- **TiXL 是唯一設計權威。** 任何不確定（節點語意 / port / 參數 / 運動 / 型別 / 失敗行為 / 圖的邏輯）→
  **直接照 TiXL 邏輯**，依序查：
  1. 本地 `external/tixl/`（**完整 C# 源碼** + `.help/docs/operators` 官方文件）— 離線、最快、含實際 compute 邏輯。
  2. `external/tixl-spec/`（935 節點報告書，已結構化）。
  3. 線上 `https://github.com/tixl3d/tixl.git`（branch `dev`）— 最新、補本地缺漏。
- **「Mac 版 TiXL」的精確定義 = 功能/邏輯/節點體系/體驗對等，技術棧重寫。**
  邏輯、port、語意、節點分類照搬 TiXL；**實作換 metal-cpp**（TiXL 的 DX11/HLSL/C# runtime 焊死 Windows、搬不動）。
  **不是**逐行翻譯 C#。皮維持 imgui-node-editor（TiXL 本身就是 ImGui 寫的，`external/tixl/ImguiWindows`，手感同源）。
- **視覺也追 TiXL（柏為 2026-06-07 選 B，「我喜歡 TiXL 的邏輯」）：** editor 版面 / 操作手感 / 節點外觀 / 配色盡量 1:1 像 TiXL。
  可行性高——TiXL editor 是 ImGui-based（`external/tixl/Editor`、`ImguiWindows`、`ScalableCanvas` 等），節點繪製 /
  canvas 縮放平移 / 連線手感 / 配色程式碼可直接讀照搬到 imgui-node-editor；焊死的只有 DX11 render backend + WinForms 外殼。
  工具：`tooll3-interaction-compatibility` skill。**順序**：UI 視覺皮要附著在能動的 runtime 上才驗得了
  （`ui-skin-pressure-gate`），故 **runtime 母線先行、UI 隨之向 TiXL 收斂**，不先停 runtime 去做空皮。
- **節點工作流（每個節點都照此）**：先讀 TiXL 對應 `.cs` + `.help/docs` 的 port/語意/行為 → 照搬成我們的
  node 契約（`node-contract-architect`）→ metal-cpp/MSL 重實作 → selftest 掛 TiXL 行為當 golden。不確定問 TiXL，不猜。

## 完成定義（2026-06-07 柏為拍板，凌駕一切）

**一件事「完成」= 柏為能在介面親手按到 / 操作 / 確認它 work，不是 selftest 綠。**
selftest 綠是必要不充分。每個功能都要有「non-technical handle」（按鈕 / 選單 / 可操作的節點），
否則對柏為等於沒做（例：save roundtrip 綠但他按不到儲存 = 未完成）。違反這條的「完成」一律退回。

## Roadmap（工作順序，2026-06-07 與柏為定；解「怕漏 / 怕即興」）

**規則**：照此順序走，不再每次即興丟 a/b/c。每做完標 ✅、指下一項。柏為是導演可隨時插隊；插隊不讓別項
消失（回 `queued`）。**完成 = 柏為親手測得到**（見上）。本清單是「未來順序」，搭配下方〈進度〉的「已發生」。

**順序（柏為定）：基本可操作編輯器層先 → runtime fan-out → 視覺/進階。** 因為沒有可操作層，runtime 做再多柏為都碰不到。

- **B0 基本可操作層（✅ v1 裝好，待柏為親手測 = 真完成）**
  - ✅ 浮動 Toolbar：**Save / Load** 按鈕（寫/讀 `~/Desktop/simple_world_project.json`，status 列回報結果）
    + **Add Node** 按鈕（popup 列 registry 全型別，未來 fan-out 的節點自動出現）。拖曳位置每幀 sync 回 graph，Save 收得到。
  - ✅ 連線（拖 pin→pin，驗一進一出不同節點）/ 刪連線 / 刪節點（選+Delete，連帶清 dangling 連線）。
  - 踩雷修正：`ed::EndCreate()`/`EndDelete()` 必須在 `if(ed::BeginCreate/Delete())` **內**（Begin 回 false 時不設
    m_InActive，在外無條件呼叫 End→assert 崩）。`ed::Begin` 回傳 void、要用 host window 的 ImGui::Begin bool 守。
  - ⬜（park）連線真的驅動任意拓撲求值 → 等 B2 通用 evaluator；現在 cook 仍是「按 type 找節點套參數」，
    所以加/連節點能畫能存，但「連線改變求值」還沒（誠實標記，柏為測時會發現加的力還沒作用）。
- **A runtime fan-out（B0 後，每個節點自動能從選單叫出 + 存檔）**：其餘 16 力 / 點生成 / 點操作 / 渲染 / mesh / image / audio
- **B2 編輯器進階**：
  - ✅ **undo/redo command 層 = Phase 1 closed（2026-06-08，柏為 7/7 驗收）**：Command/CommandStack/MacroCommand
    在 `app/`；add/link/delete/move 全走命令；Cmd+Z/Cmd+Shift+Z（注意 macOS Cmd 落在 `io.KeyCtrl`，見踩雷）。
  - ⏸ **reconnect（replace-on-input）= Phase 2 parked**：碼+selftest 完成，GUI 驗收待節點變多再做。
  - ⬜ queued：Phase 3 插入節點（TiXL 線上浮圓點）/ Phase 4 框選多選刪移 / Phase 5 複製貼上；compound 群組、通用 topological evaluator、型別/port 檢查。
- **C 視覺（北極星 B）**：TiXL 配色/節點外觀/canvas 手感、時間軸+keyframe、中文字型
- **D 基建（持續）**：效能（非阻塞/PSO 快取）、檔案格式版本

## 語言/後端（2026-06-07 拍板，已壓過，不要重議）

**C++ + metal-cpp。** 柏為拍板「Metal 是必須的、必須親手調底層 GPU」+ 先前拍板「皮 =
imgui-node-editor」。兩個約束的交集只剩 C++：imgui-node-editor 是 C++ 一等公民、metal-cpp
是 Apple 官方 C++ Metal binding（連 .mm 都幾乎不用寫）。已逐一壓掉 Swift（要反向 bridge C++
node editor）、Rust（wgpu 是 WebGPU 抽象非親手 Metal + 對柏為崖最陡）、C#（TiXL 複用是
DX11/WinForms 焊死的陷阱）、Web/TS（wgpu→Metal 非親手 + 丟 imgui-node-editor 手感）。
柏為的戰場 = MSL shader（≈他在學的 GLSL）+ 節點接線；C++/build/Metal 骨架由 Claude 扛。

## 環境前提（試壓過）

- `clang` 21 ✅ / `cmake` 4.3.2 ✅ / `git` ✅。
- **`metal` shader compiler ✅ 就緒**（2026-06-07）：完整 Xcode + `xcodebuild
  -downloadComponent MetalToolchain` 都裝好，`metal` v32023.883。注意：裝 Xcode ≠ 有
  metal compiler，Metal Toolchain 是 Xcode 16+ 的可選下載元件，要另跑 downloadComponent。
- metal-cpp headers 來源：`/tmp/metal-cpp-cmake/metal-cmake/metal-cpp/`（LeeTeng2001 vendored）。
  之後整進專案 `app/third_party/`。

## Active Lane

**lane（2026-06-08）= 「值脊椎」schema 設計對齊。** step 0→3 + B0 + 命令層 Phase 1 全 ✅ closed（見 Current Snapshot/進度）。
下一個動作：去 `external/tixl` 挖 input slot / InputValue 的真實結構，把「param = 帶預設值的 input port」的 schema
（pinId 怎麼涵蓋 param-as-port、存檔怎麼存、增刪/驅動切換怎麼走命令層）在紙上釘死，再碰 code。
**只有一個 active lane。** Phase 2 reconnect = parked（非 active）。

> 以下 step 0 / step 1 區塊是**歷史起手細節**（clone starter 指令），保留當紀錄，非當前 lane。

**step 0 — 不從零寫，clone starter 起手**（見 `2026-06-07-reusable-resources.md`）：
```
clone ikryukov/MetalCppImGui (metal-cpp + ImGui 純 C++ 已接好)
  + LeeTeng2001/metal-cpp-cmake (CMake 模板)
  + git submodule thedmd/imgui-node-editor (develop)
  → 螢幕被 Metal 清成一個顏色 + imgui 畫布顯示 + 拖一個假節點拉一條假線
```
驗收（hard）：build 過 + 螢幕亮 + 假節點可拖。證明 build 鏈通 / Metal 活 / node-editor 接上 Metal。
imgui 原生支援 metal-cpp（`IMGUI_IMPL_METAL_CPP` macro），不用自己 bridge。

**step 1 — 第一條最小粒子管線**（step 0 綠了才動）：
```
RadialPoints(生成 point buffer)
  → TransformPoints(compute 演化一次)
  → DrawMeshAtPoints(instancing)
  → 出一張 texture 顯示在 imgui-node-editor 畫布上
```
打通這條 = 同時證明「Metal 粒子能跑」+「imgui 皮接得上我的 runtime」。其餘節點都是這條的變奏。
寫 node 前先定 `EvaluationContext` struct（frame time / render res / active `MTL::CommandBuffer*`）。
慣例：丟 shader 的 struct 一律 `simd::float3`；render loop 開頭 `NS::AutoreleasePool`。

## 進度（2026-06-07）

- **step 0 第一根線 ✅ 綠**：imgui 官方 `example_apple_metal`（原生 NSWindow + Metal + imgui，
  零外部依賴）build 成功、跑出視窗（柏為視覺確認）、正常退出 exit 0。證明工具鏈能 build+跑
  原生 metal+imgui app。注意：這是探針，不是最終殼；下一步替換成專案 `app/` 純 metal-cpp 結構。
- **驗收基建（這專案的「感官」）已建立**：
  - `metal-cpp-discipline`（~/.claude/skills）：寫 metal-cpp 防呆（共享 header/對齊、AutoreleasePool、
    ownership、validation layer）。RED→GREEN 壓力測試過，Particle.h 範例編譯器雙端驗證 host=GPU=48。
  - `codex-eyes`（~/.claude/skills，配 codex-ears）：視覺自驗 = offscreen render→readback 自己的
    texture→斷言→文字 PASS/FAIL。provenance 結構性保證（讀自己 texture，不會讀錯視窗）。
    RED→GREEN 實戰過 + 獨立 agent verify 過。範本：`~/.claude/skills/codex-eyes/selftest_example.cpp`。
  - **step 0 殼的 `--selftest` 直接用 codex-eyes 的 offscreen readback 法**；每根線注入 bug 先證眼睛有視力。
- **step 0 殼 ✅ 綠（2026-06-07，待柏為視覺/手感最終確認）**：專案 `app/` 建好，純 metal-cpp
  清屏 + Dear ImGui + imgui-node-editor（兩個假節點 RadialPoints→DrawPoints + 一條假 link）+ 內建
  `--selftest`。四階段每階段 build 綠才前進，全程 `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` + ASan/UBSan：
  - build ✅ / app 啟動不崩 / validation layer 無誤 / sanitizer 乾淨。
  - `--selftest` RED→GREEN 證過：`--selftest-bug` 清錯色→FAIL exit 1，`--selftest` 清 kClearColor→
    PASS exit 0，center=(31,36,46)=round(kClearColor×255)，與 live 視窗同一常數（single source of truth）。
  - 啟動：`app/run-dev.sh`（自動 build + 開 validation layer）；`app/run-dev.sh --selftest` 跑 headless 眼睛。
  - **柏為只剩兩件事要親自確認**：開窗看到 teal-grey 清色 + node canvas、滑鼠拖得動假節點。
    （terminal 無 Screen Recording 權限，我截不了螢幕；像素正確性已由 --selftest readback 機器證明，
    但「視窗真的開出來 + 拖拉手感」只有柏為的眼睛/手能關。）
- **step 1 第一根線 ✅ 證明（2026-06-07）：RadialPoints compute → buffer readback proof**。
  `RadialParams(count,radius) → radial_points.metal compute kernel → position buffer → readback
  斷言每點 |pos.xy|≈R`。RED→GREEN 證過：`--selftest-radial-bug`（半徑減半）maxErr=1.0→FAIL，
  `--selftest-radial` maxErr=0.00000→PASS。跑法 `app/run-dev.sh --selftest-radial`（headless 文字）。
  **這根線同時建立/證明的承重基建**：
  - **shader build pipeline**：CMake custom command `xcrun metal -c … -I src/runtime` → `metallib`，
    .metal `#include "Particle.h"`（共享 header），對齊由編譯器雙端保證（discipline Rule 1 的正路，
    不用 runtime newLibrary(source) 那條容易踩對齊坑）。metallib 路徑經 `SW_SHADER_METALLIB` define 傳給 host。
  - **compute dispatch**（metal-cpp：newLibrary→newFunction→newComputePipelineState→computeCommandEncoder→
    dispatchThreads→commit/wait）+ **host/GPU struct 對齊**（`Particle` 16 bytes via simd::float3，static_assert 雙鎖）。
  - 求值模型**還沒長**：RadialPoints 目前是 headless 直跑，沒接 EvaluationContext / 通用 node evaluate /
    dirty 傳播——刻意，等 2–3 個節點後再長，先別織不承重的求值器。
  - 檔案：`app/src/runtime/Particle.h`（共享）、`app/shaders/radial_points.metal`、
    `app/src/runtime/radial_points.{h,cpp}`。
- **step 1 大切片 ✅ 完成（2026-06-07）：第一條會動的粒子線端到端打通，每根掛契約 golden。**
  `RadialPoints(compute) → TransformPoints(compute, pos+=vel*dt) → DrawPoints(render→RGBA8 texture) →
  ImGui::Image 顯示在 DrawPoints 節點裡`。柏為截圖確認：節點內白環黑底（2048 點），canvas 主畫面。
  - 四根線各自 selftest，全 RED→GREEN（人不逐步審）：`--selftest-radial`（|pos|≈R）、`--selftest-dispatch`
    （COMMAND_STREAM golden 真值表）、`--selftest-transform`（pos+=vel*dt，maxErr=0）、`--selftest-draw`
    （offscreen golden frame：nonBlack>50 且 centerBlack）。全程 MTL validation + ASan/UBSan 乾淨。
  - 骨幹（鎖死、禁改）：`Particle{position,velocity}`(32B) + `EvaluationContext{frameIndex,time,deltaTime}`(16B)
    + `BufferIndex` enum，全在 `src/runtime/Particle.h` 共享 header，static_assert 雙鎖。
  - 重用契約：MY_WORLD(PointBuffer/Cook/P1 藍圖)、FRAME_SCHEDULER(EvaluationContext)、COMMAND_STREAM
    (calcDispatchCount over-dispatch-by-one)、RESOURCE_LIFETIME(ownership)、TEXTURE_VIEW(RTV)。
  - 檔案：`shaders/{radial,transform,draw}_points.metal`、`src/runtime/{particle_system,dispatch,
    radial_points,transform_points}.{h,cpp}`；CMake 編三 .metal→一 metallib；`ParticleSystem` 類別 live+selftest 共用。
  - **已知限制（誠實標記，park）**：(1) 切向速度 + 對稱環 → 旋轉視覺上看不出來（資料有動、selftest 證了，
    但畫面像靜止）；運動設計是柏為的視覺決定。(2) Euler 積分對圓周運動會緩慢螺旋外擴（數值不穩，之後換積分器）。
    (3) update/render 每幀 commit+wait（阻塞，N 小無妨，之後改非阻塞）。(4) `ImGui::Image` 在節點內 OK
    （ImTextureID=ImU64，reinterpret_cast 指標）。
  - **sub-agent fan-out 解鎖**：骨幹已鎖死，下一批節點（LinePoints/RandomizePoints/FilterPoints/DrawLines…
    全是這條線的變奏、讀寫已配置 slot）現在可並行 fan-out（見〈工作方式〉階段二）。
- **step 2（active lane，2026-06-07）：照 TiXL 重新架構粒子系統（北極星 B 啟動）。**
  之前的 32B `{position,velocity}` scaffold 升級成 **TiXL 真實模型**（讀自 `external/tixl` 源碼）：
  - **綁定 spine 已鎖+驗證**：`src/runtime/tixl_point.h` 的 `Point`/`Particle` = **64B，照 `shared/point.hlsl` 1:1**
    （Point: Position/FX1/Rotation/Color/Scale/FX2；Particle: Position/Radius/Rotation/Color/Velocity/BirthTime，
    同 stride、屬性重詮釋）。**packed_float3 必須**（host 用明確 3-float struct，MSL 用原生 packed_float3，
    否則 stride 錯位、靜默壞畫面）。host static_assert 全過：`Pos@0 FX1@12 Rot@16 Col@32 Scale@48 FX2@60`。
  - **3 個 codex-coder agent 並行 port 數學 helper**（HLSL→MSL，純機械、獨立、寫到 `app/shaders/shared/`）：
    `noise.metal.h`(curlNoise+simplex 依賴閉包)、`quat.metal.h`(qRotateVec3/qSlerp/qLookAt)、`hash.metal.h`(hash41u)。
  - **helper 落地後 Opus 自己組**（sequential 母線）：port `ParticleSystem.hlsl` 積分器（emit cycle buffer→drag
    `vel*=pow(1-Drag,Speed)`→integrate `pos+=vel*Speed*0.01`→orient slerp→lifetime age>1 則 Scale=NAN）+
    `TurbulanceForce.hlsl`（`vel+=curlNoise(pos*Freq+Phase)*Amount`）；重組 ParticleSystem 類別為 TiXL 的
    emit→Particle buf→forces→integrate→ResultPoint→draw；每根 selftest；live 出 TiXL 式流動粒子 + 截圖。
  - 來源權威：`external/tixl/Operators/Lib/Assets/shaders/particles/{ParticleSystem,TurbulanceForce}.hlsl` +
    `shared/point.hlsl`；node 契約照 `Operators/Lib/particle/ParticleSystem.cs` 的 port（用 node-contract-architect）。
  - **✅ 完成（2026-06-07，柏為截圖確認）：第一個 TiXL 節點端到端 port 成功，畫面 = DrawPoints 節點裡一整片
    curl-noise 流動的粒子雲。** 3 helper（noise/quat/hash）由 codex-coder agent 並行 port、各自編譯過；
    積分器 `particle_sim.metal` + `turbulence_force.metal` 由 Opus port、編譯過。selftest 全 RED→GREEN：
    `--selftest-flow`（turbAmount=0→maxDev=0 FAIL；=15→maxDev=0.456 PASS，證 curl 真的動粒子）、`--selftest-draw`
    （nonBlack=2204+centerBlack PASS）、`--selftest-dispatch`。MTL validation + ASan/UBSan 乾淨。
    SwPoint/SwParticle 64B（host `Point` 撞 Carbon QuickDraw → 改 Sw 前綴；型別撞名不照 TiXL，邏輯照）。
  - 退役：舊 32B scaffold（radial_points/transform_points/Particle.h）移出 build，留盤上待清。
  - **✅ 節點契約化 v1 完成（2026-06-07）**：canvas 變成真實 4 節點管線
    `RadialPoints → ParticleSystem ← TurbulenceForce`、`ParticleSystem → DrawPoints`（DrawPoints 內顯示 live texture）。
    選節點 → Inspector 顯示其 TiXL 參數 slider（TurbulenceForce: Amount/Frequency；ParticleSystem: Speed/Drag），
    **拉 slider 即時驅動 runtime**（皮 downstream of real contract，ui-skin-pressure-gate 過）。param→runtime 路徑
    由 `--selftest-flow` 證（amount 0 vs 15 結果不同）。build + 全 selftest 綠、validation 乾淨。
    契約用 node-contract-architect 框架定（question/conversion/params 分類）。
  - **節點契約化 limits**：只開最有感的 4 個 param（Amount/Frequency/Speed/Drag）；Radius/Count 還是 structural；
    參數還是全域 `LiveParams` 不是 per-instance NodeSpec；節點還是 hardcode 畫的、不是 graph 資料模型驅動。
- **step 3（active lane → ✅ v1 完成，2026-06-07）：NodeSpec graph 資料模型（柏為選 b）。**
  用 tooll3-interaction-compatibility 閘：**借 Tooll3 的 command 詞彙/save 行為，但 schema 是自己乾淨的 native
  模型，不照搬 TiXL Symbol/Instance**（閘明令禁止）。
  - `src/runtime/graph.{h,cpp}`：`NodeSpec`(型別+ports+params，registry 照 TiXL) / `Node`(instance: type/pos/params)
    / `Connection` / `Graph`。canvas **從 graph 畫**、Inspector **編輯 graph**、runtime **從 graph cook**——graph 是唯一真相。
  - **save/load**（crude_json，node-editor 自帶）+ roundtrip：`--selftest-graph` RED(perturb param)→FAIL /
    GREEN→PASS（JSON 1001 字元、4 節點 3 連線，L6 proven）。canvas/inspector 全 graph-driven 後全 selftest 綠、
    validation 乾淨、柏為截圖確認。
  - **ladder 狀態**：L1 節點契約 ✅、L6 save/load roundtrip ✅（資料模型層）。**park**：L4 command/undo-redo、
    動態加/刪節點、node-editor 拖曳位置 sync 回 graph（存檔還沒收到拖過的位置）、檔案 save/load UI 按鈕、
    通用 topological evaluator（現在 cook 是「按 type 找節點套參數」，固定 4 節點夠用）。
  - **下一步（柏為選）**：(a) fan-out 其餘 16 種力（sub-agent 並行，照 TiXL）/ (b2) command 層（undo/redo +
    動態加節點 + 檔案 save/load + 位置 sync）/ (c) UI 向 TiXL 視覺收斂（北極星 B）。
- **進度（2026-06-08）：命令層/undo-redo Phase 1 closed + 兩個輸入 bug 修復 + hand 補鍵盤。**
  - **刪除 bug 修復**：B0 宣稱「刪節點/線 ✅」但 macOS 上其實壞的——Mac 的 delete 鍵送 `Backspace`，
    imgui-node-editor 只聽 forward-Delete (`ImGuiKey_Delete`)。在 editor_ui 加 Backspace→`ed::DeleteNode/DeleteLink`
    路由（柏為親手確認可刪節點/線/多選）。
  - **命令層 Phase 1 ✅ closed（柏為 7/7 親手驗收）**：`app/src/app/command.{h,cpp}`（Command/CommandStack/MacroCommand）
    + `graph_commands.{h,cpp}`（AddNode/AddConnection/DeleteNodes/DeleteConnections/MoveNodes，命令持 `Graph&` 可隔離測）。
    editor_ui 的 add/link/delete/move 全改走命令；Cmd+Z/Cmd+Shift+Z 綁鍵；doNew/doOpen 清空 stack。
    `--selftest-command` RED→GREEN。subagent-driven 執行（implementer + 自跑 selftest 驗）。
  - **reconnect Phase 2（replace-on-input）= parked**：`Graph::connectionToInput` + Macro{Delete,Add} 重用命令、
    `--selftest-command` 加 reconnect 場景全綠；補了 Phase 1 的 input cardinality 漏洞（同 input 不再能兩條線）。
    GUI 拖接點驗收 parked（節點太少）。**待釐清**：柏為直覺是「拖節點」而非「拖接點」改接（前者近 Phase 3）。
  - **DRY 修正**：`(pin-1)/100` 反推散三處 → 收斂成 `graph.h::pinNode`（鐵律 7，緊鄰 `pinId` 互為逆）。
  - **hand 補鍵盤注入**：`verify/hand` 加 `key`/`keychord`（`io.AddKeyEvent`），`--selftest-hand` 擴充 Cmd+Z chord RED→GREEN。
    這把尺先幫忙抓到一個 bug：**imgui `ConfigMacOSXBehaviors`（`__APPLE__` 預設 true）在 `AddKeyEvent` 內把 Cmd↔Ctrl 對調**
    → 實體 Cmd 落在 `io.KeyCtrl` 不是 `io.KeySuper`，偵測 Cmd 快捷鍵要查 `io.KeyCtrl`/`Shortcut(ImGuiMod_Ctrl)`。
    （已存 memory `[[imgui-macos-cmd-ctrl-swap]]`。）
  - **憲法合規審查通過**：五區/依賴單向/verify 薄介面/單檔大小/隔離測試全過；唯一踩到鐵律 7（DRY）的 pinNode 已修。
- **step 0 踩雷紀錄（給未來 session）**：
  - imgui-node-editor `develop` **不相容 imgui 1.92.x WIP**（`ImRect::Floor()` 被移除、`ImCubicBezierDt`
    ImVec2 operator 變動）。→ **imgui 釘在 v1.91.8**。換 imgui 版本前先確認 node-editor 跟得上。
  - imgui `imgui_impl_metal.mm` / `imgui_impl_osx.mm` 是 **MRC（手動 retain/release），不是 ARC** →
    CMake 對這兩個 .mm 設 `-fno-objc-arc`（1.92 的 metal backend 改 ARG-style texture，又是另一個踩 ARC 的坑，
    1.91.8 沒有，乾淨）。
  - osx backend 的純 C++ `void*` overload 要**同時**定義 `IMGUI_IMPL_METAL_CPP` **和**
    `IMGUI_IMPL_METAL_CPP_EXTENSIONS`（後者才開 void* 那條），main.cpp 才不必寫 shim.mm。
  - LeeTeng 模板的 `AppKit`/`MetalKit` C++ binding 在 `metal-cpp-extensions/`（不在 metal-cpp 核心目錄）→
    純 C++ 開窗（NS::Application + MTK::View），不需 GLFW（沒裝）也不需 .mm 平台層。
  - imgui_impl_osx 自動裝 event monitor + KeyEventResponder subview，**輸入免轉發**（2022 後版本）。
  - `app/third_party/`（vendored metal-cpp + imgui + node-editor）跟 `app/build/` 進 .gitignore，
    與既有 `/external/` 慣例一致；只 commit `app/src` + `app/CMakeLists.txt` + `app/run-dev.sh`。

## 工作方式（2026-06-07 定案 = gemini research〔標準 Metal 工程實踐〕+ 舊契約資產 + 現有進度，三者收斂）

**核心切線：契約層「順序鎖」、葉子層「並行 fan-out」。** 並行的單位不是「節點 vs 節點」，是「契約定義層 vs 葉子實作層」。

- **必須順序（Opus 自己做、禁 fan-out）= 契約層**：定義 host/device binding slots、PSO/pipeline descriptor、
  memory sync、write offsets、共享 struct 的東西。改這些就是動承重結構。
- **可並行 fan-out（sub-agent）= 葉子層**：封裝在單一執行單元的東西——一個 MSL 數學/物理函式、一個只讀寫
  「已配置好的 buffer slot」的 node widget、一個 offscreen 驗證 harness。

**兩階段（順序不可反）：**
1. **鎖契約骨幹（順序，現在）**：把舊契約規格翻成 C++ 共同地基——`SharedTypes.h`(Particle/PointBuffer 型別)
   + `BufferIndices` enum(binding slots 單一來源) + `EvaluationContext`(= Cook Contract + FrameScheduler 的
   frameIndex/time/deltaTime) + resource ownership 規則。**這層鎖死、agent 禁改（除非專屬指令）。**
2. **葉子 fan-out（並行，骨幹定後）**：每個節點 = 一個 agent + 它的 selftest，讀寫已配置 slot，互不踩。

**驗證三模式（人不逐步人工審）：** A `compute readback assertion`（固定 seed→readback buffer→assert 物理/數學
定律，= 已做的 `--selftest-radial`）；B `offscreen golden frame`（render→getBytes→MSE vs golden，設容差）；
C `Metal validation layer`（`MTL_DEBUG_LAYER`/`MTL_SHADER_VALIDATION`，已開）。

**回報顆粒度：** 一個「會動、能 demo」的切片做完才回報柏為（畫面 + 全綠報告 + 本檔更新）；中間逐線 selftest
由 Opus 自己爬，不要他陪看中間態。

**並行會死在哪（全源自「共享契約沒先鎖」）：** struct padding 分歧、vertex descriptor 不同步、render pass
load/store 覆蓋、語義合併衝突（git 行比對不報、memory layout 靜默壞）。→ **階段一鎖契約是不可跳的前提。**

## Spine（皮選項的最終判決，已壓過，不要重議）

| 選項 | 判決 |
| --- | --- |
| `imgui-node-editor` + 自建 Metal runtime | ✅ **走這個**。跨平台、直接 Metal、一個調度者、無 event model；TiXL 本身就是 ImGui(326 檔)，手感免費繼承，借 TiXL editor 的 .cs(如 `ScalableCanvas.cs`)當互動設計參考而非克隆。 |
| 克隆 TiXL editor(C#) | ❌ 假路：567 檔中 453 焊死 T3.Core + SharpDX(DX11) 28 + WinForms 12 + net*-windows。 |
| 魔改 Vuo GL→Metal | ❌ GL 焊進 VuoImage 型別系統，等於重寫 Vuo 一半。 |
| 兩個調度者共用 Vuo 畫布 | ❌ 要開膛 Vuo Editor，繼承雙倍複雜度，最貴。 |
| Vuo 當皮(粒子寫成 Vuo node) | ⚠️ 次選，僅當「大量用 Vuo 的 image/audio 生態」才考慮；要吃 event model + Metal↔GL 橋接。 |
| 自己從零做編輯器 | ⚠️ 幾個月全職前端，柏為不寫程式，最後才考慮。 |

## 第一批節點（柏為實際要的，重心是 GPU 粒子，不是 image）

- **核心(必自建 Metal)**：RadialPoints / LinePoints / RandomizePoints / FilterPoints /
  TransformPoints / RepeatAtPoints / MapPointAttributes / LinearSamplePointAttributes /
  CombineBuffers / DrawPoints / DrawLines / **DrawMeshAtPoints** / Raymarch Field。
- 周邊(Vuo 有、可晚點借資料)：mesh/camera/image filter/audio/輸出。
- 全清單見柏為訊息與 [[simple-world-architecture-pivot]]。

## Plan Inventory

- 本檔：**現行 master progress，唯一 dashboard。**
- `specs/2026-06-08-node-editing-commands-design.md`：**節點編輯命令層 spec**（五階段：命令層→reconnect→插入→框選→複製貼上）。active 設計契約。
- `specs/2026-06-08-value-spine-design.md`：**值脊椎 spec（current active lane 的設計契約）**：param=帶預設的 Float input port（schema 合一，TiXL InputSlot 已查證）、5 承重柱、5 起手節點（Time/Const/Multiply/Sine/Remap）、重用 Phase1/2 對照、接縫 main.cpp:322-325。schema 已釘死，待柏為過目 → 轉實作計畫。
- `2026-06-08-node-editing-commands-phase1.md`：**Phase 1 實作計畫 = closed**（命令層+undo/redo，柏為 7/7 驗收，checkbox 已勾）。closure evidence。
- `2026-06-08-node-editing-commands-phase2.md`：**Phase 2 實作計畫 = parked**（reconnect；頂部有 ⏸ 狀態 banner 說明擱置原因 + 待釐清方向）。
- `2026-06-08-architecture-constitution.md` / `specs/2026-06-08-architecture-constitution-design.md`：架構憲法（五區/7 鐵律）= `ARCHITECTURE.md` 的來源。
- `2026-06-07-reusable-resources.md`：**可用資源清單**（不重造的輪子：starter、引擎輪子、
  求值核心、設計藍圖、LLM/metal-cpp 陷阱 checklist、待驗清單）。source material。
- `2026-06-05-native-runtime-master-progress.md`：**歷史（JS/Python 實作不搬）**，但它指向的契約資產**不是**廢的（見下）。
- **`docs/runtime/*_CONTRACT.md`（19 個）+ `tests/*_contract.test.js`（21 個）= 可複用契約資產（2026-06-07 修正判定）。**
  尤其 `MY_WORLD_RUNTIME_CONTRACT.md`（總綱）的**規格層是語言中立的引擎設計法**：Runtime Type System
  （含 `PointBuffer`）、Cook Contract（7 求值階段）、FrameScheduler/Main Clock（frameIndex/time/deltaTime
  = 柏為說的 timeline）、Node/Port/Resource/Failure/Command Contract、Four Graphs（editorGraph/runtimeGraph/
  commandGraph）、`P1: Point Buffer Shell` fixture（= 現行粒子線的設計藍圖）。**新方向不重新發明這些規格，
  讀契約當 C++ 設計法 + 把 golden 驗收案例移植成 C++ selftest。** JS/Python 實作與 Vuo body-layer/proof-gate 不搬。
  **契約盤點完成（2026-06-07）。Top 5 NOW（現行粒子線該立刻複用當驗收標準）：**
  1. `MY_WORLD_RUNTIME_CONTRACT`（總綱，唯一純 SPEC）— `PointBuffer` 型別 / Cook 7 階求值 / `P1` 設計藍圖 /
     `EvaluationContext` 別重新發明。
  2. `FRAME_SCHEDULER` — TransformPoints 要 time/deltaTime；golden：一幀一個 context、`previousFrame=[null,0,1]`、
     非法 clockOwner→報錯 exit 1。
  3. `RESOURCE_LIFETIME` — buffer/texture ownership + realloc 時舊 view 失效；golden：`["allocate","reuse","reallocate",200]`。
  4. `COMMAND_STREAM`（只取 dispatch 數學）— `calcDispatchCount` 真值表 `64,16→5 / 63,16→4 / 960×540,16→61×34`。
  5. `TEXTURE_VIEW` — DrawPoints 畫進 texture 要 RTV/SRV；`createTextureView` 真值表擋「buffer 看似有效但沒綁對 view」的假成功。
  其餘契約多為 MIXED（規格/gate 邏輯可移植，Vuo 組合 / `.py` shell / `.graph.json` / DX11 名詞不搬）；4 個 IMPL/SKIP 綁死 Vuo/Python。
  **最有移植價值的不是契約散文，是 `tests/*_contract.test.js` 裡那幾組行為斷言（真值表 / dispatch 數學 /
  scheduler previousFrame 序列 / lifetime allocate-reuse-reallocate 序列）= 直接改寫成 C++ selftest 的 golden 素材。**
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/`：柏為的節點報告書（935 specs/915 C# classes），當**設計藍圖**，非程式碼來源。
- `docs/tixl-porting/`：TiXL→Vuo porting 的歷史批次，旁支。
- 354 個 `vuo-nodes/*.c`：柏為說「在錯誤時間做的」、runtime 不承重的**廢旁支**，不要再往上加東西。

## Conflict Register

- 舊 master plan 宣稱 100/100 ↔ 實際沒有能打開的 app。解決：本檔取代它；100/100 只是 headless 證明。
- **（2026-06-07，柏為提醒觸發）本檔前一版把舊 native-runtime 的「graph/command/runtimeGraph 層」整批判成
  「新方向用不到」= 判太狠的錯。** 把「TiXL/Vuo **runtime 實作**搬不動」過度推廣成「**契約規格**也用不到」。
  解決：區分**契約規格（語言中立，可複用，是資產）vs JS/Vuo 實作（不搬，是負債）**。`docs/runtime/*_CONTRACT.md`
  改判為可複用契約資產（見 Plan Inventory）。剩餘風險：契約裡夾雜的 Vuo/`.py`/`.graph.json` 細節要逐契約篩
  （SPEC/MIXED/IMPL），background agent 盤點中。**Session Safety 那條「不要照舊 100/100 蓋 graph/command/UI」
  仍成立**——指的是不要重蓋舊**實作**，不是不准讀契約規格。
- 「克隆九百多個 TiXL 節點」是幻覺：TiXL 是 net10.0-windows、SharpDX 177/DX11 133 散進節點本體無抽象層；節點主體是 915 個 C#(.cs 2464)，shader 只 424 且半數是 compute(218)+RWStructuredBuffer(182)，且常是 template 靠 C# runtime 填洞（repo 內 `tixl_mesh_draw_hlsl_to_msl_verdict` 已 reject 機械翻譯）。→ 不克隆，借報告書當藍圖。
- SPIRV-Cross 直接 HLSL→MSL：只解 pixel/vertex shader 那一小塊，不解 C# 節點語意與 DX11 runtime，不是主線解法。
- **（2026-06-07）北極星「Mac 版 TiXL」vs Spine「克隆 TiXL editor ❌ 假路」表面衝突，實則一致。** 澄清：
  北極星要的是**功能/邏輯/體驗對等**（imgui-node-editor 皮 + metal runtime **重現** TiXL），**不是**逐行翻 TiXL 的
  C#/DX11/WinForms 實作（那條焊死、已否決）。Spine 否決的是「克隆實作」；北極星要的是「照邏輯重寫」。兩者同一個方向。

## Session Safety

- 不要照舊 100/100 plan 繼續蓋自建 graph/command/runtimeGraph/UI。
- 不要往 354 vuo-nodes 加東西。
- 工作樹有 `AGENTS.md`、`skills/tixl-vuo-node-port/SKILL.md` 被外部改動，別亂動。
- 動手前先跟柏為確認皮的選擇仍是 imgui-node-editor。

## Next Handoff Sentence

新 session：先讀 memory `[[simple-world-northstar-and-method]]`（★命脈）+ 本檔。
**北極星 = Mac 版 TiXL（柏為選 B，視覺也追，照 TiXL，不確定查 `external/tixl` 源碼，不自創）。
完成定義 = 柏為能親手按到/操作/確認 work，不是 selftest 綠。照〈Roadmap〉順序走、可插隊、不漏。** 方向別重議。

**現況（2026-06-08）：step 0→1→2→3→B0 全 ✅ + B2 命令層/undo-redo Phase 1 ✅ closed（柏為 7/7 親手驗收）。**
Phase 2 reconnect = parked（節點太少難測 GUI）。Phase 3–5 = queued。
**第一個動作 = 值脊椎 schema 設計對齊**（柏為 2026-06-08 拍板的 active lane）：
- 去 `external/tixl` 挖 input slot / InputValue 真實結構（不靠記憶）。
- 把「param = 帶預設值的 input port」schema 在紙上釘死：pinId 怎麼涵蓋 param-as-port、存檔 toJson/fromJson 怎麼存、
  值線增刪 / param 驅動切換**怎麼走已建好的命令層**（可 undo）。**schema 釘死再碰 code。**
- 起手節點（最小可玩鏈 Time→Sine→Remap→粒子脈動）：Time/Const/Multiply/Sine/Remap 五個；Curve 砍到第二棒（但記著柏為遲早要）。
- 完成定義不變：柏為親眼看到粒子被連線驅動而脈動才算完（眼手可截「畫面變化」驗，比 reconnect 拖線好驗）。

寫碼紀律（已就位、沿用）：`metal-cpp-discipline`（共享 header/對齊/AutoreleasePool/ownership）；丟 shader 的 struct
要照 TiXL 64B layout 必用 **packed_float3**（host 3-float struct）；`app/run-dev.sh` 開 validation layer + ASan/UBSan；
GPU 輸出用 `codex-eyes` offscreen readback 驗、**每根線先注 bug 證眼睛**；契約層順序鎖、葉子並行 fan-out。
踩雷清單見 memory northstar 那條（imgui 1.91.8 / MRC -fno-objc-arc / SwPoint 撞 Carbon / ed::EndCreate 在 if 內）。

**不是**接舊 JS/Python runtime；舊 JS 契約**規格**可複用（語言中立）、實作不搬；**不照搬 TiXL Symbol schema**（graph 用自己 native 版）。
