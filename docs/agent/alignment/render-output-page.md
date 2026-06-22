# 渲染/輸出頁邏輯 — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 10 · 部分 1 · stale已做 0。

## 真 gap(已驗證確實未做)

### [core] 視窗層級輸出解析度選擇器完全缺席

- **對齊規格**:在 Output 視窗 toolbar 加一個解析度下拉（ImGui::BeginCombo），內建 Fill/1:1/16:9/4:3/480p/720p/1080p/4k/8k/4k Portrait（含同數值），存成資料表+builder（ARCHITECTURE 第7條資料驅動，存 settings 目錄 resolutions.json）。選出的 RenderResolution 經 ComputeResolution 邏輯算像素，傳進 framecook::run / EvaluationContext，讓 WindowFollow(0) 的 RenderTarget 與 image op 跟著走。先決條件：把 RequestedResolution thread 進 cook context（目前 16-byte GPU struct 與 resident ctx 都沒有此欄位）。session 值存進每視窗持久化狀態(見 OUT-09)。
- **驗證證據**:output_window.cpp 全檔(40-108)只有 Pin/Unpin 按鈕 + 一個 ImGui::Image，零解析度 UI。toolbar.cpp 唯一的 BeginCombo 是 "Audio In"(112行)，沒有解析度下拉。grep resolutions.json/ComputeResolution 全源零命中。解析度只活在每個 RenderTarget node 的 Resolution enum 參數(point_ops_rendertarget.cpp:521-535: WindowFollow/HD720/HD1080/UHD4K/Custom)。更根本的證據：context.RequestedResolution 從未被 thread 進 cook——value_op_floathash.cpp:24-26 明寫 GPU EvaluationContext 是『FROZEN 16-byte struct {frameIndex,time,deltaTime,_pad} — it has NO RequestedResolution』，field_camera.cpp:218-220/field_camera.h:127 把需要 RequestedResolution 的模式標成『DEFERRED (named fork)』。所以連支撐 OUT-01 的承重結構(window-res→RequestedResolution→image op)都還沒蓋。

### [important] 無 Fit / 1:1(Pixel) / Custom view mode 與 pan/zoom

- **對齊規格**:在預覽區實作可平移/縮放的 image canvas（記錄 scale+offset，滑鼠拖曳平移、滾輪縮放），toolbar 加 Fit/1:1 兩顆按鈕：Fit=貼合可用區、1:1=貼圖原生像素 1:1；使用者手動 pan/zoom 即切 Custom。底部疊 WxH ×scale 字。背景條紋為 polish 可後補。
- **驗證證據**:output_window.cpp:103-105 固定 ImGui::Image(tex, avail)，無 scale/offset 狀態、無滑鼠拖曳/滾輪縮放、無 view mode、無 Fit/1:1 按鈕、無 WxH×zoom 字、無棋盤底。grep pan/zoom/ViewMode/FitArea/SetScaleToMatch/scalable 的命中全在 timeline_window.cpp / timeline_canvas.cpp / timeline_internal.h(時間軸自己的 ScalableCanvas)，output_window 一個都沒有。keymap.cpp:142 handleFocusSelection 是 F 鍵對『node canvas』做 ed::NavigateToSelection(152行)，不是輸出 Fit。

### [important] Pin 只有單一 id，無 view-instance / evaluation-start-instance 拆分

- **對齊規格**:加獨立 g_evalStartNode（session 變數），cook 時若有設則先 cook eval-start 路徑（更新上游 stateful 狀態），再把 view-target 的 output realize 進 target()。combo/選單加 Pin as start operator / Unpin start operator，pin 顯示加 -> EvalStart (Final) 後綴。屬 important（多數 view==eval，但對 feedback/stateful 鏈是真實差異）。
- **驗證證據**:editor_ui.h:13-17 只有單一 int g_pinnedNode(註解明寫『what I'm looking at』session-only)，grep g_evalStartNode/evalStart/eval.start 全源零命中。main.cpp:231-243 targetPath 單一解析鏈(pinned→selected→current terminal→root terminal)，無『先 cook eval-start 路徑再 realize view-target』的兩段邏輯——只有一條 cook 目標(framecook::run(*g_pointGraph, targetPath) main.cpp:243)。無隱藏子視窗 cook、無 recompute:false 概念。

### [polish] 無多輸出視窗（單一固定 Output 視窗）

- **對齊規格**:低優先。把 OutputWindow 改成可多實例（每實例自己的 pin/解析度/view state，標題 Output 1/2/...），或先保持單例並標註為刻意縮減。柏為單機表演工作流多半單視窗即夠 — polish。
- **驗證證據**:output_window.cpp:48 固定 ImGui::Begin("Output") 單例；output_window.h:11 單一 drawOutputWindow()，main.cpp:262 只呼叫一次。無 instance 集合、無 AllowMultipleInstances、無 Output 1/2 標題計數、無 per-instance pin/解析度/相機/背景結構。

### [important] 無截圖功能

- **對齊規格**:toolbar 加 Snapshot 按鈕：把 previewTexture() 內容存成 <專案目錄>/Screenshots/<時間戳>.png（eye 的 PNG 寫出機制可複用為實作肉，但要從 verify 區搬成 platform/app 的產品 PNG 寫出，遵守 ARCHITECTURE 分區）。可綁一快捷鍵。
- **驗證證據**:output_window.cpp toolbar 只有 Pin/Unpin，無 Snapshot 按鈕。grep Screenshots/StartSavingToFile/CGImageDestination/writeImage 在 verify/ 之外零命中。PNG 寫出只存在於 verify/eye/eye.mm(測試路徑 dumpTextureRGBA)；platform/image_decode.mm 是 decode-only(讀，非寫)。截圖快捷鍵 grep RenderScreenshot 零命中。

### [important] 無影片/序列匯出（Render Animation + 進度條 + RenderWindow 設定）

- **對齊規格**:大件可後置。需要：(a) 離線逐格 cook 迴圈（推進 transport 到指定範圍逐格 cook，每格讀回 target()）；(b) 圖片序列輸出（PNG 序列最簡）或經 AVFoundation 包成影片；(c) 匯出設定(範圍/FPS/解析度)+進度條+cancel。建議先做 PNG 序列再談影片容器。
- **驗證證據**:grep RenderAnimation/videoExport/video.export/TryStartVideoExport 全源零命中。output_window.cpp 無進度條、無 RenderWindow 設定按鈕。無離線逐格 cook 迴圈(框架只有 frame_cook.cpp:run 的單格即時 cook，由 MTK::View::draw 驅動 main.cpp:184-244)。

### [important] 無獨立演出輸出 / Player（全螢幕播放執行檔）

- **對齊規格**:大件後置。最小可行：在現有 app 加『全螢幕演出』模式（隱藏所有 imgui chrome，只把 previewTexture() 以選定解析度貼滿整個 NSWindow/螢幕，跑 transport），含進入/退出全螢幕快捷鍵與 --loop 行為。完整對齊(獨立 player+內容包)更後置。
- **驗證證據**:grep fullscreen/full.screen/player/playback.window 在產品碼零命中(只有 transport playing/playback 等不相關詞)。main.cpp 只是編輯器 shell(NSApplication+MTKView+imgui chrome 全程畫，draw() main.cpp:184-313)，無隱藏 imgui chrome 的全螢幕放映模式、無 --loop/--windowed flag(main.cpp:89-116 的 flag 只有 --help/--version/--open)。無獨立 player 執行檔。

### [polish] 輸出視窗狀態不持久化（pin/解析度/背景/相機/gizmo 都是 session-only）

- **對齊規格**:視 sw 是否要把『看什麼/解析度/背景』隨專案存。最低限：把選定解析度與(若加了 OUT-04 的)eval-start 存進專案層級設定(.swproj)。pin 是否持久化是品味抉擇——TiXL 存 PinnedInstancePath，sw 目前刻意不存。要對齊就把 pin/解析度/背景色寫進 .swproj 一個 output-window 區塊。建議先做解析度持久化。
- **驗證證據**:editor_ui.h:13-17 g_pinnedNode 註解明寫『Session-only state — like g_selectedNode it is NEVER serialized into .swproj』。output_window.cpp:3 『the pin (g_pinnedNode) is session-only state, never serialized』。output_window.h:5-6 『Pinning never touches doc::g_graph and never enters .swproj』。grep OutputWindowState 零命中。compound_save.cpp/document.cpp 無 output-window state 序列化。原 sw_status=diverges 偏輕——這是刻意設計的『完全不存』，不是行為分歧，對齊缺口=real-gap。

### [polish] 多 output 的 op 無法選看哪個 output slot（Show Output... 子選單）

- **對齊規格**:當 viewNode 的 spec 有多個 output port 時，在 pin combo 加 output 選擇子選單，記住選定 output id；cook 目標解析改成『該 op 的選定 output』而非永遠第一個。多數現有 op 單 output，屬 polish。
- **驗證證據**:output_window.cpp:31-37 outputTypeOf 永遠取第一個非 input port(`if (!p.isInput) return p.dataType`)。grep selectedOutput/outputId/Show Output/output slot 零命中——pin combo 無 output 選擇子選單。main.cpp:228-230 viewPathOf 只傳單一 childId 進 viewProducerPath，無選定 output slot。注意 compound_graph.cpp:215 viewProducerPath 內部確有 viewSlot 變數做 traversal 正確性(沿 wire 重新 thread srcSlot)，但那是內部解析路徑用，entry point 永遠從 main output(outputDefs[0])起，使用者無法挑 slot。

### [polish] 無背景色控制（Command 型別）

- **對齊規格**:當 view 目標輸出型別是 Command(DrawPoints 等自畫節點)時，toolbar 顯示一個背景色 picker，把顏色傳進 cook 當清除色(取代目前寫死的黑)。Texture2D 時隱藏。屬 polish。
- **驗證證據**:grep BackgroundColor/background.color 在 output/toolbar/frame_cook 零命中(只命中其他不相關)。output_window.cpp:100-102 註解明寫未支援型別 cook 已清成黑，無背景色 picker。Command 型別(outType=="Command" output_window.cpp:94)走 RenderTarget executor 但清除色寫死黑，無 ColorEditButton。

## 部分完成(做了一半)

### [core] 預覽貼圖固定 512×512，從不貼合輸出視窗 / drawable

- **剩什麼**:剩兩件：(a) WindowFollow 解析度仍寫死 512²——給 PointGraph setTargetSize(w,h) 或讓建構/每格的 windowSize = Output 視窗實際內容區像素而非 512。(b) 預覽 ImGui::Image 改成依貼圖長寬比置中貼合（非整片拉伸），否則非正方形輸出(含已能出現的 displayTex)會變形。注意：『輸出貼圖能是任意解析度』這條已部分成立(displayTex 路徑)，不必從零做。
- **現況證據**:三段拆開看：(1) 建構固定 512²確認：main.cpp:165 new PointGraph(...,512,512)；point_graph.cpp:65-78 p_->width/height 來自建構參數，無 setTargetSize/resize 方法(grep setTargetSize 全源零命中)。(2) 但『預覽永遠 512²』這句不全對：point_graph.cpp:107 target() 回 `p_->displayTex ? p_->displayTex : p_->target`——當一個 RenderTarget node 帶顯式 Resolution enum(HD1080 等)cook 時，displayTex 是那個解析度的貼圖(point_graph.cpp:1359 / point_graph_resident.cpp:800 `p_->displayTex = tex`)，target() 就回非 512²的貼圖。所以非 512²輸出『可以』出現——但只在 node 自己選了固定解析度時；WindowFollow(0) 仍永遠解析成 RenderResolution{p_->width,p_->height}=固定 512²(point_graph_resident.cpp:711-712)，因為 windowSize 永遠是建構時的 p_->width/height。(3) ImGui::Image 拉伸確認：output_window.cpp:104-105 `ImGui::Image(tex, avail)` 把貼圖拉滿可用區，無長寬比置中——非正方形 displayTex 會變形。

## verify 戳破的 stale(survey 說沒做、其實做了)
