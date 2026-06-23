# sw-batch Playbook 系統 — 實作計劃

> **For agentic workers:** 本計劃實作 [PLAYBOOK_SYSTEM_DESIGN.md](PLAYBOOK_SYSTEM_DESIGN.md)。
> 這是**工作流 prompt / 文件工程**,不是 code——沒有 unit test。每個 task 的驗收 = 結構檢查
> (grep 段落齊全 / 逐條清單對得上 spec) + 人讀一致性。步驟用 checkbox(`- [ ]`)追蹤。

**Goal:** 把寫死的單體 `sw-batch.md` 重構成「憲法層凍結 + 策略層可調」的可分支可進化工作流,並落地 PLAYBOOKS 資料驅動表。

**Architecture:** 三檔——新增 `PLAYBOOKS.md`(SSOT 表)、改寫 `sw-batch.md`(劈層+選 playbook+誤選硬閘+parallel-orch+演化)、對齊 `sw-node-batch.md`(標成 leaf-mining 分支+繼承清單)。依賴序:PLAYBOOKS 先(被引用)→ sw-batch 主體 → node-batch 對齊。

**Tech Stack:** Markdown / Claude Code command(`.claude/commands/`) / git pathspec commit。

**驗收哲學(取代 TDD):** 每 task 收尾跑「結構自檢」——指定 grep/條件確認該段真的寫進去、且 spec 對應節點無遺漏。最後 Task 8 做一次「選 playbook + 誤選硬閘」的紙上 dry-run(拿三個歷史情境走流程,確認路由正確)。

---

## File Structure

| 檔 | 職責 | 動作 |
|---|---|---|
| `docs/agent/PLAYBOOKS.md` | playbook SSOT 表:4 承重+1 meta 模式,每行 8 欄(含驗證閘+已知坑) | 新增 |
| `.claude/commands/sw-batch.md` | 主自走迴圈:憲法層(凍結)/策略層(可調)、選 playbook、誤選硬閘、parallel-orch、演化 | 改寫 |
| `.claude/commands/sw-node-batch.md` | leaf-mining 獨立分支:檔頭補「繼承/重述/不適用」逐條清單 | 對齊 |
| `docs/agent/WORKFLOW.md` | Opus×Sonnet 編隊憲法 | 補一行指向 PLAYBOOKS |

---

## Task 1: 建 PLAYBOOKS.md(SSOT 表)

**Files:**
- Create: `docs/agent/PLAYBOOKS.md`

- [ ] **Step 1: 寫表頭 + 兩軸定位說明**

開頭寫(逐字):
```markdown
# PLAYBOOKS — sw-batch 工作模式表(選 playbook 的 SSOT)

> sw-batch 策略層的權威來源。憲法層(凍結)在 sw-batch.md;這裡是「可調 + 可演化」的策略。
> 加一種工作模式 = 加一行(資料驅動,CLAUDE.md 第 7 條)。
> 選哪行 = 兩軸兩問:①撞不撞 cook 核心檔(point_graph.cpp/frame_cook/resident_eval/EvaluationContext)?
> 撞→序列;不撞→可並行。②搬語義/承重 還是 複製機械?語義→Opus 全工法;機械→Sonnet 葉子。
```

- [ ] **Step 2: 寫 5 行模式表(8 欄)**

逐行從 spec §2.1 搬,欄位:`模式名 | 適用(軸一+軸二) | 並行隊形 | 模型分層 | 驗證閘(紅燈) | watchdog | 繼承憲法層哪些 | 已知坑(血證)`。五行:seam-spine / leaf-mining / subsystem-harness / fix-triage / debt-gate-sprint。每行的內容值見 spec §2.1 表 + §2.2 血證(逐顆填,不留空,沒有的標「未記錄」)。

- [ ] **Step 3: 寫「分支逃生門」說明**

逐字:
```markdown
## 分支逃生門
策略差異小 → 加一行。差異大到要改相位結構(如 sw-node-batch 的 Phase 0/1/2 撞點手術)
→ 獨立成 command 檔,但檔頭必須逐條列「繼承不變 / 照新場景重述 / 不適用」哪些承重線
(堵 91bc1eb 血證:「全部繼承」會漏掉沒明寫的求生條文)。
```

- [ ] **Step 4: 結構自檢**

Run: `grep -c '^| ' docs/agent/PLAYBOOKS.md`
Expected: ≥ 6(表頭分隔 + 5 模式行)。再人讀:每行 8 欄都有值、無空欄、誤選最危險的 seam↔leaf 在「已知坑」欄有標。

- [ ] **Step 5: Commit**

```bash
git add docs/agent/PLAYBOOKS.md
git commit -m "docs(plan): PLAYBOOKS 工作模式 SSOT 表 — 4 承重+1 meta,兩軸選路"
```

---

## Task 2: sw-batch.md 劈憲法層/策略層 + 承重線命名分類

**Files:**
- Modify: `.claude/commands/sw-batch.md`(現有「不變的憲法」§+「上下文衛生」§)

- [ ] **Step 1: 把現有條文歸類成命名 section**

把現有 rule 1-6 + 上下文衛生段,重組成兩個明確標記區塊。憲法層每條前綴分類標籤,標 `<!-- DO-NOT-EVOLVE -->`:
- **【求生】**:看門狗節拍+watchdog(禁 nohup/&)、收 kill 必接力、turn 不空手結束、單一 driver、固化先於驗證。
- **【做對】**:問 TiXL 不問柏為、改進規則(press-pass/照 TiXL)、品質閘不可省、律法自檢每 commit。
- **【撞點/衛生】**:orchestrator 不下場、worktree step-0 base-trap 解藥、pathspec commit 絕不 bare。
- **【柏為域】**:真裝置驗證+UI 手感簽收碳出,不 auto-commit、不擋 lane。

- [ ] **Step 2: 加策略層區塊(指向 PLAYBOOKS)**

新增一段(逐字):
```markdown
## 策略層(可調 + 可演化 — 權威在 docs/agent/PLAYBOOKS.md)
以下由 orchestrator 當下調、跑完可提案改(演化見結帳步驟):並行度、worktree 派發隊形、
watchdog 閾值、模型分層、選批策略、驗證波次、用哪條 playbook。**改這層走演化審議閘,
不准改上面的憲法層(凍結)。**
```

- [ ] **Step 3: 結構自檢**

Run: `grep -c 'DO-NOT-EVOLVE' .claude/commands/sw-batch.md` → Expected ≥ 1(憲法層有標)。
Run: `grep -E '【求生】|【做對】|【撞點|【柏為域】|策略層' .claude/commands/sw-batch.md` → 五類齊。
人讀:現有每條舊 rule 都被歸到某一類,無遺漏(對照 spec §3 塊一清單)。

- [ ] **Step 4: Commit**

```bash
git add .claude/commands/sw-batch.md
git commit -m "refactor(sw-batch): 劈憲法層(凍結)/策略層(可調) — 承重線顯式命名分類"
```

---

## Task 3: sw-batch.md 選批步驟加兩軸分類 + 誤選硬閘

**Files:**
- Modify: `.claude/commands/sw-batch.md`(單批迴圈 步驟 2「選批」)

- [ ] **Step 1: 選批步驟插入兩軸分類動作**

在「選批」步驟開頭加(逐字):
```markdown
- **分類本批每個工作項(兩軸兩問,見 PLAYBOOKS):** ①撞不撞 cook 核心檔? ②搬語義還是複製機械?
  → 定位到 PLAYBOOKS 某一行 → 選定該 playbook + 按本批特性微調策略層數值。
  邊界 case(兩軸答案模糊)才動用演化審議閘(見結帳步驟同一機制)選路,清楚的直接走。
```

- [ ] **Step 2: 加誤選硬閘(機械閘,非建議)**

逐字:
```markdown
- **★誤選硬閘(機械,不是建議):** 派工後偵測**任一** lane 動 point_graph.cpp / frame_cook /
  resident_eval / EvaluationContext → **強制升 seam-spine 序列 owner-lock、退出並行**,
  不管該 lane 原本選了什麼 playbook。開採前 scout 必 backward-trace(不 forward-trace)判
  「這顆需不需要一塊未蓋的 seam」;需要 → 退出 leaf-mining 進 seam-spine。
  (堵血證:DirectionalBlur/DoyleSpiral/string-rail 把需 seam 的當葉子採 → 綠燈測死路。)
```

- [ ] **Step 3: 結構自檢**

Run: `grep -E '兩軸|誤選硬閘|backward-trace' .claude/commands/sw-batch.md` → 三者齊。
人讀:硬閘列出全部四個核心檔名、且明寫「不管選了什麼 playbook」(機械性)。

- [ ] **Step 4: Commit**

```bash
git add .claude/commands/sw-batch.md
git commit -m "feat(sw-batch): 選批加兩軸分類 + 誤選硬閘(動核心檔強制升序列)"
```

---

## Task 4: sw-batch.md 派工/合流加 parallel-orch 多 lane 契約

**Files:**
- Modify: `.claude/commands/sw-batch.md`(步驟 3 派工 + 步驟 4 合流)

- [ ] **Step 1: 派工步驟加 parallel-orch 五條契約**

從 spec §6 逐條搬進派工步驟(五條:單一 driver+中央合流 / 脊椎×1+並行×N 補縫只一條 / lane=接力串非單 agent / 每 lane 看門狗+固化先於驗證+watchdog 跟 playbook 走 / 誤選硬閘逐 lane 查)。關鍵逐字:
```markdown
- **★多 lane 背景執行(parallel-orch,見 PLAYBOOKS + spec §6):** 主大腦是唯一 driver、唯一碰主線的人。
  各 lane 在自己 worktree 自驗 + [固化快照] commit 到 lane branch;併入主線一律主大腦一手做
  (全庫 --bite + check-arch + ff-merge)。**禁開多個自治頂層 /sw-batch driver 各自搶主線。**
  補 cook-core 縫只能一條(序列脊椎佔滿);採的葉子必須「既有/已解鎖」不依賴正在補的縫。
  一條 lane = 接力棒串(Plan→build→refuter→fixer)或 Workflow pipeline,非單一 agent。
- **★派工落盤紀律(spec §11 摩擦 2):** 要落盤的產出(藍圖/census/dossier)派 **general-purpose**(有 Write)
  或 **orchestrator 代存**(agent 回傳內容、主腦寫)。**Plan 與 Explore 都沒有 Write 工具,不能落盤。**
```

- [ ] **Step 2: 合流步驟強化「中央合流者」**

在合流步驟確認(逐字):
```markdown
- **中央合流是單一者職責:** 各 lane 只在 worktree 自固化,主大腦親手 ff-merge + 跑全庫
  --bite + check-arch 後才併主線。watchdog 閾值逐 lane 跟 playbook 走(seam-spine 50min/葉子 30min)。
```

- [ ] **Step 3: 結構自檢**

Run: `grep -E 'parallel-orch|唯一 driver|補.*縫只能一條|接力棒串|50min' .claude/commands/sw-batch.md` → 齊。
人讀:五條契約對照 spec §6 逐條在;「禁多自治頂層 driver」明寫(破雙開血債)。

- [ ] **Step 4: Commit**

```bash
git add .claude/commands/sw-batch.md
git commit -m "feat(sw-batch): 派工/合流加 parallel-orch 多 lane 背景執行契約"
```

---

## Task 5: sw-batch.md 結帳步驟加演化審議閘

**Files:**
- Modify: `.claude/commands/sw-batch.md`(步驟 7 結帳)

- [ ] **Step 1: 結帳步驟加演化提案 + 多 subagent 審議閘**

從 spec §5 搬,逐字:
```markdown
- **★工法演化(memory→PLAYBOOKS 斷線接上):** 結帳時反思本批工法卡點。若有改進提案,**不准單點壓過就回寫**——
  派多個 subagent 各持不同視角(真更好vs只是不一樣 / 會不會弄爆求生條文 / 對下游 parity 衝擊 / 現成 playbook 已涵蓋?),
  多輪對抗壓測(至少兩壓),多數反對則駁回。通過後分流:
  - 只動策略層數值、低風險 → 自動回寫 PLAYBOOKS(pathspec commit,訊息標 [playbook-evolve]+審議結論摘要)。
  - 動憲法層、或高風險(改 watchdog/求生/撞點) → 一律標 [待柏為簽收],不自動生效、不擋本批其他 lane。
  - **每條提案必附「紅燈在哪」**(gate-or-it-rots):改了規則,它的驗證閘/RED 證隨之更新,否則駁回。
```

- [ ] **Step 2: 結構自檢**

Run: `grep -E 'playbook-evolve|待柏為簽收|多.*subagent|紅燈在哪' .claude/commands/sw-batch.md` → 齊。
人讀:低/高風險分流判準(動不動憲法層)明確;「必附紅燈」在。

- [ ] **Step 3: Commit**

```bash
git add .claude/commands/sw-batch.md
git commit -m "feat(sw-batch): 結帳加演化審議閘 — 多 subagent 對抗+分流回寫 PLAYBOOKS"
```

---

## Task 6: 對齊 sw-node-batch.md(標 leaf-mining 分支 + 繼承清單)

**Files:**
- Modify: `.claude/commands/sw-node-batch.md`(檔頭)

- [ ] **Step 1: 檔頭加分支身分 + 逐條繼承清單**

在檔頭(現有第一段後)加(逐字框架,內容逐條對 sw-batch 憲法層填):
```markdown
## 分支身分(對齊 PLAYBOOKS)
本檔 = leaf-mining 模式的獨立分支(差異大到改了相位結構:Phase 0/1/2 撞點手術)。
對 sw-batch 每條承重線逐條表態(堵 91bc1eb「全部繼承漏線」):
- **繼承不變:** 【做對】問 TiXL/改進規則/品質閘/律法自檢、【撞點】worktree step-0/pathspec commit。
- **照並行場景重述(不可只當繼承):** 【求生】全部——看門狗 ×N lane、收 kill 必接力進同 worktree、
  turn 不空手、單一 driver、固化先於驗證(求生條文 section 已在,本清單指認它就是重述的產物)。
- **不適用:** (若有,具名;無則寫「無」)。
```

- [ ] **Step 2: 結構自檢**

Run: `grep -E '分支身分|繼承不變|照並行場景重述|不適用' .claude/commands/sw-node-batch.md` → 齊。
人讀:三類表態都有;求生條文被明確指認為「重述」而非「繼承」。

- [ ] **Step 3: Commit**

```bash
git add .claude/commands/sw-node-batch.md
git commit -m "docs(sw-node-batch): 標 leaf-mining 分支身分 + 承重線逐條繼承/重述/不適用清單"
```

---

## Task 7: WORKFLOW.md 補指向 PLAYBOOKS

**Files:**
- Modify: `docs/agent/WORKFLOW.md`

- [ ] **Step 1: 補一節指向 PLAYBOOKS 為選 playbook SSOT**

加(逐字):
```markdown
## 選工作模式(playbook)
選哪種工作模式 = 兩軸兩問,權威表在 docs/agent/PLAYBOOKS.md。憲法層(凍結)在 sw-batch.md。
```

- [ ] **Step 2: 結構自檢 + Commit**

Run: `grep PLAYBOOKS docs/agent/WORKFLOW.md` → Expected ≥ 1。
```bash
git add docs/agent/WORKFLOW.md
git commit -m "docs(workflow): 指向 PLAYBOOKS 為選工作模式 SSOT"
```

---

## Task 8: 整體 dry-run 驗收(取代 integration test)

**Files:** 無修改,純驗證(發現缺口才回前面 task 補)

- [ ] **Step 1: 三情境紙上 dry-run 選路**

拿三個歷史情境走「定位→兩軸分類→選 playbook→誤選硬閘」流程,確認路由正確:
1. **採一顆 already-unlocked image 葉子** → 預期:不撞核心檔 + 機械 → leaf-mining + Sonnet + 並行。
2. **補 S2 render-graph seam** → 預期:撞 frame_cook → seam-spine + Opus + 序列 owner-lock + watchdog 50min。
3. **DirectionalBlur(看似葉子實需 FloatsToBuffer seam)** → 預期:scout backward-trace 判「需未蓋 seam」→ 誤選硬閘攔下、退出 leaf-mining 進 seam-spine。

每個情境人讀 sw-batch.md + PLAYBOOKS,確認流程文字真的會導向預期結果。任一導錯 → 回對應 task 補。

- [ ] **Step 2: spec 覆蓋自檢**

對照 spec §3-§6 每個設計點,在三個檔裡指認落地位置(列一張對照:spec 節 → 哪個檔哪段)。任何 spec 點無落地 → 補 task。

- [ ] **Step 3: 全庫 check-arch 確認沒誤傷**

Run: `bash tools/check_arch.sh`(或專案實際 arch 閘指令) → Expected: 綠(本計劃只動文件,不應影響架構閘,但確認沒手滑動到別的)。

- [ ] **Step 4: 結帳 commit(若 dry-run 有補)**

```bash
git add -A docs/agent .claude/commands
git commit -m "docs(plan): playbook 系統 dry-run 驗收 — 三情境選路 + spec 覆蓋對照"
```

---

## Self-Review(計劃對 spec)

- **Spec 覆蓋:** §1 問題→全計劃動機;§2 兩軸+模式→Task 1;§3 劈兩層→Task 2;§4 選 playbook+誤選硬閘→Task 3;§5 演化審議→Task 5;§6 parallel-orch→Task 4;§7 資料流→流程散在 Task 3-5;§8 實作邊界→File Structure 全覆蓋(sw-batch/PLAYBOOKS/node-batch/WORKFLOW 四檔都有 task);§9 非目標→不建任何引擎(計劃零 code,守 YAGNI);§10 風險→誤選硬閘(Task 3)+ parallel-orch 契約(Task 4)+ 演化分流(Task 5)逐條對應。**無遺漏。**
- **Placeholder 掃描:** 承重段落(兩軸、誤選硬閘、parallel-orch 五條、演化閘、繼承清單)都給了逐字文字,非「add appropriate」。PLAYBOOKS 表格內容值指向 spec §2.1/§2.2 逐顆填(spec 已是完整來源,非 placeholder)。
- **一致性:** 核心檔名(point_graph.cpp/frame_cook/resident_eval/EvaluationContext)在 Task 3 硬閘與 Task 8 dry-run 情境 2 一致;watchdog 閾值(seam 50min/葉子 30min)在 PLAYBOOKS/Task 4/Task 8 一致;模式名(seam-spine/leaf-mining/...)全計劃一致。
