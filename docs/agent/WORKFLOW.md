# WORKFLOW — Opus 4.8 × Sonnet 分層編隊（批次10 起生效）

目標：同品質閘、~1/3 成本、更短 wall-clock。
原理：**品質由結構閘保證**（牙/refuter/scenario/orchestrator 復跑），**模型按判斷密度分層**。
帳本依據：批次8+9 實測 22 agent ~4M tokens，其中 ~40% 是可打包的重複導航、
fixer 全程用了超出需要的腦、live driver 90min 已被 12.9s scenario 取代。

## 一、角色 × 模型

| 角色 | 模型 | 理由 |
|---|---|---|
| orchestrator（裁決/合流/驗證/commit/記帳） | **Fable**（本體） | 判斷密度最高；上下文要橫跨全批 |
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

**規則：任何背景 subagent 跑超過 30 分鐘沒完工通知，必須查它死活。**

機制（orchestrator 不會自己醒，靠背景命令退出叫醒）：
1. 派工後**立刻**背景啟動看門狗（與 agent 同壽命）：
   `tools/agent_watchdog.sh 30 <每個 agent 的 output transcript 路徑...>`（run_in_background）。
   transcript 路徑 = Agent tool 回傳的 output_file。agent 工作中 transcript 持續長大；
   mtime 停走 ≥30min = 死/卡 → 看門狗退出 → orchestrator 被叫醒。
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
