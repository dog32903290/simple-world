# simple_world 進度逐條史（archive，grep 用，非每次必讀）

> 2026-06-08 從 master progress 的〈進度〉節抽出。master 只留薄 dashboard；要逐條細節（step0→今天的 RED→GREEN 證據、檔案、踩雷）grep 這裡。當前狀態看 master 的 Current Snapshot。

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

