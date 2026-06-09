# 點 operator 圖 runtime 實作計畫（lane A · Mac/Metal · 照 TiXL）

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。Steps 用 `- [ ]` 追蹤。
> 設計來源 = `specs/2026-06-09-point-operator-graph-design.md`（TiXL 點系統爬取 + 三拍 + 每顆固定流程）。
> 北極星 = Mac 版 TiXL。每顆 operator 照 `external/tixl/Operators/Lib/point/**` 既有設計，不自創。
> **commit 律法閘（柏為 2026-06-09 定，每 task 末）**：commit 前對照 `ARCHITECTURE.md` 自檢動到的碼（五區/依賴單向/verify一行hook/單檔≤400/--selftest/資料驅動），有違反先改完再 commit。

**Goal:** 把焊死的單一 `ParticleSystem` 換成 TiXL 那樣的**點 operator 圖**——連線扛一袋點（`MTL::Buffer` of `SwPoint` = TiXL Point 64B），每顆 operator 讀進→寫出；之後照 TiXL 逐批 fan-out 點節點。

**Architecture:** 在 `graph.cpp` 的 float 求值器（`evalFloat`/`evalParam`，只走 `"Float"` port）旁，新增一條**只走 `"Points"`/`"ParticleForce"` port 的 buffer 求值器**——新模組 `runtime/point_graph`（Metal 端，與純資料模型 `graph.h` 分離）。它持有 device/lib/queue + per-node buffer & state，每幀從 draw 節點往回拓撲走 `"Points"` 連線，逐顆 cook 出 buffer，末端 render 進 target texture。operator 介面允許**有狀態**（sim 類持久 buffer），所以無狀態生成器/變換與有狀態 ParticleSystem 同一介面。

**Tech Stack:** C++17、metal-cpp、MSL（既有 `radial_emit`/`turbulence_force`/`particle_sim`/`draw_points` kernel 可複用）、`SwPoint`/`tixl_point.h`、`dispatch.h`(`calcDispatchCount`)、`evalParam`(float 參數)、`--selftest-*` RED→GREEN、eye(offscreen readback)。

---

## File Structure

- **新 `runtime/point_graph.h` / `.cpp`**（runtime 葉子，Metal）：點 operator 介面 + cook fn 登記表 + `PointGraph` cook（拓撲走 Points 連線、per-node buffer/state、render 末端）。**這是 A.0 的承重檔。**
- **新 `runtime/point_ops.h` / `.cpp`**（runtime 葉子，Metal）：各點 operator 的 cook fn（A.1 起逐顆/逐批加；A.2 fan-out 的落點）。每顆 = 一個 cook fn + register。**fan-out 時新 operator 加在這（或自己的 `point_ops_<name>.cpp`），避免撞 point_graph.cpp。**
- **`runtime/graph.cpp`**：registry() 既有 NodeSpec 表已含 Points port（RadialPoints/ParticleSystem/DrawPoints…）；新增點節點 = 加一個 NodeSpec literal（與 cook fn 分離，float 參數/port 在這、Metal cook 在 point_ops）。
- **`main.cpp`**：Renderer 改持有 `PointGraph` 取代 `g_particles`；cook 迴圈把 `g_particles->update/render` 換成 `pointGraph.cook(graph, ctx, &reg)`。selftest 派發加 `--selftest-pointgraph`。
- **`CMakeLists.txt`**：加 `point_graph.cpp`/`point_ops.cpp` 源；新 operator 的 `.metal` 加進 `SW_SHADERS`。
- **`shaders/*.metal`**：複用既有；新 operator 各帶 kernel（A.1+）。

> **過渡策略（不大爆、不回歸）**：A.0 把新 cook 蓋好並**headless 證明**（不動 live render loop，monolith 照跑，app 不退化）。A.1 才把 live loop 切到新 cook——且先把 ParticleSystem sim 折成有狀態 operator，切換後**畫面仍是流動粒子、但已可改線**，無回歸。

---

## A.0 — 鎖點 buffer 鏈契約 + cook（順序、Opus 自己做、禁 fan-out）

**承重，這拍鎖死後 agent 禁改（除非專屬指令）。** 目標 = 一條能被 headless 證明的點 operator 鏈跑通：兩個 stateless op（generate→透傳/變換）cook 出 buffer、readback 斷言；介面允許有狀態。

### Task A0.1：設計並落 `point_graph.h` 介面（契約原子）

**Files:** Create `app/src/runtime/point_graph.h`。

設計意圖（簽名實作時可微調，以 selftest 鎖行為為準）：

```cpp
// runtime/point_graph.h — 點 buffer 求值器（Metal）。與 graph.cpp 的 evalFloat 平行，
// 但走 "Points"/"ParticleForce" port：一條連線扛 MTL::Buffer of SwPoint(=TiXL Point 64B)。
// 與純資料模型 graph.h 分離（graph.h 不碰 Metal）。runtime 葉子。
#pragma once
#include <cstdint>
#include <string>
namespace MTL { class Device; class Library; class CommandQueue; class Buffer; class Texture; }
struct EvaluationContext;
namespace sw {
struct Graph; class SourceRegistry;

// 一顆點 operator cook 時拿到的東西。inputs = 已 cook 好的上游 buffer（依 port 序，未接=nullptr）。
struct PointCookCtx {
  MTL::Device* dev; MTL::Library* lib; MTL::CommandQueue* queue;
  const EvaluationContext& ctx;
  const Graph& graph; const SourceRegistry* reg;   // 解析 Float 參數用 evalParam
  int nodeId; uint32_t count;                        // 此節點 id + 點數
  const MTL::Buffer* const* inputs; int inputCount;  // 上游 cook 好的 buffer（port 序）
  void* state;                                       // per-node 持久狀態（有狀態 op 用；無狀態忽略）
};
// cook fn：encode compute（或填）產出一個 SwPoint buffer，回傳（PointGraph 管生命週期）。
using PointCookFn = MTL::Buffer* (*)(PointCookCtx&);
// draw fn：把末端 buffer 畫進 target，無 buffer 輸出。
using PointDrawFn  = void (*)(PointCookCtx&, MTL::Texture* target, const MTL::Buffer* points);
// per-node 持久狀態的建立/釋放（有狀態 op：sim 的 particles_ 等）。回傳 nullptr=無狀態。
using PointStateNewFn  = void* (*)(MTL::Device*, uint32_t count);
using PointStateFreeFn = void  (*)(void*);

// 登記表（type -> fn）。與 NodeSpec.evaluate(float) 分離，因為這層碰 Metal。
void registerPointOp(const std::string& type, PointCookFn,
                     PointStateNewFn = nullptr, PointStateFreeFn = nullptr);
void registerDrawOp(const std::string& type, PointDrawFn);

// 點圖 runtime：持有 device/lib/queue + per-node buffer & state（跨幀）。cook 從 draw 節點
// 往回拓撲走 Points 連線、逐顆 cook、末端 render 進 target()。取代焊死的 ParticleSystem pipeline。
class PointGraph {
 public:
  PointGraph(MTL::Device*, MTL::Library*, MTL::CommandQueue*, uint32_t width, uint32_t height);
  ~PointGraph();
  PointGraph(const PointGraph&) = delete; PointGraph& operator=(const PointGraph&) = delete;
  bool valid() const;
  void cook(const Graph&, const EvaluationContext&, const SourceRegistry* reg);  // 每幀
  MTL::Texture* target() const;
 private: /* per-node buffer pool + state map + device/lib/queue refs */
};

// headless 證明：建一條 generate->passthrough 兩 op 鏈，cook，readback，斷言點數/位置；
// injectBug 讓 passthrough 不傳遞（產 0 點 / 錯位）→ FAIL。
int runPointGraphSelfTest(bool injectBug);
}  // namespace sw
```

- [ ] **Step 1:** 寫 `point_graph.h`（上述介面 + `runPointGraphSelfTest` 宣告）。
- [ ] **Step 2:** build（只加 header，還沒 .cpp → 還不連結，先確認語法/include 乾淨）：暫不加 CMake，下一 task 一起。

### Task A0.2：實作 `point_graph.cpp`（登記表 + 拓撲 cook + buffer/state 管理）

**Files:** Create `app/src/runtime/point_graph.cpp`；Modify `app/CMakeLists.txt`（加源）、`app/src/main.cpp`（selftest 派發 `--selftest-pointgraph` / `-bug`）。

實作要點（contract 決定，鎖死）：
- 登記表 = `static std::map<std::string, ...>`（type→cook/draw/state fn）。
- `cook()`：找 draw 節點（spec 有 `PointDrawFn` 註冊的 type，如 DrawPoints）→ 沿 `"Points"` 連線往回 DFS 拓撲序 → 對每個 operator 節點：湊它的 input buffer（依 port 序查 `connectionToInput`，遞迴先 cook 上游）、取/建它的持久 state、算 count（由其 `"Count"` 參數 evalParam 或上游 count）、呼叫 cook fn → 存該 nodeId 的輸出 buffer（跨幀複用，count 變才 realloc，記 RESOURCE_LIFETIME golden：allocate→reuse→reallocate）→ 末端 draw fn 畫進 target。
- 無 draw 節點 / 斷鏈 → 安全 no-op（target 清空），不崩。
- 環/缺節點 guard（depth 上限，照 evalFloat 的 cycle guard）。

- [ ] **Step 1: 寫失敗測試 `runPointGraphSelfTest`**（在 .cpp 內；用兩個臨時註冊的測試 op：一個 generator 產 N 個 x=1 的點、一個 passthrough 複製輸入）：建 graph gen→pass→[draw 收集]，cook，readback 輸出 buffer，斷言 N 個點且 x==1；`injectBug` 讓 passthrough 產 0 點 → FAIL。
- [ ] **Step 2: build → 跑 → 確認 FAIL**（實作還沒寫）：`cmake --build build -j && ./build/simple_world --selftest-pointgraph` Expected: FAIL/crash（cook 未實作）。
- [ ] **Step 3: 實作 `registerPointOp/DrawOp` + `PointGraph::cook` + buffer/state 管理**（上述要點）。
- [ ] **Step 4: build → 跑 → PASS / bug→FAIL**：`--selftest-pointgraph` PASS（exit 0）、`--selftest-pointgraph-bug` FAIL（exit 1）。
- [ ] **Step 5: 回歸**：全 selftest 仍綠（純新增模組，未動 live loop / 既有行為）：`--selftest-graph && --selftest-valuecook && --selftest-flow && --selftest-draw && …`。
- [ ] **Step 6: 對抗審查**：派 subagent「找 runPointGraphSelfTest 漏測的 cook 路徑」（斷鏈/無 draw/count 來源/realloc/環）；採納有牙的補測。
- [ ] **Step 7: 律法自檢 + commit**：新檔 `point_graph.{h,cpp}` 屬 runtime 葉子✓、不往上依賴✓、≤400 行✓、有 `--selftest-pointgraph`✓、登記表資料驅動✓。`git commit -m "feat(runtime): point-operator buffer-graph cook + headless proof (A.0)"`。

> A.0 完成 = `--selftest-pointgraph` 綠/紅，介面鎖死。柏為不必看（純水管工）。摸的東西在 A.1。

---

## A.1 — 第一刀：拆出真鏈 + 切 live（▣ 柏為第一個可摸）

> 輪到時照 A.0 鎖好的介面詳成 bite-sized。形狀（先簡單→有狀態→切 live，避免回歸）：
- **A1.1 RadialPoints cook op**（stateless 生成器）：複用 `radial_emit` kernel，照 TiXL `RadialPoints.cs`/`.hlsl` 對齊參數（Count/Radius/…）+ golden selftest（公式：點在半徑 R 上）。register 進 point_ops。
- **A1.2 DrawPoints draw op**：複用 `draw_points` render pipeline，吃末端 buffer 畫進 target。register。headless：RadialPoints→DrawPoints cook→eye 非黑+中心黑（複用既有 draw selftest 斷言）。
- **A1.3 TransformPoints cook op**（stateless 變換）：照 TiXL `point/transform/TransformPoints.cs`+`.hlsl` 爬+港新 kernel（translate/rotate/stretch/scale + F1/F2）+ golden selftest（已知變換→已知位移）。register。
- **A1.4 ParticleSystem 折成有狀態 sim op**：把 monolith 的 emit+turbulence+sim 包成一個 `PointCookFn`（input=emit buffer、state=particles_ 持久、複用既有三 kernel）+ TurbulenceForce 走 `"ParticleForce"` input。**這是本拍最難（本質的醜：有狀態 GPU sim 進 dataflow），隔離在這一步、保持可單測。**
- **A1.5 切 live loop**：`main.cpp` Renderer 用 `PointGraph` 取代 `g_particles`；cook 迴圈呼叫 `pointGraph.cook(g_graph, ctx, &reg)`；editor_ui 預覽改讀 `pointGraph.target()`。**驗無回歸**：畫面仍流動粒子（eye full 比對切換前後）。
- **A1.6 ▣ 柏為摸**：在編輯器改接線（繞過 TransformPoints / 換生成器 / 改參數）→ 畫面跟著變。第一次「連線真的驅動畫面」。
- 每步末律法自檢 + commit。每顆 op 後對抗審查 golden 是否真對上 TiXL `.hlsl`。

> 詳細 bite-sized（逐 Step 碼/build/selftest）在 A.0 落地、介面定型後補（依賴 A.0 確切簽名，現在寫會是猜）。

---

## A.2+ — 一批一批並行 fan-out（workflow）

A.0+A.1 過了、介面穩了，照 TiXL 順序逐批港。**每批 = 一個 workflow，派 N 隻 agent，每隻認領一顆 operator**，跑 spec 的「每顆固定流程」：

1. 爬 `external/tixl/Operators/Lib/point/<cat>/<Name>.cs` + 配對 `.hlsl` + `.help/docs/.../point/<cat>/<Name>.md`。
2. port：`runtime/point_ops_<name>.cpp`（cook fn + register）+ `shaders/<name>.metal`（kernel，照 .hlsl）+ `graph.cpp` 加 NodeSpec literal（float 參數/port）+ CMake 加源/shader。
3. golden selftest `--selftest-<name>`：抓 TiXL 公式當標準答案（TiXL 跑不動，用公式）；injectBug 真退化→FAIL。
4. 驗 parity：port/參數/enum 對齊 `.cs` + eye 比對 `.md` 長相 + subagent 對抗審查「golden 是否真對上 .hlsl」。
5. 律法自檢 + commit（每顆一 commit）。

**批次順序（照 TiXL 分類 + 工程相依；每批量看 budget/workflow 並行上限）：**
- **批 1 生成器**：LinePoints / GridPoints / SpherePoints（照 `generate/`，stateless，最像 RadialPoints，鞏固節奏）。
- **批 2 變換/modify**：OrientPoints / RandomizePoints / SetPointAttributes / FilterPoints（照 `transform/`+`modify/`）。
- **批 3 combine**：CombineBuffers（multi-input，串接多袋點）/ BlendPoints。
- **批 4 draw 招牌**：`DrawMeshAtPoints2`（每點長 mesh，需 mesh 資源型別 + instanced draw，最重）/ DrawLines。
- 後批：sim/SDF/field/image 取樣、進階 draw（tubes/ribbons/DOF）。

> **fan-out 安全前提（A.0 已備）**：危險的靜默撞（struct layout 分歧）由共享 `tixl_point.h` + metallib 編譯期 layout 證明擋住；機械撞（registry/CMake 行）由 operator 各自一檔（`point_ops_<name>.cpp`）+ workflow 整合步收斂。`check-arch` build target 每次抓 zone 違律。

---

## Self-Review（對 spec 覆蓋）
- spec「契約 = MTL::Buffer of SwPoint + operator 讀進寫出 + cook 拓撲走」= A0.1/A0.2 ✓
- spec「資料驅動登記、並行不撞」= 登記表 + operator 各一檔 + A.2 整合步 ✓
- spec「A.1 拆怪物成真鏈」= A1.1–A1.5（含有狀態 sim 折入）✓
- spec「每顆固定流程 crawl→port→golden→parity」= A.2 步 1–4 ✓
- spec「parity 三證（公式 golden/port 對齊/eye）」= A.2 步 3–4 ✓
- spec「commit 律法閘」= 每 task 末自檢+commit ✓
- 缺口誠實標記：A.1 bite-sized 待 A.0 介面定型後補（避免猜簽名）；有狀態 sim 進 dataflow 是本質難點，隔離在 A1.4；mesh 資源型別延到批 4。
- 過渡無回歸：A.0 不動 live loop（headless 證），A.1 切 loop 前先折 sim op（畫面不退化）。
