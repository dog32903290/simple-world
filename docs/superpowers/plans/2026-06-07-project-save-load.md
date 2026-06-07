# 專案儲存 / 載入 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 讓 simple_world 能用 macOS 原生 Finder 對 `.swproj` 專案檔做 New / Open / Save / Save As，並在未存時攔截以免丟失工作。

**Architecture:** 序列化沿用既有的 `sw::toJson`/`fromJson`（crude_json）。Finder 對話框用 nativefiledialog-extended（真 NSOpenPanel/NSSavePanel）。「要存嗎」三鈕彈窗與錯誤提示用一個極小的 Objective-C++ `NSAlert`。文件狀態（當前檔路徑 + 已存快照）放在 `main.cpp` 全域，四個操作函式為單一入口，原生 File 選單與 ImGui 工具列都呼叫它們。dirty 由「當前圖 toJson vs 已存快照」比對得出。

**Tech Stack:** C++17, metal-cpp / AppKit.hpp, Dear ImGui, imgui-node-editor (crude_json), nativefiledialog-extended (CMake), Objective-C++ (NSAlert), CMake。

**參考設計：** `docs/superpowers/specs/2026-06-07-project-save-load-design.md`

---

## File Structure

| 檔案 | 責任 | 動作 |
|---|---|---|
| `app/src/runtime/graph.h` | 宣告 `saveGraphToFile` / `loadGraphFromFile` / `runSaveLoadSelfTest` | Modify |
| `app/src/runtime/graph.cpp` | 實作上述檔案 I/O + self-test | Modify |
| `app/src/platform/dialogs.h` | 宣告 `UnsavedChoice` / `askUnsaved` / `showError` | Create |
| `app/src/platform/dialogs.mm` | NSAlert 實作（三鈕 + 單鈕錯誤） | Create |
| `app/third_party/nativefiledialog-extended/` | Finder 對話框輪子 | Create（git clone） |
| `app/CMakeLists.txt` | 引入 NFDe、加入 dialogs.mm、UniformTypeIdentifiers framework | Modify |
| `app/src/main.cpp` | 文件狀態、四操作、File 選單、標題列、關閉攔截、工具列轉呼叫 | Modify |

序列化 schema（`toJson`/`fromJson`）不變。

---

## Task 1: 檔案 I/O 輔助函式 + self-test（純邏輯，可測）

把「圖 ↔ 檔案」抽成可單獨自測的函式，沿用既有 `--selftest-graph` 的 self-test 慣例。

**Files:**
- Modify: `app/src/runtime/graph.h:57-63`
- Modify: `app/src/runtime/graph.cpp`（檔尾附近，`runGraphRoundtripSelfTest` 旁）
- Modify: `app/src/main.cpp:378-397`（arg loop 加 `--selftest-save`）

- [ ] **Step 1: 在 graph.h 宣告檔案 I/O + self-test**

於 `app/src/runtime/graph.h` 第 59 行（`bool fromJson(...)` 之後）插入：

```cpp
// Disk I/O for project files (.swproj). saveGraphToFile writes toJson(g) to path;
// loadGraphFromFile reads a file and fromJson()s it. Both return false on failure
// (unwritable path / missing file / malformed json) — callers must not mutate
// their live graph until loadGraphFromFile returns true.
bool saveGraphToFile(const std::string& path, const Graph& g);
bool loadGraphFromFile(const std::string& path, Graph& out);

// L-save proof: default graph -> file -> graph, assert identical; AND a malformed
// file must load to false. injectBug perturbs the reloaded graph so the roundtrip
// comparison must FAIL.
int runSaveLoadSelfTest(bool injectBug);
```

- [ ] **Step 2: 在 graph.cpp 實作 I/O + self-test**

於 `app/src/runtime/graph.cpp` 檔尾 `}  // namespace sw` 之前插入。先確認檔案頂部已 `#include <fstream>` 與 `#include <iterator>`；若無則加上。

```cpp
bool saveGraphToFile(const std::string& path, const Graph& g) {
  std::ofstream f(path);
  if (!f) return false;
  f << toJson(g);
  return f.good();
}

bool loadGraphFromFile(const std::string& path, Graph& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return fromJson(json, out);
}

int runSaveLoadSelfTest(bool injectBug) {
  const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
                           + "/sw_saveload_selftest.swproj";
  Graph g = defaultParticleGraph();
  if (!saveGraphToFile(path, g)) {
    printf("[selftest-save] write FAILED -> %s\n", path.c_str());
    return 1;
  }
  Graph reloaded;
  if (!loadGraphFromFile(path, reloaded)) {
    printf("[selftest-save] reload FAILED <- %s\n", path.c_str());
    return 1;
  }
  if (injectBug) reloaded.nextId += 1;  // perturb so the roundtrip must mismatch
  bool roundtrip = (toJson(g) == toJson(reloaded));

  // A malformed file MUST load to false (selection-of-wrong-file safety).
  { std::ofstream bad(path); bad << "{ this is not valid json "; }
  Graph dummy;
  bool rejectedBad = !loadGraphFromFile(path, dummy);

  bool pass = roundtrip && rejectedBad;
  printf("[selftest-save] roundtrip=%s rejectBad=%s -> %s\n",
         roundtrip ? "ok" : "MISMATCH", rejectedBad ? "ok" : "ACCEPTED-BAD",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
```

確認頂部 includes（`graph.cpp` 第 1-10 行附近）含 `<cstdio>` 與 `<cstdlib>`；若無則加上。

- [ ] **Step 3: 在 main.cpp arg loop 接上 self-test flag**

於 `app/src/main.cpp:386`（`--selftest-graph-bug` 區塊之後）插入：

```cpp
    if (std::strcmp(argv[i], "--selftest-save") == 0)
      return sw::runSaveLoadSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-save-bug") == 0)
      return sw::runSaveLoadSelfTest(/*injectBug=*/true);
```

- [ ] **Step 4: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功，無錯誤。

- [ ] **Step 5: 跑正常 self-test，要 PASS**

Run: `cd app && ./build/simple_world --selftest-save`
Expected: `[selftest-save] roundtrip=ok rejectBad=ok -> PASS`，exit code 0。

- [ ] **Step 6: 跑 bug 版，要 FAIL（證明測試會抓錯）**

Run: `cd app && ./build/simple_world --selftest-save-bug; echo "exit=$?"`
Expected: `[selftest-save] roundtrip=MISMATCH ... -> FAIL`，`exit=1`。

- [ ] **Step 7: commit**

```bash
git add app/src/runtime/graph.h app/src/runtime/graph.cpp app/src/main.cpp
git commit -m "feat: file I/O helpers for .swproj with save/load roundtrip self-test"
```

---

## Task 2: 引入 nativefiledialog-extended + CMake 接線 + spike 驗證 Finder

先把輪子裝起來、確認在這個 metal-cpp app 裡真的能跳出 Finder，再往上接邏輯。

**Files:**
- Create: `app/third_party/nativefiledialog-extended/`（git clone）
- Modify: `app/CMakeLists.txt`
- Modify: `app/src/main.cpp:170-188`（drawToolbar 暫時加 spike 按鈕）

- [ ] **Step 1: clone NFDe（釘住版本 v1.3.0）**

Run:
```bash
cd app/third_party && git clone --depth 1 --branch v1.3.0 https://github.com/btzy/nativefiledialog-extended.git
```
Expected: `nativefiledialog-extended/` 出現，內含 `CMakeLists.txt`、`src/include/nfd.hpp`。

- [ ] **Step 2: CMakeLists 引入 NFDe 子專案並連結**

於 `app/CMakeLists.txt:19`（`set(NODE_EDITOR_DIR ...)` 之後）加：

```cmake
set(NFD_DIR "${TP}/nativefiledialog-extended")
add_subdirectory(${NFD_DIR})
```

於 `app/CMakeLists.txt:93`（`target_include_directories(... )` 的 `${NODE_EDITOR_DIR}` 後、結尾 `)` 前）加一行：

```cmake
  ${NFD_DIR}/src/include
```

於 `app/CMakeLists.txt:107` 的 `target_link_libraries(simple_world PRIVATE` 區塊內，第一行加：

```cmake
  nfd
```

並在同一個 `target_link_libraries` 區塊（frameworks 之間）加：

```cmake
  "-framework UniformTypeIdentifiers"
```

- [ ] **Step 3: drawToolbar 暫時加 spike 按鈕**

於 `app/src/main.cpp:24`（`#include "imgui_node_editor.h"` 後）暫加：

```cpp
#include <nfd.hpp>
```

於 `app/src/main.cpp:178`（`if (ImGui::Button("Load")) loadProject();` 之後）暫加：

```cpp
  ImGui::SameLine();
  if (ImGui::Button("Test Dialog")) {
    NFD::Guard nfdGuard;
    NFD::UniquePath outPath;
    nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
    nfdresult_t r = NFD::SaveDialog(outPath, filters, 1, nullptr, "untitled.swproj");
    g_status = (r == NFD_OKAY) ? std::string("dialog -> ") + outPath.get()
             : (r == NFD_CANCEL) ? "dialog canceled" : "dialog ERROR";
  }
```

- [ ] **Step 4: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯與連結成功（NFDe target `nfd` 連結進來）。若連結時報缺 framework，確認 Step 2 的 `UniformTypeIdentifiers` 已加。

- [ ] **Step 5: 手動驗收（柏為親手）— Finder 真的跳出來**

Run: `cd app && ./build/simple_world`
操作：點工具列「Test Dialog」按鈕。
Expected: 跳出 macOS 原生儲存對話框（Finder），可進資料夾、檔名預設 `untitled.swproj`；選位置按儲存後工具列狀態顯示 `dialog -> <你選的路徑>`，按取消顯示 `dialog canceled`。

- [ ] **Step 6: commit（spike 先留著，Task 4 會把它換成真功能）**

```bash
git add app/CMakeLists.txt app/src/main.cpp app/third_party/nativefiledialog-extended
git commit -m "build: vendor nativefiledialog-extended and verify native Finder dialog"
```

> 註：若團隊偏好不 vendor 整個 clone，可改用 git submodule。本 plan 用 clone 以求步驟確定可重現。

---

## Task 3: dialogs.mm — 三鈕未存彈窗 + 錯誤提示（NSAlert）

**Files:**
- Create: `app/src/platform/dialogs.h`
- Create: `app/src/platform/dialogs.mm`
- Modify: `app/CMakeLists.txt`

- [ ] **Step 1: 建立 dialogs.h**

Create `app/src/platform/dialogs.h`：

```cpp
#pragma once
#include <string>

namespace sw {

// Synchronous "you have unsaved changes" prompt. Three buttons, in this order:
// Save / Don't Save / Cancel. Synchronous (NSAlert runModal) so it can be used
// inside terminate/close menu callbacks where an async ImGui modal cannot.
enum class UnsavedChoice { Save, DontSave, Cancel };
UnsavedChoice askUnsaved();

// Single-button error alert (e.g. load/save failure). Synchronous.
void showError(const std::string& message);

}  // namespace sw
```

- [ ] **Step 2: 建立 dialogs.mm**

Create `app/src/platform/dialogs.mm`：

```objc++
// Objective-C++ glue for native macOS alerts. Compiled WITHOUT ARC (-fno-objc-arc)
// like eye.mm and the imgui backends, so we [release] manually.
#import <AppKit/AppKit.h>
#include "platform/dialogs.h"

namespace sw {

UnsavedChoice askUnsaved() {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"有未儲存的變更";
  alert.informativeText = @"目前的專案有尚未儲存的變更，要先儲存嗎？";
  [alert addButtonWithTitle:@"儲存"];     // NSAlertFirstButtonReturn
  [alert addButtonWithTitle:@"不儲存"];   // NSAlertSecondButtonReturn
  [alert addButtonWithTitle:@"取消"];     // NSAlertThirdButtonReturn
  NSModalResponse r = [alert runModal];
  [alert release];
  if (r == NSAlertFirstButtonReturn) return UnsavedChoice::Save;
  if (r == NSAlertSecondButtonReturn) return UnsavedChoice::DontSave;
  return UnsavedChoice::Cancel;
}

void showError(const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"無法完成操作";
  alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
  [alert addButtonWithTitle:@"好"];
  [alert runModal];
  [alert release];
}

}  // namespace sw
```

- [ ] **Step 3: CMakeLists 加入 dialogs.mm（無 ARC）**

於 `app/CMakeLists.txt:64`（`set_source_files_properties(src/eye/eye.mm ...)` 之後）加：

```cmake
set_source_files_properties(src/platform/dialogs.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
```

於 `app/CMakeLists.txt:69`（`add_executable` 的 `src/eye/eye.mm` 行之後）加：

```cmake
  src/platform/dialogs.mm
```

- [ ] **Step 4: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功（dialogs.mm 以 -fno-objc-arc 編譯）。

- [ ] **Step 5: commit**

```bash
git add app/src/platform/dialogs.h app/src/platform/dialogs.mm app/CMakeLists.txt
git commit -m "feat: native NSAlert unsaved-changes prompt and error dialog"
```

---

## Task 4: 文件狀態 + 四個操作函式（接到暫時的工具列按鈕驗收）

**Files:**
- Modify: `app/src/main.cpp`（狀態宣告區 + 操作函式 + drawToolbar）

- [ ] **Step 1: 加入 include 與文件狀態全域**

確認 `app/src/main.cpp:24` 後已有 `#include <nfd.hpp>`（Task 2 加過）。於其後加：

```cpp
#include "platform/dialogs.h"
```

於 `app/src/main.cpp:115`（`std::string g_status = "ready";` 之後）加：

```cpp
// Document state for save/load. g_documentPath empty == never-saved (Untitled).
// g_savedSnapshot is toJson() at the last successful save/open/new; comparing it
// to the live graph yields the dirty flag (see isDirty()).
std::string g_documentPath;
std::string g_savedSnapshot;
```

- [ ] **Step 2: 加入 dirty 判斷與四個操作函式**

於 `app/src/main.cpp:155`（既有 `loadProject()` 結尾 `}` 之後）加。注意：四個操作沿用既有全域 `g_graph` / `g_relayout` / `g_status`。

```cpp
bool isDirty() { return sw::toJson(g_graph) != g_savedSnapshot; }

// Forward decl: doSave is used by confirmDiscardIfDirty before it is defined.
bool doSave();

// Returns false only when the user explicitly cancels (so callers abort).
bool confirmDiscardIfDirty() {
  if (!isDirty()) return true;
  switch (sw::askUnsaved()) {
    case sw::UnsavedChoice::Save:     return doSave();   // false if Save As canceled
    case sw::UnsavedChoice::DontSave: return true;
    case sw::UnsavedChoice::Cancel:   return false;
  }
  return false;
}

// Always prompts for a location. Returns true if a file was written.
bool doSaveAs() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::SaveDialog(outPath, filters, 1, nullptr, "untitled.swproj");
  if (r != NFD_OKAY) return false;  // cancel or error
  std::string path = outPath.get();
  if (path.size() < 7 || path.substr(path.size() - 7) != ".swproj") path += ".swproj";
  std::string json = sw::toJson(g_graph);
  if (!sw::saveGraphToFile(path, g_graph)) { sw::showError("無法寫入：" + path); return false; }
  g_documentPath = path;
  g_savedSnapshot = json;
  g_status = "saved -> " + path;
  return true;
}

// Overwrites the current document; falls back to Save As when never saved.
bool doSave() {
  if (g_documentPath.empty()) return doSaveAs();
  std::string json = sw::toJson(g_graph);
  if (!sw::saveGraphToFile(g_documentPath, g_graph)) {
    sw::showError("無法寫入：" + g_documentPath);
    return false;
  }
  g_savedSnapshot = json;
  g_status = "saved -> " + g_documentPath;
  return true;
}

void doOpen() {
  if (!confirmDiscardIfDirty()) return;
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::OpenDialog(outPath, filters, 1, nullptr);
  if (r != NFD_OKAY) return;
  std::string path = outPath.get();
  sw::Graph loaded;  // load into a temp graph; only swap in on success
  if (!sw::loadGraphFromFile(path, loaded)) {
    sw::showError("無法讀取此專案檔：" + path);
    return;
  }
  g_graph = loaded;
  g_documentPath = path;
  g_savedSnapshot = sw::toJson(g_graph);
  g_relayout = true;
  g_status = "loaded <- " + path;
}

void doNew() {
  if (!confirmDiscardIfDirty()) return;
  g_graph = sw::defaultParticleGraph();
  g_documentPath.clear();
  g_savedSnapshot = sw::toJson(g_graph);
  g_relayout = true;
  g_status = "new project";
}
```

- [ ] **Step 3: 初始化已存快照（啟動時 not-dirty）**

於 `app/src/main.cpp` 的 `applicationDidFinishLaunching`，在 `g_NodeEditor = ed::CreateEditor(&cfg);`（約第 502 行）之後加：

```cpp
  g_savedSnapshot = sw::toJson(g_graph);  //启动即與預設圖一致 -> not dirty
```

- [ ] **Step 4: 把 spike 按鈕換成真操作（暫時驗收用）**

把 Task 2 在 drawToolbar 加的 `Test Dialog` 按鈕區塊，替換成：

```cpp
  ImGui::SameLine();
  if (ImGui::Button("New")) doNew();
  ImGui::SameLine();
  if (ImGui::Button("Save As")) doSaveAs();
```

（既有的 `Save` / `Load` 按鈕暫時保留呼叫舊 `saveProject`/`loadProject`，Task 7 統一清理。）

- [ ] **Step 5: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功。

- [ ] **Step 6: 手動驗收（柏為親手）— 三需求 + 暫存安全**

Run: `cd app && ./build/simple_world`
依序操作並確認：
1. 點「Save As」→ 跳 Finder → 選資料夾、存成 `myproj.swproj`。工具列顯示 `saved -> .../myproj.swproj`。
2. 在 Inspector 拉動某個參數（改了圖）。
3. 點「New」→ 因為改過 → 跳三鈕彈窗。按「取消」→ 留在原圖。再點「New」→ 按「不儲存」→ 圖重置成預設、回到 Untitled。
4. （重開 app）`./build/simple_world` → 點 Save As 存一份；改參數；再 Save As 存第二份；New 清空；用 Add Node 旁之後的 Open（Task 5 才有選單，此處先用下一步）。
   暫時驗收 Open：可先跳過，Task 5 接上選單後一起驗。
Expected: 三鈕彈窗行為正確；New 的取消/不儲存分支正確。

- [ ] **Step 7: commit**

```bash
git add app/src/main.cpp
git commit -m "feat: document state and New/Open/Save/Save As operations with unsaved guard"
```

---

## Task 5: 原生 File 選單 + 快捷鍵 + Cmd+Q / Cmd+W 攔截

**Files:**
- Modify: `app/src/main.cpp:426-464`（createMenuBar）

- [ ] **Step 1: appQuit / windowClose callback 先過 confirm**

於 `app/src/main.cpp:436-438`，把現有 `appQuit` callback 改成：

```cpp
  SEL quitCb = NS::MenuItem::registerActionCallback("appQuit", [](void*, SEL, const NS::Object* pSender) {
    if (confirmDiscardIfDirty())
      NS::Application::sharedApplication()->terminate(pSender);
  });
```

於 `app/src/main.cpp:446-448`，把現有 `windowClose` callback 改成：

```cpp
  SEL closeWindowCb = NS::MenuItem::registerActionCallback("windowClose", [](void*, SEL, const NS::Object*) {
    if (confirmDiscardIfDirty())
      NS::Application::sharedApplication()->windows()->object<NS::Window>(0)->close();
  });
```

- [ ] **Step 2: 在 createMenuBar 插入 File 選單**

於 `app/src/main.cpp:453`（`pWindowMenuItem->setSubmenu(pWindowMenu);` 之後、`pMainMenu->addItem(pAppMenuItem);` 之前）插入：

```cpp
  // ---- File menu: New / Open / Save / Save As, with standard shortcuts ----
  NS::MenuItem* pFileMenuItem = NS::MenuItem::alloc()->init();
  NS::Menu* pFileMenu = NS::Menu::alloc()->init(NS::String::string("File", UTF8StringEncoding));

  SEL newCb = NS::MenuItem::registerActionCallback("fileNew", [](void*, SEL, const NS::Object*) { doNew(); });
  NS::MenuItem* pNewItem = pFileMenu->addItem(NS::String::string("New", UTF8StringEncoding), newCb,
                                              NS::String::string("n", UTF8StringEncoding));
  pNewItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL openCb = NS::MenuItem::registerActionCallback("fileOpen", [](void*, SEL, const NS::Object*) { doOpen(); });
  NS::MenuItem* pOpenItem = pFileMenu->addItem(NS::String::string("Open…", UTF8StringEncoding), openCb,
                                               NS::String::string("o", UTF8StringEncoding));
  pOpenItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL saveCb = NS::MenuItem::registerActionCallback("fileSave", [](void*, SEL, const NS::Object*) { doSave(); });
  NS::MenuItem* pSaveItem = pFileMenu->addItem(NS::String::string("Save", UTF8StringEncoding), saveCb,
                                               NS::String::string("s", UTF8StringEncoding));
  pSaveItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL saveAsCb = NS::MenuItem::registerActionCallback("fileSaveAs", [](void*, SEL, const NS::Object*) { doSaveAs(); });
  NS::MenuItem* pSaveAsItem = pFileMenu->addItem(NS::String::string("Save As…", UTF8StringEncoding), saveAsCb,
                                                 NS::String::string("s", UTF8StringEncoding));
  pSaveAsItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand | NS::EventModifierFlagShift);

  pFileMenuItem->setSubmenu(pFileMenu);
```

並於 `app/src/main.cpp:455-456` 的兩個 `pMainMenu->addItem(...)` 之間，讓 File 在 App 之後、Window 之前：

```cpp
  pMainMenu->addItem(pAppMenuItem);
  pMainMenu->addItem(pFileMenuItem);
  pMainMenu->addItem(pWindowMenuItem);
```

並在結尾 release 區（`pAppMenuItem->release();` 附近）加：

```cpp
  pFileMenuItem->release();
  pFileMenu->release();
```

- [ ] **Step 3: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功。

- [ ] **Step 4: 手動驗收（柏為親手）— 選單、快捷鍵、攔截**

Run: `cd app && ./build/simple_world`
確認：
1. 選單列出現「File」，含 New ⌘N / Open… ⌘O / Save ⌘S / Save As… ⇧⌘S。
2. `⌘S` 第一次（Untitled）→ 跳 Finder（退化成 Save As）；存好後再 `⌘S` → 直接覆蓋、不跳 Finder。
3. `⌘O` → 跳 Finder 選 `.swproj` → 圖被讀回、節點重新排上畫面。手滑選一個非專案檔 → 跳「無法讀取此專案檔」、原圖不變。
4. 改一個參數後按 `⌘Q` → 跳三鈕；取消 → 不退出；不儲存 → 退出。
5. （已知缺口）點視窗紅燈關閉 **不會** 跳未存提示——這是 spec 記錄的已知限制，符合預期。

- [ ] **Step 5: commit**

```bash
git add app/src/main.cpp
git commit -m "feat: native File menu with shortcuts and Cmd+Q/Cmd+W unsaved guard"
```

---

## Task 6: 標題列顯示檔名 + 未存星號

**Files:**
- Modify: `app/src/main.cpp`（全域 window 指標、updateWindowTitle、render loop 呼叫、啟動設定）

- [ ] **Step 1: 加入全域 window 指標與標題快取**

於 `app/src/main.cpp:115` 後的文件狀態區（Task 4 加的 `g_savedSnapshot` 之後）加：

```cpp
NS::Window* g_window = nullptr;  // set in applicationDidFinishLaunching, for title updates
std::string g_lastTitle;         // cache so we only setTitle when it actually changes
```

- [ ] **Step 2: 加入 updateWindowTitle()**

於 `app/src/main.cpp` 的 `doNew()` 之後（Task 4 操作函式區結尾）加：

```cpp
void updateWindowTitle() {
  if (!g_window) return;
  std::string name = g_documentPath.empty()
      ? std::string("Untitled")
      : g_documentPath.substr(g_documentPath.find_last_of('/') + 1);
  std::string title = (isDirty() ? "• " : "") + name + " — simple_world";
  if (title == g_lastTitle) return;
  g_lastTitle = title;
  g_window->setTitle(NS::String::string(title.c_str(), NS::StringEncoding::UTF8StringEncoding));
}
```

- [ ] **Step 3: 啟動時記住 window 指標**

於 `app/src/main.cpp` 的 `applicationDidFinishLaunching`，在 `_pWindow->setTitle(...)`（約第 490 行）之後加：

```cpp
  g_window = _pWindow;  // expose to updateWindowTitle()
```

- [ ] **Step 4: render loop 每幀更新標題**

於 `app/src/main.cpp` 的 `Renderer::draw`，在 `drawInspector();`（約第 591 行）之後、`ImGui::Render();` 之前加：

```cpp
  updateWindowTitle();  // filename + dirty star; no-op when unchanged
```

- [ ] **Step 5: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功。

- [ ] **Step 6: 手動驗收（柏為親手）— 標題列**

Run: `cd app && ./build/simple_world`
確認：
1. 啟動時標題列顯示 `Untitled — simple_world`（無星號）。
2. 改一個參數 → 標題變成 `• Untitled — simple_world`（出現星號）。
3. Save As 存成 `myproj.swproj` → 標題變 `myproj.swproj — simple_world`（星號消失）。
4. 再改參數 → `• myproj.swproj — simple_world`；按 ⌘S → 星號消失。

- [ ] **Step 7: commit**

```bash
git add app/src/main.cpp
git commit -m "feat: window title shows current filename and unsaved (•) marker"
```

---

## Task 7: 清理 — 工具列轉呼叫、移除舊存讀路徑

**Files:**
- Modify: `app/src/main.cpp`（drawToolbar 按鈕、移除 projectPath/saveProject/loadProject、移除暫時 include 若不需要）

- [ ] **Step 1: 工具列按鈕改呼叫新操作**

把 `app/src/main.cpp:176-179` 的 Save / Load 按鈕與 Task 4 加的 New / Save As 按鈕，整理成：

```cpp
  if (ImGui::Button("New")) doNew();
  ImGui::SameLine();
  if (ImGui::Button("Open")) doOpen();
  ImGui::SameLine();
  if (ImGui::Button("Save")) doSave();
  ImGui::SameLine();
  if (ImGui::Button("Save As")) doSaveAs();
  ImGui::SameLine();
```

（確保「Add Node」按鈕仍接在其後。）

- [ ] **Step 2: 移除舊的硬編碼存讀函式**

刪除 `app/src/main.cpp:129-155` 的 `projectPath()`、`saveProject()`、`loadProject()` 三個函式（已被 doSave/doOpen + saveGraphToFile/loadGraphFromFile 取代）。確認檔內無其他地方再呼叫它們（Task 1 起就沒有了）。

- [ ] **Step 3: build**

Run: `cd app && cmake -S . -B build && cmake --build build -j`
Expected: 編譯成功，無「未使用函式」或「未定義符號」錯誤。

- [ ] **Step 4: 全 self-test 回歸**

Run:
```bash
cd app && ./build/simple_world --selftest-graph && ./build/simple_world --selftest-save && echo "ALL PASS"
```
Expected: 兩個 self-test 都 PASS，印出 `ALL PASS`。

- [ ] **Step 5: 手動驗收（柏為親手）— 完整走一遍**

Run: `cd app && ./build/simple_world`
完整流程：New → 改圖 → Save As 存到桌面某資料夾 → 改圖（標題出現 •）→ Save（• 消失、不跳 Finder）→ New（跳三鈕、選不儲存）→ Open 讀回剛才那個 `.swproj` → 圖正確還原。全程在真 app 操作無誤。

- [ ] **Step 6: commit**

```bash
git add app/src/main.cpp
git commit -m "refactor: route toolbar through New/Open/Save/Save As, drop hardcoded path"
```

---

## 完成定義（柏為親手測得到）

- Save As 跳真 Finder、能進任意資料夾存成 `.swproj`。
- Open 跳真 Finder、選 `.swproj` 讀回；選錯檔不清空、跳提示。
- 已存過的檔按 Save 直接覆蓋、不再跳 Finder。
- New / Open / ⌘Q 在未存時跳三鈕（存 / 不存 / 取消），行為正確。
- 標題列即時反映檔名與未存星號。
- `--selftest-graph` 與 `--selftest-save` 皆 PASS。

## 已知缺口（本次不做，spec 已記）

- 桌面雙擊 `.swproj` 直接開 app（需 .app bundle 打包）。
- 點視窗紅燈關閉的未存攔截（需 NSWindowDelegate 橋接）。
- 自動定時備份、最近開啟清單。
