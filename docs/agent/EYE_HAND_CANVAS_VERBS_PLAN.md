# Plan — eye/hand 換皮無敵化：畫布直連動詞 + 遮擋旗標

> 緣起（2026-06-26，柏為）：subagent 反覆死在「手」這關（raymarch 接線驗證卡住）。
> 查清楚 = **手的座標其實準的**（toolbar/node/quick-add 點擊都通過同一管線且正常）。
> 真正死的是 **node-editor 畫布的連線手勢**：①連線沒有免座標後門（選取有 `selectnode`）
> ②imgui-node-editor 的 `BeginCreate→QueryNewLink→AcceptNewItem` 是逐幀重新命中的狀態機
> ③浮動面板遮擋畫布、手點到面板（眼睛照記 rect → map 看似正常、靜默點錯）。

## 北極星 / 兩個世界
UI = 兩個世界：
- **正常 imgui UI**（選單/toolbar/inspector/slider/popup）→ 座標手可靠（除非被遮擋）。
- **自帶座標的畫布子世界**（node-editor / timeline / curve-editor）→ 座標脆弱，圖操作要**直接改資料模型**。

**換皮存活性**（柏為問）：`clean.png`+golden + 直連動詞 = 皮無關，永遠有用；座標 map+hook = 貼著皮，換皮按重寫深度重掛。**所以驗證越往 golden + 直連動詞靠，越換皮無敵。** 本 plan 就是把「圖操作」這塊從座標層搬到資料動詞層。

## 架構鐵律（每部件遵守）
- **verify 是葉子**：`hand`/`eye` 不准依賴 app 的 graph 型別（`SymbolConnection`/`AddWireCommand`/`g_commands`）。圖 mutation 一律走 **app-owned 函式指標 hook**（既有範式：`setMidiInjectHook`/`setLearnArmHook`，hand.cpp:48-50）。
- **不是平行假路徑**：`connect` 重用真實拖曳的同一個 `AddWireCommand` + 同一套 multiInput/reconnect/dup 驗證（editor_ui.cpp:252-295），只是入口從座標手勢換成 id。
- **語義契約**：動詞用 childId/slotId（資料層 id），不用座標；換皮不影響。
- 每部件 headless RED→GREEN 自證（`--selftest-*`），先看它在錯狀態 FAIL 再看對狀態 PASS。

---

## Part A（先做，load-bearing）：`connect` / `disconnect` 直連動詞

### 設計
- **hand.cpp** 加 parse：
  - `connect <srcChild> <srcSlot> <dstChild> <dstSlot>` → 呼 app-owned `g_connectHook(srcChild, srcSlot, dstChild, dstSlot)`。
  - `disconnect <dstChild> <dstSlot> [srcChild srcSlot]` → 呼 `g_disconnectHook(...)`（拔掉某 input 的 wire；multiInput 時可選指定 src）。
  - 加 `setConnectHook`/`setDisconnectHook`（仿 `setMidiInjectHook`）。即時呼叫（side-map，非 frame-queue；同 `midi` 範式，因為 mutation 走 command system 安全）。
  - slot 參數是**字串**（slotId 是 string，見 `SymbolConnection.srcSlot/dstSlot`）。
- **app 側 hook 實作**（放 ui/ 或 app/，editor 初始化時 `setConnectHook`）：
  1. 取**當前 compound** `cur`（doc 當前編輯目標，同 editor_ui.cpp drawCanvas 的 `cur`）。
  2. 驗證 srcChild/dstChild 存在於 `cur->children`、slot 存在於各自 NodeSpec.ports，方向對（src=output、dst=input）、dataType 相容（同 editor_ui.cpp:244-250 的 guard）。任一不過 → no-op + 設 `doc::g_status` 報錯（讓 agent 從 status/dumpgraph 知道失敗），**不亂改**（仿 `selectnode` 的 bad-id GUARD：寧可 no-op 不可亂動）。
  3. 過了 → **完全重用 editor_ui.cpp:255-295 的邏輯**：建 `SymbolConnection nw`，查 multiInput/old-wire，push `AddWireCommand` 或 reconnect `MacroCommand`（經 `sw::g_commands`，所以 undoable + 觸發 recook/relayout）。
  - `disconnect`：對應 `DeleteWiresCommand`。
- **抽公因式**：editor_ui.cpp 的拖曳分支與 hook 應共用同一個「apply-connection(cur, nw)」函式（一份邏輯兩個入口），避免兩路 drift。建議抽成 `sw::ui::applyConnection(cur, nw)` / `applyDisconnection(...)`，拖曳分支與 hook 都呼它。

### Harness（Part A）
1. **headless 單元**（`--selftest-hand-connect[-bug]`）：建一個小 compound（2 node，dst 有可連 input），`feedLine("connect a 0 b in")` → 斷言 `cur->connections` 多了那條 wire（src/dst/slot 正確）。`-bug` 用不存在的 childId → 斷言**零改動**（no-op guard）。multiInput 變體：連兩個 src → 斷言兩條都在。dataType 不符 → 斷言 reject。
2. **★端到端真連線**（`--selftest-connect-cooks` 或併入既有 raymarch 路徑）：建 `SphereSDF` + `RaymarchField`，**用 `connect` 動詞**接 Field→RaymarchField，cook（production 路徑 `pg.cook`→`pg.target()`），斷言 sphere silhouette（複用 `field_raymarch_output_golden.cpp` 斷言）。`-bug`=不連 → clearTarget 畫黑 → silhouette 消失 RED。**這條證 `connect` 產生的是「能 cook 出正確畫面的真連線」**，同時補上 raymarch 那條延後的視覺驗證（免座標、免面板遮擋）。

---

## Part B：`dumpgraph` — 讓 agent 免座標拿到 id/port

### 設計
- **eye** 加 req：`touch req_graph` → 寫 `graph.json`。
- 內容 = 當前 compound 的：`children[]`（childId、symbolId/opType、ports[]={id, dataType, isInput, multiInput}）+ `connections[]`（srcChild/srcSlot/dstChild/dstSlot）+ 當前 compound id/breadcrumb。
- eye 是葉子 → app-owned `setGraphDumpHook`（回傳序列化字串，或 app 直接寫 graph.json），eye 在 req_graph 出現時呼叫。
- 用途：agent 先 `dumpgraph` 拿 childId/slotId，再 `connect`——**全程無座標**。

### Harness（Part B）
`--selftest-graphdump`：建已知 compound → req_graph → 解析 JSON 斷言 children/ports/connections 數量與 id 正確。`-bug`=漏一個 child → 斷言抓到差異。

---

## Part C：眼睛遮擋旗標 — 把「靜默點錯」變「響亮報錯」

### 設計
- `recordItem`/`recordRect` 時**同時記下 owner window**（`ImGui::GetCurrentWindow()->Name`/ID）存進 `Item`。
- `writeWidgetMap` 時，對每個 item 中心點，走 imgui window stack（`imgui_internal.h` 的 `g.Windows`，z-order front=last）找**最上層、含該點、可收輸入**（非 `NoInputs`/`NoMouseInputs`）的 window；若它**不是 item 的 owner window** → `occluded: true`。
- map.json 每列加 `"occluded": true/false`。
- 用途：hand 或 driver 點之前看 occluded，搆不到就**大聲報錯**，不靜默點到面板。（這是遮擋邊界的一勞永逸——遮擋是系統性風險，連非畫布 UI 都會中。）

### Harness（Part C）
`--selftest-eye-occlusion`：headless 開兩個重疊 imgui window，在**下層** window 畫一個 widget + recordItem，上層 window 蓋住它 → 斷言該 item `occluded==true`；移開上層 → `occluded==false`。RED→GREEN：被蓋住的必須報 occluded。

---

## 施工順序 / 隊形
- **單一序列 lane（前景，零盲區，避 build-storm）**——三部件都動 verify/ 共享檔（hand.cpp/eye.mm），序列做。
- 順序：**A 先**（解眼前 raymarch 類問題 + 端到端證明整套成立）→ B（dumpgraph，A 的好搭檔）→ C（遮擋旗標，獨立可後補）。
- A 綠了就已經解掉柏為的痛點；B/C 是把邊界補滿。

## 完成定義
- A：headless connect/disconnect 自證 + **端到端 `connect`→cook→sphere golden** 綠（+各 -bug RED）。
- B：graphdump 自證綠。
- C：occlusion 自證綠（被蓋報 occluded）。
- 全部：`check_arch.sh` 綠（verify 葉子、無 app 型別洩漏進 hand/eye）、`run_all_selftests.sh --bite` 無回歸、新牙進 PASS 不 NO-BITE。
- **不碰業務邏輯**：只加 verify 動詞 + app-owned hook 接線（一行掛 hook，仿 midi）+ 抽 `applyConnection` 公因式（拖曳與 hook 共用，零行為改變）。
