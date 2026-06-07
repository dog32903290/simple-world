# 架構憲法 + main.cpp 拆解 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 simple_world 重整成五區架構、立下跨 session 的架構憲法、把 main.cpp 拆成 app 外殼 + document + editor_ui（行為不變），並把 app/ 完整納入版控後 push。

**Architecture:** 五區 `runtime / app / ui / platform / verify`，依賴單向（ui→app→runtime，app→platform，底層葉子不往上）。驗證系統（眼耳手）獨立成 verify/ 葉子區，業務碼只留一行 hook。憲法寫進 ARCHITECTURE.md，CLAUDE.md/AGENTS.md 雙入口保證每 session 必讀。

**Tech Stack:** C++17, metal-cpp, Dear ImGui, CMake, Objective-C++。

**參考設計：** `docs/superpowers/specs/2026-06-08-architecture-constitution-design.md`

---

## File Structure

| 檔案 | 責任 | 動作 |
|---|---|---|
| `ARCHITECTURE.md` | 架構憲法全文 | Create |
| `CLAUDE.md` | Claude Code 每 session 必讀入口 | Create |
| `AGENTS.md` | Codex/通用 agent 必讀入口（前置強制段） | Modify |
| `app/src/verify/eye/eye.{h,mm}` | eye 由 `app/src/eye/` 遷入 | Move |
| `app/src/app/document.{h,cpp}` | 文件狀態 + 儲存操作（從 main.cpp 抽出） | Create |
| `app/src/ui/editor_ui.{h,cpp}` | toolbar/canvas/inspector + pin + addNode（從 main.cpp 抽出） | Create |
| `app/src/main.cpp` | 瘦身成 app 外殼 | Modify |
| `app/CMakeLists.txt` | 更新 source 路徑與新增檔 | Modify |
| `.gitignore` | 補 `/app/.eye/` | Modify |

---

## Task 1: 立憲法三檔

先立法，後面照它做。

**Files:**
- Create: `ARCHITECTURE.md`
- Create: `CLAUDE.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: 建 ARCHITECTURE.md**

Create `ARCHITECTURE.md`（專案根）：

````markdown
# simple_world 架構憲法

> 動任何程式碼前先讀這份。這是這個工程的法，跨 session 有效。

## 五區（程式碼按性質分，每區內按子系統長）

| 區 | 性質 | 路徑 | 例 |
|---|---|---|---|
| runtime | 純計算，零 UI | `app/src/runtime/` | graph, particle_system, dispatch, radial, transform |
| app | 產品行為 | `app/src/app/` | document(儲存)… 未來 timeline/preset |
| ui | imgui 畫圖 | `app/src/ui/` | editor_ui(toolbar/canvas/inspector)… 未來面板 |
| platform | 原生 macOS 接口 | `app/src/platform/` | dialogs… 未來 bundle/檔案關聯 |
| verify | 眼耳手 · agent 程式驗證 | `app/src/verify/` | eye/ … 未來 ears/, hands/ |

入口 `app/src/main.cpp` 只放 app 外殼（NSApplication/MTKView/Renderer/menu/main），保持瘦。

## 依賴方向（單向，無環）

- 底層葉子 `runtime` / `platform` / `verify`：互不依賴，不往上依賴。
- 上層：`ui → app → runtime`，`app → platform`。
- `main.cpp` 依賴全部。
- 禁止：底層 include 上層；app↔ui 纏成環。

## verify 命脈紀律（對症上一個專案的死因）

verify 是被所有層觀察的橫切工具，但它是**葉子**。
**業務碼 / UI 碼裡對驗證系統只准留一行薄 hook**（例 `eye::recordItem("Save")`）。
驗證的**所有實作肉住在 `verify/`**。上層只 include verify 的薄介面 header，永不碰其 `.mm` 內部。
（上一個專案死在把驗證邏輯整坨寫進業務檔 → debug 時挑不出哪個是哪個。）

## 6 條鐵律

1. 程式碼分五區；新增檔先決定屬哪一區。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子不往上。
3. verify 是葉子：業務/UI 對它只留一行 hook，肉全在 `verify/`。
4. 一個檔一個職責；單檔 > ~400 行或一類 > ~12 個公開方法 = 警訊，要拆。
5. 每個子系統有可單獨跑的隔離測試（`--selftest-*` CLI 模式），不靠啟動整個 app 驗一個行為。
6. 動程式碼前過自檢三題。

## 自檢三題（每次新增檔/函式前）

1. 它屬於哪一區？
2. 它的依賴方向對嗎？（只能往下）
3. 它要 hook 驗證系統嗎？有的話業務碼只留一行，肉放 `verify/`。

## 建置

`third_party/`（metal-cpp / imgui / nativefiledialog-extended）是 vendored 且 gitignored。
clone 後需自行取得：
- metal-cpp / imgui / imgui-node-editor：見既有取得方式。
- NFDe：`cd app/third_party && git clone --depth 1 --branch v1.3.0 https://github.com/btzy/nativefiledialog-extended.git`

build：`cd app && cmake -S . -B build && cmake --build build -j`
測試：`./build/simple_world --selftest-graph && ./build/simple_world --selftest-save`
````

- [ ] **Step 2: 建 CLAUDE.md**

Create `CLAUDE.md`（專案根）：

```markdown
# simple_world — Claude 工作須知

**IMPORTANT：動任何程式碼前，先讀並遵守 `ARCHITECTURE.md`（這個工程的架構憲法）。**

核心鐵律（全文見 ARCHITECTURE.md）：

1. 程式碼分五區：`runtime`(純計算) / `app`(產品行為) / `ui`(imgui 畫圖) / `platform`(原生接口) / `verify`(眼耳手驗證)。新增檔先決定屬哪一區。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子(runtime/platform/verify)不往上依賴。
3. verify 是葉子：業務/UI 碼對驗證系統只准留一行 hook（例 `eye::recordItem(...)`），實作肉全在 `verify/`。絕不把驗證邏輯寫進業務檔。
4. 一個檔一個職責；單檔 > ~400 行或一類 > ~12 個公開方法 = 警訊，要拆。
5. 每個子系統要有可單獨跑的隔離測試（`--selftest-*` CLI 模式）。
6. 動程式碼前過自檢三題：①屬哪一區 ②依賴方向對嗎 ③要 hook 驗證嗎（只留一行，肉放 verify/）。
```

- [ ] **Step 3: 在 AGENTS.md 最前面插入同樣的強制段**

Read 現有 `AGENTS.md`，在**檔案最開頭**（第一行之前）插入：

```markdown
# IMPORTANT：架構憲法

動任何程式碼前，先讀並遵守 `ARCHITECTURE.md`。核心鐵律：

1. 程式碼分五區：`runtime`(純計算) / `app`(產品行為) / `ui`(imgui 畫圖) / `platform`(原生接口) / `verify`(眼耳手驗證)。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子不往上依賴。
3. verify 是葉子：業務/UI 碼對驗證系統只留一行 hook，實作肉全在 `verify/`。絕不把驗證邏輯寫進業務檔。
4. 一個檔一個職責；單檔 > ~400 行 = 警訊，要拆。
5. 每個子系統有可單獨跑的隔離測試（`--selftest-*`）。
6. 動程式碼前過自檢三題：①屬哪一區 ②依賴方向 ③要 hook 驗證嗎（一行 hook，肉放 verify/）。

---

```

- [ ] **Step 4: commit**

```bash
git add ARCHITECTURE.md CLAUDE.md AGENTS.md
git commit -m "docs: establish architecture constitution (five-zone + cross-session entry)"
```

---

## Task 2: eye → verify/eye 遷移

純搬家 + 改路徑，功能不變。

**Files:**
- Move: `app/src/eye/eye.h` → `app/src/verify/eye/eye.h`
- Move: `app/src/eye/eye.mm` → `app/src/verify/eye/eye.mm`
- Modify: `app/CMakeLists.txt`
- Modify: `app/src/main.cpp`（include 路徑）

- [ ] **Step 1: 搬移檔案**

Run:
```bash
cd "/Users/chenbaiwei/Desktop/vibe coding/simple_world/app/src" && mkdir -p verify && git mv eye verify/eye 2>/dev/null || (mkdir -p verify && mv eye verify/eye)
```
Expected: `app/src/verify/eye/eye.h` 與 `eye.mm` 存在，`app/src/eye/` 消失。

- [ ] **Step 2: 改 eye.mm 內部的自身 include（若有）**

Read `app/src/verify/eye/eye.mm` 開頭。若它 `#include "eye/eye.h"`，改成 `#include "verify/eye/eye.h"`。若是 `#include "eye.h"`（同目錄相對）則不必改。

- [ ] **Step 3: 改 main.cpp 的 include**

在 `app/src/main.cpp`，把 `#include "eye/eye.h"` 改成 `#include "verify/eye/eye.h"`。

- [ ] **Step 4: 更新 CMakeLists 的 source 路徑與 ARC 屬性**

在 `app/CMakeLists.txt`：
- 把 `set_source_files_properties(src/eye/eye.mm ...)` 改成 `set_source_files_properties(src/verify/eye/eye.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)`。
- 把 `add_executable` 裡的 `src/eye/eye.mm` 改成 `src/verify/eye/eye.mm`。

- [ ] **Step 5: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j 2>&1 | tail -5`
Expected: 編譯成功。

- [ ] **Step 6: selftest 回歸**

Run: `cd app && ./build/simple_world --selftest-eye && ./build/simple_world --selftest-graph`
Expected: 兩個都 PASS。

- [ ] **Step 7: commit**

```bash
git add -A app/src/verify app/CMakeLists.txt app/src/main.cpp
git commit -m "refactor: move eye/ into verify/ zone (agent-verification leaf)"
```

---

## Task 3: 抽出 app/document

把文件狀態 + 儲存操作從 main.cpp 搬到 `app/src/app/document.{h,cpp}`，包進 `namespace sw::doc`。行為不變。

**Files:**
- Create: `app/src/app/document.h`
- Create: `app/src/app/document.cpp`
- Modify: `app/src/main.cpp`
- Modify: `app/CMakeLists.txt`

- [ ] **Step 1: 建 document.h**

Create `app/src/app/document.h`：

```cpp
#pragma once
#include <string>
#include <AppKit/AppKit.hpp>
#include "runtime/graph.h"

namespace sw::doc {

// The open document's graph — single source of truth for the canvas.
extern Graph g_graph;
extern NS::Window* g_window;   // set by main at launch; used by updateWindowTitle()
extern bool g_relayout;        // load/new/add asks the editor to re-layout positions
extern std::string g_status;   // status-line text shown by the toolbar

bool isDirty();                 // toJson(g_graph) != saved snapshot
bool doSave();                  // overwrite current; falls back to Save As; false if canceled
bool doSaveAs();                // always prompt; true if written
void doOpen();                  // unsaved-guard -> Finder -> temp-load -> swap on success
void doNew();                   // unsaved-guard -> reset to default graph
bool confirmDiscardIfDirty();   // false == user canceled (caller aborts)
void updateWindowTitle();       // filename + dirty • ; no-op when unchanged (uses g_window)
void initSnapshot();            // call at startup: snapshot := toJson(default graph)

}  // namespace sw::doc
```

- [ ] **Step 2: 建 document.cpp，從 main.cpp 搬入實作**

Create `app/src/app/document.cpp`：

```cpp
#include "app/document.h"

#include "platform/dialogs.h"
#include "runtime/graph.h"
#include <nfd.hpp>

namespace sw::doc {

Graph g_graph = sw::defaultParticleGraph();
NS::Window* g_window = nullptr;
bool g_relayout = true;
std::string g_status = "ready";

namespace {
std::string g_documentPath;    // empty == never saved
std::string g_savedSnapshot;   // toJson() at last save/open/new
std::string g_lastTitle;       // cache so we only setTitle when it changes
}  // namespace

// (move verbatim from main.cpp, wrapped in this namespace:)
//   isDirty, doSave (forward-declared before confirmDiscardIfDirty), doSaveAs,
//   doOpen, doNew, confirmDiscardIfDirty, updateWindowTitle
// Names resolve within sw::doc: g_graph / g_relayout / g_status / g_documentPath /
//   g_savedSnapshot / g_window / g_lastTitle are all in scope unchanged.

void initSnapshot() { g_savedSnapshot = sw::toJson(g_graph); }

}  // namespace sw::doc
```

實作搬移細則（依賴方向：app 只依賴 runtime + platform，**不依賴 ui**）：
- 把 main.cpp 的 `isDirty / confirmDiscardIfDirty / doSaveAs / doSave / doOpen / doNew / updateWindowTitle` 函式**整段 verbatim 移過來**，放進 `namespace sw::doc`。
- 這些 body 用到的 `g_graph / g_relayout / g_status / g_documentPath / g_savedSnapshot / g_window / g_lastTitle` 全在 `sw::doc` 內同名解析，**不需改名、不需 include ui**。
- `g_relayout` 與 `g_status` 是 document 的輸出狀態（載入後要重排、狀態列字串），由 ui 端讀取 `sw::doc::g_relayout` / `sw::doc::g_status`——依賴方向 ui → app，正確。
- `g_documentPath` / `g_savedSnapshot` / `g_lastTitle` 是 document 私有，放匿名 namespace 不外露。

- [ ] **Step 3: 從 main.cpp 移除已搬走的部分**

在 `app/src/main.cpp`：
- 刪除 `g_graph`、`g_documentPath`、`g_savedSnapshot`、`g_window`、`g_lastTitle`、`g_relayout`、`g_status` 的定義（移到 document）。
- 刪除 `isDirty / confirmDiscardIfDirty / doSaveAs / doSave / doOpen / doNew / updateWindowTitle` 定義（已搬走）。
- 加 `#include "app/document.h"`。

- [ ] **Step 4: 改 main.cpp 的呼叫點為 sw::doc::**

- menu callbacks：`doNew()` → `sw::doc::doNew()`，`doOpen()`/`doSave()`/`doSaveAs()` 同理。
- close guard lambda：`confirmDiscardIfDirty()` → `sw::doc::confirmDiscardIfDirty()`。
- `applicationDidFinishLaunching`：`g_window = _pWindow;` → `sw::doc::g_window = _pWindow;`；`g_savedSnapshot = ...` 那行 → `sw::doc::initSnapshot();`；`sw::installCloseGuard(..., []{ return sw::doc::confirmDiscardIfDirty(); });`。
- render loop 的 `updateWindowTitle()` → `sw::doc::updateWindowTitle()`（暫時；Task 4 後仍由 main 呼叫）。
- Renderer cook 用的 `g_graph.param(...)` → `sw::doc::g_graph.param(...)`。

> 註：此時 main.cpp 仍含 drawToolbar/drawNodeCanvas/drawInspector（它們用 g_graph 等），暫時把它們裡的 `g_graph` 改為 `sw::doc::g_graph`、`g_relayout`→`sw::doc::g_relayout`、`g_status`→`sw::doc::g_status`，以維持可編譯。Task 4 再把它們整段搬走。

- [ ] **Step 5: CMakeLists 加入 document.cpp**

在 `app/CMakeLists.txt` 的 `add_executable` 裡，`src/main.cpp` 之後加 `src/app/document.cpp`。

- [ ] **Step 6: build + selftest**

Run: `cd app && cmake -S . -B build && cmake --build build -j 2>&1 | tail -8 && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save`
Expected: 編譯成功；兩個 selftest PASS。

- [ ] **Step 7: commit**

```bash
git add app/src/app/document.h app/src/app/document.cpp app/src/main.cpp app/CMakeLists.txt
git commit -m "refactor: extract app/document (file state + save/load ops) from main.cpp"
```

---

## Task 4: 抽出 ui/editor_ui

把畫圖 + pin helpers + addNode 從 main.cpp 搬到 `app/src/ui/editor_ui.{h,cpp}`，包進 `namespace sw::ui`。

**Files:**
- Create: `app/src/ui/editor_ui.h`
- Create: `app/src/ui/editor_ui.cpp`
- Modify: `app/src/main.cpp`
- Modify: `app/CMakeLists.txt`

- [ ] **Step 1: 建 editor_ui.h**

Create `app/src/ui/editor_ui.h`：

```cpp
#pragma once
#include "imgui_node_editor.h"

namespace sw::ui {

// Node-editor context + selection live here (owned by the editor UI).
extern ax::NodeEditor::EditorContext* g_NodeEditor;
extern int g_selectedNode;

void drawToolbar();      // New/Open/Save/Save As + Add Node (calls sw::doc ops)
void drawNodeCanvas();   // the main node graph workspace
void drawInspector();    // selected node's parameters + FPS

}  // namespace sw::ui
```

- [ ] **Step 2: 建 editor_ui.cpp，從 main.cpp 搬入**

Create `app/src/ui/editor_ui.cpp`：

```cpp
#include "ui/editor_ui.h"

#include "imgui.h"
#include "app/document.h"
#include "runtime/graph.h"
#include "verify/eye/eye.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;

// (move verbatim from main.cpp, wrapped in this namespace:)
//   pinNodeId, pinPortIndex, pinIsInput, addNode,
//   drawToolbar, drawNodeCanvas, drawInspector
// Adjust inside the moved bodies:
//   g_graph     -> sw::doc::g_graph
//   g_relayout  -> sw::doc::g_relayout
//   g_status    -> sw::doc::g_status
//   doNew/doOpen/doSave/doSaveAs -> sw::doc::doNew/... (toolbar buttons)
//   g_particles target Image: declare `extern sw::ParticleSystem* g_particles;`
//     at top of this file (it is owned by main/Renderer) to keep the DrawPoints
//     preview working.

}  // namespace sw::ui
```

搬移細則：
- `drawNodeCanvas` 裡的 DrawPoints 預覽用到 `g_particles`（main/Renderer 擁有）。在 editor_ui.cpp 頂部加 `extern sw::ParticleSystem* g_particles;` 並 `#include "runtime/particle_system.h"`。這是 ui 讀 main 擁有的 render 物件——可接受（ui 顯示 runtime 輸出）。
- eye③ 的 `sw::eye::recordItem(...)` 呼叫保持原樣（include 路徑已是 `verify/eye/eye.h`）。
- `beginWidgetFrame` 等 eye 呼叫若在 drawToolbar 內也一併隨之搬移。

- [ ] **Step 3: 從 main.cpp 移除已搬走的部分 + 改 include/呼叫**

在 `app/src/main.cpp`：
- 刪除 `g_NodeEditor`、`g_selectedNode` 定義（移到 ui）。
- 刪除 `pinNodeId/pinPortIndex/pinIsInput/addNode/drawToolbar/drawNodeCanvas/drawInspector` 定義（已搬走）。
- 加 `#include "ui/editor_ui.h"`。
- render loop：`drawToolbar()` → `sw::ui::drawToolbar()`；`drawNodeCanvas()` → `sw::ui::drawNodeCanvas()`；`drawInspector()` → `sw::ui::drawInspector()`。
- `applicationDidFinishLaunching` 裡 `g_NodeEditor = ed::CreateEditor(&cfg);` → `sw::ui::g_NodeEditor = ed::CreateEditor(&cfg);`；`AppDelegate::~AppDelegate` 裡 `if (g_NodeEditor) ed::DestroyEditor(g_NodeEditor);` → `sw::ui::g_NodeEditor`。
- `g_particles`：把它的定義從 main.cpp 的**匿名 namespace 移到檔案層級全域**（`sw::ParticleSystem* g_particles = nullptr;` 放在 `namespace {` 之外，例如緊接 includes 後），讓它有 external linkage、editor_ui.cpp 才 extern 得到。其餘 `g_shaderLib / g_frameIndex / g_time` 只有 main 用，維持匿名 namespace。editor_ui.cpp 頂部（namespace 外）放 `extern sw::ParticleSystem* g_particles;`，drawNodeCanvas 內以 `::g_particles` 取用。

- [ ] **Step 4: CMakeLists 加入 editor_ui.cpp**

在 `add_executable` 裡 `src/app/document.cpp` 之後加 `src/ui/editor_ui.cpp`。

- [ ] **Step 5: build + selftest**

Run: `cd app && cmake -S . -B build && cmake --build build -j 2>&1 | tail -8 && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save && ./build/simple_world --selftest-eye`
Expected: 編譯成功；三個 selftest 全 PASS。

- [ ] **Step 6: 確認 main.cpp 已瘦身**

Run: `wc -l app/src/main.cpp`
Expected: 約 250 行（從 650 降下來）。

- [ ] **Step 7: commit**

```bash
git add app/src/ui/editor_ui.h app/src/ui/editor_ui.cpp app/src/main.cpp app/CMakeLists.txt
git commit -m "refactor: extract ui/editor_ui (toolbar/canvas/inspector) from main.cpp"
```

---

## Task 5: 行為驗收（柏為親手）

- [ ] **Step 1: 全 selftest 回歸**

Run:
```bash
cd app && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save && ./build/simple_world --selftest-eye && ./build/simple_world --selftest-dispatch && ./build/simple_world --selftest-flow && ./build/simple_world --selftest-draw && echo "ALL PASS"
```
Expected: 全部 PASS，印出 `ALL PASS`。

- [ ] **Step 2: 柏為親手測 app — 行為必須跟拆解前完全一樣**

Run: `cd app && ./build/simple_world`
確認（拆解不該改變任何行為）：
1. 儲存 / 開檔 / 新建 / Save As / ⌘S / ⌘O / ⌘N 全照舊。
2. 未存星號、紅燈攔截、三鈕彈窗照舊。
3. 加節點 / 拉連線 / 刪除 / Inspector 調參數照舊。
4. eye 截圖（若有觸發機制）照舊運作。
Expected: 一切跟拆解前一致；沒有任何行為改變。

---

## Task 6: 完整納入版控

**Files:**
- Modify: `.gitignore`
- 大量 `git add app/`

- [ ] **Step 1: .gitignore 補 eye 輸出目錄**

在 `.gitignore` 的 `/app/build/` 那行之後加：

```
/app/.eye/
```

- [ ] **Step 2: 完整 add app/ 原始碼（third_party/build 已被 ignore）**

Run:
```bash
cd "/Users/chenbaiwei/Desktop/vibe coding/simple_world" && git add app/ .gitignore && git status --short | grep -E '^A' | wc -l && echo "files staged" && git status --short | grep -vE '^A|^\?\?' | head
```
Expected: 一批 app/ 原始碼（runtime/、verify/、platform/、app/、ui/、shaders/、metal_impl.cpp、run-dev.sh）被 staged；third_party/ 與 build/ 不在其中。

- [ ] **Step 3: 確認沒有誤納產物**

Run: `git status --short | grep -iE 'build/|third_party/|\.eye/|\.metallib|\.air|\.o$'`
Expected: 無輸出（產物都被 ignore，沒誤納）。

- [ ] **Step 4: commit**

```bash
git commit -m "chore: bring full app/ source tree under version control; ignore /app/.eye"
```

---

## Task 7: push

- [ ] **Step 1: 確認 clone 可重建（在乾淨副本驗證）**

Run:
```bash
cd /tmp && rm -rf sw_clone_check && git clone "/Users/chenbaiwei/Desktop/vibe coding/simple_world" sw_clone_check 2>&1 | tail -2 && ls sw_clone_check/app/src/runtime/graph.cpp sw_clone_check/app/src/verify/eye/eye.mm sw_clone_check/ARCHITECTURE.md && echo "key files present in clone"
```
Expected: 關鍵檔在 clone 中存在（證明它們真的進了版控）。third_party/ 不在 clone 中是預期的（vendored）。

- [ ] **Step 2: push 當前分支到 origin**

Run: `cd "/Users/chenbaiwei/Desktop/vibe coding/simple_world" && git push -u origin codex/js-to-cpp-contract-migration 2>&1 | tail -5`
Expected: push 成功到 `https://github.com/dog32903290/simple-world.git`。

- [ ] **Step 3: 回報 push 結果與遠端分支連結**

確認 push 成功，把遠端分支 URL 回報給柏為。

---

## 完成定義

- 五區目錄成形；`verify/eye/` 就位；`main.cpp` ≈ 250 行。
- `ARCHITECTURE.md` + `CLAUDE.md` + `AGENTS.md` 三檔到位。
- 全 `--selftest-*` 綠；柏為親手測 app 行為與拆解前一致。
- `/tmp` clone 含全部 app/ 原始碼（third_party 除外）。
- push 到 origin 完成。
