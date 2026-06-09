# simple_world 架構憲法

> 動任何程式碼前先讀這份。這是這個工程的法，跨 session 有效。

## 五區（程式碼按性質分，每區內按子系統長）

| 區 | 性質 | 路徑 | 例 |
|---|---|---|---|
| runtime | 純計算，零 UI | `app/src/runtime/` | graph, particle_system, dispatch, radial, transform, audio(analyzer/spectrum/reaction/ingest) |
| app | 產品行為 | `app/src/app/` | document(儲存)… 未來 timeline/preset |
| ui | imgui 畫圖 | `app/src/ui/` | editor_ui(toolbar/canvas/inspector)… 未來面板 |
| platform | 原生 macOS 接口 | `app/src/platform/` | dialogs… 未來 bundle/檔案關聯 |
| verify | 眼耳手 · agent 程式驗證 | `app/src/verify/` | eye/ … 未來 ears/, hands/ |

入口 `app/src/main.cpp` 只放 app 外殼（NSApplication/MTKView/Renderer/menu/main），保持瘦。

## 依賴方向（單向，無環）

- 底層葉子 `runtime` / `platform` / `verify`：互不依賴，不往上依賴。
- 上層：`ui → app → runtime`，`app → platform`。
- `main.cpp` 依賴全部。
- 禁止：底層 include 上層；葉子互相 include；`ui` 直接 include `platform`（要經 app）；app↔ui 纏成環。
- **這條由 `tools/check_arch.sh` 自動把關**——併進 build，違律當下編譯就紅燈，不靠人記得。

### 葉子接縫（leaf seam）— 葉子需要另一個葉子時

一個葉子真的需要另一個葉子的能力時（例：`platform` 收音要跑 `runtime` 的 DSP），**不准直接 include**——那正是會反覆漂回去的違律。改用**回呼 / 資料接縫**：

- 下層葉子只暴露一個 **fn-ptr 回呼或資料出口**（零跨區 include），把原始資料交出去。
- 由**上層（app）擁有**那個 runtime 工具、註冊回呼、接線。`app → runtime` 合法，依賴方向就乾淨。

範例（已落地）：`platform/audio_capture` 只發 raw mono block 給回呼 → `app/audio_monitor` 擁有 DSP（RMS/attack/FFT）並註冊。未來的 OSC / MIDI 等 IO 葉子照同一招，不要再把 runtime include 進 platform。

## verify 命脈紀律（對症上一個專案的死因）

verify 是被所有層觀察的橫切工具，但它是**葉子**。
**業務碼 / UI 碼裡對驗證系統只准留一行薄 hook**（例 `eye::recordItem("Save")`）。
驗證的**所有實作肉住在 `verify/`**。上層只 include verify 的薄介面 header，永不碰其 `.mm` 內部。
（上一個專案 my world 死在把驗證邏輯整坨寫進業務檔 → debug 時挑不出哪個是哪個。）

## 7 條鐵律

1. 程式碼分五區；新增檔先決定屬哪一區。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子不往上、不互依。葉子要跨界→用「葉子接縫」（回呼/資料出口，上層接線，見上）。`tools/check_arch.sh` build 時把關。
3. verify 是葉子：業務/UI 對它只留一行 hook，肉全在 `verify/`。
4. 一個檔一個職責；單檔 > ~400 行或一類 > ~12 個公開方法 = 警訊，要拆。
5. 每個子系統有可單獨跑的隔離測試（`--selftest-*` CLI 模式），不靠啟動整個 app 驗一個行為。
6. 動程式碼前過自檢三題。
7. 重複的建立樣板（menu item、node 註冊、inspector 欄位…）改資料驅動：一張表 + 一個 builder，加一項 = 加一行資料，不是複製一段樣板。範例見 `app/src/app/menu.cpp`。

## 自檢三題（每次新增檔/函式前）

1. 它屬於哪一區？
2. 它的依賴方向對嗎？（只能往下）
3. 它要 hook 驗證系統嗎？有的話業務碼只留一行，肉放 `verify/`。

## 建置

`third_party/`（metal-cpp / imgui / imgui-node-editor / nativefiledialog-extended）是 vendored 且 gitignored。
clone 後需自行取得：
- metal-cpp / imgui / imgui-node-editor：見既有取得方式。
- NFDe：`cd app/third_party && git clone --depth 1 --branch v1.3.0 https://github.com/btzy/nativefiledialog-extended.git`

build：`cd app && cmake -S . -B build && cmake --build build -j`（每次 build 會先跑架構絆線 `check-arch`）
測試：`./build/simple_world --selftest-graph && ./build/simple_world --selftest-save`（各子系統都有 `--selftest-*`）
架構絆線：`tools/check_arch.sh`（依賴方向，build 時自動跑；也可 `cmake --build build --target check-arch` 單獨跑）
