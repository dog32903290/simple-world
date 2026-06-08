# 值脊椎（Value Spine）實作計畫

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development（建議）或 executing-plans。Steps 用 `- [ ]` 追蹤。

**Goal:** 讓 param 能被連線驅動：沒連線用 Inspector 常數、連了線用上游求值。最小可玩鏈 `Time→Sine→Remap→ParticleSystem.Speed`，柏為親眼看到粒子脈動。

**Architecture:** param 與 port 合一 = 帶預設值的 Float input port（schema 已釘死，見 spec）。求值用 pull 遞迴 `evalFloat`，到 input 時 `connectionToInput` 有連線→遞迴上游、無→存的常數。值只餵 `main.cpp` setter，**不碰 GPU buffer 流**。大量重用 Phase 1/2。

**Tech Stack:** C++17、`EvaluationContext`(`runtime/Particle.h`)、命令層(Phase1)、`connectionToInput`(Phase2)、`--selftest-*`。

對應 spec：[2026-06-08-value-spine-design.md](../specs/2026-06-08-value-spine-design.md)。**重用 vs 新碼**見 spec 對照表。

---

## File Structure

| 檔 | 動作 | 職責 |
|---|---|---|
| `app/src/runtime/graph.h` | 修改 | `PortSpec` 加 def/minV/maxV；退役 `ParamSpec`/`NodeSpec::params`；`NodeSpec` 加 `evaluate` fn-ptr；宣告 `evalFloat`/`evalParam` |
| `app/src/runtime/graph.cpp` | 修改 | registry 改寫（params→Float ports）+ 5 值節點 spec；evaluate 實作；`evalFloat`/`evalParam`；`runValueCookSelfTest` |
| `app/src/main.cpp` | 修改 | setter 改走 `evalParam`；`--selftest-valuecook` 派發 |
| `app/src/ui/editor_ui.cpp` | 修改 | Float 連線型別檢查；Inspector 走 Float ports + 被驅動變灰；常數編輯走 `SetInputValueCommand` |
| `app/src/app/graph_commands.h/.cpp` | 修改 | 新 `SetInputValueCommand`（改常數可 undo） |

---

## Task 1: schema 合一 — param → Float input port（存檔相容，零新行為）

風險最高的一刀（動 registry + Inspector + canvas）。目標：schema 換軌但**畫面行為不變**（除了 Float param 現在畫成 pin），存檔相容。

**Files:** `app/src/runtime/graph.h`, `app/src/runtime/graph.cpp`, `app/src/ui/editor_ui.cpp`

- [ ] **Step 1: graph.h — PortSpec 擴充 + 退役 ParamSpec**

```cpp
struct PortSpec {
  std::string id, name, dataType;   // "Points" | "ParticleForce" | "Float"
  bool isInput;
  float def = 0.0f, minV = 0.0f, maxV = 1.0f;  // 僅 Float input 用
};
struct NodeSpec {
  std::string type, title;
  std::vector<PortSpec> ports;
  // params 退役：原 param = dataType=="Float" && isInput 的 port，常數存 Node::params[port.id]
  float (*evaluate)(const float* in, int n, const struct EvaluationContext& ctx) = nullptr;  // Task 2 用
};
```
刪除 `struct ParamSpec`。`Node` 的 `std::map<std::string,float> params` **保留**（= Float input 的常數）。

- [ ] **Step 2: graph.cpp — registry 改寫（params 併入 ports 成 Float input）**

每個節點的 params 變成 ports 尾端的 Float input。id 保持與舊 param id 一致（存檔相容）。例：

```cpp
{"ParticleSystem", "ParticleSystem",
 {{"emit", "emit", "Points", true},
  {"forces", "forces", "ParticleForce", true},
  {"result", "result", "Points", false},
  {"Speed", "Speed", "Float", true, 1.0f, 0.0f, 3.0f},
  {"Drag", "Drag", "Float", true, 0.02f, 0.0f, 0.2f},
  {"OrientTowardsVelocity", "OrientTowardsVelocity", "Float", true, 0.15f, 0.0f, 1.0f}},
 nullptr},
```
RadialPoints 加 Count/Radius 兩個 Float input；TurbulenceForce 加 Amount/Frequency/Phase；DrawPoints 無 param 不變。

- [ ] **Step 3: editor_ui — addNode 與 Inspector 改讀 Float input ports**

`addNode()` 把 seed 常數的來源從 `spec->params` 改成 Float input ports：
```cpp
if (const sw::NodeSpec* s = sw::findSpec(type))
  for (const sw::PortSpec& p : s->ports)
    if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;
```
`drawInspector()` 把 `for (ParamSpec p : spec->params) SliderFloat(...)` 改成遍歷 Float input ports：
```cpp
bool any = false;
for (const sw::PortSpec& p : spec->ports) {
  if (!(p.isInput && p.dataType == "Float")) continue;
  any = true;
  float& v = sel->params[p.id];
  ImGui::SliderFloat(p.name.c_str(), &v, p.minV, p.maxV);
}
if (!any) ImGui::TextDisabled("(no editable parameters)");
```
（canvas 的 `drawNodeCanvas` 已遍歷 `spec->ports` 畫 pin → Float input 自動畫成 pin，免改。）

- [ ] **Step 4: build + 回歸（schema 換軌不能弄壞既有）**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-graph   # PASS：params map 不變、存檔相容
./build/simple_world --selftest-save    # PASS
./build/simple_world --selftest-command # PASS
./build/simple_world --selftest-flow    # PASS：param→runtime（setter 仍讀 params map）
```

- [ ] **Step 5: Commit**

```bash
git add app/src/runtime/graph.h app/src/runtime/graph.cpp app/src/ui/editor_ui.cpp
git commit -m "feat(runtime): unify param into Float input port (retire ParamSpec, schema spine)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: 求值引擎 — evaluate + evalFloat + 5 值節點（headless）

**Files:** `app/src/runtime/graph.h`, `app/src/runtime/graph.cpp`, `app/src/main.cpp`

- [ ] **Step 1: graph.h — 宣告 evalFloat / evalParam**

```cpp
// 沿連線往上游 pull 求值一個 output pin。帶 depth 防環，超限回 0。
float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth = 0);
// 解析 (node type, param port id) 的有效值：有連線→evalFloat 上游；無→存的常數 fallback。
float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                const EvaluationContext& ctx, float fallback);
int runValueCookSelfTest(bool injectBug);
```
graph.cpp 要 `#include "runtime/Particle.h"`（取 EvaluationContext）。

- [ ] **Step 2: graph.cpp — 5 值節點 spec + evaluate 函數**

registry 末端加 5 個值節點（evaluate 指到下面函數）：
```cpp
// 值節點 evaluate（純值，無 GPU）。in[] 依 Float input ports 順序。
namespace {
float evalTime(const float*, int, const EvaluationContext& ctx) { return ctx.time; }
float evalConst(const float* in, int n, const EvaluationContext&) { return n > 0 ? in[0] : 0.0f; }
float evalMultiply(const float* in, int n, const EvaluationContext&) { return n >= 2 ? in[0]*in[1] : 0.0f; }
float evalSine(const float* in, int n, const EvaluationContext&) { return n > 0 ? std::sin(in[0]) : 0.0f; }
float evalRemap(const float* in, int n, const EvaluationContext&) {
  // in: [x, inMin, inMax, outMin, outMax]? 簡化：x in -1..1 → outMin..outMax (in[1],in[2])
  if (n < 3) return 0.0f;
  float t = (in[0] + 1.0f) * 0.5f;          // -1..1 → 0..1
  return in[1] + (in[2] - in[1]) * t;       // → outMin..outMax
}
}  // namespace
```
registry 加（ports 順序 = evaluate 讀 in[] 的順序；output 放最後或標 isInput=false）：
```cpp
{"Time", "Time", {{"out", "out", "Float", false}}, evalTime},
{"Const", "Const", {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
                    {"out", "out", "Float", false}}, evalConst},
{"Multiply", "Multiply", {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
                          {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
                          {"out", "out", "Float", false}}, evalMultiply},
{"Sine", "Sine", {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
                  {"out", "out", "Float", false}}, evalSine},
{"Remap", "Remap", {{"x", "x", "Float", true, 0.0f, -1.0f, 1.0f},
                    {"outMin", "outMin", "Float", true, 0.0f, -10.0f, 10.0f},
                    {"outMax", "outMax", "Float", true, 1.0f, -10.0f, 10.0f},
                    {"out", "out", "Float", false}}, evalRemap},
```

- [ ] **Step 3: graph.cpp — evalFloat / evalParam 實作**

```cpp
float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth) {
  if (depth > 64) return 0.0f;                       // 防環
  int nodeId = pinNode(outPin);
  const Node* n = g.node(nodeId);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->type);
  if (!s || !s->evaluate) return 0.0f;
  // 收集 Float input 值（依 ports 順序，只取 Float input）。
  float in[8]; int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 8; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    int inPin = pinId(nodeId, (int)i);
    if (const Connection* c = g.connectionToInput(inPin))
      in[ni++] = evalFloat(g, c->fromPin, ctx, depth + 1);   // 連線→上游
    else {
      auto it = n->params.find(p.id);                         // 無→常數
      in[ni++] = (it != n->params.end()) ? it->second : p.def;
    }
  }
  return s->evaluate(in, ni, ctx);
}

float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                const EvaluationContext& ctx, float fallback) {
  const Node* n = g.firstOfType(type);
  if (!n) return fallback;
  const NodeSpec* s = findSpec(type);
  if (!s) return fallback;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float" && p.id == paramId)) continue;
    int inPin = pinId(n->id, (int)i);
    if (const Connection* c = g.connectionToInput(inPin))
      return evalFloat(g, c->fromPin, ctx, 0);               // 被驅動
    auto it = n->params.find(paramId);
    return (it != n->params.end()) ? it->second : p.def;     // 常數
  }
  return fallback;
}
```

- [ ] **Step 4: graph.cpp — runValueCookSelfTest（RED→GREEN）**

```cpp
int runValueCookSelfTest(bool injectBug) {
  Graph g;
  // Const(3) -> Multiply.a, Const(4) -> Multiply.b ; 求 Multiply.out == 12
  auto add = [&](const char* type) { Node n; n.id = g.nextId++; n.type = type;
    if (const NodeSpec* s = findSpec(type)) for (auto& p : s->ports)
      if (p.isInput && p.dataType=="Float") n.params[p.id]=p.def; g.nodes.push_back(n); return g.nodes.back().id; };
  int c3 = add("Const"); g.node(c3)->params["value"] = 3.0f;
  int c4 = add("Const"); g.node(c4)->params["value"] = 4.0f;
  int mul = add("Multiply");
  auto portIdx = [&](const char* type, const char* portId){ const NodeSpec* s=findSpec(type);
    for (size_t i=0;i<s->ports.size();++i) if (s->ports[i].id==portId) return (int)i; return -1; };
  int c3out = pinId(c3, portIdx("Const","out")), c4out = pinId(c4, portIdx("Const","out"));
  int mulA = pinId(mul, portIdx("Multiply","a")), mulB = pinId(mul, portIdx("Multiply","b"));
  g.connections.push_back({g.nextId++, c3out, mulA});
  g.connections.push_back({g.nextId++, c4out, mulB});
  EvaluationContext ctx{}; ctx.time = 2.0f;
  float mulOut = evalFloat(g, pinId(mul, portIdx("Multiply","out")), ctx, 0);
  bool ok = (mulOut == 12.0f);
  // Time -> Sine ; 求 Sine.out == sin(ctx.time)
  int t = add("Time"); int s = add("Sine");
  g.connections.push_back({g.nextId++, pinId(t, portIdx("Time","out")), pinId(s, portIdx("Sine","x"))});
  float sineOut = evalFloat(g, pinId(s, portIdx("Sine","out")), ctx, 0);
  ok = ok && (std::fabs(sineOut - std::sin(2.0f)) < 1e-5f);
  if (injectBug) ok = !ok;
  printf("[selftest-valuecook] mul=%.3f sine=%.3f -> %s\n", mulOut, sineOut, ok?"PASS":"FAIL");
  return ok ? 0 : 1;
}
```
main.cpp 加派發：
```cpp
if (std::strcmp(argv[i], "--selftest-valuecook") == 0) return sw::runValueCookSelfTest(false);
if (std::strcmp(argv[i], "--selftest-valuecook-bug") == 0) return sw::runValueCookSelfTest(true);
```

- [ ] **Step 5: build + RED→GREEN**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-valuecook       # PASS exit 0
./build/simple_world --selftest-valuecook-bug   # FAIL exit 1
./build/simple_world --selftest-graph && ./build/simple_world --selftest-command  # 回歸 PASS
```

- [ ] **Step 6: Commit**

```bash
git add app/src/runtime/graph.h app/src/runtime/graph.cpp app/src/main.cpp
git commit -m "feat(runtime): value cook engine (evalFloat pull evaluator + 5 value nodes)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: 接縫 — setter 改走 evalParam

**Files:** `app/src/main.cpp`

- [ ] **Step 1: 改 main.cpp:322-325 讀取點**

需要每幀的 EvaluationContext（main 已有 g_time/frame）。把死讀換成 evalParam：
```cpp
sw::EvaluationContext ctx{};
ctx.time = /* 現有 g_time 來源 */;        // 用 main 既有的時間變數
g_particles->setTurbulenceAmount(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Amount", ctx, 15.0f));
g_particles->setTurbulenceFrequency(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Frequency", ctx, 1.2f));
g_particles->setSpeed(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Speed", ctx, 1.0f));
g_particles->setDrag(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Drag", ctx, 0.02f));
```
> 動手前確認 main.cpp 既有的時間變數名（grep `g_time`/`frameIndex`/`time`），用它填 ctx；別新造時鐘。GPU buffer 流不動。

- [ ] **Step 2: build + 回歸**

```bash
cd app && cmake --build build -j && ./build/simple_world --selftest-flow   # PASS（沒連線時 = 常數，行為同舊）
```

- [ ] **Step 3: Commit**

```bash
git add app/src/main.cpp
git commit -m "feat(app): setters read params via evalParam (connection drives value at the seam)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: UI — Float 連線型別檢查 + Inspector 被驅動變灰 + SetInputValueCommand

**Files:** `app/src/app/graph_commands.h`, `app/src/app/graph_commands.cpp`, `app/src/ui/editor_ui.cpp`

- [ ] **Step 1: graph_commands — SetInputValueCommand**

graph_commands.h 加：
```cpp
// 改某節點某 Float input 的常數（Inspector 拉 slider）。可 undo。
class SetInputValueCommand : public Command {
 public:
  SetInputValueCommand(Graph& g, int nodeId, std::string portId, float oldV, float newV)
      : g_(g), nodeId_(nodeId), portId_(std::move(portId)), old_(oldV), new_(newV) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Set Value"; }
 private:
  Graph& g_; int nodeId_; std::string portId_; float old_, new_;
};
```
graph_commands.cpp：
```cpp
void SetInputValueCommand::doIt() { if (Node* n = g_.node(nodeId_)) n->params[portId_] = new_; }
void SetInputValueCommand::undo() { if (Node* n = g_.node(nodeId_)) n->params[portId_] = old_; }
```

- [ ] **Step 2: editor_ui — Float 連線型別檢查**

建線條件加 dataType 相容（Float↔Float、buffer↔同型）。在現有 `if (pa!=pb && ia!=ib && pinNodeId(pa)!=pinNodeId(pb))` 內，accept 前比對兩 pin 的 dataType：
```cpp
auto portTypeOf = [](int pin) -> std::string {
  const sw::Node* n = sw::doc::g_graph.node(pinNodeId(pin));
  if (!n) return "";
  const sw::NodeSpec* s = sw::findSpec(n->type);
  int idx = pinPortIndex(pin);
  return (s && idx < (int)s->ports.size()) ? s->ports[idx].dataType : "";
};
if (portTypeOf(pa) != portTypeOf(pb)) { ed::RejectNewItem(); }
else if (ed::AcceptNewItem()) { /* 既有 add/reconnect 分支 */ }
```

- [ ] **Step 3: editor_ui — Inspector 被驅動變灰**

drawInspector 的 Float input 迴圈，連線時不畫 slider、改顯示來源：
```cpp
int inPin = sw::pinId(sel->id, (int)i);
if (const sw::Connection* c = sw::doc::g_graph.connectionToInput(inPin)) {
  const sw::Node* src = sw::doc::g_graph.node(sw::pinNode(c->fromPin));
  ImGui::TextDisabled("%s ← %s", p.name.c_str(), src ? src->type.c_str() : "?");
} else {
  float before = sel->params[p.id];
  float v = before;
  if (ImGui::SliderFloat(p.name.c_str(), &v, p.minV, p.maxV) && v != before)
    sw::g_commands.push(std::make_unique<sw::SetInputValueCommand>(sw::doc::g_graph, sel->id, p.id, before, v));
}
```
> 註：slider 連續拖會每幀發命令；可接受（先求對，之後若 undo 太碎再合併）。或用 `IsItemDeactivatedAfterEdit` 只在放手記一格——若簡單就採後者。

- [ ] **Step 4: build**

```bash
cd app && cmake --build build -j && ./build/simple_world --selftest-command   # 回歸 PASS
```

- [ ] **Step 5: Commit**

```bash
git add app/src/app/graph_commands.h app/src/app/graph_commands.cpp app/src/ui/editor_ui.cpp
git commit -m "feat(ui): Float-typed links + Inspector greys driven params + SetInputValueCommand

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: 眼手 + 柏為親手驗收（完成定義）

- [ ] **眼（我先證）**：柏為啟動 app 後，我用 eye 截圖：接 `Time→Sine→Remap→Speed` 前後，`req_clean` 兩幀像素應不同（粒子脈動 = 畫面變化，比 reconnect 好驗）。
- [ ] **驗收 1（常數仍可調）**：選 ParticleSystem，Inspector 拉 Speed → 粒子變化；Cmd+Z 還原（SetInputValueCommand）。
- [ ] **驗收 2（值節點可加）**：Add menu 出現 Time/Const/Multiply/Sine/Remap，加得出來。
- [ ] **驗收 3（連線驅動）**：Const→Speed 接起來，Inspector 的 Speed 變灰標來源、值由 Const 決定；拔線回常數。
- [ ] **驗收 4（脈動）**：`Time→Sine→Remap(outMin,outMax)→Speed` 接成，**粒子隨時間脈動**（柏為親眼）。
- [ ] **驗收 5（型別擋）**：試著把 Float 接到 Points input → 被拒（型別檢查）。
- [ ] **驗收 6（存檔）**：接好值鏈 → Save → 重開 → Load，值鏈與常數回來。

全過 → 值脊椎封板。

---

## Self-Review（對 spec 覆蓋）
- 5 承重柱：Float 型別(T1)、evaluate(T2)、evalFloat(T2)、EvaluationContext(重用,T2/T3)、param=input-port(T1) ✓
- 5 起手節點 Time/Const/Multiply/Sine/Remap(T2) ✓
- 接縫 main.cpp:322-325 不碰 GPU buffer 流(T3) ✓
- 重用：connectionToInput(T2/T4)、Add/DeleteConnectionCommand(既有, 值線增刪)、MacroCommand/CommandStack、params map/toJson(T1)、EvaluationContext ✓
- 新碼：Float PortSpec/evaluate/evalFloat/evalParam/5 節點/SetInputValueCommand/UI ✓
- 鐵律 5 隔離測試 `--selftest-valuecook` RED→GREEN(T2) ✓；鐵律 7 evaluate 資料驅動(一表一登記) ✓
- 防環：evalFloat depth>64 回 0(T2) ✓
- 依賴方向：求值/型別/節點=runtime、SetInputValueCommand=app、UI=ui，`ui→app→runtime` ✓
- 型別一致：evalFloat/evalParam/SetInputValueCommand/connectionToInput/pinNode/pinId 跨 task 名稱一致 ✓
- 缺口誠實標記：slider 連續拖發命令的顆粒度（T4 Step3 註）；Count/Radius 成 Float input 但 runtime 未每幀讀（結構性，不影響脈動鏈）。
