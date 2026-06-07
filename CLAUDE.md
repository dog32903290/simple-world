# simple_world — Claude 工作須知

**IMPORTANT：動任何程式碼前，先讀並遵守 `ARCHITECTURE.md`（這個工程的架構憲法）。**

核心鐵律（全文見 ARCHITECTURE.md）：

1. 程式碼分五區：`runtime`(純計算) / `app`(產品行為) / `ui`(imgui 畫圖) / `platform`(原生接口) / `verify`(眼耳手驗證)。新增檔先決定屬哪一區。
2. 依賴單向：`ui → app → runtime`，`app → platform`；底層葉子(runtime/platform/verify)不往上依賴。
3. verify 是葉子：業務/UI 碼對驗證系統只准留一行 hook（例 `eye::recordItem(...)`），實作肉全在 `verify/`。絕不把驗證邏輯寫進業務檔。
4. 一個檔一個職責；單檔 > ~400 行或一類 > ~12 個公開方法 = 警訊，要拆。
5. 每個子系統要有可單獨跑的隔離測試（`--selftest-*` CLI 模式）。
6. 動程式碼前過自檢三題：①屬哪一區 ②依賴方向對嗎 ③要 hook 驗證嗎（只留一行，肉放 verify/）。
