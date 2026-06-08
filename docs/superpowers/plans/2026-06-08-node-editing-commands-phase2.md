# 節點編輯命令層 — 階段 2 實作計畫（reconnect：replace-on-input）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

> ## ⏸ 狀態：擱置（程式碼已實作，GUI 驗收未完成）— 2026-06-08
>
> - **已完成**：`Graph::connectionToInput` + reconnect macro 邏輯 + `--selftest-command` reconnect 場景全綠；editor_ui 建線分支已接上（commit `f9b8c9f`、`bb22c71`）。程式碼乾淨、無臨時診斷殘留。
> - **未完成（擱置原因）**：Task 3 的 GUI 拖接點驗收做不下去——目前 runtime 節點種類太少、缺足夠「可互接、型別相容」的節點來實測「把新線拖到已連線 input 取代舊線」。節點豐富後再回來驗。
> - **待釐清的方向**：柏為實測時的直覺是「**拖節點**疊到另一個節點/線上」來改接，而非「**拖接點**拉線」（Phase 2 做的是後者）。前者比較接近 Phase 3（節點丟到線上插入）。「改接到底要哪種手勢」這題尚未拍板（詢問被擱置）。回來做時先定這個，再決定 Phase 2 的 reconnect 是否就是最終要的手勢。
> - **回來時的入口**：本檔 Task 3 驗收四項 + 上面方向題。
>
> ---

**Goal:** 讓使用者把一條新線接到一個**已連線的 input** 時，自動移除該 input 的舊線、加上新線，綁成一個可 Cmd+Z 反悔的「Reconnect」動作；同時關閉 Phase 1 留下的 input cardinality 漏洞（同一 input 不再能有兩條線）。

**Architecture:** 沿用階段 1 的命令層。reconnect 不需新命令類別——用既有 `MacroCommand{ DeleteConnectionsCommand(舊線), AddConnectionCommand(新線) }`（DRY，鐵律 7，重用已驗命令）。「該 input 是否已有線」是純圖邏輯，下放到 `runtime/graph` 成可測函式 `Graph::connectionToInput`。

**Tech Stack:** C++17、imgui-node-editor（沿用 BeginCreate/QueryNewLink/AcceptNewItem 流程，無新 library 機制）、`--selftest-command`。

對應 spec：[2026-06-08-node-editing-commands-design.md](../specs/2026-06-08-node-editing-commands-design.md) 階段 2。
**與 spec 的偏離（刻意）**：spec 列了獨立的 `ReconnectCommand`；本計畫改用 `MacroCommand{Delete,Add}` 重用既有命令，零新類別、行為與 undo 完全相同（鐵律 7 DRY）。

## 借自 TiXL 的行為（trace，非碼）
`external/tixl` `ConnectionMaker.cs`：完成連線到 input 時，`replacesConnection = (該 slot 已有連線)`；若是，動作 = AddConnection + DeleteConnection(舊)，綁成一個名為「Reconnect to X.input」的 undo 單位。我們的圖是 input 單連線（無 multi-input），故規則簡化為：input 已有一條線就取代它。

## imgui-node-editor 機制（已查證）
`CreateItemAction` 從任何被拖的 pin 起一條新 link，**不分** pin 有無舊線、不自動脫鉤。所以「拖端點」手勢 library 沒有；我們用既有的「從 output 拖到 input」建線流程，在 accept 時自行決定 add vs reconnect。QueryNewLink 對「目標 input 已連線」照樣回報——無需新機制。

---

## File Structure

| 檔 | 動作 | 職責 |
|---|---|---|
| `app/src/runtime/graph.h` | 修改 | `Graph` 加 `const Connection* connectionToInput(int inputPin) const;` 宣告 |
| `app/src/runtime/graph.cpp` | 修改 | 實作 + 擴充 `runGraphRoundtripSelfTest`？否 → reconnect 測試放 command selftest |
| `app/src/app/graph_commands.cpp` | 修改 | `runCommandSelfTest` 加 reconnect 場景斷言 |
| `app/src/ui/editor_ui.cpp` | 修改 | 建線 accept 分支：input 已連線 → Macro「Reconnect」；否則 AddConnection |

---

## Task 1: 圖層 `connectionToInput` + reconnect 邏輯自測

**Files:**
- Modify: `app/src/runtime/graph.h`
- Modify: `app/src/runtime/graph.cpp`
- Modify: `app/src/app/graph_commands.cpp`

- [ ] **Step 1: graph.h — Graph 加宣告**

在 `struct Graph` 內，`const Node* firstOfType(...) const;` 後面加：

```cpp
  // The connection feeding this input pin, or nullptr. Inputs are single-
  // cardinality, so there is at most one. Single source of truth for the
  // "is this input already wired?" question (reconnect / cardinality).
  const Connection* connectionToInput(int inputPin) const;
```

- [ ] **Step 2: graph.cpp — 實作**

在 `firstOfType` 實作附近加（檔內 `namespace sw {`）：

```cpp
const Connection* Graph::connectionToInput(int inputPin) const {
  for (const Connection& c : connections)
    if (c.toPin == inputPin) return &c;
  return nullptr;
}
```

- [ ] **Step 3: graph_commands.cpp — runCommandSelfTest 加 reconnect 場景**

在 `runCommandSelfTest` 內、現有 move 測試之後、`if (injectBug)` 之前插入。情境：找一個已連線的 input，模擬「把它改接到另一個 output」（= editor_ui 將做的 Macro），斷言：舊線消失、新線在、該 input 仍只有一條線；undo 後回到舊線、input 仍只有一條線。

```cpp
  // Reconnect (replace-on-input): an input that already has a connection,
  // re-wired to a different source, must end with exactly ONE connection to
  // that input (old removed, new added); undo restores the original.
  {
    const Connection* existing = g.connections.empty() ? nullptr : &g.connections.front();
    if (existing) {
      int inputPin = existing->toPin;
      int oldId = existing->id;
      int oldFrom = existing->fromPin;
      // a different source output: reuse another connection's fromPin if distinct,
      // else fabricate one on a different node via pinId of some other node.
      int newFrom = oldFrom;
      for (const Connection& c : g.connections)
        if (c.fromPin != oldFrom) { newFrom = c.fromPin; break; }
      ok = ok && (newFrom != oldFrom);  // default graph has >1 distinct source

      auto countTo = [&](int pin) {
        int n = 0;
        for (const Connection& c : g.connections) if (c.toPin == pin) ++n;
        return n;
      };
      ok = ok && (countTo(inputPin) == 1);

      auto macro = std::make_unique<MacroCommand>("Reconnect");
      macro->add(std::make_unique<DeleteConnectionsCommand>(g, std::vector<int>{oldId}));
      sw::Connection nc{g.nextId++, newFrom, inputPin};
      macro->add(std::make_unique<AddConnectionCommand>(g, nc));
      stack.push(std::move(macro));

      ok = ok && (countTo(inputPin) == 1);                 // still single-cardinality
      ok = ok && (g.connectionToInput(inputPin) != nullptr);
      ok = ok && (g.connectionToInput(inputPin)->fromPin == newFrom);
      stack.undo();
      ok = ok && (countTo(inputPin) == 1);
      ok = ok && (g.connectionToInput(inputPin)->fromPin == oldFrom);  // original restored
    }
  }
```

- [ ] **Step 4: build + RED→GREEN**

```bash
cd app && cmake --build build -j
./build/simple_world --selftest-command          # PASS, exit 0
./build/simple_world --selftest-command-bug      # FAIL, exit 1
./build/simple_world --selftest-graph            # PASS (regression)
./build/simple_world --selftest-save             # PASS (regression)
```

- [ ] **Step 5: Commit**

```bash
git add app/src/runtime/graph.h app/src/runtime/graph.cpp app/src/app/graph_commands.cpp
git commit -m "feat(runtime): Graph::connectionToInput + reconnect selftest (replace-on-input)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: editor_ui 建線分支 — reconnect vs add

**Files:**
- Modify: `app/src/ui/editor_ui.cpp`

- [ ] **Step 1: 改寫 AcceptNewItem 分支**

把現有建線的 accept 區塊（`if (ed::AcceptNewItem()) { ... AddConnectionCommand ... }`）改為：

```cpp
        if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          const sw::Connection* old = sw::doc::g_graph.connectionToInput(to);
          if (old && old->fromPin == from) {
            // already wired to this exact source — nothing to do
          } else if (old) {
            // reconnect: remove the input's old link, add the new one, as one undo unit
            auto macro = std::make_unique<sw::MacroCommand>("Reconnect");
            macro->add(std::make_unique<sw::DeleteConnectionsCommand>(
                sw::doc::g_graph, std::vector<int>{old->id}));
            sw::Connection c{sw::doc::g_graph.nextId++, from, to};
            macro->add(std::make_unique<sw::AddConnectionCommand>(sw::doc::g_graph, c));
            sw::g_commands.push(std::move(macro));
            sw::doc::g_status = "reconnected";
          } else {
            sw::Connection c{sw::doc::g_graph.nextId++, from, to};
            sw::g_commands.push(std::make_unique<sw::AddConnectionCommand>(sw::doc::g_graph, c));
            sw::doc::g_status = "linked";
          }
        }
```

> `MacroCommand` 已由 `app/command.h` 提供（editor_ui 已 include）。`connectionToInput` 來自 `runtime/graph.h`（已 include）。無需新 include。

- [ ] **Step 2: build**

```bash
cd app && cmake --build build -j   # 編譯成功
./build/simple_world --selftest-command && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save
```

Expected: 全 PASS。

- [ ] **Step 3: Commit**

```bash
git add app/src/ui/editor_ui.cpp
git commit -m "feat(ui): reconnect on drop to an already-wired input (replace, undoable)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: 親手 + 眼手驗收

自測蓋住 reconnect 的圖邏輯與 undo；GUI 的拖曳手勢由柏為親手測（眼/手 harness 無 pin 座標讀回，無法全自動驗拖線——誠實標明）。

- [ ] **驗收 1（reconnect 取代）**：一個 input 已有線（例如 ParticleSystem 的某 input）。從另一個合適的 output 拖一條線到那個 input → 舊線消失、新線接上（input 仍只有一條線）。
- [ ] **驗收 2（reconnect 可反悔）**：上一步後按 Cmd+Z → 回到舊接法（舊線回來、新線消失）；Cmd+Shift+Z → 再次變成新接法。
- [ ] **驗收 3（cardinality 漏洞已補）**：對一個已連線的 input 再拖一條線，**不會**出現兩條線進同一個 input（舊的被取代，不是並存）。
- [ ] **驗收 4（一般建線不受影響）**：對一個**空的** input 拖線 → 照常新增一條線（status 顯示 "linked"），Cmd+Z 可移除。

全過 → 階段 2 完成，回 spec 開階段 3（插入節點：TiXL 式線上浮圓點）。

---

## Self-Review（對 spec 階段 2 的覆蓋）
- spec「reconnect — 拖端點改接」→ 以 replace-on-input 達成（imgui-node-editor 無端點拖曳；replace 滿足改接需求）✓，偏離已載明。
- spec ladder L5 input cardinality → `connectionToInput` + replace 強制單連線，Task 1 Step 3 斷言 `countTo(inputPin)==1`✓。
- spec「走命令、可反悔」→ MacroCommand{Delete,Add}，selftest 驗 do/undo ✓。
- 鐵律 5 隔離測試 → `--selftest-command` 加 reconnect 場景（RED→GREEN）✓。
- 鐵律 7 DRY → 重用既有命令、`connectionToInput` 單一真相，無新命令類別 ✓。
- 型別一致：`connectionToInput`、`MacroCommand`、`DeleteConnectionsCommand`、`AddConnectionCommand` 名稱與階段 1 一致 ✓。
- 依賴方向：`connectionToInput` 在 runtime（最底層，純圖）；editor_ui(ui)→app→runtime 都朝下 ✓。
