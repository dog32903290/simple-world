# 架構憲法 + main.cpp 拆解 設計

日期：2026-06-08
狀態：設計定案，待實作

## 1. 背景：為什麼現在做

上一個專案 my world（JUCE）失敗於「全部程式碼混到一起、後期無法除錯」。實際解剖（`/Users/chenbaiwei/Projects/my-world`）的死因：

1. **God Object**：`MainComponent.cpp` 2703 行，吸進 UI+音訊+MIDI+著色器+canvas+狀態 8 種職責，開頭 50+ 個 `#include`。改任何東西牽連全體。
2. **驗證碼混進業務碼**：`app/` 裡 114 個 `*ProofRunner.cpp`（17234 行）跟業務邏輯攤在同一目錄 → 「debug 時挑不出哪個是哪個」的真兇。
3. **include 爆炸**：基礎類被 50+ 檔依賴，改一個觸發全編譯。
4. **無法隔離測試**：驗一個 `moveNode()` 要啟動整個 app。

simple_world 已長出同樣的雛形：`main.cpp` 650 行，混了 toolbar+canvas+inspector+文件操作+menu+renderer+eye hooks。但也做對兩件 my world 沒做的事：`runtime/` 已按子系統分檔；`--selftest-*` 是可單獨跑的隔離測試。

**現在是最便宜的時機**：病灶只有一個（main.cpp），趁小拔掉並立下憲法。

## 2. 目標

1. 建立**五區架構**，每區按性質分，未來各自肥大而不互相污染。
2. 把「程式驗證系統」（眼/耳/手）獨立成 `verify/` 區，並立下「驗證碼不准長進業務碼」的命脈紀律。
3. 把 `main.cpp` 拆成 app 外殼 + `app/document` + `app/editor_ui`（行為不變）。
4. 把整套規則寫成**跨 session 必讀、可機械自檢的憲法**。
5. 把 `app/` 原始碼**完整、乾淨地納入版控**，push 一個可重建的起點。

## 3. 五區架構

```
app/src/
├── runtime/    引擎層 · 純計算,零 UI       (已存在)
│               graph, particle_system, dispatch, radial, transform, tixl_point…
├── app/        應用功能層 · 產品行為
│               document(儲存/文件狀態)… 未來: timeline, preset, 變奏…
├── ui/         皮層 · imgui 畫圖
│               editor_ui(toolbar/canvas/inspector)… 未來各種面板
├── platform/   平台橋接 · 原生 macOS 接口   (已存在)
│               dialogs… 未來: app bundle, 檔案關聯…
├── verify/     驗證工具層 · 眼耳手
│               eye/ … 未來: ears/, hands/
└── main.cpp    app 入口外殼(瘦)
```

### 依賴方向（單向，無環）

- **底層葉子**：`runtime`、`platform`、`verify` —— 互不依賴，各自獨立。
- **上層**：`app → runtime`、`app → platform`；`ui → app`、`ui → runtime`。
- `main.cpp` 是入口，依賴全部。
- **絕不允許**：底層往上依賴（runtime 不准 include ui/app）；同層 app/ui 互相纏繞成環。

### verify 的特殊地位（命脈紀律）

`verify/` 是被觀察所有層的橫切工具，但它是**葉子**，像 platform 一樣被上層呼叫，自己不往上依賴。對症 my world 的頭號死因：

> **業務碼 / UI 碼裡，對驗證系統只准留「一行薄 hook」**（例 `eye::recordItem("Save")`）。驗證的**所有實作肉住在 `verify/` 區**。上層只 include verify 的薄介面 header（`eye.h`），永遠不碰它的 `.mm` 內部。

my world 死在把 proof 的整坨邏輯寫進業務檔；這裡反過來：業務碼只喊一聲，verify 自己在自己的區接。

## 4. 憲法機制（跨 session 必讀 + 可自檢）

### 雙層

1. **憲法全文 → `ARCHITECTURE.md`（專案根）**：五區定義、依賴方向、verify 薄介面紀律、行數上限、新功能落點判斷規則、自檢三題。
2. **必讀入口（覆蓋所有 agent）**：
   - **`CLAUDE.md`（專案根，新建）** — Claude Code 每 session 自動注入。
   - **`AGENTS.md`（更新現有）** — Codex / 通用 agent 每 session 讀。
   - 兩檔開頭都放強制段：「**動任何程式碼前，先讀 `ARCHITECTURE.md` 並遵守**」，並**直接抄入下方 6 條鐵律**，確保不點開全文也撞到核心。

### 6 條鐵律（抄進 CLAUDE.md / AGENTS.md）

1. 程式碼分五區：`runtime`(純計算) / `app`(產品行為) / `ui`(imgui 畫圖) / `platform`(原生接口) / `verify`(眼耳手驗證)。新增檔案先決定它屬哪一區。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子(runtime/platform/verify)不准往上依賴。
3. 驗證系統(verify)是葉子：業務/UI 碼對它只准留一行 hook，實作肉全在 `verify/`。絕不把驗證邏輯寫進業務檔。
4. 一個檔一個職責；單檔 **超過 ~400 行**或一個類**超過 ~12 個公開方法**即為警訊，必須考慮拆分。
5. 每個子系統要有可單獨跑的隔離測試（沿用 `--selftest-*` CLI 模式），不靠「啟動整個 app」來驗一個行為。
6. 動程式碼前過自檢三題（見下）。

### 自檢三題（每次新增檔/函式前）

> ① 它屬於哪一區？
> ② 它的依賴方向對嗎？（只能往下，不能反）
> ③ 它要 hook 驗證系統嗎？有的話業務碼只留一行，肉放 `verify/`。

能機械回答的規則才擋得住肥大；擋不住的就是下一個 my world。

## 5. main.cpp 拆解映射

`main.cpp`（650 行）現有內容 → 去向。**行為完全不變，只搬家 + 立介面。**

| 內容 | 去向 |
|---|---|
| `g_graph` | `app/document`（一個 document 擁有一張 graph） |
| `g_documentPath` / `g_savedSnapshot` / `g_window` / `g_lastTitle` | `app/document` |
| `isDirty` / `confirmDiscardIfDirty` / `doNew` / `doOpen` / `doSave` / `doSaveAs` / `updateWindowTitle` | `app/document` |
| `g_NodeEditor` / `g_selectedNode` / `g_relayout` / `g_status` | `ui/editor_ui` |
| `pinNodeId` / `pinPortIndex` / `pinIsInput` | `ui/editor_ui` |
| `addNode` | `ui/editor_ui` |
| `drawToolbar` / `drawNodeCanvas` / `drawInspector` | `ui/editor_ui` |
| `g_particles` / `g_shaderLib` / `g_frameIndex` / `g_time` | 留 `main.cpp`（Renderer 擁有的 render 狀態） |
| `runSelfTest`(clear-screen smoke) / `Renderer` / `ViewDelegate` / `AppDelegate` / `createMenuBar` / `main()` | 留 `main.cpp`（app 外殼） |

### 跨模組共享狀態

`g_graph` 等全域**不強行消滅**（simple_world 全域才十來個、可控；完全去全域是大重構、過度工程）。做法：每個全域移到它歸屬的模組、用 header `extern` 宣告共享，歸屬清楚即可。完全去全域列為未來紀律（狀態變複雜時）。

### 介面範例

```cpp
// app/document.h
namespace sw::doc {
extern Graph g_graph;            // the open document's graph
extern NS::Window* g_window;     // set by main at launch; used by updateWindowTitle
bool isDirty();
bool doSave();                   // false if user canceled Save As
bool doSaveAs();
void doOpen();
void doNew();
bool confirmDiscardIfDirty();    // false == user canceled
void updateWindowTitle();        // uses g_window
void initSnapshot();             // call at startup: snapshot == default graph
}
```

```cpp
// ui/editor_ui.h
namespace sw::ui {
void drawToolbar();
void drawNodeCanvas();
void drawInspector();
}
```

`main.cpp` 的 render loop 改呼叫 `sw::ui::draw*()`；menu callbacks 改呼叫 `sw::doc::doNew()` 等。

## 6. eye → verify/eye 遷移

- `app/src/eye/` → `app/src/verify/eye/`（檔案搬移）。
- include 路徑 `eye/eye.h` → `verify/eye/eye.h`，更新所有引用點。
- CMakeLists 的 source 路徑與 `set_source_files_properties` 更新。
- **功能完全不變**，純搬家 + 改路徑。為未來 `verify/ears/`、`verify/hands/` 預留門牌。

## 7. 版控納入

- 把 `app/src/`（runtime/eye→verify/platform/app/ui 全部）、`app/shaders/`、`app/src/metal_impl.cpp`、`app/run-dev.sh`、`app/CMakeLists.txt` 完整 `git add`。
- `.gitignore` 補一行 `/app/.eye/`（eye 的輸出目錄，是產物不入庫）。
- `third_party/`（含 NFDe）維持 gitignored（vendored 慣例）；在 `ARCHITECTURE.md` 註明「clone 後需取得 third_party」。
- push 前 `--selftest-graph` / `--selftest-save` 全綠 + GUI 行為驗收。

## 8. 執行順序（每步可 build、可回退）

1. 建 `ARCHITECTURE.md` + `CLAUDE.md` + 更新 `AGENTS.md`（憲法先立，後面照它做）。
2. `eye/` → `verify/eye/` 搬移 + 改 include + CMake，build 綠。
3. 抽 `app/document.{h,cpp}`（搬文件狀態 + 操作），main.cpp 改呼叫，build 綠。
4. 抽 `ui/editor_ui.{h,cpp}`（搬 UI + pin + addNode），main.cpp 改呼叫，build 綠。
5. `--selftest-*` 全綠 + 柏為親手驗收 app 行為不變（儲存/開檔/節點/eye 都照舊）。
6. 完整 `git add` app/ + `.gitignore` 補 `/app/.eye/`，commit。
7. push 到 origin。

## 9. 驗收（完成定義）

- `main.cpp` 從 650 行降到約 250 行（純 app 外殼）。
- 五區目錄成形，`verify/eye/` 就位。
- `ARCHITECTURE.md` + `CLAUDE.md` + `AGENTS.md` 三檔到位，憲法可被任一 session 讀到。
- `--selftest-graph` / `--selftest-save` 全綠（行為不退化）。
- 柏為親手測：儲存 / 開檔 / 新建 / 節點編輯 / eye 截圖全部照舊。
- `git clone` 後（取得 third_party）能 build。
- push 完成。

## 10. 不做（YAGNI）

- 完全去全域（改傳 AppState）——simple_world 全域還少，過度工程。
- 拆 runtime 既有檔——它們已分好，不在本次範圍。
- 真的建 `verify/ears/`、`verify/hands/`——只預留門牌，不憑空造空殼。
- 動 third_party / 改 build 系統結構（除了新增檔的 source 行）。
