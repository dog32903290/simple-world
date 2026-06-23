# sw-batch Playbook 系統 — 可分支 + 可進化的工作流（設計 spec）

> 柏為 2026-06-23:「應該要有一個能力是讓 agent 遇到 sw-batch 中每個任務，自己思考過後改動工作流，
> 讓他不是我寫死、可以進化、或是有分支（專門寫 node / 專門做並行）。」
> 本檔 = 設計權威。實作計劃另出（writing-plans）。事實以 git/碼/memory 血證為準。

---

## 1. 問題（真因，不是表面）

現在 `sw-batch.md` 是一份**寫死的單體 command**，「每個決策的權威順序寫死在下面，不准上浮詢問」。這刻意——因為自走時柏為不在場，迴圈不能停下來問。但代價有三：

1. **僵硬**：agent 撞到新狀況（watchdog 閾值不對、worktree 撞車）只能照本本跑，不能當下調整。今晚（`sw-watchdog-cook-core-false-death`）就是寫死的 30min 閾值誤判長 build 為死。
2. **學習斷線**：agent 每批跑完把教訓寫進 memory 血證，但**這些教訓沒有迴流改 command 本體**。下個 session 還是照同一份僵硬 command 跑，靠開頭讀 memory「人肉打 patch」。學習（memory）和工作流本體（command）斷開——這才是「不能進化」的真因。
3. **分支只有一條、且 agent 不會自己選**：`/sw-node-batch` 已經是「葉子開採」的特化分支，但它是人手寫的，agent 不會根據任務類型自己選用。

**目標**：把已經在發生的手動學習迴路，結構化成「分層 + 資料驅動 + 受閘」的可分支可進化系統。
**非目標**（YAGNI，見 §8）：不蓋 A/B 試跑引擎、不做自動效能 metric、不做 playbook 版本號/自動回滾。

---

## 2. 核心發現（考古，三來源獨立收斂）

來源：memory 全部血證 + 51-121 Cut 施工圖 + sw-batch/sw-node-batch command 演化史。三個獨立考古員收斂到**同兩根承重軸**：

- **軸一（撞不撞 cook 核心檔）→ 決定能不能並行。** 撞 `point_graph.cpp`/`frame_cook`/`resident_eval`/`EvaluationContext` = 序列脊椎；不撞 = 並行。
- **軸二（判斷密度）→ 決定用哪種工法 + 哪個腦。** 語義移植/承重縫 = 全工法 + Opus；機械複製/拆檔 = 葉子 + Sonnet。

### 2.1 實際走過的工作模式（4 承重 + 1 meta）

| # | 模式 | 撞核心檔? | 並行隊形 | 腦 | 驗證閘（紅燈） | watchdog |
|---|---|---|---|---|---|---|
| 1 | **seam-spine** 承重縫/脊椎 | ✅ 撞 | 序列 owner-lock | Opus 全工法 | closed-form pixel/value golden + 獨立 refuter（`-bug` 必 exit≠0）+ scenario | **50min** |
| 2 | **leaf-mining** 葉子開採 | ❌ 不撞 | 多 worktree lane 並行（前提：家族已自登記化）| Sonnet 主，兩次不過升 Opus | golden 對 TiXL 手算 + injectBug RED + position&attribute 都對 | 30min |
| 3 | **subsystem-harness** 新子系統 lane | ❌ 新檔域 | 多 lane 並行 | Opus 傾向 | 每 lane 專屬 harness：L1=Mix 公式 golden / L2=eye-hand / L3=round-trip / L5=loopback | 30min |
| 4 | **fix-triage** 救火/排修 | 任意 | 外包單發、orchestrator 不下場 | Opus 診斷 | clean-base 隔離（紅在主線前就有?）+ 撿債前先 grep 對碼驗活 | — |
| M | **debt-gate-sprint** 拆債裝閘 | 疊加在任一模式上 | 序列，拆+壓<400+裝閘綁同 sprint | Opus + 機械搬 Sonnet | byte-diff verbatim + RATCHET 行數閘（只准降）+ 獨立驗 RED | — |

`/sw-node-batch` = 把 #2 特化成獨立檔（它有 sw-batch 沒有的 Phase 0/1/2 相位機 + 撞點理論）。`/sw-batch` = #1 為主、其餘混跑。

### 2.2 三個必須進設計的血證

1. **最危險的誤選（反覆死）**：把「其實需要一塊還沒蓋的 seam」的 op 當「乾淨葉子」並行採——DirectionalBlur（FloatsToBuffer .t3 routing）、DoyleSpiral（compound `.t3` 核心）、string-rail（resident-vs-flat 路徑）全栽在此。後果＝「綠燈測死路」（自洽 golden 過、對不上 TiXL/production）。**判別線 = 這顆需不需要未蓋的 seam。**
2. **分支會漏承重線**（`91bc1eb`）：sw-node-batch 第一版寫「憲法全部繼承」，只繼承「做對」條文、**漏掉「活下來」條文**（watchdog/接力/不空手），因求生條文散在 rule 5/6 沒被命名成 section。
3. **gate-or-it-rots 橫切真理**：沒機械閘（紅燈）的律法在產能壓力下必爛。每條「該怎麼做」都要配「紅燈在哪」。

---

## 3. 設計（三塊）

### 塊一:工作流劈兩層

把 `sw-batch.md` 重構成兩個明確標記的區塊：

- **憲法層（凍結，標 `<!-- DO-NOT-EVOLVE -->`，agent 不可改）**：
  - **求生**：看門狗節拍 + 派 agent 後跑 watchdog（禁 nohup/&）、收 kill 必接力、turn 不空手結束、單一 driver、固化先於驗證。
  - **做對**：規則訂版問 TiXL 不問柏為、改進規則（不改觀感才改+press-pass、變 render/手感照 TiXL）、品質閘不可省、律法自檢每 commit。
  - **撞點/衛生**：orchestrator 不下場、worktree step-0 base-trap 解藥、pathspec commit 絕不 bare。
  - **柏為域**：真裝置驗證 + UI 手感簽收碳出，不 auto-commit、不擋 lane。
- **策略層（指向 playbook 表，agent 可當下調 + 跑完提案改）**：並行度、worktree 派發隊形、watchdog 閾值、模型分層、選批策略、驗證波次、用哪條 playbook。

> 承重線顯式命名+分類（求生/做對/做快/撞點/柏為域），直接堵 §2.2-#2「全部繼承漏線」。分支時逐條決定「繼承不變/照新場景重述/不適用」，不准一句「全部繼承」帶過。

### 塊二:playbook 表（資料驅動，新檔 `docs/agent/PLAYBOOKS.md`）

§2.1 那張表就是 playbook 表的初版骨架。符合 CLAUDE.md 第 7 條：一張表 + 選擇邏輯，**加一種工作模式 = 加一行**。每行欄位（缺一不可，最後一欄堵 gate-or-it-rots）：

`模式名 | 適用(撞核心檔? + 判斷密度) | 並行隊形 | 模型分層 | 驗證閘(紅燈) | watchdog 閾值 | 繼承憲法層哪些 | 已知坑(血證引用)`

逃生門：策略差異小 → 加一行；差異大到要改相位結構（如 node-batch 的 Phase 0/1/2 撞點手術）→ 獨立成 command 檔，但檔頭逐條列「繼承/重述/不適用」哪些承重線。

### 塊三:接上兩條斷線

- **當下自適應（within-batch）**：選批步驟加一個動作——agent 用 §4 兩軸判斷分類本批任務 → 從 PLAYBOOKS 選 playbook → 按本批特性微調策略層數值。
- **跑完演化（between-batch）**：結帳步驟擴充——agent 反思工法卡點 → 若有改進，走 §5 的多 subagent 審議閘 → 通過後低風險自動回寫 PLAYBOOKS 表、高風險標 `[待柏為簽收]`。把「memory 血證 → PLAYBOOKS 的具體 patch」這條斷線接上（血證仍寫，但多一條落地路徑）。

---

## 4. 選 playbook 機制（柏為定:判斷 orchestrator 自己做）

開工前問兩個問題，定位到 §2.1 表的某一行:
- **Q1 撞不撞 cook 核心檔?** 撞 → 序列；不撞 → 可並行。
- **Q2 搬語義還是複製機械?** 語義/承重 → Opus 全工法；機械/複製 → Sonnet 葉子。

**誤選硬閘（堵 §2.2-#1，柏為確認「要」）**：
- 派工後若**偵測到任一 lane 動 `point_graph.cpp`/`frame_cook`/`resident_eval`/`EvaluationContext`** → 強制升 `seam-spine` 序列 owner-lock，**不管 agent 選了什麼 playbook**。這是機械閘，不是建議。
- 開採前 scout 必 backward-trace（不 forward-trace）判「這顆需不需要未蓋的 seam」；需要 → 退出 leaf-mining、進 seam-spine 序列。
- **邊界 case（兩軸答案模糊）才動用 §5 審議**；清楚的直接走，不浪費 token。

---

## 5. 演化 + 選路審議閘（柏為定:多 subagent 討論 + 多壓幾次）

**不是單點 press-pass**（agent 自我認證會漂）。當 agent 要(a)改 PLAYBOOKS 表 或 (b)遇到模糊選路 時:

1. **派多個 subagent，各持不同視角**（非冗餘複製）:例如「真更好 vs 只是不一樣」、「會不會弄爆活下來（求生條文）」、「對下游 parity 的衝擊」、「有沒有現成 playbook 已涵蓋」。
2. **多輪對抗壓測**:每個視角獨立壓，收斂前至少兩壓（press-pass 精神 ×N）。多數反對 → 駁回，不回寫。
3. **分流回寫**:
   - 只動策略層數值、低風險、審議通過 → **自動回寫** PLAYBOOKS（pathspec commit，訊息標 `[playbook-evolve]` + 審議結論摘要）。
   - 動到憲法層、或高風險（改 watchdog/求生/撞點理論）→ **一律標 `[待柏為簽收]`**，不自動生效、不擋本批其他 lane。柏為異步點頭才併入。
4. **每條演化提案必附「紅燈在哪」**（gate-or-it-rots）：改了某條規則，它的驗證閘/RED 證隨之更新，否則駁回。

> 實作時這個審議庭可用 Workflow 的 judge-panel / adversarial-verify 模式（多 agent 各持 lens、收斂）。但屬實作細節，本 spec 只定「多視角 + 多壓 + 分流回寫 + 必帶紅燈」的契約。

---

## 6. 資料流

```
開批
 └─ 定位(讀 MASTER_PLAN + memory + PLAYBOOKS)
     └─ 分類任務(§4 兩軸兩問)──模糊?──→ §5 審議選路
         └─ 選定 playbook + 微調策略層
             └─ 誤選硬閘檢查(動核心檔? → 強制 seam-spine 序列)
                 └─ 派工 → 合流 → 否證 → 活體
                     └─ 結帳:有工法卡點? ──→ §5 審議演化
                         ├─ 低風險通過 → 自動回寫 PLAYBOOKS
                         └─ 高風險 → [待柏為簽收]
                     └─ memory 血證 + Cut + MASTER_PLAN snapshot
                         └─ 回到開批
```

---

## 7. 實作邊界（會動哪些檔，零 runtime/app 程式碼）

純工法/文件層，**不碰 ARCHITECTURE.md 管的五區程式碼**:
- 改寫 `.claude/commands/sw-batch.md`:劈憲法層/策略層、承重線命名分類、選批步驟加 §4 分類 + 誤選硬閘、結帳步驟加 §5 演化。
- 新增 `docs/agent/PLAYBOOKS.md`:§2.1 表骨架 + 每行八欄位。
- 對齊 `.claude/commands/sw-node-batch.md`:標註它 = leaf-mining 的獨立分支，檔頭補「繼承/重述/不適用」逐條清單（落實 §3 塊一）。
- 可選:`docs/agent/WORKFLOW.md` 補一節指向 PLAYBOOKS 為選 playbook 的 SSOT。

---

## 8. 非目標（YAGNI,明確劃掉）

- ❌ **A/B 試跑引擎 / 自動效能 metric / 自動回滾**:現在連「並行真的能跑」都還沒站穩（今晚才撞車），先別蓋演化引擎。演化靠「多 subagent 審議 + 柏為異步簽收」，不靠自動 metric。
- ❌ **playbook 版本號系統**:git history 已是版本控制,`[playbook-evolve]` commit 訊息可追溯,夠了。
- ❌ **agent 自由改憲法層**:憲法層凍結是承重線。今晚的坑證明「自由改自己」最快會把安全約束改掉。

---

## 9. 風險與緩解

| 風險 | 緩解 |
|---|---|
| agent 分類選錯 playbook(把需 seam 的當葉子) | §4 誤選硬閘:動核心檔強制升序列 + scout backward-trace 判「需不需未蓋 seam」 |
| 演化漂移(自洽但偏離) | §5 多 subagent 對抗審議,非單點;高風險一律待柏為簽收 |
| 分支時漏承重線 | §3 承重線顯式命名分類,分支逐條決定,禁「全部繼承」 |
| 規則加了沒紅燈→產能壓力下爛 | playbook 每行必帶「驗證閘」欄,演化提案必附「紅燈在哪」 |
| 審議閘拖慢自走迴圈 | 只在「邊界模糊選路」和「演化回寫」動用,清楚的直接走 |
