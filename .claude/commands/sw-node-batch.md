# /sw-node-batch — 家族並行節點生產（消除共享撞點 → 每家族一條獨立 lane 平行織）

`/sw-batch` 的特化版，**只為一件事**：把節點生產加速。手段＝把脊椎拆成 per-family 葉子，
讓多條家族 lane 在各自 worktree 同時生產、零衝突合流（git 自動 merge 不同檔）。
憲法、權威順序、品質閘、結帳全部繼承 `/sw-batch`——下面只寫**不同的地方**。

繼承自 `/sw-batch`（不重述）：北極星＝Mac TiXL 完整 clone；規則訂版問 TiXL 不問柏為；
工作法照 `docs/agent/WORKFLOW.md`（Opus×Sonnet 分層）＋工單引 `docs/agent/CONTEXT_PACK.md`；
品質閘（run_all --bite 零 NO-BITE／對抗 refuter／RED 面／orchestrator 親手復跑後才 commit／
活體 .scn）；律法自檢每 commit；結帳補 Cut＋memory lane-state。

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

**前置（orchestrator，非 lane agent）**：派 Explore 掃各家族 `external/tixl` 缺口，產 lane ledger
（cheap 候選 + TiXL .cs/.hlsl 路徑 + 照抄的模板 op）。cheap = 純 kernel + 值參數，無隱藏
buffer/texture/curve/sim-state 依賴；**先 grep `.cs` 確認，sizing 不可信、cheap-input≠trivial-impl**
（批次18／DoyleSpiral 教訓：掃描判 cheap 但核心算術在 compound `.t3`/`_Root.cs`，要解根）。
**挑哪顆做是 orchestrator 裁決，不丟給 lane agent**——讓 Sonnet 自己掃+挑是高判斷密度，
昨夜 A/B 正是 agent 自挑、沒對 TiXL → 自創了 5 顆 TiXL 根本沒有的 op。

每條 lane 工單（CONTEXT_PACK 指標 + 指定 op + 驗收清單）：
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
2. **逐 lane 親手復跑**：worktree 內 `--bite` + 該顆 RED（agent 說綠不算數）。
3. **合流**：家族檔（op cpp/metal、registry row、registrar 行）cp/patch 到主樹——只改自己家族檔，
   零衝突。剩兩個共享檔由 orchestrator **統一加**（非並行撞點）：`point_ops.h` selftest 宣告、
   `selftests.cpp` kTable 一行（各家族 append，orchestrator 逐家族加）。真撞了＝Phase 0 沒拆乾淨，補 Phase 0。
4. **全閘 + 否證**：主樹 `run_all --bite`（零 NO-BITE）+ `check-arch` + scenario；高風險 op 派 Opus
   refuter（對 TiXL .hlsl 逐行）；fixer 波 Sonnet 兩次不過升 Opus。
5. **無人值守 commit 邊界（完成定義不被「不中斷」吃掉）**：
   - **機械/無視覺/selftest 能強驗**（Phase 0 重構、math value op、identity 可驗的 filter）→ 過全閘即
     入主線 commit。柏為的眼睛驗不出更多，selftest+refuter 已是完成。
   - **有視覺、需柏為肉眼驗**（spiral 形狀、混色對不對、point 變形）→ 固化停在 branch + PANEL 列
     「待柏為驗」+ best-effort 截圖。**柏為親手加接、畫面對，才 merge 主線。** 無人值守時段它最多是
     「半成品+證據」，不是完成。對應：無人值守 lane 首選 selftest 能強驗的 op，最依賴眼睛的留柏為在場。
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
