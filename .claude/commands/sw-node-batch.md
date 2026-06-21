# /sw-node-batch — 家族並行節點生產（消除共享撞點 → 每家族一條獨立 lane 平行織）

`/sw-batch` 的特化版，**只為一件事**：把節點生產加速。手段＝把脊椎拆成 per-family 葉子，
讓多條家族 lane 在各自 worktree 同時生產、零衝突合流（git 自動 merge 不同檔）。
憲法、權威順序、品質閘、結帳全部繼承 `/sw-batch`——下面只寫**不同的地方**。

繼承自 `/sw-batch`（不重述）：北極星＝Mac TiXL 完整 clone；規則訂版問 TiXL 不問柏為；
工作法照 `docs/agent/WORKFLOW.md`（Opus×Sonnet 分層）＋工單引 `docs/agent/CONTEXT_PACK.md`；
品質閘（run_all --bite 零 NO-BITE／對抗 refuter／RED 面／orchestrator 親手復跑後才 commit／
活體 .scn）；律法自檢每 commit；結帳補 Cut＋memory lane-state。

---

## ★求生條文（繼承 `/sw-batch` 的流血教訓，**並行放大、不可省**——這條指令最容易在這裡死）
> 上面那行「憲法全部繼承」涵蓋了「做對」的條文，但 sw-batch 流血學到的是**「活下來」**的條文。
> sw-node-batch 同時跑 N 條背景 worktree lane＋長 fan-out → 把每個坑乘以 lane 數、拉長到跨越連線斷的窗口。
> 這五條**必須照下面的並行場景重述，不准只當「繼承」帶過**（驗屍結論：generic 繼承會漏放求生條文）。

1. **每條背景 lane 派出後，立刻 `run_in_background` 跑 `tools/agent_watchdog.sh` 盯它的 transcript mtime**
   （純腳本，**別加 nohup/`&`**，否則訊號斷）。N 條並行 lane＝N 個死亡暴露點，看門狗對每條都要在。
   見 [[subagent-death-detection]]。
2. **收到任何 lane 的 task 通知（完工 OR 被殺）都必動作**：killed／API overload／連線斷死＝**立刻接力**
   進**同一個 worktree** 續工不重做（lane 的活在它自己的 worktree，可救回）。**漏接 kill 通知＝空耗頭號真因。**
   （本 session 實證：refuter 被 API overload 殺，先驗樹無 residue→立刻重派。）
3. **turn 不准空手結束＝迴圈/orchestrator 活著的命脈。** fan-out 後每個 turn 結尾必二選一：要嘛有接續工具動作、
   要嘛 `ScheduleWakeup` 排回來巡。**連線斷的真兇是長 streaming（20-34min）中途偶發中斷**（非網路故障，
   見 [[sw-autoloop-system]]，`API_TIMEOUT_MS=2400000` 已寫 settings.json）→ 長 fan-out 期間 orchestrator
   一定要 ScheduleWakeup-survive，否則靜止→harness 回收 worktree→**未固化的完工 lane 蒸發**（見第 4、Phase 2.1）。
   **★固化先於驗證這機制本身要 orchestrator 活著才能執行——所以求生條文是固化的前提，不是它的替代。**
4. **狀態永遠在磁碟**（Cut＋memory lane-state＋per-lane 固化 commit）＝完整重建點。連線斷/換 session/context 壓縮
   都從步驟 1 重新定位、零損失——所以放心連跑。
5. **orchestrator 不下場（context 衛生硬律）**：只派工/跑驗證讀綠紅/裁決/合流 commit/結帳/排程。
   **絕不親自 grep／讀 .cpp/.hlsl／clean-base 診斷**——任何「為什麼紅」一律派 triage subagent 收一頁結論。
   並行 N 條 lane 的 dossier 若 orchestrator 下場逐條讀肉＝context 秒爆＝忘記自己是迴圈＝漂掉。見 [[sw-batch-orchestrator-no-fieldwork]]。
6. **單一 driver（防雙開血債）**：啟動定位後若 git status 有符合 Resume 下一步的未 commit 改動＝另一 session/lane 在動
   → 立刻停、問柏為哪個留，落後者退出不碰檔不排 wakeup。並行 lane 各自 worktree 不撞主樹，但 orchestrator 本身
   只能有一個。見 [[sw-batch-no-parallel-launch]]。

---

## 承重洞見（這條指令存在的理由）
加一顆 point GPU op，並行家族 agent 會撞的**真共享撞點有兩類**：

1. `registerBuiltinPointOps()`（`point_ops.cpp`）——每顆 op 一行 fwd-decl + register 呼叫。
2. `CMakeLists.txt`——source 清單（`point_ops_<name>.cpp`）+ shader 清單（`<name>`）各一行。

> ⚠️ 柏為原設計點名 registry/dispatch/selftest，但實證後修正：registry 家族表早已分家（批次16-R，不撞）；
> selftest **不是並行撞點**——每顆 op 的 golden 寫在自己的 `point_ops_<name>.cpp`（現有慣例），
> 只剩 `point_ops.h` 宣告 + `selftests.cpp` kTable 各一行是共享，由 orchestrator 合流時統一加
> （見 Phase 2.3），不是家族 agent 並行撞。真撞點＝register + CMake 兩類，Phase 0 解這兩個。

已是葉子、**不撞**：registry 家族表（`node_registry_<family>.cpp`）、op cook（`point_ops_<name>.cpp`，
含該 op 的 golden selftest）、shader（`shaders/<name>.metal`，per-op）。

**Phase-0 模板已存在**：`node_registry.cpp` 就是答案的長相——薄 builder，固定幾行
`append(<family>Specs())`，家族內加 op 不動這檔。Phase 0 把這形狀推廣到 register + CMake。

---

## Phase 0（一次性架構改進，序列、先落地再 fan-out）
**單 agent、單 worktree、序列做完並 commit 後，Phase 1 才准並行。**（Phase 0 本身動共享檔，
不能和家族 lane 同時跑。）開跑前先驗 Phase 0 是否已完成（grep `registerBuiltinPointOps`
是否仍是單體 monolith 的 fwd-decl+call 清單）；已拆＝跳過 Phase 0 直接進 Phase 1。

真撞點各自的解（照 `node_registry.cpp` 的 per-family-builder 風格，**零行為變更**、
op 名/port/cook 綁定逐字搬，附 `--bite` 全綠＋一顆 RED 證沒搬壞）：

- **撞點 1（register）**：`registerBuiltinPointOps()` 拆成 per-family 註冊函式
  （`registerGeneratorPointOps()` / `registerPointModifyPointOps()` / `…Combine` / `…Particle` /
  `…Draw` / `…ImageFilter`），各家族一檔。中央函式只剩固定家族呼叫行，凍結——家族內加 op 只動自己那檔。
  ⚠️ **RED 證要選不自註冊的承重路徑（RadialPoints）**：很多 op 的 golden 在自己檔內又 register 一次，
  拔 registrar 也不紅；唯獨 RadialPoints selftest 只靠中央 builder（註解 `registerGeneratorPointOps()`
  → `--selftest-radialop` FAIL 才證 per-family 路由真接上）。
- **撞點 2（CMake）**：`point_ops*.cpp` 與 `shaders/*.metal` 改 `file(GLOB … CONFIGURE_DEPENDS)`
  收集（加檔零 CMake 編輯）。glob 後 reconfigure，驗證抓全（盤點 op 數 == 重構前中央）。
- **selftest 不需 per-family 化**：見承重洞見——op golden 在自己 op 檔，共享的 `point_ops.h` 宣告
  + kTable 一行由 orchestrator 合流統一加（Phase 2.3），不是 Phase 0 的活。

Phase 0 收尾＝`--bite` 全綠 + `check-arch` 綠 + commit（訊息照既有格式）。

---

## Phase 1（並行生產，每家族一條 worktree lane）
家族 lane 清單（對齊 registry 家族）：generators／point_modify／point_combine／
particle／image_filter／math。**每條 lane 一個 agent、一個 worktree、只改自己家族的檔。**

派工隊形：葉子並行＝各 lane 同時開，`isolation: "worktree"`（**必設**——四航教訓：
背景 implementer 漏設 worktree→在主樹幹活→收割未交 dossier）。模型分層照 WORKFLOW.md
（語義/對抗 op = Opus；機械 cheap op = Sonnet，兩次不過閘升級 Opus）。
**★worktree base-trap 解藥（每張 lane 工單第一條，不只偵測要 ff 修好）**：Agent 內建 `isolation:worktree`
從 **main（落後活躍 HEAD）** 切，不是活躍分支 → N 條 lane 全部 base-wrong。工單 step-0 ＝
`bash "<主倉絕對路徑>/tools/agent_worktree_setup.sh"`（ff 到活躍 HEAD ＋ symlink third_party ＋ ccache
build 一次到位），再驗 `git rev-parse --short HEAD == 活躍 HEAD`，不符＝WRONG BASE 立停。見 [[worktree-base-main-trap]]。

**前置（orchestrator，非 lane agent）**：派 Explore 掃各家族 `external/tixl` 缺口，產 lane ledger
（cheap 候選 + TiXL .cs/.hlsl 路徑 + 照抄的模板 op）。cheap = 純 kernel + 值參數，無隱藏
buffer/texture/curve/sim-state 依賴；**先 grep `.cs` 確認，sizing 不可信、cheap-input≠trivial-impl**
（批次18／DoyleSpiral 教訓：掃描判 cheap 但核心算術在 compound `.t3`/`_Root.cs`，要解根）。
**挑哪顆做是 orchestrator 裁決，不丟給 lane agent**——讓 Sonnet 自己掃+挑是高判斷密度，
昨夜 A/B 正是 agent 自挑、沒對 TiXL → 自創了 5 顆 TiXL 根本沒有的 op。

每條 lane 工單（CONTEXT_PACK 指標 + 指定 op + 驗收清單）：
0. **step-0 base-trap 解藥（第一條指令，逐字）**：`bash "<主倉絕對路徑>/tools/agent_worktree_setup.sh"`
   → 驗 `git rev-parse --short HEAD == 活躍 HEAD`，不符立停。**漏這條＝lane 從落後 main 幹活，產出對不上活躍樹。**
1. **orchestrator 指定一顆具體 op** + TiXL 權威路徑（.cs/.hlsl）+ 照抄的模板 op 檔。
2. 每顆 op = 葉檔（`point_ops_<name>.cpp`，cook + golden selftest **同檔**）+ `shaders/<name>.metal`
   + registry 家族檔 append 一行 row + Phase 0 的 per-family 註冊檔加一行。**全是自己家族的檔。**
   另加 `point_ops.h` 宣告 + kTable 一行供自驗（這兩個共享檔 orchestrator 合流時統一加）。
3. 每顆附 RED 證（injectBug 翻一個真邏輯 → 全套 FAIL）+ TiXL parity 對照
   （.hlsl/.cs 逐項，position **與** attribute 都對）。

### 護欄（每張工單必含，逐字）
- **查 TiXL 不發明 port**：任何旋鈕/輸入/數值來自 `external/tixl` 源碼，無對應物就不加
  （批次8 假旋鈕雷／批次18 憑空 Strength port 都是這條沒守）。
- **NodeSpec append 不 insert**：port id 是 index-based，新 port 只能接在尾巴，
  插中間會錯位既有 .swproj 連線。
- **每顆牙證 RED**：injectBug 注一個真退化，沒看過全套變紅＝牙是假的。
- **fork 具名**：TiXL 分岔處留具名註解（誰/為何/權威源碼行）。
- **不 commit**：lane agent 只在 worktree 改 + 自驗，回報 dossier；合流由 orchestrator 做。

---

## Phase 2（固化先於驗證 → 合流 → 完成邊界）
**鐵律：完工通知到達即固化，固化先於驗證。** 昨夜丟 5 顆的真因＝agent 不 commit + worktree
被收割（session 閒置時 harness 回收 worktree，未 commit 改動隨之蒸發）。驗證要時間，固化不能等。

1. **即時固化（收到完工通知第一動作）**：orchestrator 進該 worktree `git add -A && git commit`
   到它自己的 worktree branch（`[固化快照]` 訊息標未驗證未進主線）。產出鎖進 branch ref，
   即使 worktree 目錄被收割，commit 不丟。**先固化，後做任何驗證、否證、合流。**
   - **★被殺/連線斷的 lane 也要固化（求生條文 2 的下游）**：收到 **kill 通知**（非完工）＝先進該 worktree
     `git add -A && git commit`（`[固化快照·未完]`）搶救已寫的部分，**再立刻接力**（同 worktree 續工不重做）
     →補完→再固化。**漏接 kill＝那條 lane 的活蒸發。** 非隔離（無 worktree）lane 被殺＝活在主樹可救回。
2. **逐 lane 親手復跑**：worktree 內 `--bite` + 該顆 RED（agent 說綠不算數）。
3. **合流**：家族檔（op cpp/metal、registry row、registrar 行）cp/patch 到主樹——只改自己家族檔，
   零衝突。剩兩個共享檔由 orchestrator **統一加**（非並行撞點）：`point_ops.h` selftest 宣告、
   `selftests.cpp` kTable 一行（各家族 append，orchestrator 逐家族加）。真撞了＝Phase 0 沒拆乾淨，補 Phase 0。
4. **全閘 + 否證**：主樹 `run_all --bite`（零 NO-BITE）+ `check-arch` + scenario；高風險 op 派 Opus
   refuter（對 TiXL .hlsl 逐行）；fixer 波 Sonnet 兩次不過升 Opus。
5. **commit 邊界＝機器驗證（柏為 2026-06-16 定 + 2026-06-20 再確認，覆寫舊「等柏為肉眼」版本）**：
   完成定義＝**對 TiXL 機器驗證得到**（golden 對手算 TiXL 公式/源碼常數 + 獨立 refuter + scenario 全綠）。
   **柏為的眼睛不在 parity 鏈上**（他手上沒 TiXL 可對）。
   - **★參數覆蓋閘（柏為 2026-06-22 批准，防「以為補到、細節其實沒對」）**：完成定義再加一條——
     golden 必涵蓋 NodeSpec 宣告的**每個參數/輸入軸**。**宣告了卻零測試碰過的參數＝覆蓋洞＝旋轉 bug 同類**
     （task_eef5757e：Rot 軸沒人測→綠燈出貨錯、用戶看得到、隱形數日）。refuter 攻擊清單**必含**「逐一點名
     未被任何 golden 變動的宣告參數」；有未覆蓋＝**BLOCK 非 pass**，真無法測的（dead/Visibility 類）需具名
     豁免+理由。**為何不靠現有 refuter 就好**：refuter 攻擊種子從實作者的「疑慮/盲區」長（CONTEXT_PACK §四-6），
     實作者自己沒意識到的盲點不會進清單→這條墊一塊「機械窮舉每個參數」的地板在判斷之下。
     **★難兄弟＝路徑覆蓋（class ②，string-rail 型）**：golden 是否同時測 flat＋resident production 路徑
     （string-rail flat 測了 resident 沒測=綠燈測死路；VT1 resident 缺口同類）。目前靠 refuter 判斷抓、尚未機械化
     ——標記待補機械閘。
   - **所有 op（含視覺 render/mesh op）過全閘即入主線 commit**——視覺 parity 用 **closed-form pixel-readback golden**
     對 TiXL 公式/常數機器驗（render 島 Cut 96-99 已證可行），不停 branch、不等眼睛。
   - **唯一例外＝TiXL 源碼查無答案的歧義**（需跑真 TiXL 對，Windows lane [[windows-tixl-copilot-kit]]）
     ∧ 需柏為實體動作 → 才進拍板佇列；其餘 orchestrator 自決。
   - ⚠️ **舊版本這條寫「視覺 op 固化停 branch 等柏為肉眼驗才 merge」＝已被 06-16 指令覆寫，別照舊版**
     （柏為仍可隨時自己抽看畫面，但**非 merge 前置、非 parity 閘**）。
6. 結帳：施工圖補 Cut N（事實/fork/verdict/柏為親測欄/Resume）→ memory lane-state 換頭。

---

## 停止條件
繼承 `/sw-batch` 四條。額外一條：**Phase 0 未完成且當批所有家族 lane 都被它擋住** →
先把 Phase 0 做完落地，再開 Phase 1（這不是阻塞，是序列前置）。

## 啟動
1. 驗 Phase 0 狀態（grep `registerBuiltinPointOps` 是否仍單體）。未拆 → 跑 Phase 0、commit。
2. 已拆 → orchestrator **前置掃缺口**：派 Explore 對 `external/tixl` 盤各家族 cheap 候選 → lane ledger。
   `$ARGUMENTS` 指定家族則只掃那幾條；否則全家族，有 cheap 缺口的才開 lane。
3. orchestrator 從 ledger **挑定**每 lane 一顆具體 op → fan-out Phase 1（worktree 並行）→
   Phase 2（固化先於驗證 → 合流 → 完成邊界）→ 結帳 → 回步驟 1。
$ARGUMENTS（若有）視為本批家族範圍/優先項覆寫。
