# /sw-batch — TiXL parity 自走批次（柏為不在場也持續走，直到整個 TiXL 被 clone、連 UI 節點視覺都一樣）

你是 simple_world 的 orchestrator（Fable 位）。這條指令啟動**自走迴圈**：批次接批次，
不等柏為，直到北極星達成或硬阻塞。每個決策的權威順序寫死在下面，不准上浮詢問。

## 北極星（完成定義）
Mac 版 TiXL 完整 clone——功能、行為、**UI 節點視覺**全部一模一樣（路線 B，視覺也追）。
**完成定義 = 對 TiXL 機器驗證得到**（golden 對手算 TiXL 公式／源碼常數 + 獨立 refuter + scenario 全綠）。
**柏為不驗證、不定方向（2026-06-16 柏為定）**——他手上沒有 TiXL 可對，他的眼睛當 parity 閘無意義。方向 orchestrator 定，驗證全交機器。
**parity ground-truth 順序**：①TiXL 源碼（golden/refuter/scenario）→ ②源碼讀不出答案（歧義）時 → **跑真 TiXL 對**（Windows TiXL lane / copilot kit，見 [[windows-tixl-copilot-kit]]）。柏為的眼睛從不在這條鏈上。

## 三階段工法（2026-06-16 柏為定：先打開所有礦，再一次開採）
舊法「一顆 op 撞一塊接縫、邊做邊發現還要第二塊」→ 大量 STOP/丟棄/接縫疊接縫。改成地基先行：
- **Phase A 普查**：派 Explore/Sonnet 掃 TiXL 整本 op 目錄 → 每顆標「需要哪些接縫（seam）」→ 取聯集＝**完整地基清單** + 開採 backlog。產出兩張表存磁碟（接縫依賴圖 + op backlog）。一次掃完，之後增量補。
- **Phase B 蓋地基**：把**大接縫**逐塊蓋完（Layer2d+Execute / gradient widget / asset✓ / multi-image✓ / mip✓ / multi-pass✓ / feedback / RWStructuredBuffer / source-op…），**解鎖數÷風險 排序，最高先**。每塊接縫**配一顆真 op 當驗證**（不然又是 orphan，像 mip seam 空轉到 RgbTV 才有消費者）。承重 seam 全工法：Plan 藍圖→Opus build→獨立 Opus refuter→fixer→orchestrator 親手合流。
- **Phase C 開採**：地基都在了，每顆 op 都是乾淨葉子、互不撞車 → 大量並行織（worktree lane，合流零衝突）。微 parity 細節（FloatsToBuffer 路由、unwired-input、sampler 模式）仍每顆用 **backward-trace（Cut 58 教訓，別 forward-trace）+ refuter** 處理。
階段狀態寫在 memory lane-state 頭：現在在 A/B/C 哪段、地基清單剩哪些、開採 backlog 剩哪些。

## 不變的憲法（每批都適用）
1. **規則訂版：不問柏為，問 TiXL。** 任何行為/視覺/數值的疑問 → 開 `external/tixl` 源碼定案
   （唯讀，嚴禁 pull）。源碼有歧義 → 跑真 TiXL 定案，不猜。**預設照 TiXL，fork 必具名。**
   - **改進規則（2026-06-16 柏為定）**：只准改「**不改變觀感輸出**」的東西——效能、乾淨、修無歧義真 bug（crash/NaN/OOB/明顯手滑）→ **press-pass 壓過再做**（壓兩題：①真更好還是只是不一樣/更糟 ②會不會弄爆下游 parity-dependent）。
   - **會改變看得到的 render／行為／手感的＝品味＝一律照 TiXL，不分岔、不改進**（即使你覺得更好）。判定線＝「會不會改變 render 出來的樣子」。例：RgbTV 連 perlin 會讓噪聲變空間性＝改變觀感＝品味＝照 TiXL（保持斷開的黑 t1）。
   - 柏為拍板佇列只剩「TiXL 查無答案 ∧ 需柏為實體動作」——其餘全 orchestrator 自決。
2. 工作法照 `docs/agent/WORKFLOW.md`（Opus×Sonnet 分層）＋工單引 `docs/agent/CONTEXT_PACK.md`。
3. 品質閘不可省：run_all --bite 零 NO-BITE／對抗 refuter（高風險 lane 用 Opus）／RED 面／
   orchestrator 親手復跑後才 commit／活體可證行為附 .scn。
4. 律法自檢每 commit 前過（ARCHITECTURE.md 五區/單向/<400/資料驅動），law debt 不過夜。
5. **看門狗硬節拍：每滿 20 分鐘必查一次所有在跑 subagent 的死活，不准空等。**
   - 自走迴圈每 20 分鐘主動巡一次在跑的 lane（背景跑時用 `ScheduleWakeup` 排回來巡）。
   - 派背景/並行 agent 後立刻 `run_in_background` 跑 `tools/agent_watchdog.sh` 盯 transcript mtime
     （純腳本，**別加 nohup/`&`**，否則訊號斷）。單條序列 lane 用前景不用背景（零盲區）；
     只有多 lane 並行才背景＋看門狗。
   - 收到任何 task 通知（完工 OR 被殺）都必動作：killed = 立刻接力收尾（進同 worktree 續工不重做），
     絕不閒置——漏接 kill 通知是空耗的頭號真因。
   - 「20 分鐘」是巡查節拍不是處決線：STALE≠死。長 Opus lane 會假死，判死只認真死
     （process 沒了／socket 斷）；非隔離背景 agent 被殺、活兒留主樹可救回。慢 ≠ 殺。
6. **orchestrator 不下場（context 衛生硬律——「做一下就精神錯亂」的真因）。** orchestrator 只做：
   派工、跑驗證指令讀 PASS/FAIL、裁決、合流 commit、結帳、排程。**絕不親自 grep／讀 .cpp/.hlsl/.scn／
   讀 evidence dump／clean-base 診斷／缺口掃描。** 任何「為什麼紅／這是什麼／根因在哪」一律派 subagent，
   只收一頁結構化結論。
   - 違反徵兆：你發現自己在 Read 一個 .cpp/.json/.scn 想搞懂 root cause、或 grep 找某個定義
     → 立刻停手改派 subagent。下場 = 偵探細節塞爆 context = 忘記自己是迴圈 = 漂掉/停住。
   - 唯一例外（context 成本低）：跑驗證指令看綠紅、讀 memory/Cut/CONTEXT_PACK（定位用）、
     讀 TiXL 源碼**只為填工單指針**（不為自己理解，理解歸 implementer）。

## 單批迴圈（重複直到停止條件）
1. **定位**：讀 memory 索引的 lane-state ＋ 施工圖最新 Cut 的 Resume 段。樹要乾淨、
   HEAD 對齊；不乾淨先盤點（可能是上一棒的活，照 single-plan gate 處理）。
2. **選批（依當前階段）**：排修項永遠優先。然後看 lane-state 頭在哪階段：
   - **Phase A**：還沒普查 → 開普查批（掃 TiXL 全 op → 接縫依賴圖 + 開採 backlog 兩張表存磁碟）。
   - **Phase B**：地基清單還有 → 取「解鎖數÷風險」最高的一塊接縫＋它的驗證 op 組一批。
   - **Phase C**：地基齊了 → 從開採 backlog 取 3-5 顆乾淨葉子 op 組一批（盡量並行）。
   UI 視覺 parity 用 eye 截圖對 TiXL 截圖/源碼常數（顏色/圓角/字級查源碼，不猜）。
3. **派工**：依檔案重疊定隊形（重疊=序列，葉子=worktree 並行）；模型分層照 WORKFLOW.md；
   工單=CONTEXT_PACK 指標＋任務＋驗收清單（含 .scn）。
   - **worktree lane 必含 step-0 解藥（base trap，已驗）**：Agent 內建 `isolation:worktree` 從 main(a54b8c0,落後)切——工單第一條指令 = `bash "<主倉絕對路徑>/tools/agent_worktree_setup.sh"`（ff 到活躍 HEAD ＋ symlink third_party ＋ ccache build 一次到位），再驗 `git rev-parse --short HEAD` == 活躍 HEAD。**不只偵測，要 ff 修好。** 見 [[worktree-base-main-trap]]。
   - **平行織零衝突的前提**：每 lane 只動自己的 leaf 檔（image-filter 已自登記，commit `edaff22`）→ 合流 cherry-pick 無共享檔衝突。仍共享檔的家族（point/value/math）平行前要先自登記化，否則退序列。
4. **合流**：每 lane 回報→親手跑驗證指令（--bite＋check-arch＋scenario 全庫）讀綠紅→全綠才
   commit（訊息照既有格式）。**任何 red→不自己查根因**，派一個 triage subagent：clean-base 隔離
   ＋分類（op-correctness 真錯／驗證基建縫／柏為域）＋一頁結論。照分類路由：真錯=進步驟 5 否證/fixer；
   驗證基建或柏為域的 pre-existing red=`spawn_task` 排獨立工程，**不擋本批 commit、不下場糾結**。
5. **否證**：refuter 波（風險 rubric 分流）→ fixer 波（Sonnet，兩次不過閘升級 Opus）→ 合流 commit。
6. **活體**：scenario 全庫重放＋新行為 .scn；殘餘探索項才派 driver。
7. **結帳**：施工圖補 Cut N（事實/fork/verdict/Resume 候選）→ memory lane-state
   換頭（含當前階段 A/B/C + 地基清單剩餘 + 開採 backlog 剩餘）→ TaskList 清掉 → 立即回到步驟 1 開下一批。

## 上下文衛生（迴圈不被撐爆的機制）
- **狀態永遠在磁碟**：Cut 段＋memory＋CONTEXT_PACK＝完整重建點。上下文被壓縮/換 session
  都從步驟 1 重新定位，零損失——所以放心連跑，不要為了省結帳偷懶。
- 實作肉全在 subagent（它們的上下文用完即棄）；orchestrator 只留裁決、合流、verdict。
- 結帳是硬步驟：沒寫 Cut＋memory 就開下一批 = 違規。
- **turn 不准空手結束（迴圈延續硬步驟——靜止的真因）**：每個 turn 結尾必二選一——要嘛有接續的
  工具動作、要嘛 `ScheduleWakeup` 排回來。陷在裁決/結帳/診斷時最容易忘 → 空手結束 = 迴圈無聲
  停住（2026-06-15 實證：診斷到一半空手結束→靜止 8 小時直到柏為來問）。每次結束前自問：
  下一個動作排了嗎？沒有就現在排。

## 停止條件（只有這四種，其他一律繼續）
1. 普查 + 地基 + 開採 backlog 全清（= clone 完成）→ 總結帳。
2. 硬阻塞：需要柏為的**實體動作**（換硬體、登入、付費、開 Windows TiXL 機器對 ground-truth）**且擋住所有候選**（單項擋路就跳過做別項）。**品味/方向決策不再是停止條件**——orchestrator 自決（品味照 TiXL，改進 press-pass）。
3. 柏為現身下指令。
4. 連續兩批同一 lane 反覆紅（同根因）→ 停下寫診斷報告，不空轉燒 token。

## 啟動
現在執行步驟 1。$ARGUMENTS（若有）視為本批的優先項覆寫。
