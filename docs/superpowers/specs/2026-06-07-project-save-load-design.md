# 專案儲存 / 載入功能設計

日期：2026-06-07
狀態：設計定案，待實作

## 1. 背景與現況

simple_world 是 macOS 原生 C++ / Metal 的 node-canvas app（Mac 版 TiXL 方向）。
柏為要的儲存功能本質是**檔案系統層級**的存讀：真的開 Finder、真的選位置、硬碟上有一個能被看到、能被讀回的專案檔。

**已經有的（底層序列化）：**
- `app/src/runtime/graph.cpp:102` `toJson(Graph)` — 整張圖（nextId / nodes / connections / params）序列化成 JSON
- `app/src/runtime/graph.cpp:130` `fromJson(json, Graph&)` — 反序列化，回傳成功與否
- `app/third_party/imgui-node-editor/crude_json.*` — 輕量 JSON 庫（已在用）
- `app/src/runtime/graph.cpp:183` roundtrip 自測

**缺的：**
- 寫死路徑：現在 Save/Load 永遠寫 `~/Desktop/simple_world_project.json`（`app/src/main.cpp:128`），沒有 Finder、不能選位置
- 沒有「記住當前工作檔」的狀態
- 沒有未存保護（改了沒存會默默丟失）
- 沒有 File 選單 / 標準快捷鍵（原生 menu bar 骨架在 `app/src/main.cpp:423`，但只有 Quit / Close Window）
- 載入失敗的行為沒人擋過（選錯檔可能清空當前圖或 crash）

## 2. 範圍

### 做
- **四個操作**：New / Open / Save / Save As
- **記住當前工作檔**：`g_documentPath`
- **未存保護**：dirty 偵測 + New/Open/關閉前的三鈕攔截（存/不存/取消）
- **標題列**：顯示當前檔名 + 未存星號
- **載入健壯性**：選錯檔 / 壞檔不 crash、不清空當前圖
- **副檔名 `.swproj`**（內容仍是現有 JSON 格式）

### 不做（明確砍掉，避免過度工程）
- **自動定時備份**（TiXL 有）— 儲存格式還在變動期，現在加是織不會被踩的對角線
- **三檔分離**（TiXL 的 .t3/.t3ui/.cs）— 那是為 C# 編譯，單一 JSON 檔就夠
- **最近開啟清單** — 加碼，之後要加很便宜
- **桌面雙擊直接開** — 前置是把 app 打包成 `.app` bundle（牽動 build + 資源路徑，且要先驗證 metallib/shader 在 bundle 裡跑得動），是**獨立的下一塊工程**。副檔名先用 `.swproj`，將來做雙擊開時舊檔不用改名。

## 3. 輪子選擇

| 組成 | 來源 | 說明 |
|---|---|---|
| 圖 ↔ JSON 序列化 | 現有 crude_json + toJson/fromJson | 已有 |
| Finder 對話框（Open/Save/選資料夾）| **nativefiledialog-extended (NFDe)** | 輪子，丟進 `third_party/` |
| 「要存嗎」彈窗 | 一個極小 `.mm` 的同步 `NSAlert` | 約 25 行，New/Open/關閉共用 |
| dirty 偵測 / 當前檔路徑 / 攔截 | 自寫膠水（薄） | 跟 graph model 綁死，無輪子 |

**為何選 NFDe（不選 portable-file-dialogs）：**
- NFDe v1.3.0（2026-01）活躍維護；macOS 用**真正的 NSOpenPanel/NSSavePanel**（= 真 Finder）；支援副檔名過濾、自動補副檔名；Zlib license；CMake `add_subdirectory` 整合。
- portable-file-dialogs 在 macOS 用 AppleScript（osascript 子進程，模擬），六年未更新。為了它自帶的 message box 而把整個 Finder 體驗降級不划算。

**為何 alert 用 `.mm` 的 NSAlert 而非 ImGui modal：**
- 關閉攔截（`Cmd+Q` / 視窗紅燈）是 macOS 原生事件，系統不會停下來等 ImGui 在下一幀畫的 modal。
- 同步的 `NSAlert` 在 `applicationShouldTerminate:` 裡直接用，可靠且一致。NFDe 已引入 AppKit 依賴，多一顆 NSAlert 成本極低。

## 4. 架構與元件

### 新增檔案
- `app/third_party/nativefiledialog-extended/`（git 引入，CMake 子專案）
- `app/src/platform/dialogs.mm` + `dialogs.h` — 只暴露一個函式：
  ```cpp
  enum class UnsavedChoice { Save, DontSave, Cancel };
  UnsavedChoice askUnsaved();   // 同步 NSAlert，三鈕
  ```
  （Finder 對話框直接呼叫 NFDe 的 C++ API，不再自己包）

### 新增狀態（放在 main.cpp 現有 `g_graph` 旁）
```cpp
std::string g_documentPath;    // 空 = 還沒存過的新檔
std::string g_savedSnapshot;   // 上次存檔當下的 JSON，用來比對 dirty
```

### 新增操作函式（main.cpp）
`doNew()` / `doOpen()` / `doSave()` / `doSaveAs()` — 選單與 ImGui 按鈕都呼叫這四個（single source of truth）。

### 輔助
```cpp
bool isDirty();        // toJson(g_graph) != g_savedSnapshot
std::string titleBar(); // 檔名（或 "Untitled"）+ dirty 星號
// 攔截：dirty 時跑 askUnsaved，回傳「是否可以繼續(丟棄/已存)」或「取消」
bool confirmDiscardIfDirty(); // false = 使用者取消，呼叫端中止
```

## 5. 操作流程

### Save As（永遠跳 Finder）
1. NFDe save dialog → 路徑（取消則中止）
2. 確保副檔名為 `.swproj`
3. `s = toJson(g_graph)`；寫檔
4. `g_documentPath = 路徑`；`g_savedSnapshot = s`
5. 更新標題列

### Save（覆蓋當前）
1. `g_documentPath` 為空（沒存過）→ **退化成 Save As**
2. 否則：`s = toJson(g_graph)`；寫回 `g_documentPath`；`g_savedSnapshot = s`；更新標題

### Open
1. `confirmDiscardIfDirty()` → 取消則中止
2. NFDe open dialog（過濾 `.swproj`）→ 路徑（取消則中止）
3. 讀檔字串 → `fromJson` 到**暫存 Graph**（不直接蓋 `g_graph`）
4. **失敗** → NSAlert 報錯；`g_graph` 原封不動
5. **成功** → `g_graph = 暫存`；`g_documentPath = 路徑`；`g_savedSnapshot = 讀進來的字串`；`g_relayout = true`（觸發節點重排到編輯器）

### New
1. `confirmDiscardIfDirty()` → 取消則中止
2. `g_graph = defaultParticleGraph()`
3. `g_documentPath = ""`；`g_savedSnapshot = toJson(g_graph)`；`g_relayout = true`

### 未存攔截（confirmDiscardIfDirty）
- 不 dirty → 直接回 true
- dirty → `askUnsaved()`：
  - **Save** → 跑 `doSave()`（若退化成 Save As 且使用者取消，視為 Cancel）；成功回 true
  - **Don't Save** → 回 true（丟棄）
  - **Cancel** → 回 false

## 6. UI 接線

### 原生 File 選單（加在 main.cpp:423 menu bar）
| 項目 | 快捷鍵 | 函式 |
|---|---|---|
| New | `Cmd+N` | `doNew()` |
| Open… | `Cmd+O` | `doOpen()` |
| Save | `Cmd+S` | `doSave()` |
| Save As… | `Cmd+Shift+S` | `doSaveAs()` |

沿用 `app/src/main.cpp:433` 現有的 callback 註冊機制。

### ImGui 工具欄按鈕
現有 Save/Load 按鈕（main.cpp:175）**改成呼叫上面同一組函式**，不另寫一份邏輯。

### 關閉攔截
- `Cmd+Q` → `applicationShouldTerminate:` 回 `NSTerminateCancel` 直到 `confirmDiscardIfDirty()` 通過
- 視窗紅燈 → `windowShouldClose:` 同樣先過 `confirmDiscardIfDirty()`

### 標題列
render loop 每幀算 `titleBar()`（檔名 + dirty 星號），設給 NSWindow title。圖小，每幀比對成本可忽略。

## 7. 錯誤處理
- **Open 讀檔失敗 / fromJson 失敗** → NSAlert 報「無法讀取此專案檔」；當前圖不動。
- **Save 寫檔失敗**（權限 / 磁碟滿）→ NSAlert 報錯；`g_documentPath` / `g_savedSnapshot` 不更新（維持 dirty）。
- **副檔名容錯**：Open 不強制只認 `.swproj`，只要 `fromJson` 過就接受（使用者可能存成別的名）。

## 8. 測試
- **既有 roundtrip 自測**（graph.cpp:183）持續綠燈 = 序列化不退化。
- **dirty 偵測單元測試**：改一個 param → `isDirty()` 為真；存檔後為假。
- **Save→Open roundtrip**：存到暫存路徑、New、再 Open，圖一致。
- **壞檔載入**：餵一個非 JSON / 殘缺 JSON 給 Open 流程，斷言 `g_graph` 不變、回報失敗。
- **柏為親手驗收**（完成定義）：Save As 跳 Finder 選資料夾存檔 → 改圖 → Save 覆蓋 → New 跳三鈕 → Open 讀回，全程在真 app 操作。

## 9. 風險與假設（要試壓）
- **[承重線] Open 先進暫存、驗證成功才換** — 否則手滑選錯檔會清空正在編輯的圖。這是需求2最易被忽略的破點。
- **[承重線] 關閉攔截用同步 NSAlert** — ImGui modal 是非同步畫的，接不住原生 terminate 流程。
- **[假設] NFDe 在此 metal-cpp app 的 CMake / framework 整合** — 需加 `UniformTypeIdentifiers` framework（AppKit 已連結）。第一步先把 NFDe build 起來、單獨跳一次 Finder 驗證可行，再接流程。
- **[假設] dirty 用每幀快照比對** — 圖小時成本可忽略；圖變大後再考慮改成 mutation 標記或 hash。
