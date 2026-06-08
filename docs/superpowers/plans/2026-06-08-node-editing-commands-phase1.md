# 節點編輯命令層 — 階段 1 實作計畫（地基 + undo/redo + 搬遷現有操作）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

> ## ✅ 狀態：CLOSED（2026-06-08，柏為 7/7 親手驗收）
>
> Phase 1 全部完成並由柏為親手測過 Task 7 的七項驗收（加/刪/移可反悔、多步 undo、New 清空、存檔不受影響）。
> commit：`e9e9010`(核心) `5c07af6`(add/conn) + delete/move 命令 + `bbea009`(editor_ui 路由) `3b04d75`(Cmd+Z) `1d7f548`(pinNode DRY)。
> selftest：`--selftest-command` / `--selftest-command-bug` RED→GREEN。下面的 checkbox 全數完成（保留作紀錄）。
>
> ---

**Goal:** 在 app 區建一個最小的 Command / CommandStack / MacroCommand 命令層，把現有的「加節點 / 接線 / 刪節點線 / 移動」全部改成走命令，接上 Cmd+Z / Cmd+Shift+Z，讓這四類編輯都能反悔。

**Architecture:** 每個命令持有一個 `sw::Graph&` 參照（建構時注入），`doIt()` 改圖、`undo()` 還原。`CommandStack` 維護 undo/redo 兩個堆疊。命令在 `app/` 區（依賴 runtime/graph，符合 `ui → app → runtime`）。editor_ui 不再直接改 `g_graph`，改成 `push` 一個命令。命令邏輯由 `--selftest-command` 用獨立 local graph 驗 do/undo/redo；GUI 手勢接線由柏為親手測。

**Tech Stack:** C++17、imgui-node-editor、現有 `--selftest-*` CLI 自測慣例（見 `src/runtime/graph.cpp` 的 `runGraphRoundtripSelfTest`）、cmake build。

對應 spec：[2026-06-08-node-editing-commands-design.md](../specs/2026-06-08-node-editing-commands-design.md)（本計畫只做其中**階段 1**；階段 2–5 待地基落地後各自成計畫）。

---

## File Structure

| 檔 | 職責 | 動作 |
|---|---|---|
| `app/src/app/command.h` | `Command` 介面、`CommandStack`、`MacroCommand`、全域 `g_commands`、`runCommandSelfTest` 宣告 | 新增 |
| `app/src/app/command.cpp` | `CommandStack` / `MacroCommand` 實作、`g_commands` 定義 | 新增 |
| `app/src/app/graph_commands.h` | 五個具體命令的宣告 | 新增 |
| `app/src/app/graph_commands.cpp` | 五個命令實作 + `runCommandSelfTest` 實作 | 新增 |
| `app/src/ui/editor_ui.cpp` | 加/接/刪/移改走命令；綁 Cmd+Z/Cmd+Shift+Z | 修改 |
| `app/src/app/document.cpp` | doNew/doOpen 時 `g_commands.clear()` | 修改 |
| `app/src/main.cpp` | `--selftest-command` 派發 | 修改 |
| `app/CMakeLists.txt` | 加入兩個新 .cpp | 修改 |

---

## Task 1: 命令核心（Command / CommandStack / MacroCommand）

**Files:**
- Create: `app/src/app/command.h`
- Create: `app/src/app/command.cpp`
- Modify: `app/CMakeLists.txt`（在 `src/app/document.cpp` 那行附近加入新檔）

- [x] **Step 1: 寫 command.h**

```cpp
// app/command — 編輯命令層（undo/redo 的地基）。
// Zone: app. 命令持有 Graph& 參照，改的是 runtime 的圖（app -> runtime）。
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace sw {

struct Command {
  virtual ~Command() = default;
  virtual void doIt() = 0;            // 執行 / 重做
  virtual void undo() = 0;            // 退回
  virtual const char* name() const = 0;  // for status line / command log
};

// 把多個命令綁成一個原子動作：doIt 依序、undo 反序。給插入節點 / 複製貼上用。
class MacroCommand : public Command {
 public:
  explicit MacroCommand(std::string name) : name_(std::move(name)) {}
  void add(std::unique_ptr<Command> c) { children_.push_back(std::move(c)); }
  bool empty() const { return children_.empty(); }
  size_t size() const { return children_.size(); }
  void doIt() override;
  void undo() override;
  const char* name() const override { return name_.c_str(); }

 private:
  std::string name_;
  std::vector<std::unique_ptr<Command>> children_;
};

class CommandStack {
 public:
  void push(std::unique_ptr<Command> cmd);  // 執行 cmd，推入 undo 堆，清空 redo 堆
  void undo();                              // 退一格
  void redo();                              // 前進一格
  void clear();                             // New / Open 時呼叫
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  const char* lastUndoName() const;         // 最近一個可 undo 的命令名（空堆回 ""）

 private:
  std::vector<std::unique_ptr<Command>> undo_;
  std::vector<std::unique_ptr<Command>> redo_;
};

// 全域命令堆（editor_ui 與 document 共用）。
extern CommandStack g_commands;

// 隔離自測：用 local graph 跑 add/delete/move 的 do/undo/redo，斷言圖等同預期。
// injectBug=true 時刻意在 undo 後留下偏差，斷言必須 FAIL。
int runCommandSelfTest(bool injectBug);

}  // namespace sw
```

- [x] **Step 2: 寫 command.cpp**

```cpp
// app/command — CommandStack / MacroCommand 實作。
#include "app/command.h"

namespace sw {

CommandStack g_commands;

void MacroCommand::doIt() {
  for (auto& c : children_) c->doIt();
}
void MacroCommand::undo() {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo();
}

void CommandStack::push(std::unique_ptr<Command> cmd) {
  cmd->doIt();
  undo_.push_back(std::move(cmd));
  redo_.clear();
}
void CommandStack::undo() {
  if (undo_.empty()) return;
  std::unique_ptr<Command> c = std::move(undo_.back());
  undo_.pop_back();
  c->undo();
  redo_.push_back(std::move(c));
}
void CommandStack::redo() {
  if (redo_.empty()) return;
  std::unique_ptr<Command> c = std::move(redo_.back());
  redo_.pop_back();
  c->doIt();
  undo_.push_back(std::move(c));
}
void CommandStack::clear() {
  undo_.clear();
  redo_.clear();
}
const char* CommandStack::lastUndoName() const {
  return undo_.empty() ? "" : undo_.back()->name();
}

}  // namespace sw
```

- [x] **Step 3: CMake 加入新檔**

在 `app/CMakeLists.txt` 的 `add_executable(simple_world` 區塊，`src/app/document.cpp` 那行下面加兩行：

```cmake
  src/app/document.cpp
  src/app/command.cpp
  src/app/graph_commands.cpp
  src/app/menu.cpp
```

（`graph_commands.cpp` 在 Task 2 才會建檔；本步先把兩行都加進去，Task 2 結束前 build 才會成功，故本 Task 不單獨 build。）

- [x] **Step 4: Commit**

```bash
git add app/src/app/command.h app/src/app/command.cpp app/CMakeLists.txt
git commit -m "feat(app): command layer core (Command/CommandStack/MacroCommand)"
```

---

## Task 2: 加節點 / 接線命令（AddNodeCommand / AddConnectionCommand）

**Files:**
- Create: `app/src/app/graph_commands.h`
- Create: `app/src/app/graph_commands.cpp`

- [x] **Step 1: 寫 graph_commands.h**

```cpp
// app/graph_commands — 改 sw::Graph 的具體命令。每個命令持有 Graph& 參照。
// Zone: app. 依賴 runtime/graph。
#pragma once
#include <vector>

#include "app/command.h"
#include "runtime/graph.h"

namespace sw {

// 加一個已建好（含 id）的節點。undo 用 id 移除。
class AddNodeCommand : public Command {
 public:
  AddNodeCommand(Graph& g, Node node) : g_(g), node_(std::move(node)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Node"; }

 private:
  Graph& g_;
  Node node_;
};

// 加一條已建好（含 id/from/to）的連線。undo 用 id 移除。
class AddConnectionCommand : public Command {
 public:
  AddConnectionCommand(Graph& g, Connection c) : g_(g), conn_(c) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Connection"; }

 private:
  Graph& g_;
  Connection conn_;
};

}  // namespace sw
```

- [x] **Step 2: 寫 graph_commands.cpp（Add 命令 + 自測骨架）**

```cpp
// app/graph_commands — 具體命令實作 + 命令層自測。
#include "app/graph_commands.h"

#include <algorithm>
#include <cstdio>

namespace sw {

void AddNodeCommand::doIt() { g_.nodes.push_back(node_); }
void AddNodeCommand::undo() {
  auto& ns = g_.nodes;
  ns.erase(std::remove_if(ns.begin(), ns.end(),
                          [this](const Node& n) { return n.id == node_.id; }),
           ns.end());
}

void AddConnectionCommand::doIt() { g_.connections.push_back(conn_); }
void AddConnectionCommand::undo() {
  auto& cs = g_.connections;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [this](const Connection& c) { return c.id == conn_.id; }),
           cs.end());
}

// --- 自測（Task 2/3/4 逐步擴充）---
int runCommandSelfTest(bool injectBug) {
  Graph g = defaultParticleGraph();
  const size_t baseNodes = g.nodes.size();
  const size_t baseConns = g.connections.size();
  CommandStack stack;

  // Add node: push 後 +1，undo 後回 base，redo 後再 +1。
  Node n;
  n.id = g.nextId++;
  n.type = "RadialPoints";
  stack.push(std::make_unique<AddNodeCommand>(g, n));
  bool ok = (g.nodes.size() == baseNodes + 1);
  stack.undo();
  ok = ok && (g.nodes.size() == baseNodes);
  stack.redo();
  ok = ok && (g.nodes.size() == baseNodes + 1);
  // 留作後續 Task 擴充的尾巴：本步先把 add 收掉，回到乾淨狀態。
  stack.undo();
  ok = ok && (g.nodes.size() == baseNodes);

  if (injectBug) ok = !ok;  // 反向：注 bug 時必須回報失敗
  printf("[selftest-command] add baseNodes=%zu baseConns=%zu%s -> %s\n", baseNodes, baseConns,
         injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
```

- [x] **Step 3: 加 main.cpp 派發**

在 `app/src/main.cpp` 的 selftest 派發區（`--selftest-save-bug` 那段附近）加入，並確保檔頭已 `#include "app/command.h"`：

```cpp
    if (std::strcmp(argv[i], "--selftest-command") == 0)
      return sw::runCommandSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-command-bug") == 0)
      return sw::runCommandSelfTest(/*injectBug=*/true);
```

在 main.cpp 既有 include 區加（若尚無）：

```cpp
#include "app/command.h"
```

- [x] **Step 4: build + 跑自測（先紅再綠）**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-command
./build/simple_world --selftest-command-bug
```

Expected:
- `--selftest-command` → `[selftest-command] ... -> PASS`，exit 0
- `--selftest-command-bug` → `... -> FAIL`，exit 1（證明自測抓得到偏差）

- [x] **Step 5: Commit**

```bash
git add app/src/app/graph_commands.h app/src/app/graph_commands.cpp app/src/main.cpp
git commit -m "feat(app): AddNode/AddConnection commands + --selftest-command"
```

---

## Task 3: 刪除命令（DeleteConnectionsCommand / DeleteNodesCommand）

刪節點要連帶刪它的連線，且 undo 要把節點**和**那些連線都救回來，所以刪除命令必須在 `doIt()` 時把移除的東西快照下來。

**Files:**
- Modify: `app/src/app/graph_commands.h`
- Modify: `app/src/app/graph_commands.cpp`

- [x] **Step 1: graph_commands.h 加兩個刪除命令**

在 `AddConnectionCommand` 類別後、`}  // namespace sw` 前插入：

```cpp
// 刪 N 條連線。doIt 快照被刪的連線，undo 還原。
class DeleteConnectionsCommand : public Command {
 public:
  DeleteConnectionsCommand(Graph& g, std::vector<int> ids) : g_(g), ids_(std::move(ids)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Connections"; }

 private:
  Graph& g_;
  std::vector<int> ids_;                 // 要刪的連線 id
  std::vector<Connection> removed_;      // doIt 時快照，供 undo 還原
};

// 刪 N 個節點 + 它們的入射連線。doIt 快照節點與連線，undo 全部還原。
class DeleteNodesCommand : public Command {
 public:
  DeleteNodesCommand(Graph& g, std::vector<int> ids) : g_(g), ids_(std::move(ids)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Nodes"; }

 private:
  Graph& g_;
  std::vector<int> ids_;                 // 要刪的節點 id
  std::vector<Node> removedNodes_;       // doIt 時快照
  std::vector<Connection> removedConns_; // doIt 時快照（入射連線）
};
```

- [x] **Step 2: graph_commands.cpp 實作（放在 AddConnection 之後、`runCommandSelfTest` 之前）**

需要從某條連線推回它屬於哪個節點。`pinId(nodeId, port) = nodeId*100 + port + 1`，反推 `nodeId = (pin - 1) / 100`（與 editor_ui 的 `pinNodeId` 一致）。

```cpp
namespace {
int connNodeOf(int pin) { return (pin - 1) / 100; }  // 與 ui::pinNodeId 一致
}  // namespace

void DeleteConnectionsCommand::doIt() {
  removed_.clear();
  auto& cs = g_.connections;
  for (const Connection& c : cs)
    if (std::find(ids_.begin(), ids_.end(), c.id) != ids_.end()) removed_.push_back(c);
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [this](const Connection& c) {
                            return std::find(ids_.begin(), ids_.end(), c.id) != ids_.end();
                          }),
           cs.end());
}
void DeleteConnectionsCommand::undo() {
  for (const Connection& c : removed_) g_.connections.push_back(c);
}

void DeleteNodesCommand::doIt() {
  removedNodes_.clear();
  removedConns_.clear();
  auto inSet = [this](int nodeId) {
    return std::find(ids_.begin(), ids_.end(), nodeId) != ids_.end();
  };
  // 快照入射連線後刪。
  auto& cs = g_.connections;
  for (const Connection& c : cs)
    if (inSet(connNodeOf(c.fromPin)) || inSet(connNodeOf(c.toPin))) removedConns_.push_back(c);
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](const Connection& c) {
                            return inSet(connNodeOf(c.fromPin)) || inSet(connNodeOf(c.toPin));
                          }),
           cs.end());
  // 快照節點後刪。
  auto& ns = g_.nodes;
  for (const Node& n : ns)
    if (inSet(n.id)) removedNodes_.push_back(n);
  ns.erase(std::remove_if(ns.begin(), ns.end(), [&](const Node& n) { return inSet(n.id); }),
           ns.end());
}
void DeleteNodesCommand::undo() {
  for (const Node& n : removedNodes_) g_.nodes.push_back(n);
  for (const Connection& c : removedConns_) g_.connections.push_back(c);
}
```

- [x] **Step 3: 擴充 runCommandSelfTest（在現有 add 段之後、`if (injectBug)` 之前插入刪除驗證）**

```cpp
  // Delete a node that has incident connections: undo must restore node + conns.
  // defaultParticleGraph: ParticleSystem 連著 3 條線，刪它應移除 1 節點 + 3 連線。
  const Node* ps = g.firstOfType("ParticleSystem");
  ok = ok && (ps != nullptr);
  if (ps) {
    int psId = ps->id;
    stack.push(std::make_unique<DeleteNodesCommand>(g, std::vector<int>{psId}));
    ok = ok && (g.nodes.size() == baseNodes - 1) && (g.connections.size() == 0);
    stack.undo();
    ok = ok && (g.nodes.size() == baseNodes) && (g.connections.size() == baseConns);
  }

  // Delete a single connection: undo restores it.
  if (!g.connections.empty()) {
    int cid = g.connections.front().id;
    stack.push(std::make_unique<DeleteConnectionsCommand>(g, std::vector<int>{cid}));
    ok = ok && (g.connections.size() == baseConns - 1);
    stack.undo();
    ok = ok && (g.connections.size() == baseConns);
  }
```

> 註：`baseConns == 3` 是 `defaultParticleGraph` 的事實（見 `--selftest-graph` 輸出 `conns=3`）。若刪 ParticleSystem 後連線非 0，表示 `connNodeOf` 反推或 cascade 有誤。

- [x] **Step 4: build + 跑自測**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-command
./build/simple_world --selftest-command-bug
```

Expected: 前者 PASS exit 0，後者 FAIL exit 1。

- [x] **Step 5: Commit**

```bash
git add app/src/app/graph_commands.h app/src/app/graph_commands.cpp
git commit -m "feat(app): DeleteNodes/DeleteConnections commands with undo snapshot"
```

---

## Task 4: 移動命令（MoveNodesCommand）

拖動過程不記命令；放手時記一格，含每個被移動節點的舊座標與新座標。

**Files:**
- Modify: `app/src/app/graph_commands.h`
- Modify: `app/src/app/graph_commands.cpp`

- [x] **Step 1: graph_commands.h 加 MoveNodesCommand**

```cpp
// 移動 N 個節點。記錄每個節點的舊/新座標；undo 設回舊，doIt/redo 設新。
class MoveNodesCommand : public Command {
 public:
  struct Move { int id; float oldX, oldY, newX, newY; };
  MoveNodesCommand(Graph& g, std::vector<Move> moves) : g_(g), moves_(std::move(moves)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Move Nodes"; }

 private:
  Graph& g_;
  std::vector<Move> moves_;
};
```

- [x] **Step 2: graph_commands.cpp 實作（放在刪除命令之後）**

```cpp
void MoveNodesCommand::doIt() {
  for (const Move& m : moves_)
    if (Node* n = g_.node(m.id)) { n->x = m.newX; n->y = m.newY; }
}
void MoveNodesCommand::undo() {
  for (const Move& m : moves_)
    if (Node* n = g_.node(m.id)) { n->x = m.oldX; n->y = m.oldY; }
}
```

- [x] **Step 3: 擴充 runCommandSelfTest（在連線刪除驗證之後、`if (injectBug)` 之前）**

```cpp
  // Move a node: undo restores old coords, redo reapplies new.
  if (!g.nodes.empty()) {
    int mid = g.nodes.front().id;
    float ox = g.node(mid)->x, oy = g.node(mid)->y;
    std::vector<MoveNodesCommand::Move> mv{{mid, ox, oy, ox + 50.0f, oy + 30.0f}};
    stack.push(std::make_unique<MoveNodesCommand>(g, mv));
    ok = ok && (g.node(mid)->x == ox + 50.0f) && (g.node(mid)->y == oy + 30.0f);
    stack.undo();
    ok = ok && (g.node(mid)->x == ox) && (g.node(mid)->y == oy);
    stack.redo();
    ok = ok && (g.node(mid)->x == ox + 50.0f);
    stack.undo();  // 收回，保持乾淨
  }
```

- [x] **Step 4: build + 跑自測**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-command && ./build/simple_world --selftest-command-bug; echo "bug-exit=$?"
```

Expected: command PASS exit 0；command-bug FAIL exit 1。

- [x] **Step 5: 全套回歸自測（確認沒弄壞既有）**

```bash
cd app && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save
```

Expected: 兩者皆 PASS。

- [x] **Step 6: Commit**

```bash
git add app/src/app/graph_commands.h app/src/app/graph_commands.cpp
git commit -m "feat(app): MoveNodes command + selftest move/undo/redo"
```

---

## Task 5: editor_ui 改走命令（手勢路由）

把 editor_ui 裡四處直接改 `g_graph` 的地方，換成 push 命令。**這一 Task 沒有自動測試**——命令邏輯已由 Task 2–4 的 `--selftest-command` 證明；這裡只是把 UI 的手勢接到命令上，正確性由 Task 7 的柏為親手測驗收。

**Files:**
- Modify: `app/src/ui/editor_ui.cpp`

- [x] **Step 1: 加 include**

在 editor_ui.cpp 既有 include 區（`#include "runtime/graph.h"` 附近）加：

```cpp
#include "app/command.h"
#include "app/graph_commands.h"
```

- [x] **Step 2: addNode() 改走命令**

把 `app/src/ui/editor_ui.cpp` 的 `addNode()`（目前 L38-49 直接 `g_graph.nodes.push_back(n)`）改為：

```cpp
void addNode(const std::string& type) {
  sw::Node n;
  n.id = sw::doc::g_graph.nextId++;
  n.type = type;
  n.x = 120.0f;
  n.y = 120.0f;
  if (const sw::NodeSpec* s = sw::findSpec(type))
    for (const auto& p : s->params) n.params[p.id] = p.def;
  sw::g_commands.push(std::make_unique<sw::AddNodeCommand>(sw::doc::g_graph, n));
  sw::doc::g_relayout = true;
  sw::doc::g_status = "added " + type;
}
```

- [x] **Step 3: 接線改走命令**

把建立連線那段（目前 `sw::doc::g_graph.connections.push_back({...})`，約 L131）改為：

```cpp
        if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          sw::Connection c{sw::doc::g_graph.nextId++, from, to};
          sw::g_commands.push(std::make_unique<sw::AddConnectionCommand>(sw::doc::g_graph, c));
          sw::doc::g_status = "linked";
        }
```

- [x] **Step 4: 刪除改走命令（收集 id → push，cascade 去重）**

把 `if (ed::BeginDelete()) { ... }` 整段（目前 L142-169，在裡面直接 `erase`）改為「收集 id、Accept、組命令」。Backspace 路由那段（Task 修正已加，在 BeginDelete 之前）維持不動。改寫後：

```cpp
  // Delete links / nodes (select + Delete key, or Backspace routed above).
  if (ed::BeginDelete()) {
    std::vector<int> delLinks, delNodes;
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid))
      if (ed::AcceptDeletedItem()) delLinks.push_back((int)lid.Get());
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid))
      if (ed::AcceptDeletedItem()) delNodes.push_back((int)nid.Get());
    ed::EndDelete();

    // 入射於被刪節點的連線交給 DeleteNodesCommand 處理，從 delLinks 去重，
    // 否則同一條線會被刪兩次（undo 也會重複還原）。
    auto incidentToDeletedNode = [&](int linkId) {
      for (const sw::Connection& c : sw::doc::g_graph.connections)
        if (c.id == linkId) {
          int fn = (c.fromPin - 1) / 100, tn = (c.toPin - 1) / 100;
          return std::find(delNodes.begin(), delNodes.end(), fn) != delNodes.end() ||
                 std::find(delNodes.begin(), delNodes.end(), tn) != delNodes.end();
        }
      return false;
    };
    std::vector<int> standaloneLinks;
    for (int id : delLinks)
      if (!incidentToDeletedNode(id)) standaloneLinks.push_back(id);

    if (!delNodes.empty() || !standaloneLinks.empty()) {
      auto macro = std::make_unique<sw::MacroCommand>("Delete");
      if (!standaloneLinks.empty())
        macro->add(std::make_unique<sw::DeleteConnectionsCommand>(sw::doc::g_graph, standaloneLinks));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteNodesCommand>(sw::doc::g_graph, delNodes));
      sw::g_commands.push(std::move(macro));
      sw::doc::g_status = "deleted";
    }
  } else {
    ed::EndDelete();
  }
```

> 註：`ed::BeginDelete()` 回 false 時仍須呼叫 `ed::EndDelete()`（imgui-node-editor 規約）。原碼把 EndDelete 放在 if 內，這裡補上 else 分支的 EndDelete。動手前確認 `<algorithm>` 與 `<vector>` 已 include（editor_ui.cpp 開頭已有 `<algorithm>`；加 `<vector>`）。

- [x] **Step 5: 移動改走命令（放手才記一格）**

目前 L174-180 的 else 分支每幀把 editor 位置寫回 graph。改成：偵測「正在拖動」→ 拖動開始時記下舊座標；放手（不再拖動且確有位移）時 push 一個 `MoveNodesCommand`。在 `drawNodeCanvas()` 內、`namespace sw::ui` 範圍加一個 file-static 暫存，並改寫位置同步段：

在檔案 anonymous namespace（`namespace {` 內，`addNode` 附近）加：

```cpp
// 拖動暫存：node id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;
```

把位置同步段（目前 `} else { for (...) { node.x = p.x; node.y = p.y; } }`）改為：

```cpp
  if (sw::doc::g_relayout) {  // initial / after add / after load
    for (const sw::Node& node : sw::doc::g_graph.nodes)
      ed::SetNodePosition(node.id, ImVec2(node.x, node.y));
    sw::doc::g_relayout = false;
  } else {
    bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging) {
      // 拖動中：記下還沒記過的節點的起始座標，並即時把位置反映到 graph（畫面跟手）。
      for (sw::Node& node : sw::doc::g_graph.nodes) {
        ImVec2 p = ed::GetNodePosition(node.id);
        if (g_dragStart.find(node.id) == g_dragStart.end())
          g_dragStart[node.id] = ImVec2(node.x, node.y);
        node.x = p.x;
        node.y = p.y;
      }
    } else if (!g_dragStart.empty()) {
      // 放手：把真正有位移的節點組成一個 MoveNodesCommand。
      std::vector<sw::MoveNodesCommand::Move> moves;
      for (auto& kv : g_dragStart) {
        ImVec2 now = ed::GetNodePosition(kv.first);
        if (now.x != kv.second.x || now.y != kv.second.y)
          moves.push_back({kv.first, kv.second.x, kv.second.y, now.x, now.y});
      }
      g_dragStart.clear();
      if (!moves.empty()) {
        // 命令的 doIt 會再設一次新座標（冪等），先把 graph 設回舊座標避免雙重記錄混亂。
        for (auto& m : moves)
          if (sw::Node* n = sw::doc::g_graph.node(m.id)) { n->x = m.oldX; n->y = m.oldY; }
        sw::g_commands.push(std::make_unique<sw::MoveNodesCommand>(sw::doc::g_graph, moves));
      }
    } else {
      // 沒拖動：照常把 editor 位置同步回 graph（例如程式性移動）。
      for (sw::Node& node : sw::doc::g_graph.nodes) {
        ImVec2 p = ed::GetNodePosition(node.id);
        node.x = p.x;
        node.y = p.y;
      }
    }
  }
```

> 須在 editor_ui.cpp 開頭 include `<map>`（若尚無）。

- [x] **Step 6: build（無自動測；下一 Task 手測）**

```bash
cd app && cmake --build build -j
```

Expected: 編譯成功，無 error。

- [x] **Step 7: Commit**

```bash
git add app/src/ui/editor_ui.cpp
git commit -m "feat(ui): route add/link/delete/move gestures through command stack"
```

---

## Task 6: 綁 Cmd+Z / Cmd+Shift+Z + New/Open 清空堆疊

**Files:**
- Modify: `app/src/ui/editor_ui.cpp`
- Modify: `app/src/app/document.cpp`

- [x] **Step 1: 在 drawNodeCanvas 內接 undo/redo 快捷鍵**

在 editor_ui.cpp 的 Backspace 路由那段附近（同樣在 ed::Begin/End 之內、`ImGui::IsWindowFocused()` 為前提），加入：

```cpp
  // Undo / Redo: Cmd+Z / Cmd+Shift+Z (macOS).
  // IMPORTANT: imgui's ConfigMacOSXBehaviors (default on __APPLE__) swaps
  // Cmd->Ctrl inside AddKeyEvent, so a physical Cmd press lands in io.KeyCtrl,
  // NOT io.KeySuper. Detect Cmd via io.KeyCtrl. (Verified by --selftest-hand.)
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsWindowFocused() && io.KeyCtrl && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      if (io.KeyShift) sw::g_commands.redo();
      else             sw::g_commands.undo();
      sw::doc::g_status = io.KeyShift ? "redo" : "undo";
      sw::doc::g_relayout = true;  // canvas re-seeds node positions from the restored graph
    }
  }
```

> `io.KeyCtrl` = macOS 的 Cmd 鍵（因 ConfigMacOSXBehaviors 的 Cmd↔Ctrl 對調）。`IsKeyPressed(..., false)` 第二參 false = 不重複觸發（按一次退一格）。undo/redo 改的是 `g_graph`，故設 `g_relayout=true` 讓下一幀 canvas 依還原後的圖重置節點位置。
> 驗收用 hand：`keychord cmd z`（undo）、`keychord cmd+shift z`（redo）。

- [x] **Step 2: New / Open 清空命令堆**

在 `app/src/app/document.cpp` 檔頭加 `#include "app/command.h"`，並在 `doNew()` 與 `doOpen()` 成功改圖後各加一行 `sw::g_commands.clear();`：

`doNew()` 在 `g_graph = sw::defaultParticleGraph();` 之後加：

```cpp
  sw::g_commands.clear();
```

`doOpen()` 在 `g_graph = loaded;` 之後加：

```cpp
  sw::g_commands.clear();
```

- [x] **Step 3: build**

```bash
cd app && cmake --build build -j
```

Expected: 編譯成功。

- [x] **Step 4: Commit**

```bash
git add app/src/ui/editor_ui.cpp app/src/app/document.cpp
git commit -m "feat(ui): Cmd+Z/Cmd+Shift+Z undo-redo; clear stack on New/Open"
```

---

## Task 7: 柏為親手驗收（完成定義）

自測蓋不到 keypress 與畫面；這一關是「柏為親手測得到」。逐項做，任何一項失敗就退回對應 Task 用 systematic-debugging 找根因。

- [x] **驗收 1（加節點可反悔）**：Add Node 加一個 → Cmd+Z → 它消失 → Cmd+Shift+Z → 它回來。
- [x] **驗收 2（刪節點可反悔）**：選一個有連線的節點，按 delete → 節點與它的線一起消失 → Cmd+Z → 節點和那些線**全部**回來。
- [x] **驗收 3（刪線可反悔）**：選一條線 delete → 線消失 → Cmd+Z → 線回來。
- [x] **驗收 4（移動可反悔）**：拖動一個節點到別處放手 → Cmd+Z → 它跳回原位 → Cmd+Shift+Z → 回到拖動後的位置。
- [x] **驗收 5（多個 undo 連續退）**：連做加、移動、刪三個動作 → 連按三次 Cmd+Z → 逐步退回最初狀態。
- [x] **驗收 6（New 清空）**：做幾個編輯 → New（捨棄）→ Cmd+Z 不應把舊文件的東西叫回來（堆疊已清）。
- [x] **驗收 7（存檔不受影響）**：編輯後存檔、重開該檔，圖正確（沿用既有 save/load，undo 堆不入檔）。

全部通過 → 階段 1 完成，回 spec 開階段 2（reconnect）的計畫。

---

## Self-Review（對 spec 的覆蓋檢查）

- spec 階段 1「command + undo stack 地基」→ Task 1 ✓
- spec「把現有 add/delete/move 搬上 command」→ Task 5（route）+ Task 2/3/4（命令）✓
- spec 命令清單 AddNode/AddConnection/DeleteConnections/DeleteNodes/MoveNodes → Task 2/3/4 ✓（Reconnect/Paste 屬階段 2/5，不在本計畫）
- spec「移動：放手才記一格」→ Task 5 Step 5 ✓
- spec 鐵律 5「可單獨跑的隔離測試」→ `--selftest-command` / `--selftest-command-bug`（Task 2–4）✓
- spec ladder L5 圖不變式（無斷線、id 唯一）→ 刪除 cascade 去重（Task 5 Step 4）+ undo 還原原物件（id 不重用）✓
- spec ladder L6 save/load 不被弄壞 → Task 4 Step 5 回歸 ✓
- 型別一致性：`MoveNodesCommand::Move`、`g_commands`、`runCommandSelfTest` 在宣告與使用處名稱一致 ✓
- 架構憲法依賴方向：命令在 app 改 runtime（app→runtime）、editor_ui 在 ui 用 app（ui→app）、document(app) 用 command(app) ✓
