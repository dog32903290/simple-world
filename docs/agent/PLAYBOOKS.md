# PLAYBOOKS — sw-batch 工作模式表(選 playbook 的 SSOT)

> sw-batch 策略層的權威來源。憲法層(凍結)在 `.claude/commands/sw-batch.md`;這裡是「可調 + 可演化」的策略。
> 加一種工作模式 = 加一行(資料驅動,CLAUDE.md 第 7 條)。
> 選哪行 = **兩軸兩問**:
> ①**撞不撞 cook 核心檔**(`point_graph.cpp`/`frame_cook`/`resident_eval`/`EvaluationContext`)? 撞→序列;不撞→可並行。
> ②**搬語義/承重 還是 複製機械**? 語義→Opus 全工法;機械→Sonnet 葉子。

---

## 核心表(選路看這張)

| 模式 | 軸一:撞核心檔? | 軸二:判斷密度 | 並行隊形 | 模型 | watchdog |
|---|---|---|---|---|---|
| **seam-spine** 承重縫/脊椎 | ✅ 撞 | 高(語義/承重) | 序列 owner-lock | Opus 全工法 | **55min + 活性探測** |
| **leaf-mining** 葉子開採 | ❌ 不撞 | 低(機械複製) | 多 worktree lane 並行 | Sonnet 主,兩次不過升 Opus | 30min |
| **subsystem-harness** 新子系統 lane | ❌ 新檔域 | 中高 | 多 lane 並行 | Opus 傾向 | 30min |
| **fix-triage** 救火/排修 | 任意 | 高(外包不下場) | 外包單發 | Opus 診斷 | — |
| **debt-gate-sprint** 拆債裝閘(meta,疊加) | 疊加 | 機械重構 | 序列,拆+裝閘綁同 sprint | Opus + 機械搬 Sonnet | — |

---

## 每行展開(驗證閘 / 繼承憲法 / 已知坑)

### seam-spine — 承重縫 / 序列脊椎
- **適用**:蓋大接縫(Phase B)+ 動 cook-core 序列脊椎 S*。本質複雜(GPU 同步、resident-vs-flat、多輸入 pushContext)。
- **驗證閘(紅燈)**:closed-form pixel/value golden + **獨立 refuter(另一 agent,對 TiXL 源碼,`-bug` 必 exit≠0)** + scenario。每塊縫配 2-3 顆驗證消費葉子(防 orphan)。
- **繼承憲法層**:求生全部 + 做對全部(含 refuter 真閘) + 撞點/衛生全部。
- **已知坑(血證)**:watchdog 30min 誤判長 build 死→雙 driver 險撞(2026-06-23);mip seam 蓋成 orphan 空轉到 RgbTV;DirectionalBlur forward-trace 誤判 .t3 routing 被丟棄(必 backward-trace)。

### leaf-mining — 乾淨葉子大量開採
- **適用**:地基已蓋好後,從 backlog 取乾淨葉子(純 kernel + 值參數,無隱藏 buffer/texture/curve/sim-state)大量複製。Phase C。`/sw-node-batch` 是此模式的獨立分支。
- **驗證閘(紅燈)**:golden 對 TiXL 手算公式/源碼常數 + injectBug RED + position **與** attribute 都對。
- **繼承憲法層**:做對 + 撞點/衛生 + 求生(**照並行場景重述,非「全部繼承」帶過** — 91bc1eb 血證)。
- **已知坑(血證)**:worktree 從 stale main 切(step-0 必 ff 修);cheap-input≠trivial-impl(核心算術藏 compound .t3);憑空發明 TiXL 沒有的 port;**把「其實需要未蓋 seam」的 op 當葉子採→綠燈測死路**(誤選硬閘擋)。

### subsystem-harness — 新子系統 lane(harness-first)
- **適用**:MASTER_PLAN 並行 lane L1-L6(Variation/UI/檔案/IO/音訊匯出)。不是複製 op,是蓋新功能子系統。
- **驗證閘(紅燈)**:**每條 lane 一種專屬 harness**——L1=Mix 公式 golden / L2=eye-hand 斷言 / L3=round-trip golden / L5=loopback golden / L6=備份 round-trip。harness-first:第一個 deliverable 永遠是 harness。
- **繼承憲法層**:harness-first 硬閘 + 柏為域碳出(裝置/手感簽收不 auto-commit) + 求生。
- **已知坑(血證)**:L5 MIDI channel 0-based vs TiXL 1-based(golden 把錯的烤進去,refuter 才抓);L6 備份漏外部資產 soundtrack(單檔備份是死路,refuter 才抓)。**→ build agent 自報綠燈不算數。**

### fix-triage — 救火 / 排修
- **適用**:任何 unplanned red、debt-ledger 排修項、spawn_task chip。判斷密度高、最易污染 orchestrator。
- **驗證閘(紅燈)**:clean-base 隔離(紅在主線前就有?)+ **撿債前先 grep 對碼驗它還活著**(債帳會 stale)。
- **繼承憲法層**:**orchestrator 絕不下場(硬律)** + 做對。
- **已知坑(血證)**:旋轉 bug 債帳 stale 9 天(早被修好)→蓋假閘全 revert;診斷下場吃肉→context 爆→靜止 8 小時;soundtrack flaky 誤當 pre-existing 放行。

### debt-gate-sprint — 拆債 + 裝機械閘(meta,疊加在任一模式上)
- **適用**:律法在產能壓力下爛掉時(單檔 >400 行、債 stale、覆蓋有洞)。
- **驗證閘(紅燈)**:byte-diff verbatim(行為零變更)+ **RATCHET 行數閘(只准降不准升)** + 獨立驗 RED(新超標檔→紅)。
- **繼承憲法層**:律法自檢 + 做對。
- **已知坑(血證)**:`stateful_value_ops.cpp` 飄到 2927 行才發現;只拆不裝閘=白拆;ratchet 提 cap(495→497)違規;瞄錯類別建假閘後全 revert(撿前先對碼)。

---

## 分支逃生門
策略差異小 → 加一行。差異大到要改**相位結構**(如 `/sw-node-batch` 的 Phase 0/1/2 撞點手術)
→ 獨立成 command 檔,但檔頭必須**逐條列「繼承不變 / 照新場景重述 / 不適用」哪些承重線**
(堵 `91bc1eb` 血證:「全部繼承」會漏掉沒明寫的求生條文)。

## 橫切真理(gate-or-it-rots)
沒機械閘(紅燈)的律法在產能壓力下一律會爛。每加一條「該怎麼做」都要同時回答「它的紅燈在哪」——
所以本表每行**必帶驗證閘欄**,演化提案(改本表)**必附「紅燈在哪」**,否則駁回。
