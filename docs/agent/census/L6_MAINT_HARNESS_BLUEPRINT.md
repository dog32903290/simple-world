# L6 音訊匯出 + 維運 — 維運層（Maintenance/Perf Overlay/Auto-Backup）檔存在/Round-Trip 自檢藍圖

> 柏為 L6 分區（Lane L6）：「音訊匯出 + 維運」兩大半。
> 本藍圖聚焦**零依賴半**：auto-backup、perf overlay、console log——現在就能開。
> 音訊匯出半標記 S1 依賴，自檢定義如下方。

---

## 一、現況清點

### A. 簡單世界已有基礎

| 子系統 | 路徑 | 現狀 | 備註 |
|--------|------|------|------|
| **音訊擷取** | `app/src/platform/audio_capture.h/mm` | 完整 AUHAL 驅動 | 原生葉子，回呼供 app 層 DSP |
| **音訊撥放** | `app/src/platform/audio_playback.h/mm` | 完整 AVAudio 驅動 | 支援播放、速度、位置查詢 |
| **音訊分析** | `app/src/app/audio_monitor.h/cpp` | RMS、attack envelope、FFT spectrum | 讀線程安全（render 線程讀）|
| **存檔基礎** | `app/src/runtime/compound_save.h` | `libToJsonV2() / libFromJsonAny() / saveLibToFile() / loadLibFromFile()` | 已有圓形 v2 証（runSaveV2SelfTest） |
| **音軌** | `app/src/app/soundtrack.h/cpp` | 播放+位置同步+drift follow | 與 AudioPlayback 整合 |
| **UI 基礎** | `app/src/ui/output_window.h/cpp` | 浮動視窗+預覽紋理 | 該窗可轉換成音訊/統計面板 |

### B. TiXL 對標（讀取模式）

| 功能 | TiXL 實現 | 檔案:行 | 說明 |
|------|----------|---------|------|
| **錄音匯出** | `WasapiAudioInput.BeginRecording() / EndRecording()` | WasapiAudioInput.cs:99-160 | WAV 檔案名 = suffix+timestamp |
| **波形快取** | `WaveFormProcessing.PopulateFromExportBuffer()` | WaveFormProcessing.cs | 浮點 buffer → PNG 縮圖 |
| **音訊渲染鏈** | `AudioRendering.PrepareRecording() / EndRecording()` | AudioRendering.cs:45-143 | 暫停即時器、建臨時 mixer、消音軌 |
| **自動備份** | `AutoBackup.CheckForSave()` | AutoBackup.cs:27-66 | 每 3 分鐘呼叫一次;背景 zip 任務 |
| **備份命名** | `#{index:D5}-{timestamp}{-minimal}.zip` | AutoBackup.cs:108 | `.temp/Backup` 子資料夾 |
| **效能監視** | `RenderStatsCollector.RegisterProvider()` | RenderStatsCollector.cs:1-56 | 每幀統計 → `ResultsForLastFrame` dict |
| **Crash 通知** | `CrashReporting.InitializeCrashReporting()` | CrashReporting.cs | Sentry 或本地記錄 |

---

## 二、零依賴構造（現在開）

### 2.1 自動備份（Auto-Backup）

**目標**：定期後台備份專案，檔已存在 + 往返讀取相同。

#### 檔案拓樸

```
新建：
  app/src/app/auto_backup.h          — 公開 API（CheckForSave, ActiveBackupPath, 設定）
  app/src/app/auto_backup.cpp        — 實裝（zip 縮放、去重、時間表）
  app/src/app/auto_backup_selftest.cpp  — round-trip 証

機制：
  - 每幀（main 迴圈：frame_cook 後）呼叫 CheckForSave()。
  - 若距上次 >= SecondsBetweenSaves（預設 180s），觸發後台 Task。
  - 後台 Task：lock 檢查是否正在存檔，遍歷所有開放符號，建 zip 到 `.swproj 根/.temp/Backup/#NNNNN-TIMESTAMP.zip`。
  - zip 內容：recurse 遍歷 `.swproj` 和 assets（影像/音訊）；跳過 `.temp/*`、`.git/*`、二進位製品。
```

#### 自檢定義（runAutoBackupSelfTest）

```cpp
// 路徑：app/src/app/auto_backup_selftest.cpp

Proof: auto-backup round-trip 證
────────────────────────────────────────────────────────
[前置]
  - 建臨時 .swproj（copy 預設圖）並開啟（isOpen=true）。
  - 標記 dirty(修改一個 input 值，bump libRevision)。

[觸發一次備份]
  - 呼叫 CheckForSave()，等待後台 Task 完成（用信號量 / 輪詢）。
  - 斷言：.temp/Backup 目錄已存在。
  - 斷言：恰有一個 .zip 檔案（名稱符合 #NNNNN-* 格式）。

[讀回測試]
  - 從 zip 中提取 .swproj 檔案到臨時位置。
  - 呼叫 loadLibFromFile()，讀入 new SymbolLibrary。
  - 比對：new library 與當前 g_lib 的 libToJsonV2() 相等（round-trip proof）。

[重複修改 + 備份]
  - 再修改一個 input（bump revision 又一次）。
  - 再呼叫 CheckForSave()，等待。
  - 新 zip 應有新 index（#00002-*）；若內容相同，舊 zip 時戳更新、新 zip 丟棄。

[去重驗證]
  - 在不修改任何東西的情況下再呼叫 CheckForSave()。
  - 新 zip（pending）與前一個 .zip 位元組相等 → 丟棄 pending，舊 zip touch 時戳。
  - 驗證：index 沒有增加。

[清理]
  - injectBug：修改 library、存 zip；之後在讀回前檢查內容
    → 如果 bug 導致保存內容不同，round-trip 失敗（teeth）。
  - 刪除臨時檔案。

Refuter：
  ✗ 檔案不存在（CheckForSave 沒有被呼叫、或後台任務失敗）→ zip exists 斷言失敗
  ✗ Round-trip 失敗（save 或 load 有 bug）→ JSON diff 斷言失敗
  ✗ 去重邏輯死（應該跳過新 zip，反而都存了）→ index 計數不對
```

#### 實裝路標

- **CheckForSave()**（app 層呼叫點）：
  - 檢查 `isOpen() && isDirty() && elapsed >= interval`。
  - 若真，啟動背景 `BackupTask()`（std::async 或工作隊列）。
  - 設旗標 `_isBackupInProgress` 以防重入。

- **BackupTask()**：
  - 建臨時 `.zip` → 完成後改名或 swap。
  - **遞迴列舉**：從 `documentPath()` recurse；跳過 `.temp/` 及 gitignored。
  - **進入 zip 目錄結構**（相對路徑）。
  - **去重**：讀上一個 .zip → byte-compare → 若相同，touch mtime、丟新 zip。

- **UI 掛接**：toolbar 或 settings 面板「Auto-Backup」toggle + interval 拖帶。

#### CMakeLists.txt 更新

```cmake
src/app/auto_backup.cpp
src/app/auto_backup_selftest.cpp
```

新增到 `add_executable(simple_world ...)` 的 `src/app/` 段。

---

### 2.2 效能監視層（Perf Overlay / Console）

**目標**：每幀採集統計（cook 耗時、節點計數、記憶體），機器驗證計數遞增。

#### 檔案拓樸

```
新建：
  app/src/ui/perf_overlay.h          — 公開 API（startFrame, recordStat, draw）
  app/src/ui/perf_overlay.cpp        — 實裝（immediate 繪製、統計累積）
  app/src/ui/perf_overlay_selftest.cpp  — 計數遞增証

掛接點：
  app/src/main.cpp:260 (frame_cook 後)  — startFrame() 及其他 frame-end 鉤子
  app/src/app/frame_cook.cpp          — 各處 recordStat("cook/resident", duration_ms)
```

#### 統計指標

根據 TiXL RenderStatsCollector.cs + 簡單世界現有的 audio_monitor，蒐集：

| 名稱 | 位置取得 | 型別 | 意義 |
|------|---------|------|------|
| `cook/resident` | frame_cook::cook() 計時 | ms | 常駐評估圖耗時 |
| `cook/dynamic` | frame_cook 動態段計時 | ms | 變化的 parameter 重算 |
| `node_count` | libQueryAll() | count | 即時圖中節點數 |
| `child_count` | symbol.children.size() | count | 當前符號的子項 |
| `audio/rms` | audio_monitor::rms() | float | 即時音訊能量 |
| `audio/spectrum[0..7]` | audio_monitor::spectrum() | float[] | FFT 8 頻帶 |

#### 自檢定義（runPerfOverlaySelfTest）

```cpp
// 路徑：app/src/ui/perf_overlay_selftest.cpp

Proof: perf 指標採集 + 計數遞增
────────────────────────────────────────────────────────
[初始化]
  - startFrame() 設定高水位（初始 `_lastCookMs=0`）。

[幀迴圈 A：基線]
  - 迴圈 10 幀，每幀：recordStat("cook/resident", 5.0)。
  - 驗證：`resultForLastFrame["cook/resident"] == 5` (ms)。

[幀迴圈 B：遞增]
  - 迴圈 10 幀，frame i 時 recordStat("cook/resident", 5 + i)。
  - 驗證：結果逐步遞增（5, 6, 7, ..., 14）。

[多指標並行]
  - 同一幀內：recordStat("node_count", 42) + recordStat("audio/rms", 0.3)。
  - 驗證：resultForLastFrame["node_count"]==42 && resultForLastFrame["audio/rms"]==0.3。

[draw() 無crash]
  - 呼叫 draw(ImGui context, canvas rect)。
  - 驗證：未拋例外；text 顯示統計。

[清理]
  - injectBug：recordStat 後立即回傳舊值（沒 flush）→ 下一幀應看新值，不會看舊值
    → 斷言失敗（teeth）。

Refuter：
  ✗ recordStat 沒有更新 → resultForLastFrame 永遠一樣
  ✗ draw() 讀了過時數據 → 顯示的數字與實際不符（在自檢中難以驗證，但可在 eye 中看）
  ✗ audio/spectrum 陣列越界 → 索引 > 7 時 crash（但簡單世界先只用 8 帶）
```

#### 實裝路標

- **PerfOverlay 類**（singleton）：
  ```cpp
  struct FrameStats {
    std::map<std::string, float> metrics;  // "cook/resident" -> 5.2
  };
  
  void startFrame();                   // 新幀，清舊統計
  void recordStat(const char* name, float value);  // 累積（或覆蓋，視設計）
  const std::map<std::string, float>& lastFrameStats();  // 讀
  void draw(ImDrawList*, ImVec2 topLeft, ImVec2 size);  // 繪製小型面板
  ```

- **frame_cook.cpp 掛接**（在 cook() 開/結尾）：
  ```cpp
  auto t0 = std::chrono::steady_clock::now();
  // ... cook 邏輯 ...
  auto t1 = std::chrono::steady_clock::now();
  perf_overlay::recordStat("cook/resident", 
    std::chrono::duration<float,std::milli>(t1-t0).count());
  ```

- **main.cpp 掛接**（frame 迴圈）：
  ```cpp
  perf_overlay::startFrame();
  // ... frame_cook() ...
  perf_overlay::recordStat("node_count", g_lib.allSymbols().size());
  // ... draw ...
  perf_overlay::draw(drawList, ...);  // 疊在畫布右上角或分開窗口
  ```

#### CMakeLists.txt 更新

```cmake
src/ui/perf_overlay.cpp
src/ui/perf_overlay_selftest.cpp
```

---

### 2.3 主控台視窗（Console Log / Debug Output）

**目標**：蒐集 stderr/stdout + 內部日誌，以浮動視窗顯示；機器驗證 log 行存在。

#### 檔案拓樸

```
新建：
  app/src/ui/console_window.h        — 公開 API（logMessage, draw）
  app/src/ui/console_window.cpp      — 實裝（環形 buffer、color tag）
  app/src/ui/console_window_selftest.cpp  — log 行內容証

掛接點：
  app/src/main.cpp:260              — drawConsoleWindow() 與其他 draw 呼叫並列
  app/src/app/{各模組}             — 用 fprintf(stderr, ...) 或 printf() 以供被攔截
```

#### 自檢定義（runConsoleWindowSelfTest）

```cpp
// 路徑：app/src/ui/console_window_selftest.cpp

Proof: console 日誌蒐集 + 內容驗證
────────────────────────────────────────────────────────
[日誌添加]
  - logMessage("[info] Test line A")。
  - logMessage("[warn] Test line B")。
  - 驗證：getLastMessages(10) 包含兩行（順序正確）。

[環形 buffer 滿溢]
  - 迴圈添加 1000 行。
  - 驗證：buffer size 不超過配置限制（e.g. 256 行）。
  - 驗證：最新 256 行被保留。

[color tag 解析]
  - logMessage("[ERROR] Failed to load asset") — tag=[ERROR]。
  - draw() 時該行應用紅色字。
  - getLastMessages(1) 回傳內容（驗證 tag 被保留或刪除，看設計）。

[清理]
  - injectBug：添加後立即清空 buffer（logMessage 內部 clear）→ getLastMessages 應回傳空
    → 若沒清，斷言失敗（teeth）。

Refuter：
  ✗ logMessage 沒有進 buffer → 讀不到
  ✗ draw() 時 buffer 內容被損壞 → 顯示亂碼 / 段塊
  ✗ color tag 解析錯誤 → 紅色警告顯示成綠色
```

#### 實裝路標

- **ConsoleWindow 類**（singleton）：
  ```cpp
  static constexpr int kMaxLines = 256;
  struct LogEntry {
    std::string text;
    std::string tag;  // "info", "warn", "error"
    float timestamp;
  };
  
  void logMessage(const char* msg, const char* tag = "info");
  std::vector<LogEntry> getLastMessages(int count);
  void draw(ImDrawList*, ImVec2 pos, ImVec2 size);
  void clear();
  ```

- **簡易 tag 解析**：
  ```cpp
  // "[TAG] message" 格式；TAG 內不含 space
  // 無 tag → "info"
  ```

- **與 stderr 橋接**（可選，低優先度）：
  - POSIX dup2() 攔截 stderr → 管道 → 非同步讀線程餵 logMessage()。
  - 或簡單地讓業務碼顯式呼叫 console::logMessage() 而不靠 stderr 攔截。

#### CMakeLists.txt 更新

```cmake
src/ui/console_window.cpp
src/ui/console_window_selftest.cpp
```

---

## 三、S1 依賴（音訊匯出 — 往後規劃）

### 3.1 問題陳述

音訊匯出需要 S1「輸出解析度縫」已解鎖，原因：

- S1 在 `EvaluationContext` 中新增 `requestedResolution` ——這決定了 cook 的輸出大小。
- 音訊匯出時，須同步影像+ 音訊，共用 `requestedResolution`（e.g. 1920×1080 @ 60fps 為 960000 樣本/秒）。
- 若沒 S1，`requestedResolution` 浮動或未定義，匯出時無法銷定音訊時鐘。

### 3.2 自檢框架（待 S1 解鎖後填實）

#### 檔案拓樸

```
新建（S1 後）：
  app/src/platform/audio_export.h        — 公開 API（beginExport, endExport, exportFrame）
  app/src/platform/audio_export.cpp      — 實裝（WAV 寫入、mixer 管理）
  app/src/app/audio_export_selftest.cpp  — round-trip、WAV 標頭驗證

依賴：
  - S1 解鎖 frame_cook::cook() 中的 RequestedResolution → EvaluationContext。
  - 簡單世界 audio_monitor 已有頻譜分析，無需新增（TiXL 做的 AudioRendering 可參考）。
```

#### 自檢定義（勾勒）

```cpp
// 路徑：app/src/app/audio_export_selftest.cpp（S1 後）

Proof: audio export round-trip（WAV 檔存在 + 重讀相同）
────────────────────────────────────────────────────────

[前置]
  - 有音訊檔案加載到 soundtrack。
  - requestedResolution = 1920x1080 @ 60fps（假設）。

[匯出啟動]
  - beginExport(path="/tmp/test.wav", durationFrames=300, fps=60.0)。
  - 驗證：export mixer 已建立;global mixer 已暫停。

[幀迴圈]
  - 迴圈 300 幀：
    - frame_cook(frame i) → cook 生成音訊樣本。
    - exportFrame(i) → WAV writer 寫該幀的樣本。

[匯出結束]
  - endExport()。
  - 驗證：WAV 檔案存在;檔案大小 > 0。
  - 驗證：WAV 標頭正確（44 bytes 固定標頭 + RIFF SIZE）。

[重讀驗證]
  - 用 platform/audio_playback::load(path) 讀回。
  - 驗證：durationSeconds() 符合預期（300 frames / 60fps = 5 秒）。

[內容一致性]
  - （難度高）若有參考金檔，byte-compare 匯出 WAV。
  - （易度低）至少驗證：樣本 RMS ≈ 原始 audio_monitor 讀數。

[清理]
  - injectBug：skip WAV write → 檔案不存在斷言失敗。

Refuter：
  ✗ export mixer 未建立 → cook() 回傳 0 樣本
  ✗ WAV 標頭損壞 → 載回時 load() 失敗
  ✗ 音訊時鐘未同步 → duration 計算錯誤
```

#### 實裝路標（S1 時）

- **platform/audio_export.h**（新葉子）：
  ```cpp
  bool beginExport(const std::string& wavPath, int durationFrames, double fps);
  void exportFrame(int frameIndex);      // 寫當前 cook 輸出的音訊樣本
  bool endExport();                      // 關檔案，restore global mixer
  ```

- **核心算式**：
  ```cpp
  // WAV header: RIFF + fmt + data chunks
  // sample_rate = round(durationFrames / (durationFrames / fps)) = fps * sampleCount/durationFrames
  // 或簡單：user 指定 sample_rate（e.g. 48000 Hz）
  ```

- **與 frame_cook 協調**：
  - frame_cook 輸出樣本緩衝（來自 audio_monitor + audio_playback 的混合）。
  - exportFrame() 讀該緩衝，寫 WAV。
  - （與 TiXL AudioRendering.cs:145-200 類似邏輯）。

---

## 四、跨區依賴與衝突檢查

### 4.1 檔域分配

根據 ARCHITECTURE.md 五區原則：

| 模組 | 區 | 理由 |
|------|---|------|
| **auto_backup** | `app/` | 存檔策略 = 業務邏輯（與 document.h 同區） |
| **perf_overlay** | `ui/` | ImGui 繪製 + 統計顯示 |
| **console_window** | `ui/` | ImGui 視窗，類似 output_window |
| **audio_export** (S1 後) | `platform/audio` | 原生 audio 子系統葉子 |

### 4.2 與 L2 衝突評估

L2「UI 範式」同樣觸及 `ui/` 區，特別是 inspector + canvas：

- **L2 涉及的檔案**（current 已知）：
  - `ui/editor_ui.cpp`：canvas 佈局 + 背景繪製。
  - `ui/inspector.cpp`：node 參數編輯面板。
  - `ui/output_window.cpp`：預覽視窗。

- **L6 新增的 ui/ 檔**：
  - `ui/perf_overlay.cpp`：獨立繪製邏輯，疊在 canvas 右上角（不與 inspector 交集）。
  - `ui/console_window.cpp`：獨立視窗，與 output_window 並列（非互斥）。

- **結論**：**無衝突**。L2 可同時進行；最多在 main.cpp 的 draw 迴圈中新增兩個 draw 呼叫，無邏輯互動。

### 4.3 CMakeLists.txt 更新總表

當所有檔案備齊時，add_executable 段新增：

```cmake
# L6 Auto-Backup (現在開)
src/app/auto_backup.cpp
src/app/auto_backup_selftest.cpp

# L6 Perf Overlay (現在開)
src/ui/perf_overlay.cpp
src/ui/perf_overlay_selftest.cpp

# L6 Console Window (現在開)
src/ui/console_window.cpp
src/ui/console_window_selftest.cpp

# L6 Audio Export (S1 後)
# src/platform/audio_export.cpp      # 待 S1
# src/app/audio_export_selftest.cpp  # 待 S1
```

---

## 五、自檢執行指令

當全部檔落地後：

```bash
cd app && cmake -S . -B build && cmake --build build -j

# 三個零依賴自檢（應全綠）
./build/simple_world --selftest-auto-backup
./build/simple_world --selftest-perf-overlay
./build/simple_world --selftest-console

# S1 後新增（連帶驗證 S1 的 requestedResolution 縫）
# ./build/simple_world --selftest-audio-export

# 完整煙霧測試
./build/simple_world --selftest-graph
```

---

## 六、現在開 vs S1 等待表

| 功能 | 現在開 | 堡壘 | 說明 |
|------|--------|------|------|
| 自動備份 | ✓ | 無 | document.h + compound_save.h 已全 |
| 效能監視 | ✓ | 無 | frame_cook 可計時;audio_monitor 已完 |
| 主控台 | ✓ | 無 | 純 UI,無依賴 |
| 音訊錄製（離線 WAV） | ✗ | S1 | 需 requestedResolution 同步 |
| 波形快取（FFT→PNG） | ✗ | S1 + 新檔 | WaveFormProcessing(TiXL) 需要規格確認 |
| 音訊播放速度追蹤 | ✓ | 無 | audio_playback.cpp 已完 |

---

## 七、驗收定義（Lane L6 完成條件）

1. **檔存在**：三個 `.cpp` + 三個 `_selftest.cpp` 存在且編譯通過。
2. **自檢全綠**：
   - `--selftest-auto-backup` 通過：backup 檔存在 + round-trip 證。
   - `--selftest-perf-overlay` 通過：指標遞增 + draw 無 crash。
   - `--selftest-console` 通過：log 行蒐集 + 內容一致。
3. **UI 有無 crash**：`./build/simple_world` 正常啟動，toolbar + 三個新浮動視窗可見。
4. **S1 阻滯解除後**：`--selftest-audio-export` 通過，補完音訊匯出半身。

---

## 附錄 A：與 TiXL 的設計決策鏈

- **備份去重（byte-compare）**：TiXL AutoBackup.cs:97 同一招，避免內容相同但 mtime 不同導致堆積。
- **性能監視（RenderStatsCollector）**：TiXL 用字典累積幀統計，sw 照搬。
- **音訊匯出（AudioRendering 暫停即時+臨時 mixer）**：TiXL 分離影片和音訊的 export path，sw 待 S1 仿照。

---

## 附錄 B：開發工作序列建議

1. **先做 auto_backup**（最獨立）→ 證明 round-trip 基礎穩固。
2. **再做 perf_overlay**（較簡單）→ 驗證 frame_cook 計時點可正確插入。
3. **再做 console_window**（純 UI）→ 無新依賴。
4. **S1 解鎖後**著手 audio_export（最重，wait-for 性質）。

---

**版本**：2026-06-23  
**Lane Owner**：（待指派）  
**Status**：藍圖完成，實裝待開
