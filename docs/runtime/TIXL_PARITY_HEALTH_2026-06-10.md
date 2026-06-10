# TiXL Parity 健檢報告 — 2026-06-10

> **緣起**：柏為點名病灶——「AI 爬 TiXL repo 時沒爬完疏理整條線，只拿它以為需要的」，要求全面健檢 + 設計一個高自動化但保證語意一致的工作系統。
> **方法**：5 隻稽核 agent 並行（求值核心/Symbol 模型/序列化/點 op 債/時間契約），每隻**否證式**任務（拿 TiXL 源碼當權威反壓我方契約，找漏讀/誤讀/未覆蓋），全部引文對 `external/tixl` @ `395c4c55` 驗過；最重兩條指控由主 session 親手複核源碼成立。
> **狀態**：純健檢，未動任何契約/實作碼。修正需柏為拍板（見〈拍板佇列〉）。

---

## 0. 總判

1. **病灶確認且比預期深**：今天（2026-06-10）拍板的 compound 契約裡有**兩條地基級誤讀 + 三條地基級未覆蓋**（§1）。它們還沒變成碼（批次 1 未動工），現在修是改文件，批次 4 才發現是重寫 walker。
2. **病因不是「讀得不夠深」，是「驗證的形狀錯了」**：契約 2.0 的源碼複驗是真的（B 清單全對得上）——但複驗是「驗證自己引的那幾行」（confirmation），沒有人「枚舉宣稱所覆蓋的領土」（coverage sweep）。`Slot.cs/DirtyFlag.cs` 讀穿了，`Operators/Lib/flow/` 整個資料夾沒人掃——而 flow 家族系統性違反契約的三個前提。
3. **好消息**：批次 0 的資料模型（四元組/sentinel/reuse/override）對 TiXL 是忠實的；SwPoint 64B layout **零債**；audio AudioReaction lane 真的對齊了。地基的「骨」是對的，錯在「求值怎麼跑」與「animation 住哪」。

---

## 1. 地基級發現（批次 1 動工前必須修進契約）

### C1｜「eager 一趟 == TiXL lazy 兩趟」等價宣稱對 flow/render 兩族 op 不成立 ⟂ 契約 2.4/2.8
TiXL 是 lazy pull：**op 的 Update 自己決定拉不拉、何時拉、用什麼 context 拉**。三類反例（主 session 親驗 Switch）：
- **條件式拉取**：`Operators/Lib/flow/Switch.cs:67` 只 GetValue 選中分支；`Execute.cs:17-36` IsEnabled=false 整串不拉；`Group.cs:58-77`、`PickTexture.cs:23-24`、`TimeClipSlot.cs:55-59`（出範圍子樹不評）。eager 後序會把關掉的 Command 分支也執行 → **發出不該發的 GPU 副作用**，行為錯非 perf 差。
- **拉取前改 context**：`Transform.cs:42-45`（ObjectToWorld）、`Camera.cs:36-45`、`RenderTarget.cs:81-159`（解析度/矩陣/背景色）、`DrawMesh.cs:26-44`（換 PbrMaterial 再拉內圖）。context 是「拉的當下」流進子圖的——**op body 就是排程器**。「先解 input 再跑 node」= 子圖拿錯 context，這是 TiXL 場景圖語意的本體不是邊角。
- **一幀多次重評同一子圖**：`Loop.cs:23-40` 每 iteration 寫 ctx 變數 → `GlobalInvalidationTick++` → InvalidateGraph → GetValue。「幀」不是求值單位，**evaluation pass 才是**；count-based selftest（「LIVE 每幀 1 次」）單位要跟著改。
**修法方向**：等價宣稱降級為「**純值圖**（無條件拉取、不改 ctx、無觸發語意）等價」——現行 9 顆點 op 在此範圍內安全；**Command/flow 圖必須 pull-driven**（op body 驅動 input 解析），walker 要暴露 re-entrant「以新 tick 重評子圖」原語。

### C2｜「diamond 去重：一節點一幀只算一次」與 Command-always 互斥 ⟂ 契約 2.4
`DirtyFlag.cs:24-34,93` 的 InvalidationTick memo 只用在 **invalidation 趟**，不是 cook 去重。值 slot 靠 version/cache 天然短路；**Command slot 每 pull 必跑**（`Slot.cs:162`），同一 Command 子樹被兩個 Transform 父各拉一次=畫兩次（**這是 TiXL 的 reuse-by-reference 畫法**，`ExecRepeatedly.cs:43-49` 同幀連拉 N 次全靠它）。selftest 若寫死「diamond 一幀 1 次」，會把 TiXL 的正確行為當 bug 抓。

### C3｜Automation 擁有權放錯層：TiXL 住 Symbol 定義層，決策 9 放 resident 實例層 ⟂ 契約 2.5b/3
`Symbol.cs:55`（主 session 親驗）：**Animator 是 Symbol（定義）的欄位**，曲線 keyed by `[childId][inputId]`（`Animator.cs:26`），序列化在母 Symbol 的 .t3 內（`SymbolJson.cs:34`），**所有 reuse 實例共用同一份曲線**（`Animator.cs:446-459` 廣播重掛）。slot 上的 `OverrideWithAnimationAction` 只是接線期**投影**，不是權威儲存。
決策 9 把 driver（含 Automation）的權威放在 resident（攤平實例）input 上、存檔從 resident graph 序列化——①語意分岔（兩個 reuse 實例可各自動畫=TiXL 表達不了；改定義動畫全變=我方做不到）②**契約內部矛盾**（契約 3 的 symbols[] 是定義形狀，2.5b 的序列化是實例形狀，reuse 時 N 個 path 對一個槽）。
**修法方向**：權威搬回定義層 `(symbolId, childId, inputId)`；resident input 的 driver = build/patch 時的投影。**決策 9 的求值面（slot 扛三事、isLiveSource 從 driver 推導、兩鐘 ctx）全保留**，只把「家」搬對層。

### C4｜Multi-input 三層整缺（宣告/編輯/求值） ⟂ 契約 1/4
TiXL 同一 input 收多條**有序**連線是合成的唯一機制（`Execute` = `MultiInputSlot<Command>`，全庫 148 處用）。順序=Connections list 內相對順序（`Symbol.cs:165-168`）；`AddConnection(conn, multiInputIndex)` 單輸入自動 replace、多輸入 insert-at-index（`Symbol.cs:169-229`）；跨 compound 邊界遞迴攤平保序（`MultiInputSlot.cs:29-41`）。我方四元組 vector **模型本身同構可表達**，但 `SlotDef/PortSpec` 無 isMultiInput、編輯碼寫死單基數、命令層無 index、求值取第一條。連線承重結構正在改（pin-id→四元組），**此時不一起定，之後要再動一次地基**。

### C5｜dirty/cache 粒度 = per-output-slot，契約寫成 per-node ⟂ 契約 2.4
TiXL 的 DirtyFlag/UpdateAction/Value 長在**每個 output slot**（`Slot.cs:35-39`），多輸出 op 各 output 獨立 dirty/update。我方已有三輸出 op（AudioReaction）。2.5b「resident 節點=slot」口號是對的，2.4 的資料形狀（per-node 單一 version+cache）沒跟上。

---

## 2. 承重級發現（按時間點補進契約/schema，不擋批次 1 起步）

| # | 發現 | TiXL 證據 | 咬到誰 |
|---|---|---|---|
| S1 | **值編輯 = edit-time push invalidate**，契約 2.0「值改動走 pull」寫反（版本不會自己動） | `InputSlot.cs:57-63`、`ChangeInputValueCommand.cs:122` | 批次 1b 實作藍圖 |
| S2 | **bypass/disabled 全套語意**：disabled=**值凍結在最後一次結果**（非回 default）、bypass=型別白名單直通；兩者皆入 .t3 | `Slot.cs:75-89`、`Symbol.Child.cs:106-149`、`SymbolJson.cs:83-86,137-139` | driver enum 缺兩態、存檔 v2 schema |
| S3 | **per-output DirtyFlagTrigger 是使用者資料**（UI 可設、per-child 覆寫、序列化）→ isLiveSource「只從 driver 推導」少了兩個來源（手動設定、op runtime 自翻 `Once.cs:26-33`） | `EditNodeOutputDialog.cs:20-38`、`SymbolJson.cs:117-134` | 決策 7、存檔 v2 |
| S4 | **DirtyFlag 是 op 可讀寫的一級狀態**（當事件/邊緣偵測用，手動 Clear）；walker 先解 input 會把訊號吃掉 | `ExecuteOnce.cs:20-24`、`Once.cs:23-34` | 觸發系 op 整族（bang/gate） |
| S5 | **Switch 可限縮 invalidation 走訪到 active 分支**（TiXL 自己的 op 級走訪短路，幾千 instance 規模的對策）；修正 2.6「TiXL 每幀照走全圖」的絕對化敘述（降級決定仍成立） | `MultiInputSlot.cs:14,57-69`、`Switch.cs:71-86` | 契約 2.0/2.6 敘述 |
| S6 | **ICompoundWithUpdate**：帶 update body 的 compound 一級類（DrawMesh/DrawScene），「邊界一律 inline 透明」對它不成立 | `Slot.cs:188-206`、`DrawMesh.cs:7,18-44` | buildEvalGraph |
| S7 | **ctx 變數 dict 使 cache context-blind**：Loop 寫變數、GetVar 讀，「同 version 同值」不變式靠 op 主動 bump-tick 再失效**維護**出來，非天然成立 | `Loop.cs:25-34`、`EvaluationContext.cs:156-168` | cache 正確性 |
| S8 | **ForceInvalidate 外部注入口**（資源熱重載/audio 從 invalidation loop 外打進來）= 第三種失效來源，「每幀 bump LIVE 集合」防不到 | `AbstractResource.cs:136-146`、`DirtyFlag.cs:36-40` | 🪤#1 防禦設計 |
| S9 | **Command 是三段協定**（PrepareAction/Execute/RestoreAction，消費端負責次序：全 Prepare→全 GetValue→全 Restore） | `Command.cs:8-9`、`Execute.cs:19-35` | port BlendScenes/SetFog 時 |
| S10 | **EvaluationContext 兩鐘以外 17 個欄位群** + 每幀 Reset 歸零語意（變數/燈/材質/矩陣/解析度/前景背景色/gizmo/粒子系統…）；第一個撞上的是 RequestedResolution | `EvaluationContext.cs` 全檔、`:43-58` Reset | render 圖一深就撞 |
| S11 | **Symbol 編輯廣播 = 六個操作各有明確路徑**（加/刪連線、加/刪 child、改 default、IO 變更）+ child→全 path 實例**直達索引**（hash by path，不遞迴下行） | `Symbol.cs:222-330`、`Symbol.Child.cs:68,905-913`、`Symbol.TypeUpdating.cs:13-39` | 批次 1 增量 patch golden 的枚舉清單 |
| S12 | **InputValue 是型別化系統**（float/int/Vec2-4/**string 檔案路徑**/gradient/curve…），我方 SlotDef/overrides 寫死 float；資源型 op 一進來就斷 | `Symbol.ConnectionSubClasses.cs:22-30` | 存檔 v2 value 欄型別 |
| S13 | **刪 InputDefinition 的收屍規則**：by-id 保留既有 override、孤兒連線含 multi-input 保序掃除；我方無 dangling 清理 | `Symbol.TypeUpdating.cs:99-132,213-261` | reuse 編輯、combine |
| S14 | **循環防護**：TiXL 防線全在 editor 接線手勢層（`Structure.CheckForCycle`），Core 裸的；symbol 自我巢狀**整個 TiXL 都沒擋**。我方接線層無檢查、flattener 無終止條件——常駐圖時代一條循環=每幀 hang | `Structure.cs:314-370`、`ConnectionMaker.cs:378` | 批次 1 flattener 必帶訪問棧 |
| S15 | **載入容錯哲學**：壞資料局部丟棄、load 不死、下次存檔自我治癒（六條規則：missing symbol/dangling 連線 self-heal/obsolete input/output/孤兒曲線/新版本警告不擋）；我方 fromJson 全有全無 | `SymbolJson.cs:183-187,269-309`、`Instance.Connections.cs:77-84` | 批次 2 世界觀選擇 |
| S16 | **.t3 不是自描述格式**（slot 定義住 C# class；.t3 的 Inputs 只覆寫 default、無 outputs 段）→ 我方 v2 必須是自描述 superset；契約 3 schema 行漏 outputDefs/name/順序語意 | `SymbolJson.cs:353-365`、`Symbol.cs:53` | 存檔 v2 schema |
| S17 | **Animator 曲線存檔格式**（keys 的雙邊張力/切線角/weighted/broken + Pre/PostCurve 外插 + 多通道 Index）契約 3 隻字未提；現行 SourceRegistry binding **完全不序列化**（live binding 現在就掉檔） | `Animator.cs:371-408`、`CurveState.cs:56-82` | 時間 lane S3 + 批次 2 |
| S18 | **CompositionSettings**（BPM/soundtrack/audio 裝置/音量/resync 閾值）存 .t3、breadcrumb 向上找；我方 schema 無位 | `CompositionSettings.cs:110-177` | transport 持久化 |
| S19 | **.t3ui 遠多於位置**：per-input Min/Max/Clamp/Scale（**使用者可調可存的 slider 手感**）、GroupTitle、Comment、Annotations 畫布註解框、pinned 視窗狀態；我方 minV/maxV 是定義級不可存 | `SymbolUiJson.cs`、`FloatVectorInputValueUi.cs:219-241` | 「外觀一模一樣」可見項 |
| S20 | **檔案佈局分岔未記錄**：TiXL 一 symbol 一檔（+.t3ui sidecar），我方拍了單檔庫——不是錯，是該在契約 3 明文的 divergence（影響跨專案 reuse/版控 diff/互通） | `SymbolPackage.cs:50-60` | 契約 3 |

葉子級：SetCurrentValueAsDefault 操作（`Symbol.Child.cs:198-210`）、Slot 子類「5 個」實為 2 個 InvalidationOverride（契約抄到源碼錯註解，工比想的少）、FormatVersion 雙欄遷移機制。

---

## 3. 點 op 債的真實大小（lane A 帳本外的語意層）

Ledger 帳面（31 缺 USED port + 15 default 錯 + 型別/命名/結構各 1-2）已準確。**抽 2 顆再挖出 8 項 ledger 沒有的語意債**：

1. **TransformPoints Euler 合成順序錯**：TiXL `CreateFromYawPitchRoll` = Z→X→Y；我方 metal = X→Y→Z。單軸一樣、兩軸以上發散。golden 測 0 旋轉所以隱形。
2. **PointSpace 漏寫 Scale attribute**（TiXL `TransformPoints.hlsl:93` 要 `p.Scale *= lerp(...)`，我方不碰）。
3. Shearing 真缺（M12/M21/M13 進矩陣）。
4. **RadialPoints 預設 Rotation 非 identity**（TiXL Classic 模式每點隨角度自旋），我方烤死 identity 且**註解宣稱 TiXL-equivalent——宣稱是錯的**。
5. **F1/F2 中性值 = 1 非 0**：TiXL Point 預設建構子 F1=F2=1、Scale=1；我方零初始化=「死點」語意整個相反。下游 StrengthFactor=F1 在 TiXL 全強度、在我方死。
6. **NaN separator 慣例整缺**（`Point.Separator()` Scale=NaN = DrawLines 斷線的系統級慣例）。
7. Count clamp 域差 1220 倍（TiXL 1..10M；我方 UI 16..8192）。
8. **隱形旋鈕**：cook 讀 `Cycles/StartAngle/RadiusOffset` 但 NodeSpec 無此 port——selftest 直接餵 params 所以綠，**柏為的 UI 永遠搆不到**。直接違反完成定義哲學。

**外推：真實債 ≈ ledger 帳面 × 1.5~2**（Euler 序/attribute 寫回/慣例缺席這類「port 數不出來」的，其餘 7 顆同類風險未查）。
**唯一全綠**：SwPoint 64B layout 逐欄位 offset + stride 與 TiXL 三源（point.hlsl/Point.cs FieldOffset/.t3 Stride）完全一致，static_assert 釘死。

**現行 golden 的結構性盲區**（為什麼上面全是綠燈）：①比對對象是手寫不變量不是 TiXL 參考值（「半徑=2 的環」錯版也對）②default 永不被測（每個 golden 都顯式 set params）③enum 零驗證 ④NodeSpec 表面不被約束（spec 外 param 照樣綠）⑤組合不測（旋轉測 0°）⑥邊界不測。

---

## 4. 時間/交互契約 vs「一模一樣」——分岔清單（需柏為逐條重審）

CONTRACT_ALIGNMENT_LEDGER 的 TiXL 對照大半紮實（L1/L8/L9/L10/L5 子集/L3 實質同構），但以下是**刻意或未察覺的分岔**，按撞擊面排序：

- **D1/D2/D3（一根：參數編輯/錄製的手感模型）**：我方=Ableton 三件套（彈層無記憶／黏著 override+re-enable 鈕／arm punch-in 錄曲線）。TiXL=**動到已動畫參數=當場在播放頭寫 key**（`Animator.cs:505-527`）、**無 override 概念、無參數錄製**（Record 鈕錄的是音訊+MIDI/OSC clip，不是參數曲線）。兩個模型互斥，只能選一個當預設行為。
- **D4（automation 的家）**：我方=scoreGraph 第五張圖（全域、一 patch 多版本）。TiXL=**per-Symbol Animator**（每層 compound 自帶 timeline，TimeClip 重映子層時間，無版本分岔）。**這條現在跟 compound 地基直接相撞（§1 C3），不能再停車**。
- **D9（時間單位，未拍板的暗洞）**：TiXL **bars 原生**（`TimeInSecs=TimeInBars×240/Bpm`，曲線 key/TimeClip/Loop 全以 bars 存檔，改 BPM 全部 automation 跟拍縮放）。我方合約寫「bars/seconds 兩可」、BPM 全文未出現。**檔案格式+求值語意雙重撞點**。
- **D5（LiveSource driver kind = 殘留）**：audio 已重織成 AudioReaction 節點+拉線（柏為自己定的），契約 driver enum 裡的 LiveSource 已無活用例，TiXL 的 live 輸入一律是節點。可直接收斂。
- **D6/D7/D8**：transport length（TiXL 無此概念）／音檔當時鐘主人（TiXL 相反：播放頭永遠 master、soundtrack 追+0.04s 閾值 resync）／AudioFrame 值型別（TiXL 用 ambient context 不入圖）。各自獨立可逐條收斂。
- **D10/D11**：延遲閘門、FrameScheduler 內部結構——**無外部撞擊面，保留無虞**。
- **D12（Curve 照抄時會漏的四個洞）**：合約型別表漏 `Horizontal` 內插（TiXL 6 種）；Pre/PostCurveMapping 外插（Cycle/Oscillate…）整段沒列；雙邊張力 TensionIn/Out + Weighted 才走 Bezier 的 gate；TimePrecision=4 捨入是 key 身分。S3 動工從源碼抄，別從 ledger 抄。
- **停車但撞外觀**：TimeClips 停多久，timeline 外觀就跟 TiXL 差多久（TiXL timeline 一半是 clips lane）。

---

## 5. Repo 衛生（meta 層，這次健檢順手驗出）

1. **`external/tixl` 無戶口**：不是 submodule、沒鎖 commit，是追著 upstream main 的活 clone（現 @ `395c4c55`，2026-06-03）。所有 spec 引文都是對浮動樹的裸行號——誰 `git pull` 一次，引文全體腐爛。→ 鎖 SHA 制（見 §7 閘 0）。
2. **文件地層無分界**：docs/ 疊四個時代（Vuo 06-04 → TIXL_MESH_DRAW 06-04~05 → NATIVE_* 06-05~06 → 現行 06-07+），死時代的 proof/contract 沒有 era banner；`docs/tixl-porting/PORT_STATUS_BOARD.md` 名字像 dashboard 實為 Vuo 時代物。**未來任何 agent grep docs/ 都可能把前朝法律當現行法**——柏為點名的病（partial reader 被誤導）我們自己的 repo 也在製造。
3. **Master plan 自我矛盾一條**：末行「**不照搬 TiXL Symbol schema**（graph 用自己 native 版）」已被 compound 契約 1（Graph 升 Symbol 照 TiXL）推翻，stale line 還在 handoff 段。
4. 分支 `codex/js-to-cpp-contract-migration` 領先 origin **8 個 commit 未 push**（compound 批次 0 + 契約都在裡面，單點風險）。
5. 這台機器**無 dotnet**（§7 閘 3 Tier-1 oracle 的前置，`brew install dotnet-sdk` 級）。

---

## 6. 病因診斷（為什麼「已複驗」還是漏）

契約 2.0 的複驗動作是真的、引文全對（行號抽查 18 條全中）。漏的機制：

- **複驗的形狀是 confirmation**（驗證自己引的行）**不是 coverage**（枚舉宣稱覆蓋的領土再 diff）。「每幀走訪+version 比對」讀自 `Slot.cs`——對；但「op 怎麼消費 slot」住在 `Operators/Lib/` 的 1000+ 顆 op 裡，**沒有任何 sweep 義務逼人去掃 flow/ 資料夾**。
- **讀的路徑由實作需求驅動**：當下在做點 op（純值圖），所以讀到的都是純值圖會碰的機制——這正是柏為說的「只拿了他以為他需要的」的精確機轉。
- **selftest 驗自己的不變量不驗 TiXL 的行為**：手寫「半徑=2」不會抓到 Euler 序錯；綠燈的證據等級沒有標示（oracle 級/掃描級/轉述級混在一起都叫綠）。

→ 治法不是「叫 agent 讀更仔細」（不可度量），是讓**覆蓋率變成可審計的物件**、讓**驗證者的任務是否證不是確認**、讓**golden 數據來自機器跑出來的 oracle 不是轉述**。= §7。

---

## 7. 工作系統提案：三閘一帳本（待柏為拍板）

> 設計目標：高度自動化（fan-out 量產 op）同時保證 TiXL 語意一致。安全不靠「相信 agent 誠實」，靠三個彼此獨立的機制。

### 閘 0｜戶口（provenance）
`external/tixl` 鎖定 parity target SHA（現 = `395c4c55`），寫進一個 `PARITY_TARGET.md`（SHA + 日期 + 升級儀式=顯式 re-pin + 全帳本 drift 稽核）。所有 spec/passport 引文自動繼承這個錨。**成本：一個檔案。**

### 閘 1｜領土護照（coverage 可審計）——治「不知道自己沒讀什麼」
每個移植單元（一顆 op／一條機制宣稱）動工前先產 **passport**：
- **領土清單由 sweep 產生，不由實作需求產生**：grep-complete 的枚舉（「`Operators/Lib/flow/` 全部 op 的 Update」「全部寫 `ctx.X` 的呼叫點」「全部 `InvalidateGraph` 呼叫者」），每個檔標 `read / ported / diverged / parked`，**loop-until-dry**。
- 行為清單：ports/defaults/enums/輸出 attributes/ctx 讀寫/dirty 互動/邊界 case。
- 沒有 passport 不准動工；passport 進 repo（機器可 diff）。

### 閘 2｜三權分立 pipeline（Workflow 引擎）——治「讀的人=寫的人=驗的人」
用 Claude 的 **Workflow** 工具把今天這次健檢的手動流程固化成可重跑的 pipeline，每單元三角色**強制隔離**：
1. **Reader**：只讀 TiXL、產 passport，**禁止看我方碼**（不被實作需求帶偏）。
2. **Porter**：照 passport 實作 + selftest（可以是 Codex/葉子 agent，契約層仍歸主 session 順序鎖）。
3. **Refuter panel**：獨立掃領土，prompt 是**否證式**（「枚舉 TiXL 領土中違反此宣稱的 op/機制」，不是「確認此宣稱」），多 lens（defaults/edge/attributes/ctx/dirty），**連續 2 輪挖不出新反例才放行**。
今天的健檢=這條 pipeline 的手動原型；它找到的每一條，都是「confirmation 式複驗」放過的。**速度論證：最慢的路是返工——今天證明返工會打到當天拍板的地基。**

### 閘 3｜Oracle 數據分級——治「golden 驗自己不驗 TiXL」
- **Tier 1（裝 dotnet 後即可，全自動）**：`tools/parity-oracle/` 放一個小 dotnet console，**直接引用/抽取 TiXL 的純 C# 數學**（Curve/VDefinition/Bezier 取樣、System.Numerics 四元數合成序、TransformMatrix）在 Mac 跑出 JSON/二進位 fixtures → 我方 selftest 逐欄 diff。**Euler 序這類 bug 從此機器可抓。**（TiXL 焊死的是 DX11/WinForms；純數學類不焊。）
- **Tier 1b（立刻可做）**：**spec-parity 靜態表**——agent pipeline 從 TiXL `.cs`+`.t3` 機器生成每顆 op 的 `port/型別/default/enum labels` 資料檔，selftest 機械 diff NodeSpec + **cook↔spec 一致性檢查**（cook 讀的 param 必須存在於 spec port）。**殺掉 default 漂移與隱形旋鈕這兩族病，一勞永逸。**
- **Tier 0（可選，一次性）**：若有任何 Windows 機（朋友的/雲端），真 TiXL headless 跑 N 張標準圖 dump point buffer 成二進位 fixtures 進 repo = 真 oracle。沒有就停在 Tier 1。
- **眼手 harness + 柏為親手 = 完成定義，不變。**

### 帳本｜證據分級
PARITY ledger 通用化：每單元的「綠」帶等級——`oracle-verified`（Tier 0/1 數據）/ `sweep-verified`（閘 1+2 過）/ `transcribed-only`（現行多數）。看一眼就知道每顆節點的「一模一樣」是哪種等級的一模一樣。

### Claude 能力映射
| 能力 | 用在 |
|---|---|
| **Workflow**（deterministic fan-out/pipeline/否證 panel） | 閘 2 引擎；per-op 量產 pipeline（read→passport→port→golden→refute）；契約級宣稱的 territory sweep |
| **Agent + worktree 隔離** | lane 並行不互擾（已是現行紀律） |
| **/loop 或 scheduled task** | drift 巡檢：夜掃帳本抽樣 re-refute + upstream TiXL 漂移偵測（對 PARITY_TARGET diff） |
| hooks / commit 律法閘 / --selftest | 已有，不動 |

### 上線順序（提案）
1. **先修契約**（§1 C1–C5 + §2 的 S1/S2/S3/S14 進契約 2/3，柏為拍板）——這是返工成本最高的一層，文件改起來最便宜。
2. **閘 0 + Tier 1b**（戶口檔 + spec-parity 靜態表 pipeline）——一天級，立刻封住 default/隱形旋鈕兩族病。
3. **閘 2 固化成 Workflow 腳本**——拿「契約 2 修正後的 re-refute」當第一個正式 run。
4. **Tier 1 oracle**（裝 dotnet + Curve/quat fixtures）——在時間 lane S3（Curve）動工前就位。
5. 批次 1 動工（修正後契約）；點 op parity 修正批（lane A 解凍後）直接走新 pipeline。

---

## 8. 拍板佇列（只有柏為能決定的）

| # | 題目 | 狀態（2026-06-10 15:20 拍板） |
|---|---|---|
| P1 | **參數編輯/錄製手感**（D1/D2/D3 一根） | ✅ **拍板：照 TiXL 當預設**（動到已動畫參數=播放頭寫 key、無 override）；Ableton 彈/黏著 override/punch-in 三件套**停車**，之後做成可開關表演模式（蓋在 TiXL slot override 縫 `Slot.cs:91-117` 上） |
| P2 | **automation 的家**（D4 + C3） | ✅ **拍板：照 TiXL，per-Symbol Animator**（曲線權威=定義層、reuse 共曲線、每層 compound 自帶 timeline）；scoreGraph 第五張圖**作廢**；「多版本 score」停車（之後用 TiXL Variations/Snapshots 概念接） |
| P3 | **時間單位**（D9） | ✅ **拍板：bars 原生**（曲線 key/TimeClip/loop 以小節存，秒=bars×240/BPM；BPM 進 transport+存檔；改 BPM=automation 跟拍） |
| P4 | **工作系統三閘一帳本**（§7） | ✅ **已採**（柏為 22:17 授權「直接決定」）：閘 0 戶口檔已立（`PARITY_TARGET.md`，鎖 SHA 395c4c55+升級儀式）；閘 1/2 即日生效為工作紀律（本檔的二輪否證就是首次正式 run）；**Tier 1b（spec-parity 靜態表）觸發點＝lane A 解凍**；**Tier 1（dotnet oracle）觸發點＝時間 lane S3 動工前**（機器尚無 dotnet，屆時裝）；**Tier 0（Windows 真 dump）＝有 Windows 機才做**，不主動找 |

**P1–P3 拍板後已執行（同日）**：C1–C5 + S1/S2/S3/S5–S20 已修進 compound spec（14 處，各標「健檢修正」、原句作廢保留）；`CONTRACT_ALIGNMENT_LEDGER.md` 與時間 lane build plan 已加取代 banner（D1–D5/D9 處置、D6/D7/D8 留 M2/M4 前拍、D12 四洞釘進 S3）。

## 9. Refuter 二輪結果（2026-06-10 晚；修正後契約的否證複掃）

兩隻獨立 refuter 對**修正後**的契約全文否證（R1：契約 2 vs Slots 13/13 檔+flow 11 op+render 8 op；R2：契約 1/3/4 vs Symbol/Json/Animator 全文+Editor combine/copy+全庫 1,298 個 .t3 機器普查）。**結果：修正主幹全部站住**（C1–C5/Animator 定義層/bars/容錯/curve 全欄等 70+ 引文逐條核過），**抓到 12 項二修，全數照 TiXL 修進契約**：

- **我寫歪的（5）**：S9 三段協定不是統一批次（Execute 批次/Group 逐顆交錯/Switch 無 Prepare——port 哪顆照哪顆）；S5 限縮是 opt-in（`OptimizeInvalidation` 預設 false）；2.0「往下游標髒」方向反（InvalidateGraph 往上爬；真髒源=接線 ValueVersion=-1/斷線 ForceInvalidate）；.t3 普查數字錯（真實=28,894 條、SharpDX 3.0%、Lib 13%——enum 映射表升必做件）；C3 段「改曲線→重掛」混了兩件事（閉包共曲線即時生效；重掛=加/移除/reconnect）。
- **批次清單沒跟上自己的修正（1，最危險）**：批次 1/1b 行還留著作廢的 driver enum {…LiveSource|Override}——已同步。
- **新洞（6，全為承重級）**：walker 第四原語=有狀態 op 的 FxTime 時間門（漏了→粒子掛 reuse/Loop 下狀態雙步進）；GetVar 全族 output 宣告 Animated 恆髒（S7 主機制）；增量 patch 的 version 規則組（新生 slot 初始髒/接線 ValueVersion=-1 非 bump/斷線復活 constant driver）；keep 槽三用途互斥（P1 表演模式的縫要先定疊加）；**combine 必搬動畫曲線**（P2 推論，漏了=動畫蒸發）+ port 粒度=per 邊界連線；**copy/paste 資料語意整缺**（柏為驗收「複製第二份」第一天就踩）。另 S13 保留條件補「型別相同」、bypass 兩個前置條件、S18 Enabled 旗、S19 PreviousId/SnapshotGroupIndex。

**結算：批次 1/1b 領土（Slots/Symbol/Json/Animator）已被兩輪獨立否證掃過、契約與源碼一致；⛔ 解凍，批次 1 可動工。** 未掃領土（誠實標）：MagGraph 編輯路徑、Variations 內部、資源系統、.t3ui 全深度——皆批次 3+ 視野，到批次前用閘 1/2 流程掃。

---

## 附錄：已驗證一致（安心區，這些不用再疑）

- 批次 0 資料模型：四元組/sentinel(`kSymbolBoundary=0`≡`Guid.Empty`)/reuse/override-only-non-default/兩階段 load 順序/單輸入 replace-on-connect/型別檢查拒接——**全部與 TiXL 同構**。
- version-chasing 本體/`_valueIsCommand`/editor-only dirty 統計不抄/兩鐘形狀（TimeInBars vs FxTimeInBars + Playback 暫停語意）/TimeClipSlot save-restore 非 stack/決策 9 的「slot 扛三事、無獨立 binding 層」——**契約 2 這半邊讀對了**。
- SwPoint 64B layout 零債（static_assert 釘死）。
- AudioReaction lane 對齊屬實。
- 「雙軌 invalidate」REFUTED 維持成立；查無 KeepDirty/SkipUpdate/refcount（契約沒提它們不是漏）。
- 契約 1 的 inputDefs[]/outputDefs[] 設計方向正確（正好補上 .t3 依賴 C# 的洞，必要 superset 非過度設計）。

*稽核者：Claude（主 session + 5 隻否證式 subagent），2026-06-10。引文錨 = external/tixl @ 395c4c55。*
