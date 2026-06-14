# WORKFLOW — Opus 4.8 × Sonnet 分層編隊（批次10 起生效）

目標：同品質閘、~1/3 成本、更短 wall-clock。
原理：**品質由結構閘保證**（牙/refuter/scenario/orchestrator 復跑），**模型按判斷密度分層**。
帳本依據：批次8+9 實測 22 agent ~4M tokens，其中 ~40% 是可打包的重複導航、
fixer 全程用了超出需要的腦、live driver 90min 已被 12.9s scenario 取代。

## 一、角色 × 模型

| 角色 | 模型 | 理由 |
|---|---|---|
| orchestrator（裁決/合流/驗證/commit/記帳） | **Opus 4.8**（本體；2026-06-13 Fable 不可用後接手） | 判斷密度最高；上下文要橫跨全批。context 1M 與 Fable 同→橫跨全批容量零損失；對抗 refuter 本就是 Opus 級→orchestrator 不需贏過自己的 refuter，品質閘扛正確性。校正見 §七 |
| implementer：**語義移植 lane**（TiXL 行為對齊、時鐘/同步、資料模型） | **Opus 4.8** | 讀權威→翻譯語義→fork 判斷，錯了 refuter 也難救全 |
| implementer：**機械 lane**（拆檔、scaffolding、資料驅動表擴項、doc/scenario 撰寫） | **Sonnet** | 低判斷密度；規格寫死就做得好 |
| refuter：**對抗否證**（高風險 lane：新子系統/時鐘/毀資料面） | **Opus 4.8** | 兩批 21 條 BROKEN 全靠深推理（speed 相消、宣告序拓撲）；這裡省=假綠 |
| refuter：**checklist 複核**（低風險 lane：跑牙、對 TiXL 引文、邊界掃描） | **Sonnet** | 機械複核夠用；存疑升級 Opus |
| fixer（根因+修向已由 refuter 具名） | **Sonnet**，紅了升級 Opus | 照單施工+牙；超綱發現照規回報不硬修 |
| live driver | **先 scenario**（零模型）；殘餘探索項 Sonnet；新手勢首驗 Opus | 確定性已進腳本 |

升級規則：Sonnet agent 兩次嘗試不過驗收閘 → 同工單原文升級 Opus 重派（工單不變，只換腦）。

## 二、批次管線（與批次8/9 同形，加分層與打包）

```
0. orchestrator：拉線（檔案重疊定隊形）＋ 開 TaskList ＋ 工單引用 CONTEXT_PACK
1. implementer 波（worktree 並行；同檔者序列）
     工單 = CONTEXT_PACK 指標 + 任務 + 驗收清單（含 .scn 交付）
     交付 = dossier（含 TiXL 原文引述）→ 直接成為下一棒上下文
2. orchestrator：合流（diff apply）→ run_all --bite + check-arch + scenario 全跑 → commit
3. refuter 波（風險分流：對抗=Opus / 複核=Sonnet）
     攻擊清單從 dossier 的「疑慮/盲區」段長出來；引文先對、存疑才開 TiXL
4. fixer 波（Sonnet 照單施工；refuter probe 轉正式牙）
5. orchestrator：合流 commit → scenario 全庫重放（回歸）→ 殘餘活體項派 driver
6. 結帳：Cut 記帳 + memory + 柏為親測欄（聲音/手感/品味永遠是人的）
```

## 三、省 token 的四個結構手段

1. **CONTEXT_PACK**：每 agent 省 30-50k 的重複導航；工單從 ~800 字縮到 ~200 字。
2. **dossier 接力**：implementer 報告含 TiXL 原文引述 → refuter/fixer 不再三遍重讀同一源碼。
3. **scenario-first 活體**：確定性手勢+斷言進 .scn（12.9s/條）；agent 只調查紅項。
   新規：活體可證的行為，implementer 交付附 .scn。
4. **refuter 鷹架復用**：probe 統一掛拋棄式 selftest 樣板（照 refuter_e1_probe 前例），
   不重發明 cmake wiring。

## 四、不准省的地方（血的教訓）

- **對抗 refuter 的深度**：resync 機關槍、宣告序幽靈、雙擊毀滅鏈——全是 Opus 級推理抓的。
  高風險 lane 的 refuter 永遠用最強腦。
- **orchestrator 親手復跑**：agent 說綠不算數，合流後 run_all --bite + check-arch 必親跑。
- **RED 面**：每顆牙、每條 scenario 都要證過「改錯必紅」，否則是瞎眼。
- **fork 具名**：與 TiXL 的每一條分岔寫進註解+dossier；省這個=下一批考古成本爆炸。

## 五、風險分流 rubric（refuter 用哪個腦）

對抗（Opus）：時鐘/同步/音訊、資料毀損面（undo/save/併鍵）、新子系統第一刀、
跨 lane 交互（rebuild×cache、bypass×freeze 這類組合洞）。
複核（Sonnet）：機械拆分、UI 樣式、資料驅動表擴項、文檔、scenario 庫。
拿不準 → 對抗。省錯邊的代價不對稱。

## 六、subagent 看門狗（2026-06-13 柏為定；批次15 兩 agent 無聲死亡的血）

**規則：任何背景 subagent 跑超過 ~60 分鐘沒完工通知，才查它死活（批次16 修正：原 30min 對長 Opus lane 太敏感——X 跑 57min、在 >30min 安靜段被誤判死亡）。**

機制（orchestrator 不會自己醒，靠背景命令退出叫醒）：
1. 派工後**立刻**背景啟動看門狗（與 agent 同壽命）：
   `tools/agent_watchdog.sh 60 <每個 agent 的 output transcript 路徑...>`（run_in_background）。
   transcript 路徑 = Agent tool 回傳的 output_file。agent 工作中 transcript 持續長大；
   mtime 停走 ≥60min = 疑似死/卡 → 看門狗退出 → orchestrator 被叫醒（疑似，不是確定，見 §六補遺2）。
2. 被叫醒後的**接力程序**：
   a. `git -C <該 agent worktree> status/diff` 盤半成品（worktree 還在，活不會丟）。
   b. 確認無殘留 process（repo_pids 模式查殺）。
   c. 派**接力 agent 進同一個 worktree**（工單=原工單＋「先盤點前一棒做到哪、編譯態修綠再續工、
      dossier 標注接手段」）。**不開新 worktree、不重做。**
   d. 接力 agent 也要掛新看門狗。
3. 死因常態=session 閒置時背景 agent 被收走，非工單問題——接力即可，不升級模型。
   同一工單接力兩次仍死 → 工單太大，拆半。

§六補遺（首日實戰修正）：**agent 完工收割後，立刻殺掉對應看門狗**（背景命令 TaskStop/kill），
否則 transcript 停寫 30min 後狗會誤報 STALE（完工與死亡在 mtime 上同形）。被吵醒時先查
「報警的 agent 是否已收割」——已收割=假警報，殺殘狗即可；未收割才走接力程序。

§六補遺3（2026-06-15 柏為定：「自己判斷 agent 死掉，在 workflow 增加自己檢查的能力，死掉再派一個，繼續走」）：
看門狗（§六）是**背景 Agent 工具**派工的死活長停損，靠人肉接力。**更乾淨的結構化機制 = `Workflow` 工具**：
它的 `agent()` 在 subagent 終端死亡（socket 死／API error 重試耗盡）時**回 `null`**，於是「檢測死亡→換一個再派→繼續」
可以寫成 retry 迴圈，不需 orchestrator 人肉盯。已落地可重用腳本 **`tools/workflows/self_healing_node_batch.js`**：
- `resilient(prompt, opts, label, maxTries)` 包每次 `agent()`：`null`=死 → 帶 salvage 提示（先 `git status` 盤主樹半成品、覆寫補完非從零）換一個再派，最多 `maxTries` 次；全死才放棄回 `null` 並 `log` 繼續下一 op。
- 隊形 = **sequential**（implementer 寫主 checkout 共享樹，不能並行寫）；每 op：resilient(implementer,3) → resilient(refuter,2)；回 `{op, impl(schema), verdict(schema)}`。
- 用法：`Workflow({scriptPath:".../self_healing_node_batch.js", args:[{op,kind,tixlCs,tixlHlsl,template,baseProbe,selftest,fn,notes,outputs,golden,refuteFocus}, ...]})`。
- 工具回傳後 orchestrator 再 build＋--bite＋親核＋逐 op commit（固化）＋結帳。**死亡韌性在 workflow 內，最終品質閘仍在 orchestrator。**
- Workflow agent 預設**非隔離=主樹=正確 base**（自動避開 [[worktree-base-main-trap]]；勿加 `isolation:'worktree'`，那會撞 main-base 陷阱又無法並行寫）。
適用：節點量產批（point/image/math op）。單發/高判斷裁決仍走 Agent 工具前景。

§六補遺2（批次16 血，最重要）：**STALE ≠ 死亡。** harness 的**完工通知**才是可靠死活訊號（批次16 R/X/P/F 四 agent 全部正確觸發了完工通知）；看門狗只是「無通知的真·silent death」（批次15 那種）的長停損，不是死亡判定。批次16 X(57min)與 F 都在長安靜段被 30min 閾值誤判 STALE，X 還被早摘半飛快照 → 權威版與早摘版**分岔**（幸 gate「不在 red 上 commit」擋住沒封錯版）。**故被 STALE 吵醒時的正確程序：① 先確認 harness 是否仍可能送完工通知（長 Opus lane 常常還在跑，transcript 只是某長階段沒寫）② 查 worktree 有無活 process（`pgrep -fl <worktree>/app/build/simple_world`；在 build/跑 selftest=鐵定活著）③ 只有確認真死（無 process＋遠超閾值＋不會再有通知）才接力/harvest——絕不 harvest 可能還活著的 worktree。** 閾值提到 60min 已大幅減少假死頻率。

## 七、orchestrator 跑 Opus 4.8 的三條校正（2026-06-13；Fable 不可用→Opus 接手）

context/output 與 Fable 同（1M/128K），價更低（$5/$25 vs $10/$50），**容量與成本都不是問題**。
真正要防的是 Opus 4.8 的三個出廠脾氣，每一個都正面撞 /sw-batch 的命脈。orchestrator 自我約束：

1. **自走不上浮**（治 4.8「小決策也停下來問、收尾問『要不要我也…』」）：
   權威序＝TiXL ＞ Cut/memory ＞ CONTEXT_PACK，**不是柏為**。隊形/命名/等價路徑選哪條這類小決策
   自己定並在 Cut 註記，不問；只有 /sw-batch §停止條件四種才停。禁止收尾反問——下一批直接開。
2. **預設沉默**（治 4.8「tool call 間旁白多、收尾長」）：tool call 之間不旁白；只在 verdict／
   合流結果／結帳寫字，一句一件。不逐檔複誦 subagent 做了什麼——dossier 已經有。
3. **永遠委派、永遠從磁碟重建**（治 4.8「保守、不主動派 subagent／不用檔案記憶、想自己幹」——最致命）：
   實作肉一律派 subagent，**orchestrator 絕不自己改實作碼**（自己幹會把碼塞進橫跨全批的 context、幾批就爆）；
   每批開頭一律從 Cut＋memory＋CONTEXT_PACK 重新定位，不靠 live context 記。這兩條是 §二管線與
   /sw-batch §上下文衛生的根本，Opus 4.8 預設會偷懶跳過，必須硬性自我要求。

參數：effort 預設 **high**（長程 agentic／合流否證吃判斷，full task spec 本就在 /sw-batch 給足）；
adaptive thinking 開。子 agent 仍照 §一分層派（Sonnet 機械／Opus 對抗），與本體是 Opus 無關。

## 八、平行化的單位是「子系統」不是「op 批次」——加層時機與形式（2026-06-13 柏為問「能不能再加一層」）

問題：把 orchestrator 再升一層（我派 N 個 session 各跑一批，我只盯活死＋調度），能不能加速克隆？
**答案：現在不能，到 §D 子系統時能，且形式是磁碟帳本不是活監工。**

- **為什麼現在不能（脊椎序列）**：TiXL clone 不是 embarrassingly parallel——有中央承重脊椎
  （node_registry／共享契約／trunk）。加 op 全撞 registry（§A55 撞檔律的根）。多 N 個 orchestrator-session
  ＝撞檔放大成批次級＋N 路合併集中到頂層＋頂層 context 揣 N 份活狀態爆更快。**加管理層平行不掉序列脊椎
  （Amdahl）**。「盯活死＋調度」是輕的 10%，合流／否證／衝突解決是重的 90% 且變 N 路更難。
- **平行化的真單位＝子系統，不是 op 批次**：op 庫被脊椎序列住；但 §D 子系統（Fullscreen Output／
  真實資源載入／獨立 Player／MIDI-OSC）互相獨立、多週級、幾乎不撞脊椎——這裡 session 級平行才賺錢。
- **加層時機**：一條 lane 長成一個 §D 子系統、大到會獨佔一個 orchestrator 的 context／合流頻寬時，
  才升 session。在那之前：**拉寬每批的非重疊 lane**（op ∥ §B 視覺 ∥ §C 互動，改不同檔；批次12 已實證）
  ——便宜的平行，不加層。
- **加層的形式＝磁碟派工帳本，不是活監工**：此 harness 裡 agent 盯 agent 盯 agent 支援差＋context 算不過。
  更穩：N 個獨立 /sw-batch session 各擁一個 §D 子系統，全靠磁碟協調（一張 assignment ledger：誰擁哪個
  子系統／整合順序／契約接縫），順「狀態永遠在磁碟」世界觀，各自 worktree（「多 session 同樹互擾」雷）。
  meta 角色守的不是「他們在不在工作」，是**子系統之間的契約接縫**（合流／否證）——那才是難的部分。
