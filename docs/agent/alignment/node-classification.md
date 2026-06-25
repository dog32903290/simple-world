# 節點分類機制與 library — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 8 · 部分 0 · stale已做 0。

## 真 gap(已驗證確實未做)

### [core] 節點沒有 namespace/category 分類軸

- **對齊規格**:在 NodeSpec 加 `std::string ns;`（原子 op，如 "Lib.numbers.float.basic"），在 Symbol 加 `std::string ns;`（compound 預設 "user"）。每個 op 註冊時填它對應 TiXL 的 namespace（external/tixl/Operators 下資料夾路徑為 ground truth，Operators/Lib/X/Y/Z → "Lib.X.Y.Z"）。承重線——後三條 gap（relevancy、browse-tree、usage、tags 加權）全依賴它。
- **驗證證據**:graph.h:47-55 `struct NodeSpec { std::string type, title; std::vector<PortSpec> ports; float (*evaluate)(...); }` — 無 ns 欄位。compound_graph.h:112-136 `struct Symbol { id; name; atomic; inputDefs; outputDefs; children; connections; nextChildId; animator; annotations; }` — 無 ns 欄位。node_registry_generators.cpp:11-32 的 NodeSpec literal 是 positional aggregate init `{"RadialPoints","RadialPoints",{ports...},nullptr}`，欄位數對應 {type,title,ports,evaluate}，無 namespace 槽。全 app grep namespace 分類用法（排除 C++ `namespace` 關鍵字）= 零命中。rebuildAllItems (quick_add.cpp:62-74) 只 push specTypes() 與 compound s.id，從未讀 namespace。

### [core] 搜尋結果無排序（registry 順序）vs TiXL 多因子 relevancy

- **對齊規格**:rebuildDisplayItems 改為對每個通過過濾的 item 算 relevancy 分數再降冪排序。第一階核心權重子集：equals×8.6 / startsWith×8.5 / contains×8.4 / 單字元 startsWith×20 / PascalCase 子序列×4 / namespace 以"Lib"開頭×3 / namespace 含"_"或"dx11"×0.1 / name 以"_"開頭×0.1 / 名稱含"OBSOLETE"×0.01。usage/project 加權延後。輸出取前 100。權重逐一照抄 TiXL 不自創。依賴 namespace-metadata。
- **驗證證據**:rebuildDisplayItems (quick_add.cpp:93-101) 只做 `for item in g_allItems: if matchFilter(...) push` — 無任何分數計算、無排序、無 Take(100)。輸出順序 = g_allItems 順序 = specTypes() registry 順序 + compound map 迭代序。全 app grep `relevancy`/`usageCount` 在分類語境 = 零命中（只有 point op 的 Scatter 等領域詞、compound_load 的 'obsolete override' 警告，皆無關）。

### [important] 過濾是純子字串 vs TiXL 散射 regex（字元間插 .*）

- **對齊規格**:matchFilter 升級為散射子序列：query 字元在名稱中依序（可不連續）出現即命中（等價 char-interleaved .* regex，手寫 O(n) 雙指標，不必引 std::regex）。match 範圍擴成 name OR namespace OR description（待 namespace 落地）。two-part preset 搜尋標 fork 延後（無 preset 系統）。注意散射放大結果集，必須與 relevancy 排序一起上。
- **驗證證據**:matchFilter (quick_add.cpp:81-90) = case-fold 兩邊後 `lower.find(fLower) != npos` 純連續子字串。檔頭 comment (line 76-80) 與 quick_add.h:8-10 都自承這是 fork "QuickAddFilter_Substring"，明說 TiXL 用 scatter regex 而我們用 strstr。match 範圍只比 item 名（displayName 也只回 name/id），未涵蓋 namespace（不存在）或 description（不存在）。runQuickAddSelfTest Test2 (line 301-315) 斷言的正是連續子字串行為。

### [important] 搜尋框為空無策展瀏覽樹 vs TiXL namespace 分類樹

- **對齊規格**:空搜尋畫分類樹取代『列全部』：第一階用 namespace 第一段（Lib.numbers/Lib.image/Lib.point/...）做可折疊 group header。完整對齊照抄 SymbolBrowsing.UpdateLibPage 的策展 Group 樹。需先有 namespace 欄位。Essential 過濾需 tags；無 tags 時第一版葉節點列全部+relevancy 排序，標 fork。依賴 namespace-metadata + symbol-tags。
- **驗證證據**:空 filter 時 matchFilter 回 true（quick_add.cpp:81-82 `if (!filter || filter[0]=='\0') return true`），rebuildDisplayItems 把整個 g_allItems 全推進去，drawQuickAdd 的 list loop (line 218-247) 扁平列全部 — 無 group header、無折疊、無 namespace 階層、無 Essential 過濾。runQuickAddSelfTest Test1 (line 291-299) 斷言空 filter = 列全部，正是這行為。

### [important] 從 port 拖線加 op 無型別預過濾 vs TiXL inputFilter/outputFilter

- **對齊規格**:openQuickAdd 增可選 inputFilterType/outputFilterType（dataType 字串）。先實作『拖線到空白→開 quick-add』手勢（TiXL QueryNewNode + PlaceholderCreation.Open），帶入被拖 port 的 dataType。rebuildAllItems/rebuildDisplayItems 在有 filter 時只保留 ports 內有相容方向且 dataType 相等(或可轉換)的 op。頂端畫型別提示列。阻塞於拖線-加-op 手勢。
- **驗證證據**:openQuickAdd 簽名 = `void openQuickAdd(float cx, float cy)`（quick_add.h:25），無型別參數。唯一呼叫者 keymap.cpp:169 `openQuickAdd(canvasPos.x, canvasPos.y)`（Cmd+F 鍵），不帶型別。全 app grep inputFilter/outputFilter/filterType = 零命中。更關鍵：依賴的『從 port 拖到空白→加 op』手勢本身不存在 — editor_ui.cpp 的 BeginCreate/QueryNewLink (line 236-302) 只處理 pin→pin 連線，type 不合就 RejectNewItem，無 QueryNewNode/placeholder-creation 路徑（grep QueryNewNode = 零命中）。此 gap 阻塞於該手勢尚未實作。

### [polish] 無使用次數/最近用加權 vs TiXL UsageCount 灌入 relevancy

- **對齊規格**:排序加權加入使用次數：開 palette 時掃 g_lib 計每個 op 型別被多少 SymbolChild 引用，於 relevancy>閾值時乘 `1 + K*count/total`（K=500，照 TiXL）。同 package 加權標 fork（無 multi-package 概念）。依賴 search-relevancy-ranking 先落地。
- **驗證證據**:全 app grep usageCount/usage_count/recent 的節點分類用法 = 零命中。quick_add.cpp 從未掃 g_lib 計引用數，rebuildDisplayItems 無任何 usage 乘子。前置 relevancy 排序（search-relevancy-ranking）也尚未落地，故此調味層雙重缺席。

### [polish] Symbol 無 tags(Essential/Obsolete/NeedsFix) vs TiXL SymbolTags

- **對齊規格**:在 NodeSpec 加 tags 位元欄（至少 Essential、Obsolete）。Essential 供 browse 樹精選；Obsolete 進 relevancy 沉底（名稱含 OBSOLETE 的 ×0.01 是 TiXL 既有路徑，可先用名稱規則替代直到 tags 落地）。NeedsFix/Unused 屬 SymbolLibrary 維護視窗範疇（尚無該視窗），整體 polish。
- **驗證證據**:NodeSpec (graph.h:47-55) 與 Symbol (compound_graph.h:112-136) 皆無 tags 位元欄。全 app grep essential/symbolTags/needsfix = 零命中；'obsolete' 唯一命中是 compound_load.cpp:231/251 的『obsolete override』警告（指 stale 覆蓋鍵，與 SymbolTags 無關）。無 Essential→browse 樹無從精選；無 Obsolete tag→只能靠名稱含 OBSOLETE 規則替代。

### [polish] 結果列無型別顏色/型別名後綴/help tooltip vs TiXL DrawSymbolUiEntry

- **對齊規格**:結果列：(1) 依 op 第一個 output port 的 dataType 取色畫文字 — 直接重用 ui/node_style.cpp 既有 typeColor() 表（無需新建表）；(2) op title 後綴 dim 的 dataType 名；(3) hover tooltip。tooltip 內容理想用 op description，但 NodeSpec/Symbol 無 description 欄位（缺）→第一版可顯示 type/ports 摘要，或與加 description 欄位一起做。視覺細修，polish。
- **驗證證據（6/25 已做 e427d55）**:✅ 三項核心特徵全部落地，抽進共享 helper：`app/src/ui/quick_add_tree.cpp:64` `drawResultRow(...)` 一處渲染所有結果列，(1) 標題型別色 — `labelColor(outType)`（`outType = firstOutputType(id)`，quick_add_tree.cpp:66/73）以 PushStyleColor 替標題上色；(2) dim dataType 後綴 — `ImGui::TextDisabled("  %s", outType)`（line 82，gated on showType：flat 搜尋列 showType=true 顯示、namespace 樹列 showType=false 抑制，對齊 TiXL SymbolBrowsing.cs:81/163）；(3) hover tooltip — `portSummaryTooltip(id)`（line 85，顯示 type/ports 摘要）。色表用既有 `app/src/ui/node_style.cpp:69` `labelColor(const std::string& dataType)`（string-keyed OperatorLabel variation，b1.3 s0.4 a1.0，PlaceHolderUi.cs:424）。flat-list 呼叫點 `quick_add.cpp:322` 傳 showType=true。scenario `tests/scenarios/quick_add_type_color.scn` 行使此路徑且 PASS。**唯一 deferred sub-detail**：tooltip 的 op-description 加值版（NodeSpec/Symbol 仍無 description 欄位）— 規格本即把它列為「第一版顯示 type/ports 摘要，或與加 description 欄位一起做」，三項核心不依賴它。

## 部分完成(做了一半)

## verify 戳破的 stale(survey 說沒做、其實做了)
