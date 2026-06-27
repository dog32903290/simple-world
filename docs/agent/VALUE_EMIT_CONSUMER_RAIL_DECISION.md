# 決定：value-emit consumer rail 走方案 A（不走 B）

> **決定者**：柏為，2026-06-27（新 session，與另一同-checkout session 並行期間做的架構決定）。
> **狀態**：DECIDED — 尚未實作。`PointToMatrix → camera`（或任何消費 point-into-frame value-emit 的）consumer rail **開工前必先讀本檔**。
> **依據**：Opus subagent 對 cook-core 真實程式碼的對抗壓測（非摘要推斷）。
> **不在本批動程式碼**——本檔僅落定方向。

---

## 一句話決定

`PointToMatrix` / `GetPointDataFromList` 這族「從點群抽出數值」的輸出，未來被 in-graph 節點消費時，**走方案 A（強制每幀 cook 供應點群 + 1-frame settle + scope-correct forced cook），接受連續流動下慢一幀的延遲**。**不走方案 B（多輪 cook 零延遲）**；B 的 hook 也先不預留。

---

## 背景：為什麼這裡需要一個決定

SEAM 1「point-into-frame value-emit pass」（`cookPointValueOutputNodes`，`app/src/app/frame_cook.cpp:391`，跑在 `pg.cookResident` **之後**）帶兩條 latent risk。目前 **0 forward consumer**，所以兩條都 inert；一旦有節點開始消費這些輸出（典型用法：點群重心 → 驅動 camera），就會踩到：

- **Risk 1（one-frame-late／慢一幀）**：value-emit 寫在 `cookResident` 之後，而其餘所有 emit pass 寫在之前。in-graph consumer 在 `cookResident` 期間 pull 這些值 → 讀到**上一幀**的值。consumer 讀取路徑已完整存在（`resident_eval_graph.cpp:112-114` 泛用回傳 `extOut`；camera 消費點落在 cookResident 內的 command walk，`point_graph_resident_command_cook.cpp:141-144`）。這是 sw **push-cook** 的本質——TiXL 是 **pull-graph**，無此延遲。
- **Risk 2（stale-points-off-display-subtree／殭屍值）**：resident cook 是 target-driven（只 cook 顯示子樹）；`outBuf` 幀間從不清空（`point_graph_internal.h:125`）。若 `PointToMatrix` 的上游 Points src 不在顯示子樹內 → `residentCookedPoints` 回傳 **non-null 但 stale** 的前幀 buffer（`point_graph_debug.cpp:128-135`），非 identity。值可以任意舊（只要點群一直在顯示子樹外就永不更新）。

壓測裁決：**兩條 risk 都屬實。Risk 2 比 Risk 1 嚴重**（Risk 1 的值下一幀會追上；Risk 2 可任意舊且靜默無警告）。

---

## 方案 A：怎麼解（白話 + 工程定位）

A 是補三個洞，使用者只做一個動作（拉一條線「這群點 → 餵 camera」），三件事系統在背後自動做：

### 洞一 — 殭屍值（解 Risk 2）
**白話**：被拿去餵 camera 的那群點，就算沒接到畫面，也**強制每幀都算**。
**工程**：在 `cookResident` 的 `cookNode` 遞迴（`point_graph_resident.cpp:155`）終端 dispatch 前後，對 consumer rail 註冊的 supplier path 加一輪強制 cook；memo 保證在顯示子樹內的不重算，只補 off-subtree 那段。需要一個掃圖工具找出 `PointToMatrix → 上游 Points src` 的 supplier path 清單，由 `frame_cook.cpp` / `cook_host_values.cpp` 傳入。

### 洞二 — 強制算時忘了帶 scope（A 的隱藏陷阱，壓測揪出）
**白話**：強制算那群點時，必須把它原本的 camera / 解析度設定一起帶上，否則點算到錯位置。
**工程**：forced-cook 必須走能建 scope 的 `cookCommand` 路徑，**不能**只走光禿禿的 `cookNode`。off-subtree 強制 cook 若 miss `LiveCameraScope` 會踩 `point_graph_resident_command_cook.cpp:141-142` 自證的「prod-only black-hole」。MV 重心驅動 camera 的供應點群通常**不**在 camera 子樹內 → 正好會踩，必須處理。

### 洞三 — 硬切那一格（收掉 Risk 1 唯一可見破綻）
**白話**：偵測到「點群換了」的那一幀，camera 先**定住不動**（中性值/identity），不去吃還沒算好的舊重心；下一幀就正常。
**工程**：consumer rail 給一個 1-frame settle 規則——src path 變更的當幀，emit identity / hold 而非吃 stale 矩陣。排程一行不動。

---

## 殘留行為（落定後實際會看到什麼）

- **連續流動**：camera 仍**慢一幀**（16ms）。A 不消除這個。粒子炸開、camera 慢一幀跟上，肉眼幾乎無感。
- **beat 上硬切**：camera **不會**先飛到舊點群的莫名位置再彈回；而是**定住一格**，下一幀到新位置。把「看得見的錯誤彈跳」換成「看不見的一格停頓」。
- **MV 離線出片**：完全免疫延遲（離線逐幀算，16ms 不存在）。

---

## 為什麼不走 B（壓測的核心反轉）

B = 把每幀 cook 拆成多輪（算點 → 抽矩陣 → consumer 用剛算好的矩陣 → render），換零延遲。**駁回，理由如下：**

1. **VJ 最愛的 camera-feedback 環會在 sw 無聲壞掉。** VJ 常做 feedback 環（camera → 影響點 → 點算矩陣 → 回頭影響 camera）。sw **沒有 cycle 偵測**，只有 depth-cap（`resident_eval_graph.cpp:50` depth>64 回 0；`point_graph_resident.cpp:55-68` cook depth-cap → safe empty + 一次 warn）。TiXL 遇環**報錯**，sw **靜默砍成 0**。B 的「零延遲」承諾在環裡無法兌現，反而觸發靜默歸零。
2. **A 的一幀延遲反而讓 feedback 環收斂。** push-cook clone 用跨幀延遲解環是標準做法——A 把使用者接出的同幀環，強制翻譯成 sw 唯一合法的形態（跨幀）。這是順著架構世界觀，不是違背。
3. **效能**：B 第二輪 cook 若不加跨輪 memo，等於 cook-core 成本翻倍（含所有 GPU dispatch）。VJ 即時場景直接吃掉一半 frame budget。
4. **B 縫的是當初判斷「搬不動」的那條線**：B 想在 push 的身體上把 pull 的零延遲縫回來——那正是 TiXL 的 dirty-flag Slot runtime，當初明確判定搬不動。

**B 的 hook 先不預留**：在 cook-core 排程脊椎上鑿一個目前沒消費者的接縫，違反「只改承重線」。真有零延遲剛性需求時（目前沒有：MV 離線無延遲、VJ 一幀可接受）再開。

---

## 架構脈絡（為什麼 sw 在這點和 TiXL 不同）

- 純量 value 那條，sw 與 TiXL **一樣是 pull**（`db6d9ef` "evalFloat pull evaluator"）。
- 分岔只在 **point / GPU / stateful** 這條：GPU resident buffer pull 起來又貴又重複、跨幀粒子模擬在 pull 的無狀態世界觀裡彆扭、且決定性離線 render 要的正是「每幀一個統一 frame context」（`FRAME_SCHEDULER_CONTRACT.md`：state nodes update once per frame boundary）。
- TiXL 用 pull + dirty-flag Slot cache 撐住 GPU，那整套和 C# 物件模型深綁 = 搬不動的 runtime。sw 在搬不動處換成更簡單、對 GPU 與決定性 render 更友善的 **push-cook resident buffer**。
- **代價集中在一處付**：求值順序要手動顧（pull 免費包了這個）。本決定就是在付這唯一一筆帳的具體形態。

→ 這是**本質的醜**（GPU 同步 + 決定性 render 逼出來的），不是意外的醜。包進乾淨接縫（使用者只碰「哪群點餵 camera」旋鈕），不暴露內臟。

---

## 開工檢查清單（consumer rail 真正動工時）

1. supplier-path 掃圖工具（`PointToMatrix → 上游 Points src`）
2. forced-cook 走 `cookCommand`（帶 scope），非裸 `cookNode`
3. 1-frame settle（src path 變更當幀 emit identity）
4. golden 必須覆蓋 **multi-frame consumer path**（現有 goldens 用 stub accessor，不覆蓋）——至少：(a) off-subtree 供應點群每幀 fresh、(b) 硬切當幀 camera hold、(c) 連續流動慢一幀符合預期
5. 碰 cook-core driver → 驗 4 島守門綠
