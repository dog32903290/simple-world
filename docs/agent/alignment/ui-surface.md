# UI 介面與互動 — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 8 · 部分 4 · stale已做 2(6/25:reset-to-default 校準掉)。

## 真 gap(已驗證確實未做)

### [core] 畫布佈局範式：MagGraph 磁吸網格 vs imgui_node_editor 自由擺放

- **對齊規格**:最小對齊：節點固定寬度 140 canvas-unit、列高 35；輸入垂直堆頂部主 anchor+左側次 anchor，輸出底部主+右側次；拖放結束時 round 到最近 35 倍數並偵測共邊吸附（門檻 30 螢幕px）。若不重寫 MagGraph，至少釘 width=140、列高=35 讓視覺比例先對。完整磁吸（無縫+退化三角）列 important 後續。
- **驗證證據**:editor_ui.cpp:236-302 用 ed::BeginCreate/QueryNewLink + 內建 bezier；:454-487 拖曳直接把 ed::GetNodePosition 寫回 child.x/y（自由座標，無 round/snap）。grep snap/round/fmod/'* 35'/'/ 35' 在 editor_ui.cpp 的拖放路徑全無命中（只有 :57-128 的背景格線繪製用到 35，那是畫格線不是吸附）。node_draw.cpp 全檔無 SetNodeSize / 140 / 寬度釘定（grep 0 命中）——節點寬度由 imgui 內容自然撐開，非 MagGraphItem.Width=140。盤子上沒有磁吸貼齊、共邊、退化三角連線。

### [polish] 缺必填輸入(Required)的紅色指示器

- **對齊規格**:PortSpec 需先有 required 旗標（或 relevancy 列舉）。畫布在未接線且 required 的輸入 pin 旁畫閃爍小三角/點（blinkValue() 已存在可重用）。先在 spec 標 required 的 op（如需 Points 輸入的 modify 家族）。引入 Relevancy 概念才會升 important。
- **驗證證據**:graph.h:27-46 struct PortSpec 欄位為 def/minV/maxV/pinless/vecArity/multiInput/widget 等——grep required/Required/relevancy/Relevancy 在整個 graph.h 與 app/src/ 業務碼全 0 命中（唯一 'missing_input' 命中在 runtime/attack_detector.h 是別的東西）。node_draw.cpp 無任何未接線檢查或閃爍指示器。blinkValue() 雖存在（node_draw.cpp:159 用於 hover border）但未用在缺輸入提示。

### [important] 加節點搜尋：scatter-regex 比對 + 多因子相關度排序

- **對齊規格**:1) matchFilter 升級成 scatter：query 每字元依序出現（中間可夾字），大小寫不敏感。2) 結果照相關度排序：字首完全相符最高>包含>scatter；'_'開頭與內部 op 降權沉底。3) 至少做到打 rad 第一個 highlight 是 RadialPoints。簡化 relevancy（startsWith×8.5/contains×8.4/scatter×1/'_'前綴×0.1）即可，>2000 types 再升完整版。
- **驗證證據**:quick_add.cpp:81-90 matchFilter 仍是 case-fold 後 lower.find(fLower)（純子字串），檔內 :76-80 自承為 'QuickAddFilter_Substring' fork。:93-101 rebuildDisplayItems 只做 matchFilter 過濾，push_back 順序=g_allItems 原始註冊順序（specTypes 然後 compound），完全無 relevancy 排序。打 'rad' 第一個是註冊表第一個含子字串者，非 RadialPoints 優先。

### [polish] 加節點面板：空搜尋時的命名空間樹狀瀏覽

- **對齊規格**:NodeSpec 需先有 category/namespace 欄位。空搜尋時依 category 分群顯示（可摺疊群組或兩欄），群名用該 category 的 typeColor；打字時切回扁平 relevancy 清單。op 數還小，列 polish。
- **驗證證據**:quick_add.cpp:62-74 rebuildAllItems 把 specTypes()（atomics）+ g_lib.symbols（compounds）攤平成單層 g_allItems flat list，無分群/分頁/category。:217-247 結果就是一個扁平 Selectable 迴圈。graph.h NodeSpec（:47）無 category/namespace 欄位（grep 0 命中），故連分群所需資料都不存在。

### [important] 參數列：重置為預設值的 affordance — ✅ 已做(6/25 複驗 stale gap)

- **對齊規格**:1) '被 override 過' 用亮字、'跟隨 default' 用灰字（sel->overrides.count(p.id) 可判）。2) 加重置：點參數名 or 右鍵 'Reset to default' → 等同 erase override，包成 SetOverrideCommand(had=true,...erase 路徑)。3) 右鍵選單除 Animate 外加 'Reset to default'（非預設才 enabled）。inspector 最明顯功能缺口。
- **現況證據(6/25 校準)**:重置 affordance **已實作並 commit**——right-click context menu 的 'Reset to default' 項落在 `inspector_param_menu.cpp:26-37`（從 inspector.cpp 拆出，ARCHITECTURE rule 4）:gated on `anyOverride`（非預設才出現），逐 slot 跑 `ResetOverrideCommand` 包成 MacroCommand("Reset Value")→可 undo。inspector 兩處接線:free Float 列 `inspector.cpp:289-290`、Vec 列 `:175` 都呼叫 `animateContextMenu(..., resets)` 餵 ResetSlot。scenario `tests/scenarios/inspector_param_reset.scn`（override→reset→undo）PASS。兩個獨立 agent 確認存在。(原 6/22 survey 只讀 inspector.cpp、漏看拆出去的 param_menu→誤判真 gap。)規格 3 子項中 (2)(3) 已對；(1) override-vs-default 字色亮/灰仍可後續補。

### [polish] 右鍵/選單的 Undo 顯示下一步動作名稱

- **對齊規格**:CommandStack 補 peekUndoTitle()/peekRedoTitle()（各 command 已有 name()）。右鍵選單加 'Undo (<title>)'/'Redo (<title>)'，無可 undo/redo 時 disabled。polish——功能已在，缺顯示。
- **驗證證據**:command.h:39-41 CommandStack 只暴露 canUndo()/canRedo()/lastUndoName()（最近一個 undo 名）——無 peekRedoTitle，且無 UI 把它顯示出來。combine_dialog.cpp node_ctx(:149-198) 無 'Undo (...)' 項（grep 'Undo (' 0 命中）。功能本身在（editor_ui.cpp:324 Cmd+Z g_commands.undo()），缺的是把下一步名稱顯示在選單。

### [polish] 符號邊界 Input/Output 節點的特殊形狀(箭頭凸角)

- **對齊規格**:邊界 Input/Output 節點改畫帶方向尖角的五邊形（輸入左凸、輸出右凸）取代普通方塊，凸角用該 def 的 typeColor。目前文字+slot 可讀但形狀語彙與 TiXL 不同。polish。
- **驗證證據**:node_draw.cpp:174-210 drawBoundaryDef 畫成普通灰圓角方塊（:177-180 NodeBg 灰 + rounding 6 + border 1px），用文字 'in:'(:186)/'out:'(:194) + drawSlot 三角/圓標方向。無 AddConvexPolyFilled 五邊形凸角、無 framesSinceLastUpdate fade 凸角。

### [polish] 工具列/視窗框架範式：浮動 Toolbar vs TiXL 選單列+停靠窗格

- **對齊規格**:刻意簡化 fork（單浮動工具列+固定 inspector，非完整 docking+menu bar），對 native 單機工具合理，不建議盲目對齊整套 docking。若要靠近：New/Open/Save/Add 收進 BeginMainMenuBar、transport 留工具列。列出供柏為決定——非機能缺口，是介面組織差異。polish。
- **驗證證據**:toolbar.cpp:66 ImGui::Begin("Toolbar") 單一浮動視窗塞 New/Open/Save/Save As/Add Node/transport（:67-136）。grep BeginMainMenuBar/BeginMenuBar 0 命中——無頂部選單列。inspector.cpp:94 ImGui::Begin("Inspector") 固定右側浮窗，非可停靠 Parameter window。這是刻意簡化 fork。

### [important] 節點本體內的輸出縮圖預覽(Texture2D)

- **對齊規格**:節點本體內嵌縮圖是 TiXL 重要視覺工作流。要對齊需在 node_faces 為 Texture2D 輸出節點加通用 face：取該 child 的 resident 輸出貼圖 → ImGui::Image 畫在本體。core 級工作流差異，但與 'view⊥graph' 設計決策衝突，需柏為拍板要不要把預覽搬回節點上。
- **驗證證據**:node_faces.cpp kFaces 表(:123-125)只有一筆 {"AudioReaction", drawAudioReactionFace}——無 Texture2D 縮圖 face。drawNodeFace(:129-132) 只 dispatch 該表。node_draw.cpp 無 ImGui::Image。editor_ui.cpp:180-183 註解明寫 'live preview NOT welded to any node body … lives in Output window (view⊥graph)'——刻意把預覽拆到獨立 Output window。

## 部分完成(做了一半)

### [important] 節點本體內顯示參數 label + 即時值字串

- **剩什麼**:剩：在每個輸入列右側畫該 slot 有效值字串（sw::effectiveInput 取值，%.3f / enum 用 label / bool true|false），字色 labelColor.Fade(0.7)；scale<~0.25 隱藏 label、<~0.4 隱藏值；標題字級依寬度縮放；multi-input slot 畫左側群組框。label 那半已有，值字串+縮放+multi-input 框是缺口。
- **現況證據**:node_draw.cpp:116-144 pinRow 對每個非 pinless input 畫 type-colored slot + p.name（port 名稱 label 已畫）。但 grep effectiveInput / GetValueString / %.3f / valueString 在 node_draw.cpp 全 0 命中——本體內沒有任何即時值字串。也無 multi-input 群組框（node_draw 無 trapezoid/group-frame 繪製）。標題 :109 直接 TextUnformatted，不隨節點寬度 downScale。CanvasScale gating（>0.25 隱藏 label、>0.4 隱藏值）也不存在——pinRow 無條件畫。

### [polish] 參數值編輯互動：jog-dial vs ImGui 滑桿

- **剩什麼**:把 free scalar 從 SliderFloat 換成 DragFloat/DragScalar（無軌道、拖曳 scrub、雙擊打字，行為比 SliderFloat 接近 TiXL jog-dial）。靈敏度用 p.scale（目前 PortSpec 無 scale 欄位，需補或用固定值），min/max 只在 Clamp 為真時當硬界。Vec 已對。
- **現況證據**:inspector.cpp:300 free 常數用 ImGui::SliderFloat（有軌道+硬界 p.minV/p.maxV），非 DragFloat。Vec 已用 DragScalarN（:185/:144，靈敏度 0.01f）——這半已對 TiXL jog 行為（拖曳 scrub+雙擊輸入）。scalar 那半仍是 SliderFloat。

### [important] 節點右鍵選單缺：Disable/Duplicate/Select/Align/Display as/Add Comment

- **剩什麼**:node_ctx 補上 important 群：(a) Disable（節點層啟用切換，目前只有 inspector per-output Disabled）；(b) Duplicate（= copyChildrenToClipboard+pasteClipboardAt 偏移，現成 API 直接組）；(c) Delete（選單保險路徑，目前只有鍵盤）。後續 polish：Select connected/Align/Display as。Bypass 已有故非從零。
- **現況證據**:combine_dialog.cpp:149-198 node_ctx 現有：Copy(:152)、Paste(:155)、Rename Node(:163)、Rename Definition(:167，僅 compound)、Bypass(:181)、Combine(:194)。grep 確認無 Disable / Duplicate / Cmd+D / 選單 Delete / Align / Select connected / Display as / Add Comment（combine_dialog.cpp 0 命中那些字串；:85 的 BeginDisabled 是 quick-add cyclic greying 無關）。Duplicate 所需 API（copyChildrenToClipboard+pasteClipboardAt）現成（copy_paste_ui）。Delete 目前只靠鍵盤（editor_ui.cpp:307-315 Backspace + :345 BeginDelete）。

### [polish] 連線繪製：貼齊退化三角 + 各方向箭頭 + hover-to-split

- **剩什麼**:顏色/厚度層已對。缺：(a) 目標端朝向箭頭三角（ed::Link 無此能力，需自繪 overlay）；(b) hover 連線中段→插節點 split（完全沒有）。箭頭=polish 可自繪；split=important 但需自繪+命中測試。貼齊退化三角在非 MagGraph 佈局不適用，與 layout-maggraph 綁一起。
- **現況證據**:顏色/厚度/idle-fade 那層已忠實：node_style.cpp:103-117 connectionLineColor（DrawConnection.cs:44 Fade(Lerp(0.6,1,p))）+ connectionThickness（:114-117 Lerp(0.25,2.0,p)+selected?2）。editor_ui.cpp:199-221 ed::Link 畫內建 bezier，無箭頭、無 split、無吸附橘閃（grep AddTriangle/arrow/split/StatusAttention/orange 在 editor_ui.cpp+node_style.cpp 全 0 命中）。

## verify 戳破的 stale(survey 說沒做、其實做了)

### 框選矩形的填色/邊色數值

- fence_preview.cpp:127 填色 IM_COL32(26,26,26,26)（=0.1×255≈26，對 TiXL Color(0.1f)）；:128-129 邊框 IM_COL32(0,0,0,102)（=0.4×255≈102，對 Color(0,0,0,0.4)）外擴 1px；live 高亮被框節點 :117-123（strict rectsOverlap）；門檻 5px 與 Alt 擋在檔內其餘段（上一棒已驗）。唯一 fork=預覽 only、commit 交 ed 內建 fence——合理相容性 fork。可視為 done。
