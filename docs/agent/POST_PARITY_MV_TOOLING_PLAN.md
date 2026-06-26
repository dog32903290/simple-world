# POST_PARITY_MV_TOOLING — 柏為 MV 製作工具化施工圖（clone 完 TiXL 後開工）

> 柏為 2026-06-26:「未來路一和路二都要做到，但等 clone 完 TiXL 再來做。你先寫文件，讓做工程的人知道未來可以做這件事。」
> **本檔 = 未來工作的 roadmap，不是現在的工單。** 開工前提見下方「★開工閘」。現在動工會撞正在跑的 TiXL parity lane。
> 與其他檔關係：parity 排程權威仍是 [MASTER_PLAN](MASTER_PLAN.md)；體驗軸 gap 在 [EXPERIENCE_SCOPE_GAPS](EXPERIENCE_SCOPE_GAPS.md)。本檔是**柏為真實創作用途驅動的 post-parity 方向**，部分與 parity/體驗軸重疊（見最後一節），但驅動力不同：不是「clone TiXL 既有功能」，是「讓 simple_world 成為柏為做 MV 的工具」。

---

## ★開工閘（硬規）

1. **等 TiXL clone 收尾再開**——MASTER_PLAN census 接近 749/749、承重 seam（point-render、S4 infra、camera3d value-output）落定後才動。現在開會撞 parity。
2. **這幾塊大量動 cook-core / runtime**（frame_cook、transport、particle_system、point_graph、新 platform video I/O），**需柏為在場 + 單一 driver**（不並行雙開，見 [[sw-batch-no-parallel-launch]]）。
3. **開工先讀「★源碼地圖」一節**——TiXL 藍本一律是本地 `external/tixl/` 源碼路徑（非 issue 編號），sw 錨點用 grep 符號重定位。本檔行號是 2026-06-26 快照（HEAD `c37fdce`），**會漂、不可信死行號**，一律 grep 符號。

---

## 為什麼有這份文件（背景）

柏為的真實用途在 2026-06-26 釐清：**simple_world 不是給 live VJ 用的，最近一次實戰是 MV 製作。** 這個定位推翻了「sw 站 VJ 世界觀、不需要 export/text/精準 timing」的舊假設——MV 製作下，那些全變承重。

來源鏈：分析一支 TiXL 評論影片（YouTube `gNCgKrY5sKY`「A free Software for Motion Graphics?」，逐字稿在 repo 外 `_transcripts/`）→ 那支影片站在「broadcast motion graphics / client work」打 TiXL 的 export/字型/timing 限制 → 柏為對照自己用途，確認他要的正是那些被罵的功能，但**用途不同**（見下四需求）。

---

## 四個承重需求（柏為原話翻譯）

| # | 需求 | 柏為要的（不是字面） |
|---|------|--------------------|
| 1 | **中文字 = geometry 來源** | 讀系統字型 → 字形變成 **point** → 對點做 procedural（扭曲/轉/把每個字的點當座標去 spread）。**不是字幕排版**——剪接軟體做不到這個，sw 的 point 世界天生擅長。 |
| 2 | **video import = 主體/背景分離** | import 影片，分離主體與背景**分別做效果**（背景 displace、主體別的）。**不是真實拍合成。** 柏為的降維洞見：遮罩追蹤不要即時，把它算成「離線算好、可在 timeline 固定住的遮罩關鍵幀」。 |
| 3 | **frame-accurate 決定性 export** | 在 sw 看到的時間點 T 的畫面，逐幀輸出到 DaVinci 要**完美對上**。這是 stateful 粒子模擬的「不能隨意 scrub」本質難點的回馬槍。 |
| 4 | **高品質 export 規格** | alpha 通道（ProRes 4444，疊實拍命脈）/ 可選 fps（對拍，非寫死 30）/ 可選 timeline 範圍 / frame-accurate。**macOS AVFoundation 給得起，TiXL 在 Windows 給不到 ProRes4444 = sw 結構性贏點。** |

---

## ★源碼地圖（agent 開工的單一入口——錨點＝可開啟的源碼，不是 issue 編號）

> **鐵律（同本工程 ground-truth 慣例 [[baiwei-direct-decide-tixl-default]]）**：藍本＝本地 `external/tixl/` 源碼，**不是 GitHub issue**。issue 是 TODO／討論，源碼才是實作。下方行號是 2026-06-26 快照，**漂移時用 grep 括號內的符號名重定位，不信死行號**（runtime 檔正被別的 lane 動）。

### TiXL ground-truth 源碼（本地 `external/tixl/`，parity 對照一律讀這裡，非 issue）

| 塊 | 直接開這個源碼 | 拿什麼 |
|---|---|---|
| **A text→points** | `Operators/Lib/point/io/LineTextPoints.cs`（＋例 `Operators/examples/lib/point/LineTextPointsExamples.cs`） | **TiXL 早有 text→points → A 是 parity 非原創**。讀它拿取點演算法 + count policy |
| **B video import** | `Operators/Lib/io/video/VideoClip.cs`（來源）/ `PlayVideoClip.cs`（播放器）/ `PlayVideo.cs`；同目錄 `.t3`/`.t3ui`＝節點圖定義；例 `Operators/examples/user/pixtur/research/VideoClipLayer.cs` | video 解碼/seek/timeline 接法 |
| **C export/render** | 整個 `Editor/Gui/Windows/RenderExport/`：`RenderProcess.cs` `RenderSettings.cs` `RenderTiming.cs` `RenderPaths.cs` `ScreenshotWriter.cs` ＋ `MF/MfVideoWriter.cs`（Media Foundation writer＝v4.2 要換 ffmpeg 的那個，sw 改寫成 AVFoundation 的參照）＋ `Editor/UiModel/Exporting/PlayerExporter.cs` | render 流程／timing／設定／寫檔全在這 |
| **路2 IO-record** | `Core/IO/SimulatedIoBus.cs` ＋ `Operators/Lib/io/data/SimulateIoData.cs` ＋ `Core/DataTypes/DataSet/DataClip.cs` | 把即時 IO 固定成可 replay 的 track＝柏為「離線遮罩降維」的 TiXL 雛形 |

### sw 現有錨點（grep 符號為準；行號＝2026-06-26 快照，會漂）

| 用途 | 檔 | grep 符號 |
|---|---|---|
| 點結構（A 寫入目標） | `app/src/runtime/tixl_point.h` | `struct SwPoint`（64B：Position/FX1/Rotation/Color/Scale/FX2） |
| mask 合成下游（B 白送） | `app/src/runtime/point_ops_blendwithmask.cpp` | `cookBlendWithMask` |
| 靜態載圖（B 要改 per-frame） | `app/src/runtime/point_ops_loadimage.cpp` | `cachedAssetTexture` / `no-hot-reload` |
| wall-clock dt（C 要解耦） | `app/src/app/frame_cook.cpp` | `measureDeltaSeconds`（def~46）/ `simDeltaFromWall`（def~90, 呼叫~303） |
| 粒子吃絕對 time（C 證據） | `app/src/runtime/particle_system.cpp` | `ParticleSystem::update`（`/*dt*/` 參數未用）/ `runTurbulence` |
| 無 render-to-file（C） | `app/src/runtime/transport.h` | grep `render-to-file` |

---

## 三塊工程 + 架構風險 + 現況證據

四個需求收斂成三塊工程：

### A. Text→Points generator —— [important] 中偏小（~1 週，無架構障礙）

- **要做的**：新增一個 generator op：CoreText 取字形向量輪廓（CGPath）→ flatten 成 polyline → 沿線取點寫進 point buffer。
- **不要做的**：排版、kerning、文字框、SDF font atlas（那些才是大工程，柏為不要）。
- **★A 是 parity，不是原創**：TiXL 早有 text→points（`external/tixl/Operators/Lib/point/io/LineTextPoints.cs`，見★源碼地圖）。**開工先讀它拿取點演算法＋count policy，別從零造。** sw 的差異只在實作：TiXL 文字渲染走 SDF atlas（CJK 受限）、sw 走 CoreText glyph-path → **CJK 在 sw 免費**（取中文字輪廓跟拉丁字一樣）。這是「同 parity、更好實作」，不是「TiXL 沒有」。
- **現況證據**：
  - 點結構現成：`app/src/runtime/tixl_point.h:36-43` — `SwPoint` 64B（Position + FX1 + Rotation + Color + Scale + FX2）。
  - point graph 基建成熟（particle / point_ops / stateful 都活著）。
  - **缺口**：無 CGPath / CoreText 取點碼。需新增 `platform/` 的 CoreText→polyline + 一個 point generator op。純加法、零核心改動。

### B. Video import + mask 分離 —— [important] 中偏輕（~1–1.5 週）

- **好消息：下游白送。** `BlendWithMask`（3 輸入 ImageA/ImageB/Mask → 合成）已完整實作 → 柏為「主體背景分別做效果再合回」的下游現成。
- **缺口只有一塊**：per-frame video decoder source op（現 LoadImage 是 memoize 一次的靜態載入）。AVAssetReader 依 `transport.position` 解出對應幀 → Metal texture。matte 來源 = 第二條 video/matte 輸入，餵進 BlendWithMask。
- **要小心**：影片 frame-accurate seek（影片第 N 幀對得上 timeline）。
- **現況證據**：
  - mask 合成現成：`app/src/runtime/point_ops_blendwithmask.cpp:78-146`（3 輸入，bind texture slot 0/1/2）。多輸入 TexCookCtx seam 已開可重用。
  - 靜態載入缺 per-frame path：`app/src/runtime/point_ops_loadimage.cpp:11-13`（`cachedAssetTexture()` decode 一次 memoize）+ `:42-44`（fork[no-hot-reload]、ImageSequenceClip deferred）。要的是 cook 時依 timeline 位置解幀，非 memoize 1:1。

**★深化設計（柏為 2026-06-26 定深入 B；盤點證實「一勞永逸＝graph 層幾乎免費」）**：
- **一勞永逸的技術根因**：① per-frame 時間機制已備——每個 texture cook 帶 `ctx->time`（`app/src/runtime/eval_context.h` grep `EvaluationContext`），cook 裡直接讀它 seek video，不動 graph 架構（現無 time-varying texture op，VideoClip 是第一個用它的）② texture source 模板現成（`point_ops_loadimage.cpp` grep `cookLoadImage` + `ImageFilterOp` 自登記，不撞 point_graph.h）③ 多輸入 seam 已開（`point_graph_cook_ctx.h` grep `inputTextures`，5 消費者）。→ VideoClip ＝ 複用 LoadImage 模板的 texture source op，輸出 texture → 整個 texture 生態白送。**不碰孤島，graph 層 SMALL。**
- **工程重心全在 platform 層**：唯一真工作＝新增 `app/src/platform/video_decode.h`（AVFoundation video→MTL texture，仿 `image_decode.h`）；CVPixelBuffer→MTLTexture 用 CVMetalTextureCache。
- **★承重設計分岔（seek API，跟 C 塊耦合）**：互動預覽走 **AVPlayer + AVPlayerItemVideoOutput**（async，player 管 seek/buffer，**MEDIUM**）；C 塊路2 離線決定性 export 走 **AVAssetReader** 線性逐幀（LARGE 但只在離線路徑）。各用對地方，對應 C 塊路1/路2 二分。
- **簡化（守邊界，別過度設計）**：① video 的 audio 軌**不要**（video 純取畫面，配樂/旁白是 #7 audio track）② proxy（TiXL #1086）**別提前做**——盤點發現 TiXL 源碼自己都還沒實作（grep proxy 無），AVPlayer seek 夠用、慢了再加（先求簡單）。
- **TiXL 藍本**：`external/tixl/Operators/Lib/io/video/PlayVideoClip.cs`——① 時間映射 bars→seconds（grep `SecondsFromBars`）② **seek threshold**（deltaTime 超閾值才 seek，避免每幀 seek 卡）。`VideoClip.cs`＝TimeClipSlot（timeline 上的 video clip，TimeRange+SourceRange）。
- **matte 來源**：另一個 VideoClip（matte 序列）或 image source 接 `BlendWithMask` 第二輸入，零新基建。
- **可拓展性邊界**：VideoClip source ＝「任何 video 進 graph」通用入口（glitch / 貼圖 / 餵粒子色全接它），範圍**停在 source 節點，不造 video 框架**。
- **工程量重定 MEDIUM**（走 AVPlayer；盤點原報 LARGE 是含 AVAssetReader 同步 seek，那條只在離線 export 路徑必要）。

### C. 決定性 export（路1 + 路2）—— [core] 唯一承重塊

**難點不是寫 video 檔**（AVAssetWriter 是已知 pattern；目前確實零 video writer，但純加法）。**難點是粒子 stateful GPU 模擬硬綁 real-time wall-clock** → 「export 第 N 幀 == 你看到的第 N 幀」不天生成立。

- **現況證據（wall-clock 耦合）**：
  - `app/src/app/frame_cook.cpp:46-52` — `measureDeltaSeconds()` 無條件讀 `steady_clock`，**無注入固定 dt 的縫**。
  - `frame_cook.cpp:304` — `dtSimSecs = simDeltaFromWall(dtSecs)`（clamp 0.25s）；`:305` transport 吃真 wall dt。
  - `app/src/runtime/particle_system.cpp:177` — `update(time, /*dt*/)`：**dt 參數沒用到**，integrator 吃絕對 `time`（fxTime 秒）。
  - `particle_system.cpp:109-128` — turbulence `Phase = time`（wall 秒）。
  - stateful ops（Damp/Spring/AudioReaction）也吃 real `dtSecs`。
  - `app/src/runtime/transport.h:25` — 自註「我們沒有 render-to-file 模式」。
  - video writer 缺：`platform/` 只有 audio I/O（audio_capture AUHAL / audio_playback AVAudioEngine），grep AVAssetWriter = 0。

**兩條路（柏為定：未來都要做到）**：

| | 路1：real-time 邊播邊錄 | 路2：決定性離線 render |
|---|---|---|
| 機制 | 不碰模擬，real-time 跑、每幀餵進 video writer | dt 來源改成固定值注入、從 t=0 逐幀推進 |
| 量 | ~1 週 | ~2 週（explore 估 2–3 週；但粒子吃**絕對 time** 不吃 per-step dt，故只要 fxTime 由固定 dt 累積、粒子 shader 不用改 → 可能偏樂觀） |
| 強 | 最快能出片；錄的就是你看到的 | 慢機器也算對、任意 fps、bit-reproducible |
| 弱 | 場景太重掉幀 → fps 飄、對不准；fps 上限受 real-time | 要處理離線 audio-sync（離線時 audio 不能跟 real-time 時鐘跑） |
| 承重一刀 | 加 AVAssetWriter | **dt 來源可注入固定值**——做完後粒子（吃累積 fxTime）+ stateful ops（吃 dt）自動決定性，因為沒有別的 wall-clock 入口 |

**explore 的架構建議（值得採納）**：路2 用**獨立 offline render 模式/exe**，load 專案 → transport 由 frame counter 驅動（`position = frameIndex / fps`）→ 手動跑 cook loop N 次固定 dt → 每幀寫檔。重用 PointGraph cook 引擎，只換掉 frame_cook 的 wall-clock driver。這樣不污染互動 app，且繞過 audio-sync。

**★誕生條件（path-2 生出一座新的「離線決定性」孤島，別讓它裸著誕生）**：path-2 落地必須同批附 `--selftest-deterministic-render`——固定 dt 從 t=0 跑 N 幀**兩次**、輸出 buffer 必須 byte-match；`-bug` 咬合＝注入 wall-clock 依賴 → 兩次不一致 → 紅燈。詳見 [MAINTENANCE_HARDENING_PLAN](MAINTENANCE_HARDENING_PLAN.md) 工作 3。

---

## 施工順序（最穩的最短路徑）

| 序 | 塊 | 為什麼這個位置 | 交付 |
|---|---|---|---|
| **1** | **C 寫檔地基 + 路1** | **打通出海口**：sw 現有的粒子/SDF/image/point 全是活的，唯一缺出海口。export 一通，現有能力立刻能做 MV，不必等 A/B | 能出 ProRes4444 含 alpha 進 DaVinci |
| **2** | **A 中文字→點** | 柏為腦裡最具體的畫面；獨立低風險 | text-as-geometry，CJK 免費 |
| **3** | **B video + 主體背景分離** | 實拍進 sw 做效果；下游 BlendWithMask 白送 | per-frame video decoder |
| **4** | **C 路2 決定性離線** | **拿路1 當 golden**；最重、收尾 | 慢場景也精準逐幀出片 |

**路1/路2 的共生關係（為什麼這順序省重工）**：
1. 路1、路2 共享同一個下半身（AVAssetWriter 寫檔管線），做一次。
2. **路1 錄出來的 = 路2 的 golden**：路2「決定性 render 對不對」的唯一驗證，就是「機器跑得動的場景下，離線 render 要跟路1 real-time 錄的逐幀一致」。先做路1 才有 ground truth 驗路2；先做路2 是盲驗。
3. 路2 的 dt 解耦**反哺**路1：掉幀時路1 也能改用固定 dt 穩住 fps。

**建議**：第 1 步打通後，先停下來用現有 sw 能力**實做一小段 MV 試水**，用真實創作需求校準 A/B/C 的節奏，而不是照紙上順序硬跑。A/B 順序可對調（看柏為當下創作更急哪個）。

---

## 與 TiXL parity / 體驗軸的重疊（待開工時核對）

本檔是 post-parity 方向，但有些塊 parity 過程可能順帶長出來，開工前先查 census 現況、別重做：

- **A**：**TiXL 確有 text→points**＝`external/tixl/Operators/Lib/point/io/LineTextPoints.cs`（見★源碼地圖）。A 是 parity，CoreText glyph-path（CJK 免費）是 sw 的實作差異，非「TiXL 沒有」。→ 核對 census 有沒有港 LineTextPoints。
- **B**：TiXL video op＝`external/tixl/Operators/Lib/io/video/VideoClip.cs`＋`PlayVideoClip.cs`；census 裡若已 port，video decoder 可能部分現成。`BlendWithMask` 已是 parity 產物。→ 核對 census 的 video op。
- **C export**：TiXL 源碼＝`external/tixl/Editor/Gui/Windows/RenderExport/`＋`PlayerExporter.cs`（見★源碼地圖）；屬體驗軸 [EXPERIENCE_SCOPE_GAPS](EXPERIENCE_SCOPE_GAPS.md) 的 render-output / Player 延伸。但 **ProRes4444 alpha + 決定性離線 render 是 sw 要超越 TiXL 的**（TiXL 用 MF/ffmpeg、8bit），非單純 parity。`transport.h` grep `render-to-file` 已標缺席。

---

## TiXL 上游動態（2026-06-26 爬 github.com/tixl3d/tixl issues）

TiXL 用 milestone 當 roadmap：**v4.2（due 2026-07-31，收尾中）/ v4.3（due 2026-09-30，規劃）/ v4.4 / Later**。爬下來對本檔三塊有直接藍本：

- **B/C 塊有 TiXL 藍本（v4.2 剛 closed）**：
  - **#1084 ffmpeg Video Encoding**——TiXL 為相容 Linux 把 Media Foundation 換 ffmpeg。**= 路線分岔確認**：TiXL 為跨平台犧牲 native codec；sw 在 macOS 走 AVFoundation native（ProRes4444 alpha），無此包袱。
  - **#1085 Improve VideoClips**——VideoClip→VideoClipPlayer、拖 timeline、中間插 ColorGrade 做效果 = **B 塊「video 分離做效果」藍本**。
  - **#1086 proxy videos for fast seeking** = **B 塊 frame-accurate seek 的 TiXL 解法**（proxy）。
  - **#1090 連續錄製 video encoding / #1089 render to video file** = **C 塊 export（含路1 連續錄製）藍本**。
  - **★開工前核對**：census 有沒有納入這批 v4.2 video op（VideoClip / VideoClipPlayer / render-to-video / ffmpeg encoding）。剛 close，可能 census 還沒收。
- **路2（決定性離線 render）TiXL 同向（v4.3 open）**：
  - **#992 Record IO signals as track for simulated playback and rendering**——把 live IO（MIDI/OSC/camera/audio timing）錄成 DataTrack → replay simulation → 「render a higher quality video」。**與柏為「把即時固定成 track、離線高品質 render」同一思想；柏為的離線遮罩關鍵幀是其子集。** 可參照 DataTrack 概念設計固定遮罩。TiXL 還沒做 → sw 路2 走在它前面。
  - **#1042 Time scrubbing widget**——TiXL 已有 JKL（AVID）+ T 鍵 scrub，要做 widget。MV 剪接 scrub 可參照。
- **A 塊（text→points）TiXL 確有對應（修正 2026-06-26 早先誤判）**：源碼＝`external/tixl/Operators/Lib/point/io/LineTextPoints.cs`（見★源碼地圖）。**早先憑 issue title 搜尋誤判「TiXL 無對應、A 是原創」——錯，是沒 grep 本地源碼。A 是 parity**；CoreText glyph-path（CJK 免費）是 sw 實作差異。issue 端只有 #628 rich text / #335 colored text（[Text] 2D 渲染）＝不同東西。
- **開工必讀**：**#911 Notes on creating motion graphics animations**——**作者＝pixtur 本人**（非外部用戶），TiXL 核心親自做 motion graphics 短片（腳本+旁白+配樂，與柏為工作流幾乎相同）的痛點自白（他自己 comment「still valid」）。詳見下「pixtur 痛點地圖」節。
- **戰略背景**：社群最想要 #77 Linux（38👍）、#32 Mac（22👍），TiXL Windows-only、正為跨平台掙扎（#739 D3D vs Vulkan、#1028 Linux port、#1084 ffmpeg）。**TiXL 精力分流去移植 → sw 站 Mac native 是時間窗。**

---

## pixtur 的 motion-graphics 痛點地圖（TiXL #911，作者＝pixtur 本人）

#911 是 **pixtur（TiXL 核心 maintainer）親自用 TiXL 做 motion graphics 短片時列的痛點自白**（他 comment 確認「still valid」）。對柏為做 MV ＝ 一張「該避的坑 + 該做對的功能」免費地圖。四條直接相關：

### ★#7 第二音軌（voice-over）— 柏為認定最重要，已盤出設計（MEDIUM，~100–150 行，3 核心檔）

TiXL「不可能加第二軌」（pixtur 原話）；sw 好做。**機制（柏為直覺翻譯）**：不是兩個 timeline、不是兩個 clock，是 **一個 clock（transport 中央時鐘，零改動）＋ 一條 timeline ＋ 餵 N 個 audio source**。

**為什麼好做（兩個纏結 sw 架構天生已解）**：
- **clock 纏結無**：transport 是中央時鐘，所有東西讀它，audio 無獨立時鐘（`app/src/runtime/transport.h` grep `position` / `fxTime`）。
- **分析纏結無**：BPM/頻譜來自麥克風（`audio_capture`），跟 soundtrack 播放**零耦合**（`app/src/app/frame_cook.cpp` grep `spectrum` ＝來自 audio_monitor 非 soundtrack）→ voice-over 純播放不污染節拍同步。

**要做的＝把單 source 泛化成多 source**（grep 符號為準，行號會漂）：
- 資料模型：`app/src/runtime/compound_graph.h` grep `CompositionSettings`（`soundtrackPath` 單 string → `vector<AudioTrack>{path, volume, offset?}`）
- 播放：`app/src/app/soundtrack.cpp` grep `playback`（單例 `static AudioPlayback` → 多實例）；`app/src/platform/audio_playback.mm` grep `connectChain`（每軌一 player node → 同一 `mainMixerNode`，AVAudioEngine 本來吃多軌）
- 同步：`app/src/app/frame_cook.cpp` grep `syncFrame` ＋ `app/src/app/soundtrack.h` grep `followFrame`（單軌 → 逐軌迴圈，全跟同一 transport seek）
- transport / 分析層：**零改動**

**唯一小心**：`.swproj` 存檔相容（舊單 path → 新 list 升級）——這是 MEDIUM 非 SMALL 的唯一原因。
**設計自由度**：per-track flag「這軌參不參與 beat/spectrum」（預設關；未來想讓視覺反應旁白節奏時開）——這個 flag 是下面 #7b 的入口。
**TiXL 無源碼可抄**（它根本沒做）→ 這條 sw 領先，無 parity 藍本。

### ★#7b audio 進 node graph：InputAudio 節點 + AudioReaction 選源（柏為 2026-06-26 補，MEDIUM）

把 audio 從「app 層播放系統」提升成「node graph 信號源」，讓視覺能反應**指定那一軌**（旁白驅動字幕、配樂驅動背景），而非只吃全局麥克風。**這是 #7 的延伸，同一塊 audio 工程。**

**承重前提（盤點 2026-06-26 證實）**：sw 與 TiXL 的 graph 內 audio **都是 control-rate**（只流分析數據 spectrum/level/beat 的 float，不流 audio buffer）。所以這塊**不碰 audio-rate graph 那座大孤島**，是在既有 control-rate 模型上加 routing。

**三件事的設計**：
- **InputAudio 節點**：把某一軌暴露成 graph 源 + 確保那軌被分析。
- **AudioReaction 加 Source input**：接 InputAudio，registry lookup 讀那軌的分析快照（不再寫死麥克風）。
- **mixer 節點＝不需要**：混音在 app 層 AVAudioEngine `mainMixerNode` 自動發生（TiXL 同，混音在 BASS mixer、非 graph 節點）。

**承重的那條線（不在 routing，在「讓那軌真的被分析」）**：現在只有麥克風掛 SpectrumAnalyzer，soundtrack 播放軌沒人分析它。要讓視覺反應某軌＝在該軌 player node 上 **AVAudioEngine `installTap`** 拿播放 buffer → 餵 SpectrumAnalyzer → registry → AudioReaction 選它。installTap 是 macOS 現成機制。

**要動（grep 符號為準）**：
- 新檔 `app/src/runtime/spectrum_source.h`：SpectrumSource registry（0=麥克風 default / 1+=各軌）
- `app/src/runtime/node_registry_math_anim.cpp` grep `AudioReaction`：加 `Source` input slot
- `app/src/app/frame_cook.cpp` grep `spectrum`（~`audio_monitor::spectrum()` 寫死處）：換成 registry lookup
- soundtrack 軌掛 tap：`app/src/platform/audio_playback.mm` 加 `installTap` → SpectrumAnalyzer

**★sw 領先 TiXL（無 parity 藍本，但同 control-rate 模型故低風險）**：TiXL 的 `external/tixl/Operators/Lib/io/audio/AudioReaction.cs` 也 hardcoded 全局 `AudioAnalysis` singleton、不能選源、無 InputAudio 節點、無 graph mixer。TiXL 用戶只能用「全局混音後總頻譜」打所有視覺；柏為的**分軌分析**（各軌打各自視覺）比 TiXL 精細，對 MV 是真實優勢。TiXL 端參照（理解用）：`AudioReaction.cs` / `Core/Audio/AudioAnalysis.cs`。

### #9 project resolution — sw 已較好，不用做
pixtur 痛點：TiXL 沒 project resolution（出片尺寸沒在新專案對話框問）。sw 已有 out-resolution-selector（B 軌 DONE）＋ `app/src/runtime/point_graph.h` grep `setFrameResolutionOverride`。

### #18 Easy text animations / text helper compound — ＝ A 塊
pixtur 自承「缺、也許要個小 helper op」。**正是柏為的 A 塊（text→points→procedural）**，方向比 pixtur 設想的 helper 更強。見「三塊工程 A」。

### #14 Shift-C 加 offset keyframe 到所有可見 track → 毀掉動畫 — 警告，非待辦
TiXL 的危險 UX bug。sw 的 curve editor 是自己蓋的——**設計 keyframe 操作時別重蹈：一個操作不該無聲毀掉使用者既有的 keyframe。**

---

## 未決（開工時拍板）

- A/B 施工順序（柏為當下創作哪個更急）。
- 路2 是否走獨立 offline exe（explore 推薦）還是 in-app offline 模式 flag。
- C 路1 的 fps 上限策略（real-time 掉幀時 degrade gracefully 還是固定 dt 假即時）。
- video matte 來源形式（匯入預算好的 matte 序列 vs sw 內可 keyframe 的遮罩 op）。
