# Compound Graph + 常駐增量求值 設計契約（一條地基）

> **這是 compound lane 的設計契約（active lane 的驗收合約）。** 2026-06-10 柏為拍板：
> 暫停所有節點/render-target 製作，先把 compound 圖模型這層做好；**功能 100% 照 TiXL**，
> **所有設計決策權威 = `external/tixl` 源碼**（不自創）。本檔是「先設計、不動碼」的產物。
> 實作計畫待 writing-plans 產。SSOT dashboard 仍是 `2026-06-07-imgui-metal-pivot-master-progress.md`。
>
> **★ 2026-06-10 第二次拍板（側議壓測 + 源碼複驗，合併兩條地基）**：目標規模 = **跟 TiXL 一樣大**（幾千節點、
> 深度巢狀、滿屏靜態預算）。在這個規模下，**compound（圖模型）與增量求值不是兩條正交地基，是同一條**——
> 一個**常駐的、可增量更新的求值圖**，上面跑 version/dirty + per-node cache。原契約 2 的承重決策 3
> （「cook 前每幀展平成 throwaway flat graph、runtime 一行不動」）**作廢**（見下，已標 ❌ 作廢 + 理由）。
> 改成：**展平結果常駐住、只在編輯圖時增量更新**，cache 掛在常駐節點上。批次 0 資料模型（Symbol/Child/
> Connection/sentinel/reuse）**不受影響、仍正確**；改的只是「求值怎麼跑」，而那還沒實作（批次 1 才動）。
> TiXL 求值機制已對源碼逐條複驗（`Slot.cs`/`DirtyFlag.cs`/`DirtyFlagTrigger.cs`，見契約 2）。
>
> **★ 2026-06-10 第三次拍板（柏為「壓時間軸底層」逼出，拍板 A，見決策 9 / 契約 2.5b）**：compound、增量求值、**時間 binding** 三條原本當並列 lane 的東西，地基是**同一個物件**——TiXL 的 `Slot` 一個物件扛接線/dirty/driver 三件事。→ **batch 1 的 resident 節點直接 = slot**：input 帶 `driver`、`isLiveSource` 從 driver 推導不另存、時間 lane 的 S1 `SourceRegistry` 收編、`EvaluationContext` 即刻長兩鐘形狀。時間 lane（`2026-06-09-runtime-time-interaction-build.md`）排序不動，只是 S3–S6 automation 往下接 resident driver、不接平行 registry。
>
> **★ 2026-06-10 第四次（晚）：全 repo parity 健檢修正 + 柏為拍板 P1–P3。** 健檢（5 隻否證式 agent + 主 session 親驗源碼；SSOT = `docs/runtime/TIXL_PARITY_HEALTH_2026-06-10.md`）發現本契約五條地基級誤讀/未覆蓋（C1–C5），已修進本檔（各處標「健檢修正」、原句作廢保留）。柏為同日拍板：**P2 automation 權威 = Symbol 定義層 Animator（照 TiXL）**——scoreGraph 第五張圖作廢、「一 patch 多版本 score」停車（之後用 TiXL 自家 Variations/Snapshots 概念接）；**P3 時間單位 = bars 原生**（曲線 key/TimeClip/loop 以小節存，秒 = bars × 240/BPM，BPM 進 transport + 存檔）；**P1 參數編輯手感 = 照 TiXL**（播放中動到已動畫參數 = 當場在播放頭寫 key、無 override 概念），Ableton 彈/黏著 override/punch-in 三件套停車、之後做成可開關的表演模式（蓋在 slot 換 update action 的縫上 = TiXL 自己的 override 機制 `Slot.cs:91-117`）。

## 北極星 / 範圍

- **完整 TiXL compound**：巢狀節點圖（母節點內含子圖）、任意深度、**Symbol 級 reuse**（一個定義多處實例化，改定義→全部變）。不是窄化的「教學容器」。
- **決策權威 = TiXL 源碼**。本檔每個契約點都標 TiXL 機制 + 證據檔案。我們**邏輯/語意 100% 照 TiXL，實作換技術棧**（C++/metal-cpp/imgui-node-editor，非 C#/自製 canvas）。
- 北極星「Mac 版 TiXL」本質要這個——TiXL 整個圖模型就是巢狀的（沒有 flat 圖這回事），我們現在的 flat 圖只等於 TiXL 最外層那一張。地基趁上面還輕時改。

## TiXL compound 的精華（一句話）

`Symbol`(定義) / `Symbol.Child`(實例) / `Connection`(四元組) + **`Guid.Empty` sentinel 表達跨邊界連線**；**沒有 Input/Output proxy 節點**；**求值期邊界透明**（接線期解析掉）。

---

## 契約 1：資料模型（照 TiXL `Symbol`/`Child`/`Connection`）

**TiXL 機制**（`Core/Operator/Symbol.cs:29-50`、`Symbol.Child.cs:24-104`、`Symbol.ConnectionSubClasses.cs:80-148`）：

- **Symbol = 定義**：`Id`(Guid) + `InputDefinitions[]`(對外 input 槽，含型別+預設) + `OutputDefinitions[]` + `Children[]`(子圖實例) + `Connections[]`(子圖連線)。所有實例共用。
- **Symbol.Child = 實例**：`Id`(此實例 Guid) + `Symbol`(引用哪個定義) + per-instance `Inputs`(輸入覆寫，`IsDefault` 標記是否還用定義預設)。多個 Child 引用同一 Symbol = **reuse**（改 Symbol.Connections/InputDefinitions → 廣播到 `_childrenCreatedFromMe`，`Symbol.cs:224-229`）。
- **Connection = 四元組**：`(SourceParentOrChildId, SourceSlotId, TargetParentOrChildId, TargetSlotId)`。
  - `SourceParentOrChildId == Guid.Empty` → source 是**母 Symbol 自己的 InputDefinition**（外面連進來），`SourceSlotId` = 那個 InputDefinition.Id。
  - `TargetParentOrChildId == Guid.Empty` → target 是**母 Symbol 自己的 OutputDefinition**，`TargetSlotId` = 那個 OutputDefinition.Id。
  - 否則 = 子圖內某個 Child 的 slot。（`Instance.Connections.cs:125-198`）
- **無 proxy 節點**：compound 對外 port 直接是 InputDefinitions/OutputDefinitions，不靠子圖內的特殊 Input/Output operator。**這是 TiXL 的精華，照搬。**

**我們的對應改動**（最小形狀，照 TiXL）：

| TiXL | 我們現況 | 改成 |
|---|---|---|
| Symbol(定義) | `Graph{nodes,connections}` + `NodeSpec`(中央表) | Graph 升成 **Symbol**：加 `id`(穩定 UUID) + `inputDefs[]`/`outputDefs[]`(id+name+dataType+default)。一個 .swproj = 一個 **Symbol 庫**（多個 Symbol 定義）+ rootSymbolId。leaf operator(RadialPoints…) 各是一個「原子 Symbol」(無 children，cook 由 registry 提供)。 |
| Symbol.Child(實例) | `Node{id,type,params,x,y}` | Node 升成 **Child**：`type`→`symbolId`(引用哪個 Symbol，原子 Symbol 的 id = 舊 type 字串對應的固定 UUID)；`params` 分成「定義 default(在 Symbol)」vs「實例 override(在 Child，只存非預設)」。 |
| Connection 四元組 | `Connection{fromPin,toPin}`(全域 pin id = node*100+port+1) | 改成 **`(srcNodeId, srcSlotId, dstNodeId, dstSlotId)`**；`nodeId == 0`(我們的 sentinel，等同 Guid.Empty) = 「母 Symbol 自己的對外 slot」。 |

> **承重決策 1（照 TiXL）**：不做 Input/Output proxy 節點。對外 port = Symbol 的 inputDefs/outputDefs，跨邊界連線用 sentinel nodeId=0。
> **承重決策 2（照 TiXL）**：reuse = 多 Child 引用同 symbolId，定義單一來源。改 Symbol → 引用它的全部 Child 都變。
>
> **健檢修正（契約 1 補欄，2026-06-10）**：
> - **C4：multi-input 三層補齊（趁 pin-id→四元組這刀一起定）**——`InputDefinition` 加 `isMultiInput`（照 `Symbol.ConnectionSubClasses.cs:27`；TiXL 全庫 148 處用、`Execute`=`MultiInputSlot<Command>` 是把多條 draw 分支合進一個畫面的唯一機制）。**順序編碼**：同一 (target,slot) 的多條連線順序 = Connections list 內相對順序（`Symbol.cs:165-168`）。**編輯語意**照 `AddConnection(conn, multiInputIndex)`（`Symbol.cs:169-229`）：單輸入自動 replace、多輸入 insert-at-index（index==count 則 append）、`RemoveConnection` 按 index 在同 (target,slot) 子序列挑刪（同 source 重複連同一 multi-input 合法、靠 index 區分）；跨 compound 邊界遞迴攤平保序（`MultiInputSlot.cs:29-41`）。命令層簽名帶 multiInputIndex；求值端 = 2.4 的 version 取和（已有）。**（refuter 二修）刪除語意明定「按 list 位置刪」**：TiXL `RemoveConnection` 選定 index 後用四元組**等值**在 list 找第一個相等的刪（`Symbol.cs:256-269`、`ConnectionSubClasses.cs:113-130`）——重複連線時永遠刪到子序列最前那條、不是 index 指的那條（multi-input 順序有疊圖語意，這是 TiXL 自身 bug 級邊角）。語意照 TiXL（重複合法、index 定位），實作按位置刪、不按等值刪。
> - **S2：Child 結構補欄（皆入 .t3、皆日常編輯動作）**——`name`（自訂名）、`isBypassed`（型別白名單直通 Inputs[0]→Outputs[0]，`Slot.cs:75-89`、`Symbol.Child.cs:250-261`；refuter 二補：①主 output 未連線時 bypass 拒絕生效（`Symbol.Child.cs:287-300`）②白名單 9 型中 ShaderGraphNode 在執行端 `SetBypassFor` 無 case＝靜默不生效——TiXL 自身不一致，**我們白名單以執行端 8 型為準**）、per-output `isDisabled`（**語意=值凍結在最後一次結果、Command 變 no-op**——不是回 default 也不是跳過節點，`Symbol.Child.cs:106-149`、`Slot.cs:43-67`）、per-output `triggerOverride`（見 2.5 修正 S3）。
> - **S12：InputValue 型別化 roadmap**——TiXL 的 default/override 是型別化值（float/int/VecN/**string 路徑**/gradient/curve，`Symbol.ConnectionSubClasses.cs:22-30`）。我們 float 先行**成立**（現行參數宇宙全 float：vector=N float port、enum/bool=float），但 override map 與存檔 value 欄要在**資源型 op（texture/audio 檔案路徑）進來前**升 variant——標為那批的開工前提，非 batch 1 阻擋項。
> - **S13：刪 InputDefinition 收屍規則**——by-id 保留既有 Child.Input（override 存活）、新 def 補建拿預設、刪除的移除；母圖指向已刪 slot 的連線同步掃除（含 multi-input 保序刪，`Symbol.TypeUpdating.cs:99-132,213-261`）；讀檔遇 obsolete input 跳過不死。**（refuter 二修）保留條件＝id 相同「且 `ValueType` 相同」**——型別變了就丟棄 override 重建拿新預設（`Symbol.TypeUpdating.cs:248-261`）；v2 的 inputDefs 是使用者可編輯資料，「改 compound input 型別」會真實發生，缺這條＝錯型別 override 殘留。我方現況 dangling 四元組無清理，補。
> - **S14：循環防護兩層**——接線手勢層擋連線循環（TiXL `Structure.CheckForCycle` 的純資料版等價物：從 source 反向收集依賴、看 target 在不在內；TiXL Core 不驗、防線全在手勢層）+ buildEvalGraph 入口訪問棧（見 2.3 修正；TiXL 對 symbol 自我巢狀整個沒擋=會爆棧，**不抄這個行為**）。

---

## 契約 2：求值 — 常駐增量求值圖（compound 攤平 + version/cache 合一）

### 2.0　TiXL 求值機制（對源碼逐條複驗，這是設計權威）

> 以下每條都對 `external/tixl` 源碼驗過，不是轉述。是新契約要對齊/取捨的基準。

- **version-chasing dirty，不是 content hash**：每個 slot 帶 `DirtyFlag{ SourceVersion, ValueVersion }`（`DirtyFlag.cs:61-62`）。`IsDirty => TriggerIsEnabled || ValueVersion != SourceVersion`（`DirtyFlag.cs:19`）。`Update()` 只在 `IsDirty` 時跑 `UpdateAction`，跑完 `Clear()` 把 `ValueVersion=SourceVersion`（`Slot.cs:160-169`）。**這就是省下靜態重算的那層——大錢在這。**
- **time/animation = always-dirty source**：`DirtyFlagTrigger{ None, Always, Animated }`（`DirtyFlagTrigger.cs:6-11`）。Trigger 非 None → `IsDirty` 恆真 → 每幀重算（`DirtyFlag.cs:67-76`、`Slot.cs:296-298`）。**這就是「會動的源頭」的 TiXL 對應物。**
- **每幀走訪整個「連到輸出」的子圖**：render loop 每幀 `GlobalInvalidationTick++`（`Program.RenderLoop.cs:53-54`）後呼叫 `output.InvalidateGraph()`（同檔:84）。`InvalidateGraph()`（`Slot.cs:266-323`）遞迴爬上游每條連線，把上游的 `SourceVersion` 抓下來比對。memoize（`InvalidationTick==GlobalInvalidationTick`，`Slot.cs:269`）**只防同一幀內 diamond 重訪、不跨幀**。→ 連到輸出的幾千節點，每幀每顆被踩一次。**但這個踩很便宜**：指針追 + 一個 int 比較、`AggressiveInlining`、零配置，**且不重算**（重算被上面的 `IsDirty` 擋掉）。
- **edit 期 push invalidate（refuter 二修：方向）**：`InvalidateGraph()` 是**往上游爬**抓版本、只把被編輯的 slot 自己標髒（`Slot.cs:266-323`），**沒有 downstream push 機制**——下游靠下一個 tick 的 per-pass invalidation 才看到新版本。接線的真實髒源＝`AddConnection` 設 `ValueVersion=-1` 強制首拉必算（`Slot.cs:199,205`）；斷線＝`ForceInvalidate()`（`Slot.cs:244`）；編輯後呼叫 `InvalidateGraph()`（`Instance.Connections.cs:230`、`Symbol.Child.cs:669-671`）是讓被編輯端立即重抓上游版本，不是往下推。**健檢修正 S1：值改動也是 edit-time push**——`SetTypedInputValue` 設值後當場 `DirtyFlag.Invalidate()`（`InputSlot.cs:57-63`；編輯器路徑用更強的 `ForceInvalidate()`，`ChangeInputValueCommand.cs:122`）。求值期 version 比對只**傳播**版本（連線端 adopt 上游版本，`Slot.cs:287-289`），不產生版本——版本源頭只有兩種：trigger 葉 + edit-time push。原句「值改動走 pull（求值期 version 比對）」**作廢**（照它實作 = 沒有東西 bump 版本，改參數永遠卡舊）。
- **健檢修正 C1-c：「幀」不是 invalidation 單位，tick 才是**——op 可在 Update 中途 `GlobalInvalidationTick++` + `InvalidateGraph()` + `GetValue()`，對同一子圖**同幀多次重評、每次不同 context**（`Loop.cs:23-40` 每 iteration 寫 ctx 變數後重評；同型 `ResetSubtreeTrigger.cs:18-23`、`Group.cs:65-69` ForceColorUpdate）。「重評子圖」是任何 op 都可呼叫的原語，不是中央 loop 專屬。求值單位 = **evaluation pass**，count 類 selftest 單位跟著改。
- **健檢修正 S5：invalidation 走訪可被 op 限縮**——`Switch` 用 `LimitMultiInputInvalidationToIndices` 讓 invalidation 只遞迴 active 分支（`MultiInputSlot.cs:14,57-69`、`Switch.cs:71-86`；源碼自註這是幾千 instance 規模下 invalidation 變瓶頸的對策）。「每幀照走全圖」不絕對；未選分支版本豁免期間**不傳播**，re-enable 後下一 tick 補傳播——「invalidation 可暫時不完整」是 TiXL 自己接受的語意。**（refuter 二修：此限縮是 opt-in**——整段被 `OptimizeInvalidation` input 閘住（`Switch.cs:71`），其 .t3 預設 `false`；**預設行為=全分支照走、無豁免**。照抄=預設關、使用者可開。）
- **偶然複雜（editor-coupled，可不抄）**：`DirtyFlag` 上的 `FramesSinceLastUpdate`/`NumUpdatesWithinFrame`/`SetUpdated`/`_lastUpdateTick` 全標 `// editor-specific`，唯一消費者是 GraphNode 畫節點邊框閃爍（`DirtyFlag.cs:48-91`）。5 個 Slot 子類（`InputSlot`/`MultiInputSlot`/`TimeClipSlot`/`TransformCallbackSlot`+base）因 C# generic per-type slot + `InvalidationOverride` 而生（`Slot.cs:275-282`）。
- **複驗修正一條轉述**：側議說 TiXL「雙軌 invalidate」——**REFUTED**。只有一個 `InvalidateGraph()`，兩個呼叫點（per-frame lazy / edit-time eager）。不是兩套碼。

### 2.1　我們的起點與真實差異

我們現況 = **stateless re-cook**：每幀 `PointGraph::cook(graph, targetNode)` 從終端往回 walk 重煮，per-node-id 的 GPU buffer/state 跨幀複用（buffer map keyed by node id）。沒有持久節點列表、沒有 version、沒有 cache、沒有 dirty。

> **關鍵觀察（這決定整個合併）**：我們其實**已經有半套常駐**——那個「per-node-id buffer map」就是一個照 id 定址、跨幀活著的常駐結構。少的只是：①一份不每幀重建的常駐節點列表 ②每節點的 version + cached output ③LIVE/STATIC 分區。**這不是從零重寫，是把已隱含的常駐（buffer map）升成顯式的常駐求值圖。**（Karpathy 先求簡單：能用現有結構就不另起爐灶。）

### 2.2　❌ 作廢：原承重決策 3（每幀展平、runtime 不動）

> **原文（保留作歷史）**：「compound 是圖模型/編輯/存檔層概念；cook 前先展平成等價 flat working graph（throwaway），runtime cook 完全不變；成本=每幀重建 flat graph（便宜）。」
>
> **為何作廢（規模定在 TiXL 後）**：①每幀把幾千節點重攤平成新 graph = 配置churn + 對快取不友善 + **丟掉每節點的跨幀身份**。②更致命：**cache 必須掛在一個跨幀活著、有穩定身份的節點上**——「上次的 output + version」要有家。每幀丟掉 flat graph，cache 就無處可掛。→ 「每幀展平」與「要 cache」在 TiXL 規模直接互斥。這是 compound 和增量求值**被逼成同一條地基**的根因。

### 2.3　★ 承重決策 3（新）：常駐增量求值圖

**compound 攤平的結果是「常駐求值圖」（resident eval graph）：建一次、編輯圖時增量更新、cache 掛在它的節點上。**

- `buildEvalGraph(symbolLib, rootSymbolId) -> ResidentEvalGraph`：遞迴 inline 每個 compound Child 的子圖；用 **path-qualified node id**（compositionPath 的 hash，如 `childId₁/childId₂/localNodeId`）給每個 inlined 實例唯一且**跨幀穩定**的 id。sentinel 邊界(nodeId=0)在建圖時就地解析成直連。**= TiXL「接線期解析邊界、求值期透明」的等價物。**
  **健檢修正 S14/S6**：①flattener 帶**訪問棧**擋 symbol 自我/循環巢狀——TiXL 這裡是裸的（防線全在 editor 接線手勢層 `Structure.CheckForCycle`，`Structure.cs:314-370`；Core 不驗、自含 symbol 會無限遞迴），我們照搬手勢層檢查 + buildEvalGraph 入口再驗一道，**兩層都要**（常駐圖時代沒有每幀重建兜底，一條循環 = 每幀 hang）。②「邊界一律 inline 透明」對 **ICompoundWithUpdate**（帶 update body 的 compound 一級類，`Slot.cs:188-206`；DrawMesh/DrawScene 屬此且是最常用 op）不成立——第一批把這類 op 當原子 Symbol 處理，常駐圖留「邊界節點帶 body」的位。
- **path-qualified id 一鑰三用**：①常駐節點的身份 ②cache key ③現有 per-id buffer map 的鍵。三者同一把鑰匙——這是 compound 與 cache 對齊的接縫。
- **不每幀重建**：editor 改圖（加/刪節點/連線、進出 compound、改 reuse 的 Symbol 定義）時，**增量 patch** 常駐圖（加/刪對應常駐節點、重接、標記受影響區重算分區+失效 cache）。圖沒被編輯的幀，常駐圖原封不動。
- **健檢二補（refuter）：增量 patch 的 version 規則組（「patch==全重建」golden 的依據，照 TiXL）**：①新生 slot `SourceVersion=1, ValueVersion=0`＝**initially dirty**（`DirtyFlag.cs:62` 自註）②接線＝採納上游 SourceVersion + 設 `ValueVersion=-1` 強制首拉必算（`Slot.cs:198-205`；**不是 bump sourceVersion**——亂 bump 會打歪 multi-input 版本取和的算術）③斷線＝恢復接線前的 update action（constant driver 復活，`_actionBeforeAddingConnecting`/`RestoreUpdateAction`）+ `ForceInvalidate`（`Slot.cs:233-245`）④ICompoundWithUpdate 的 output 接線時保留 op 自身 body 不換 pass-through（`Slot.cs:195-201`，呼應上面 S6）。
- **reuse 狀態隔離自然成立**：同 Symbol 多 Child → 不同 path-qualified id → 各自獨立的常駐節點 + buffer/state + cache，跨幀穩定。
- **runtime 仍受保護**：cook 的逐顆 operator body（render-target pivot 剛 land 的）不動；改的是「外層怎麼 walk + 何時跳過」。承重線：常駐圖 + 求值 walker 是新元件，operator kernel 不動。

### 2.4　★ 承重決策 6（新）：version-chasing dirty + per-node cache（地基；照搬 TiXL 語意）

**這是地基，也是你「滿屏靜態白燒」的真正解。** 照搬 TiXL 的 version-chasing（`DirtyFlag.cs:19/61-62`），實作換我們的結構。

- **健檢修正 C5：粒度 = per-output-slot，非 per-node**——`{ sourceVersion, valueVersion, cachedOutput, updateAction }` 長在常駐節點的**每個 output slot** 上（TiXL `Slot.cs:35-39`；多輸出 op 各 output 獨立 dirty/update，`Switch.cs:6-15` Output+Count 各掛 action；我們現成例 = AudioReaction 三輸出）。dirty = `valueVersion != sourceVersion`（**version、不用 hash**，照 TiXL）。2.5b「resident 節點 = slot」的精確展開：**一個節點 = N 個 output slot + M 個 input（帶 driver）**。
- 走到某節點：dirty → 重算、寫 cachedOutput、`valueVersion = sourceVersion`；不 dirty → 回 cachedOutput、**不重算**。→ 貴的靜態 op（mesh 細分 / texture 預處理鏈）第一幀算一次、之後每幀回 cache、**GPU dispatch 不再發**。**這條就是「算一次存著」，跟 TiXL 完全對齊。**

> **★ 底層對齊關鍵（源碼驗過，我前一版講糊了，這版釘死）：TiXL 每幀「兩趟」，我們「一趟」，但結果等價、且我們更簡單是有正當理由的，不是耍聰明。**
> - **TiXL 兩趟＝被 lazy-pull 逼的**：`output.GetValue()` 只在自己已 dirty 時才往下 recurse（op 的 update 內部才呼叫 `input.GetValue()`，如 `AddVec3.cs:19`、`Transform.cs:27-44`）。所以深處 leaf 的改動，必須**先**一趟 `InvalidateGraph()`（`Slot.cs:266-323`）把 dirty 從 leaf 傳到 top，否則 lazy pull 在 top 看到乾淨就回 cache、永不下探。兩趟＝版本傳播趟 + lazy 計算趟。
> - **我們一趟＝eager 後序的副產物**：我們的 re-cook 從終端 eager 後序走（先解 input 再算 node，連到終端的節點每幀都踩到）。走到一個 node 時 input 已解、版本已知 → **版本傳播與重算在同一趟完成**。TiXL 要分趟的理由（lazy 在 top 會停）我們天生沒有。
> - **等價性**：兩者重算的節點集合相同（dirty cone）、結果相同。我們省掉 TiXL 的第二趟 re-descend（marginal）。**這是低風險、可驗的簡化（golden：一趟結果==TiXL 語意的兩趟結果）。**
>
> **⛔ 健檢修正 C1（地基級）：上述等價只對「純值圖」成立。** 成立前提有三：①op 無條件拉取全部 input ②op 不在拉取前改 context ③無觸發語意。TiXL 的 flow/render 兩族**系統性違反全部三前提**（主 session 親驗 + 否證 agent 掃 `Operators/Lib`）：
> - **條件式拉取**：`Switch.cs:67` 只 GetValue 選中分支（-1 全不拉）；`Execute.cs:17-36` IsEnabled=false 整串不拉；`Group.cs:58-77`、`PickTexture.cs:23-24`、`TimeClipSlot.cs:55-59`（出範圍子樹不評）。eager 後序會把關掉的 Command 分支也執行 = **發出不該發的 GPU 副作用**，行為錯非 perf 差。
> - **拉取前改 context**：`Transform.cs:42-45`（ObjectToWorld）、`Camera.cs:36-45`、`RenderTarget.cs:81-159`（解析度/矩陣/背景色）、`DrawMesh.cs:26-44`（換 PbrMaterial 再拉內圖）——save-mutate-pull-restore 是 render 圖標準型，**context 是拉的當下流進子圖的，op body 就是排程器**。「先解 input 再跑 node」= 子圖拿錯 context，這是 TiXL 場景圖語意的本體。
> - **dirty 是 op 可讀寫的一級狀態（S4）**：`ExecuteOnce.cs:20-24` 把 input 的 `DirtyFlag.IsDirty` 當布林事件讀 + 手動 `Clear()`；`Once.cs:23-34` 同型且 runtime 自翻自己 output 的 Trigger。walker 先解 input 會把訊號吃掉，trigger 系 op（bang/once/gate）整族做不了。
> - **ctx 變數通道（S7，refuter 二修）**：`Loop.cs:25-34` 寫 ctx 變數、下游 GetVar 類值節點讀。**主機制＝Get\*Var 全族 output 宣告 `DirtyFlagTrigger.Animated` 恆髒**（`GetFloatVar.cs:6`，7 顆全標）——每次 pull 必重讀 ctx；bump-tick 只負責把髒**往 GetVar 的下游**傳（Loop 需要）。反證：`TimeClip.cs:21-22` 寫變數完全不 bump tick，正確性全靠 GetVar 的 Animated。port GetVar 類節點時「op 宣告恆髒」這刀不可漏；「寫入者不 bump、GetVar 下游隔層值節點同幀 stale」是 TiXL 自己接受的殘餘語意，照抄。cache key 只有 version、不含 context。
> → **求值驅動模型分兩界**：**值圖 = eager 後序一趟**（現行 9 顆點 op 與批次 1b 範圍，安全、等價成立）；**Command/flow 圖 = pull-driven**（op body 驅動 input 解析與 ctx 變異 = TiXL lazy pull 本體，不可省）。walker 須暴露**四個** op 原語：①以新 tick 重評子圖（Loop 類）②讀寫自身 input dirty 當事件 + 手動 Clear（trigger 類）③ `ForceInvalidate` 外部注入口（資源熱重載/audio 從 loop 外打進來，`AbstractResource.cs:136-146`，無 memo、跨 tick）④（refuter 二補）**有狀態 op 的時間門自我去重**——stateful op 用 FxTime 變化門擋同 pass 多評：`Once.cs:16-19`（LocalFxTime 差 <0.0001 直接 return）、`EvaluationContext.HasTimeChanged(ref last)` + `TimeResolutionInBars=0.001`（`EvaluationContext.cs:60-73`）。沒有④，粒子/sim 類掛進 reuse-by-reference 或 Loop 下會**狀態雙步進**——我們的 ParticleSystem 屬此類，批次 1b 就要帶。**Command 圖接上常駐圖（render 流深化）之前，這條界線是承重前提；批次 1b 不需要蓋完它，但不准蓋出與它矛盾的東西。**

- **Command／副作用 = 永遠跑（不 cache），照搬 TiXL `_valueIsCommand`（`Slot.cs:162`）**：TiXL 對 Command 型 slot `if(IsDirty || _valueIsCommand)`——Command 是「要被觸發的動作」，每次 GetValue 都得執行、不能 cache。→ 我們的「Command/present 永遠重發」**不是保守 workaround、是 TiXL 字面行為**。
- **健檢修正 C2：去重按型別分界，原句「一節點一幀只算一次」作廢**——**值 slot**：version/cache 天然短路（同 pass 第二次拉 = 回 cache），不需也不該另設 per-frame visited 旗；**Command slot：每 pull 必跑、無去重**（`Slot.cs:162` `_valueIsCommand`；同一 Command 子樹被兩個 Transform 父各拉一次 = 兩個 context 下各畫一次，**這是 TiXL 的 reuse-by-reference 畫法**，`ExecRepeatedly.cs:43-49` 同幀連拉 N 次全靠它）。原句錯在兩處：把 TiXL 正確行為當 bug；把 `InvalidationTick` 認成 cook 去重（它是 **invalidation 趟**的 memo，`DirtyFlag.cs:24-34,93`，update 趟 TiXL 沒有任何 per-frame cook 去重）。
- **multi-input / time-remap 的 version-combine**：照搬 TiXL `InvalidationOverride`（`MultiInputSlot.cs:46-83` 版本取**和**＝任一 input 變就 dirty；`TimeClipSlot.cs:124-149` time-range 進出 re-entry）。我們化成 walker 裡的分支，非 C# per-type 子類。
- **EvaluationContext = 單一可變物件穿透整趟**（`EvaluationContext.cs`：LocalTime/LocalFxTime/RequestedResolution/transform stack/材質/變數）。time-remap 節點 save/restore LocalTime 給子圖（`TimeClipSlot.cs:62-81`），**非 push/pop stack**。我們的 cook context 照此。**健檢補 S10**：全欄普查 = 兩鐘外 **17 個欄位群** + **每幀 `Reset()` 歸零語意**（清變數/燈/材質累積容器、重抓兩鐘、解析度由 caller 在 Reset 後設，`EvaluationContext.cs:43-58`），完整表見健檢檔 §2 S10；第一個會撞的是 `RequestedResolution`（`RenderTarget.cs:55` size=0 時反向讀 ctx 自定尺寸——我們 render-target 已有解析度 pin，接 compound 時要對上）。

### 2.5　★ 承重決策 7（新）：LIVE source = always-dirty（地基的一小塊）

- **LIVE source（會動的源頭，少數、可枚舉）= TiXL 的 `DirtyFlagTrigger.Always/Animated`**（`DirtyFlagTrigger.cs`、`Animator.cs:201` 掛動畫曲線 → `Trigger |= Animated`、`RunTime.cs:8` Time op 宣告 `Animated`）。我們：節點規格 + per-input animated 狀態掛 `isLiveSource` 旗，**= 每幀 bump sourceVersion = 恆 dirty = 每幀重算**。Time / Audio / stateful sim（粒子，每幀推進狀態）/ 被動畫驅動的 input 屬此。
- **健檢修正 S3：恆髒來源實有四個**——①op 宣告（spec 級，OutputDefinition 預設）②animated（從 driver 推導）③**使用者 per-output 手動設定**（TiXL 的 UI 下拉直接設 per-child `DirtyFlagTrigger` 覆寫、序列化進 .t3，`EditNodeOutputDialog.cs:20-38`、`SymbolJson.cs:117-134`、`Symbol.Child.cs:157-163`）④**op runtime 自翻**（`Once.cs:26-33` 動態 Always↔None）。→ resident 的每個 output 帶可序列化的 `triggerOverride` 欄（nullable，覆寫 spec 預設）；`isLiveSource` = driver 推導 ∨ trigger 覆寫 ∨ op 宣告。「只從 driver 推導」**不夠**，原句修正。
- **健檢修正 S8：第三種失效來源 = 外部 push**——資源熱重載/audio 從 invalidation loop **外**對 dependent slots `ForceInvalidate()`（`AbstractResource.cs:136-146`、`AudioRendering.cs:375`、`DirtyFlag.cs:36-40` 無 memo 的 bump）。🪤#1 的「每幀開頭遍歷 LIVE 集合 bump」防不到它——walker 要留對應的 ForceInvalidate 級 API（無 memo、跨 tick 有效）。失效來源全集 = trigger 葉（每幀）+ edit-time push + 外部 push。
- **🪤 #1 每幀正確性不變式（最大雷，地基級）**：**每個 LIVE source 每幀開頭必須 bump sourceVersion**（= Trigger=Always 字面行為）。漏一個 → 下游誤判 not-dirty → 卡舊畫面。防禦一行：每幀開頭遍歷 LIVE source 集合 bump。寫死進 selftest（漏 bump 的 RED 變體）。
- 注意：**這塊不靠分區**。LIVE source 恆 dirty + 決策 6 的 eager 一趟 walk，就已經正確地「LIVE 每幀算、STATIC 回 cache」。下面的分區短路是**之上的可選優化**，不是正確性所需。

### 2.5b　★★ 承重決策 9（新，2026-06-10 柏為拍板 A）：resident 節點 = TiXL `Slot`（三 concern 匯流）+ ctx 兩鐘形狀

> **這條把 compound / 增量求值 / 時間 binding 三條原本當「並列 lane」的東西，收斂成同一個物件——因為 TiXL 的 `Slot` 本來就是一個物件扛三件事。** 柏為直覺「兩層牽在一起」逼出來的，源碼複驗確認。

**底層事實（源碼）**：TiXL 的 `Slot` 一個物件同時扛——①**接線拓樸**（`InputConnections`，= compound 攤平後的 wiring）②**dirty/version/cache**（`DirtyFlag`，= 增量求值）③**參數怎麼被驅動**（`UpdateAction` + `Trigger`，= 時間 binding）。「這 input 怎麼被驅動」**只存一份、在 slot 上**：常數=預設值、連線=`ConnectedUpdate`、**automation=`Animator.cs:186-189` 把 `UpdateAction` 換成 `curve.GetSampledValue(ctx.LocalTime)` 並 `Trigger|=Animated`**、override=再換 UpdateAction（`Slot.cs:91-117`，存原本的、re-enable 換回）。**沒有獨立 binding 解析層。dirtiness 從同一個 slot 的 Trigger 讀 → binding 與 dirty 是融的。**

**承重原則（拍板 A）**：**「參數怎麼被驅動」一個擁有者，dirty 從它推導、不獨立存。** 實作 = **driver 直接住在 resident 節點的 input 上**（= TiXL slot），不另開平行結構。

- **resident 節點的每個 input 帶 `driver`（健檢修正：enum 收斂照 TiXL）**：`{ kind: Constant | Connection | Automation(curveRef) }`。**`LiveSource` 種類作廢**——TiXL 的 live 輸入一律是**節點**（MidiInput/OscInput/AudioReaction/KeyboardInput）靠連線進參數，恆髒來自 op 宣告 trigger；我們的 audio 也已重織成 AudioReaction 值節點+拉線（柏為 06-09 定的），此 kind 已無活用例，且「參數無線無動畫記號卻在動」違反 TiXL 視覺語言。**`Override` 不是 driver 種類**——是「暫換 update action」的可逆狀態（= TiXL `Slot.cs:91-117` 存原 action、re-enable 換回；P1 拍板停車的 Ableton 表演模式之後就蓋在這個縫上）。`isLiveSource` 從 driver + trigger 覆寫 + op 宣告推導（見 2.5 修正 S3），不另存旗——automation 掛上 = 設 driver = 同一處同時決定「解析值」與「LIVE/dirty」，不可能 drift。
- **健檢二補（refuter）：「存原 action」的 keep 槽是單一且三用途互斥**——TiXL 的 disabled（`Slot.cs:50-57`）/bypass（`Slot.cs:77-87`）/animation（`Slot.cs:91-102`）共用同一個 `_keepOriginalUpdateAction`：disabled/bypass 遇槽被佔＝Log.Warning 拒絕；animation 無條件換但不更新 keep（疊加次序是暗雷，TiXL 此處語意髒）。我們照搬「同時只有一個 action 替換者」的互斥+拒絕模型，**不逐 bug 照抄疊加行為**；P1 表演模式上線前先把疊加次序明定。
- **S1 `SourceRegistry` 收編（不是重做、是換家）**：它在 stateless era（無常駐節點可掛）是對的；batch 1 造出新家 → 其 `(nodeId,portId)→binding/override` 資料遷移到**定義層 driver 表**（見下條 C3 修正），resident input 在 build/patch 時拿投影。SourceRegistry 退成「寫定義層 driver 的編輯期 API」或併入。
- **⛔ 健檢修正 C3（柏為拍板 P2）：driver 權威擁有者 = Symbol 定義層，resident = 投影。原句「driver 住 resident graph、存檔從 resident 序列化」作廢。** TiXL 端事實（主 session 親驗）：Animator 住 **Symbol**（`Symbol.cs:55`），曲線 keyed by `(childId, inputId)`（`Animator.cs:26`），序列化在母 Symbol 的 .t3 內（`SymbolJson.cs:34`），**所有 reuse 實例共用同一份曲線**（refuter 二修措辭：update action 閉包持**同一個 Curve 物件**——改 key 即時全變、無需重掛；重掛時機＝加/移除曲線與 reconnect，`Animator.cs:446-459`、`Instance.Connections.cs:92`）——改一處全變，正是柏為完成定義「改定義兩份都變」那條；slot 上的 `OverrideWithAnimationAction` 只是接線期**投影**、非權威儲存。作廢理由：driver 放實例層 = ①兩個 reuse 實例可各自動畫（TiXL 表達不了的語意分岔）②與契約 3 的 symbols[]（定義形狀）序列化互撞——reuse 時 N 個 path 對同一 (symbol,child,input) 槽，要嘛重複要嘛丟資訊。→ **我們照搬：driver 權威存放 = 定義層 `(symbolId, childId, inputId)`；resident input 的 driver = buildEvalGraph/增量 patch 時的投影（= TiXL reconnect 重掛 action 的等價，`Instance.Connections.cs:92`）；`.swproj`（批次 2）從定義層序列化；undo 記定義層。** 求值面不變：slot 扛三事、每幀讀投影後的 driver 解析。
- **P3 拍板：時間單位 = bars 原生（照 TiXL）**——`TimeInSecs = TimeInBars × 240/Bpm`（`Playback.cs:8-9,47-48`，無拍號=固定 4/4 等效）；**曲線 key/TimeClip/loop range 一律以 bars 存**；BPM 是 per-Symbol 持久化設定（CompositionSettings 等價物，預設 120）。改 BPM = 全部 automation 跟著音樂縮放（音樂人預期行為 = TiXL 字面行為）。兩鐘欄位（localTime/localFxTime）單位 = bars。
- **★ ctx 兩鐘形狀（從 batch 1 第一天就長對）**：TiXL 兩鐘——`TimeInBars`（**播放頭**，scrub/暫停凍）vs `FxTimeInBars`（**牆鐘**，暫停照跑 idle motion），`EvaluationContext` 同時帶 `LocalTime`+`LocalFxTime`（`Playback.cs:102-129`、`EvaluationContext.cs:48-49`）。**automation 取樣播放頭**（`Animator.cs:186` `ctx.LocalTime`）、**有狀態 sim 取樣牆鐘**（`AdsrEnvelope.cs:70` `ctx.LocalFxTime`、`Time.cs:38` enum）。→ 我們的 `EvaluationContext` 即刻長成 `{ localTime, localFxTime, ... }` 兩欄（現在都 = `g_time` placeholder），Transport（時間 lane S5）續留後面才造真兩鐘——但 ctx 形狀現在定，automation 上來**不必重塑 ctx**。
- **時間 lane 排序不動、只改接點**：Curve(S3)/scoreGraph(S4)/Transport(S5) 仍是上半部 authoring；它們**往下不接平行 SourceRegistry，直接寫 resident 節點 input 的 driver**（= TiXL `OverrideWithAnimationAction` 的等價）。

> **一句話**：batch 1 不再只是「常駐求值圖」——它是 **compound 攤平 + 增量 dirty/cache + 時間 binding** 三者的匯流節點（= slot）。三條原本各排的 lane，地基是同一個。

### 2.6　○ 可選優化 E 階（降級，原承重決策 7/8 的分區短路）：LIVE/STATIC 分區 + pull 短路

> **⚠ 2026-06-10 底層複驗後降級（我自己的修正）**：原把「分區短路」當地基承重 + 我們贏 TiXL 的點。**錯了。** TiXL **沒有**分區、每幀照走全圖、在幾千節點規模跑得好（走訪＝指針追+int 比較、ns 級、`AggressiveInlining`、不重算）。分區只多省「**走訪**靜態子圖」的二階 overhead（microsecond 級），卻扛起整份設計**最重的 stale-frame 風險**。**「對齊 TiXL」＝這條 TiXL 不放地基、我們也不放。** 降成可選 E 階。（健檢修正 S5 補一刀：TiXL 無**全域**分區，但有 **op 級**走訪短路豁免——`Switch.LimitMultiInputInvalidationToIndices`。降級決定不變、論據修正：TiXL 對走訪瓶頸的解法是 op 局部豁免，不是全域分區。）

- **內容（之後若 profiler 證走訪是瓶頸再做）**：把 `isLiveSource` 往下游傳染算出 LIVE/STATIC 分區（編輯時算一次存常駐）→ cook walk 走到 STATIC 邊界就停、不踏進靜態子圖。
- **省什麼**：只省「走訪」overhead，**不省重算**（重算決策 6 早跳掉了）。在「滿屏靜態 + 少數在動」的超大圖（遠超 TiXL 常見規模）才線性顯現。
- **代價（為何不放地基）**：要維護「分區何時失效」——①加/刪連線 ②加/刪節點 ③input 常數↔animated toggle ④LIVE 集合變 ⑤改 reuse Symbol 定義。漏任一 → 該 LIVE 的被當 STATIC → **卡舊畫面（最危險的 bug 類型）**。地基不該為二階 perf 扛這風險。
- **採納門檻**：profiler 顯示走訪 + per-node IsDirty 檢查在真實 simple_world 圖吃掉可感知 frame budget（估計 >5–10 萬節點才到）。在那之前不做。

### 2.7　★ Command / present 規則（render-target pivot 接縫，地基級）

- **終端 present（畫到 swapchain/螢幕）= 永遠跑**：back buffer 每幀 clear+重畫。= 決策 6 的 Command always-run（TiXL `_valueIsCommand`）。
- **中間 RenderTarget 貼圖（第一批保守）**：**Command 節點一律重發**——這是 TiXL 對 Command 的字面行為，不是我們偷懶。靜態 texture 預處理鏈「算一次跨幀 cache 貼圖」是 GPU 資源生命週期問題（要保證貼圖不被 alias/覆寫），屬 **E3（GPU 層）單獨驗**，那時才放寬。
- **健檢修正 S9（refuter 二修）：Command 是三段協定，不只 always-run**——`PrepareAction` / Execute / `RestoreAction`（`Command.cs:8-9`）。**正確不變式是弱保證：對每條 command，Prepare（若有）在 GetValue 前、Restore 在 GetValue 後；「批次」vs「逐顆交錯」是 per-op 自由**——`Execute.cs:19-35` 批次三段（全 Prepare→全 GetValue→全 Restore）、`Group.cs:60-76` **逐顆交錯**（P₁E₁R₁ P₂E₂R₂）、`Switch.cs:44-68` **根本不呼叫 Prepare**（只 GetValue→Restore）。兩種次序在多 command 下 GPU state 順序可觀測不同——**port 哪顆照哪顆的源碼，不自創統一**。實際掛 Prepare/Restore 的生產者是 DX11 pipeline-stage op（`Operators/Lib/render/_dx11/api/*`）。我們的 RenderCommand 升為「值上掛兩鉤」的協定；現在留型別位、不准蓋出與它矛盾的消費端。

### 2.8　誠實對帳：「更好更簡單」survive 什麼、retract 什麼（底層複驗後）

| 主張 | 結算 |
|---|---|
| pull 不閃中間狀態 | ❌ retract：TiXL 也是 pull、也不閃。pull 贏 push，非我們贏 TiXL。 |
| 用 version 不用 hash | ❌ retract：TiXL 用的就是 `SourceVersion`/`ValueVersion`。一樣。 |
| 砍 editor-only dirty 統計 | ✅ survive：`FramesSinceLastUpdate` 等確 editor-only（`DirtyFlag.cs:48-91`），求值核心不抄。 |
| 少 Slot 子類 | ⚠ 部分：資料驅動 walker 分支取代 per-type C# slot 子類，但 version-combine 邏輯（multi-input 取和 / time-clip re-entry）仍要。少型別、不少邏輯。 |
| **一趟 vs TiXL 兩趟** | ⚠ **survive 但範圍縮（健檢修正 C1）**：等價只對純值圖（無條件拉取/不改 ctx/無觸發語意）；Command/flow 圖必須 pull-driven（op body 是排程器）。批次 1b（值圖）安全；render 圖接常駐圖前此界線是承重前提。 |
| 靜態分區 pull 短路 | ❌ **retract「是我們的 edge / 地基承重」→ 降可選 E 階**：TiXL 自己沒做、二階、扛最重風險。見 2.6。 |
| 常駐 vs 每幀重建 | ❌ retract「我們不用常駐」：TiXL 規模下也要常駐（決策 3）。補回來、不是贏。 |

**一句話對齊結論（健檢修正後）**：地基 = TiXL 的 version-chasing dirty + cache + Command-always + LIVE-source-trigger，**邏輯 100% 對齊**；「eager 一趟」是**值圖限定**的正當簡化（Command/flow 圖照 TiXL pull-driven、op body 驅動）；分區短路 TiXL 不放地基、我們也不放。

---

## 契約 3：存檔（照 TiXL `.t3`/`.t3ui` + 兩階段 load）

**TiXL 機制**（`Core/Model/SymbolJson.cs`、`SymbolPackage.cs:191-388`、真實 `HowToUsePoints.t3`）：
- `.t3` = 一個 Symbol 定義：`Id` + `Inputs`(對外槽+預設) + `Children`(每個 = `Id`+`SymbolId`引用+**只存非預設**的 InputValues) + `Connections`(四元組，sentinel 全零 Guid)。
- `.t3ui` 分開存 UI 位置(per ChildId)，**不影響執行**。
- **reuse 證據**：同一 `SymbolId` 被多個 Children 引用，定義在它自己的檔(`HowToUsePoints.t3` 裡兩個 RepeatAtPoints 同 SymbolId)。
- **兩階段 load**：先載所有 Symbol 定義進 registry → 再 apply children(resolve SymbolId)。順序不可反，否則引用 resolve 失敗。

**我們的對應**：
- `.swproj` 升 **v2**：`{ version:2, symbols:[ {id, inputs[], children[{id, symbolId, inputOverrides[]}], connections[四元組]} ], rootSymbolId }`。sentinel 跨邊界連線用 nodeId=0。UI 位置(x,y) 可內嵌 child 或分 sidecar(對應 .t3ui；先內嵌求簡單，之後分)。
- **兩階段 load 照搬**：先建所有 Symbol 進 registry，再 resolve children 的 symbolId。
- **舊檔 migration**：舊 flat `.swproj` = 一個 root Symbol 的 children(node id 不變當 child id，舊 type 字串 → 原子 Symbol 的固定 UUID)。版本欄位分流，舊檔自動升 v2。

> **承重決策 4（照 TiXL）**：兩階段 load(先定義庫後 apply children)。原子 operator 的 symbolId 用**固定 UUID**(type 字串 → UUID 映射表)，跨檔/跨版本穩定。
>
> **健檢修正（契約 3 補承重欄，2026-06-10；v2 schema 在批次 2 動工前以此為準）**：
> - **S16：v2 必須自描述**——TiXL 的 .t3 **不是**自描述格式：slot 定義（name/type/順序/isMultiInput）住在 C# class、檔內沒有（`SymbolJson.cs:353-365`；.t3 的 Inputs 只覆寫 default、symbol 級無 outputs 段）。我們無 C# 可依 → v2 的 `inputDefs[]/outputDefs[]` 必含 name+dataType+default+isMultiInput，且**明文「陣列順序=定義順序」**（TiXL 寫檔 OrderBy(Id) 非顯示順序，顯示順序來自 C# 成員順序——我們得自己扛）。原 schema 行漏 outputDefs/name/順序語意，補。
> - **Animator 段（P2 拍板：權威=Symbol 定義層）**——per-Symbol `animator:[{childId, inputId, index(多通道,0 省略), curve}]`；curve 全欄照 TiXL（`Animator.cs:371-408`、`CurveState.cs:56-82`、`VDefinition.cs:180-205`）：keys{time(**bars，P3 拍板**), value, in/outInterpolation(**6 種含 Horizontal**), 切線角, **雙邊** tensionIn/tensionOut, weighted, brokenTangents} + **pre/postCurveMapping{Constant, Cycle, CycleWithOffset, Oscillate}**（外插）+ `TimePrecision=4` 捨入=key 身分的一部分。讀取在 children resolve 之後、孤兒曲線丟棄。**現行 SourceRegistry binding 完全不序列化（live binding 現在就掉檔）——批次 2 一併收。**
> - **S18：CompositionSettings 等價段**——BPM(預設 120)/soundtrack 路徑與 clips/audio 來源(ProjectSoundTrack|ExternalDevice)/輸入裝置名/音量/resync 閾值（`CompositionSettings.cs:110-177`，per-Symbol、breadcrumb 向上找 active）。transport 持久化的家在這，不另立。（refuter 二補：**`Enabled` 旗是語意承重**——breadcrumb 找的是 Enabled 的那層、寫檔條件＝Enabled∨有 clips（`Symbol.cs:57-63`、`CompositionSettings.cs:112`）；另有 Syncing/gain/decay/beat-lock/Export 段，動工時對源碼列全。）
> - **Children 補欄**：name、isBypassed、outputs 段（per-output triggerOverride / isDisabled / outputData——TimeClip 之後存 outputData，`SymbolJson.cs:112-146`）。
> - **multi-input 保序**：同 (target,slot) 連線順序=陣列順序；寫檔排序用 **stable sort** 保群內相對序（照 `SymbolJson.cs:61` 語意）；讀檔按陣列順序派 multiInputIndex。
> - **S15：載入容錯哲學（照 TiXL，世界觀級）**——壞資料**局部丟棄、load 不死、下次存檔自我治癒**：missing symbolId→丟 child+警告；dangling 連線→instance 接線失敗時**就地從 Symbol.Connections 移除**（self-heal）；obsolete input/output/孤兒曲線→跳過；FormatVersion 較新→警告不擋（`SymbolJson.cs:183-309`、`Instance.Connections.cs:77-84`）。我方現行「malformed→整檔 false」**作廢**（compound 時代「引用的 Symbol 不在」會真實發生）。
> - **S19（.t3ui 對應，批次 3 視野）**：sidecar 除位置外的承重欄=per-input **Min/Max/Clamp/Scale（使用者可調可存的 slider 手感）**、GroupTitle(inspector 分組)、Comment、Annotations(畫布註解框)、pinned 視窗狀態。注意結構差：我方 PortSpec minV/maxV 是定義級中央表，TiXL 是 per-symbol 可改可存——收斂方向照 TiXL。（refuter 二補：child 帶 `PreviousId` 韌性機制——.t3ui child id 對不上時用它補配、duplicate 場景 UI 不掉位（`SymbolUiJson.cs:423-435`）；`SnapshotGroupIndex` 直通 P2 停車的 Variations/Snapshots；另有 ConnectionStyleOverrides/Style/Size/TourPoints 等，批次 3 對表。）
> - **版本欄明文**：v1=無 version 欄（以缺欄辨識），v2 起雙欄（formatVersion + app 版本）照 TiXL 遷移法（legacy 鍵雙讀）。
> - **S20：檔案佈局=具名分岔（記錄在案）**——TiXL 一 symbol 一檔(+.t3ui sidecar、guid 以檔內 Id 為準)；我們先單檔庫（.swproj=Symbol 庫+rootSymbolId），為簡先行，知道代價（版控 diff 粒度/跨專案 reuse/與 TiXL 檔互通），日後可分檔。**.t3 值型別普查（refuter 全量重跑修正數字：全庫 1,298 檔 28,894 條 InputValues、SharpDX 型 3.0%；`Operators/Lib` 單看 13.0%、examples 0.9%；且 SharpDX 型全是 enum 字串如 CullMode/TextureAddressMode，非二進位綁定）——「讀 TiXL 專案檔」長期不是格式問題、是 operator 覆蓋率問題；定性結論不變，但 Lib 13% 表示 DX11 enum 映射表是必做件、不可忽略。**

---

## 契約 4：編輯互動（照 TiXL，但皮是 imgui-node-editor 不是自製 canvas）

**TiXL 機制**（`ProjectView.cs:28,240-345`、`Combine.cs:27-259`、`UndoRedoStack.cs`、`MagGraphView.cs:48-139`）：
- **進出層級** = `compositionPath: List<Guid>`(instance id 路徑，非 push/pop stack)。雙擊節點 push childId、雙擊空白 pop、麵包屑逐層。per-composition 的 pan/zoom 外存(keyed by childId)。
- **combine-into-symbol**：選取 → 掃連線找跨邊界(**input** = target 在選中∧source 不在；**output** = 反之) → 建新 Symbol → copy 選取進去 → 父圖建 compound child → 重接外部連線到新 port → 刪原 → **清 undo history**(TiXL 因動態編譯而清；我們資料驅動**不需清**，可保留 undo)。**無 explode/拆開**(TiXL 沒實作)。
- **跨層 undo**：全域 stack，每個 command 自帶 `compositionSymbolId`(哪一層)，靠 id 定址查回 Symbol 再操作。跨層自然成立。
- **TiXL 不用 imgui-node-editor**(自製 ScalableCanvas)，切層 = 換資料不換 context。

**我們的對應**：
- **composition path** = `vector<NodeId>`(或 symbolId 路徑) 掛 session 層。雙擊 compound child 進、雙擊空白/麵包屑出。**單一 `ed::EditorContext`，切層時清空+重建節點列表**(imgui-node-editor 單 context 顯示一層，照 TiXL 單層顯示——夠用、風險最低)。per-composition view state 外存(我們已有 canvas view state)。
- **combine 邊界偵測照搬**(純資料掃描)：input=跨界入、output=跨界出。建新 Symbol + 重接 + 刪原。**我們資料驅動 → undo 可保留**(比 TiXL 好)。
- **跨層 undo 照搬**：現有 command 層加 `compositionId` 欄位，Do/Undo 靠 id 查對的 Symbol/層。全域 stack 不變。

> **承重決策 5（照 TiXL 語意、皮換我們的）**：單一 EditorContext 切層換資料(非多 context 同框)。compositionPath 導航。combine 邊界偵測照搬。跨層 undo 用 command 自帶 compositionId。
>
> **健檢二補（refuter，契約 4 補三刀；批次 4/5 動工前提——這塊健檢首輪沒掃 Editor 端，二輪補上）**：
> - **combine 必搬動畫曲線（P2 的直接推論，漏了＝柏為 combine 一顆有動畫的節點、動畫蒸發）**：照 TiXL——先把選中 children 的曲線 `CopyAnimationsTo` 進新 Symbol 的 Animator（childId 經 oldToNew 重映射、**先於** child 實例建立，`CopySymbolChildrenCommand.cs:196-199`、`Animator.cs:28-55`），再從母 Symbol Animator 移除原曲線（`Combine.cs:190`）。
> - **combine 對外 port 生成粒度＝per 邊界連線、非 per 目標 slot**：一個 multi-input 收 3 條外線＝新 Symbol 長 **3 個** input（同名計數器 dedup，`Combine.cs:34-39,57,78-83,97`）。照抄。TiXL 的 `connectionToNewSlotIdMap` keyed by 連線等值、重複連線會撞（自身炸點）——我們 keyed by list 位置避炸、語意不變。
> - **copy/paste 資料語意（柏為驗收動作「複製第二份」第一天就踩，契約原本整缺）**：每 child 配新 id + oldToNew 重映射；**只複製兩端都在選取內的連線、外部連線一律剪斷**；multi-input 保序（整批 reverse + insert-at-0，`Combine.cs:223,235` 同招）；per-child 全狀態搬（input override+IsDefault、per-output outputData/triggerOverride/isDisabled）；**bypass 延後到連線建好才套**（未連線時 SetBypassed 拒絕生效）；曲線跟著 copy；跨 Symbol 貼上走 clipboard 序列化（transient symbol 不進 registry）。（`CopySymbolChildrenCommand.cs:90-317`）
> **風險（imgui-node-editor）**：node id 全域整數——切層重建要 `ed::ClearSelection` + 重建 node 列表；若未來要「父子同框小地圖」需兩 context + id namespace 分離(暫不做)。

---

## 本質複雜（誠實標，不假裝簡單）

- compound + 增量求值是改圖模型 + 求值核心的大梁，TiXL 長了多年。我們有 TiXL 源碼當藍圖會快很多，但這是**多 session 的大 lane**，不是一天實作完。今天交付 = 本設計契約鎖死。
- 常駐求值圖是新承重元件(雖然 operator kernel 隔離不動)：path-qualified id 穩定性、reuse 狀態隔離、**增量 patch 正確性**要驗。（LIVE/STATIC 分區失效是 E 階可選、不在地基——見 2.6 降級。）
- **增量正確性是本質難（GPU 同步同級的本質醜，不要假翻成假直覺）**：version/dirty、diamond 去重、LIVE source 每幀 bump、Command/present 規則——這些 TiXL 也是複雜在這、我們照搬其語意。包進乾淨接縫（柏為操作旋鈕：節點宣告 isLiveSource / 看 perf 表），不要他碰 walker 內臟。
- 連線定址從 pin-id 改成 (node,slot) 四元組 = 動承重結構，命令層/存檔/UI 連線碼都要跟著改(blast radius，趁節點還少時做正是此因)。
- imgui-node-editor 撐單層切換已驗(TiXL 模式)；多層同框未驗(暫不做)。

## 實作批次順序（contract 先、葉子後；每批可獨立驗收）

> **批次 0 已完成**（commit `38dde11`，資料模型不受求值決策修正影響）。批次 1 起改成「常駐求值圖」路線（原「每幀展平 throwaway」作廢，見契約 2.2）。求值機制（決策 6/7/8）穿插在 1–2，**可先在現行 flat 圖上做、與 compound 巢狀正交**。

0. ✅ **資料模型契約鎖**(commit `38dde11`)：Graph→Symbol、Node→Child、Connection→四元組+sentinel。selftest：純資料 roundtrip。**不受求值修正影響。**
1. **常駐求值圖 + 展平 + resident 節點=slot（決策 3/9）**：`buildEvalGraph(symbolLib,root)→ResidentEvalGraph`(path-qualified id + sentinel 解析，**常駐、編輯時增量 patch**，不每幀重建)。**resident 節點的 input 帶 `driver{Constant|Connection|Automation}`（健檢修正後 enum，LiveSource/Override 已作廢；= TiXL slot；driver 權威住定義層、resident=投影，照 C3/P2；S1 SourceRegistry 收編進定義層 driver 表）**；`EvaluationContext` 即刻長成 **兩鐘形狀 `{localTime, localFxTime}`（單位=bars，P3 拍板）**（現都=g_time placeholder）。cook/evalFloat 改 walk 常駐圖、解析值讀（投影後的）driver。**selftest：巢狀圖求值 == 等價手寫 flat 圖**(golden)；reuse 兩實例狀態獨立；**編輯後增量 patch 結果 == 全重建結果**(golden)——**健檢修正 S11：patch golden 枚舉六種編輯操作**（照 TiXL 廣播全集，`Symbol.cs:222-330`、`Symbol.TypeUpdating.cs:13-39`）：①加連線 ②刪連線 ③加 child ④刪 child（含牽涉連線清除）⑤改定義 default（只 invalidate 還在用 default 的實例，`Symbol.Child.cs:677-698` 的 IsDefault 過濾刀）⑥IO 定義變更（含孤兒連線收屍）——**漏哪種 = 哪種編輯後 resident graph stale**；**driver 解析回歸 == S1 `--selftest-resolve` 全綠（override→binding→constant 不退化）**。
   > **slice 1 ✅（headless 模組，commits `722e09b`→`1d4628b`）**：`resident_eval_graph.*`(.h 87/.cpp 211/selftest 147 行，runtime leaf，arch OK) = ResidentEvalGraph/Node/Input + 兩鐘 `ResidentEvalCtx` + `buildEvalGraph`(遞迴 inline/sentinel 邊界/自我巢狀 cycle guard) + `evalResidentFloat`(重用 `NodeSpec::evaluate`、兩條路徑共用同一數學)。golden `--selftest-residenteval`：巢狀==等價 flat(3×4=12)、reuse 隔離、driver{Constant/Connection/Automation-stub} 解析、兩鐘(Time 讀 localFxTime=牆鐘 14 非播放頭 693)；`-bug` 變體 FAIL(有牙：污染 def→第二顆 Const 讀 99→297)。**未接 production cook**(現役仍走 flat Graph 路徑)。**延後(named，未靜默砍)：slice 2 = point cook 接常駐 + cook==flat-cook golden；slice 3 = 增量 patch(六種編輯)+ patch==全重建 golden(中間先 rebuild-on-edit)；slice 4 = 1b(下方)；production swap = 批次 2。** 計畫:`docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md`。**獨立 refuter(opus)已否證 `buildEvalGraph`：抓到 1 條 slice-1 真缺陷已修(`457a7cc`：compound 子節點 input 用 override/default 驅動時被靜默丟、只傳 wire→已 pre-seed effectiveInput 修正+golden 補 boundary-input 覆蓋)；survive=子節點順序/三層巢狀/cycle 非崩/壞 ref 容錯。延後(named，slice 2)：①pass-through compound(boundary-in→boundary-out 直連)現留 dangling 輸出邊解析成 0、非乾淨空輸出；②compound 讀「後面的」compound 兄弟時 `childOuts` 順序未測、結構可疑(增量 patch 需穩健拓樸排序時一併解)。**
   >
   > **slice 2 ✅（resident point-cook，commits `8fdeff4`→收尾）**：`PointGraph::cookResident(rg,ctx,reg,targetPath)`(point_graph.cpp)走常駐圖 buffer 流——path-qualified id 當 buffer key、Points/Force input 走 Connection driver、count 靠 resident "Count" input driver 解析(Constant 直讀/Connection 走 evalResidentFloat)、cmd-op capture 終端。golden `--selftest-residentcook`:resident cook == flat cook(8 點 x=2.0)、reuse 隔離(兩 RadialPoints 實例 Count 4 vs 8 各自 cook→畫 Count=4 那顆得 4 點);`-bug`(target 假 path→空 bag→resident≠flat) FAIL 有牙。**純加:現役 `cook(Graph&)` + slice-1 引擎(`resident_eval_graph.*`)零改;per-cook buffer(跨幀 cache=slice 4)。延後(named):slice 2b=cmd/texture executor parity(現用 capture 終端)+stateful op 狀態接常駐;production swap=批次 2。** ⚠ **arch debt(誠實標)：`point_graph.cpp` 477 行越過 ~400 警訊線**——cook/cookResident 並存,自然拆點=production swap 時兩者收斂(int↔string key),故不在 slice 內硬拆(會拆完再重併);已開 task chip 追。計畫:`docs/superpowers/plans/2026-06-11-resident-cook-batch1-slice2.md`。**獨立 refuter(opus)已否證 `cookResident`:buffer 走訪本身 survive 4 攻擊(ParticleForce 線程/未接 input/count==0/巢狀 compound 終端,皆 byte-identical with flat)。抓到 1 條 resident≠flat 分岔但根因在 flat 端、非 cookResident:生成器 "Count" 被『連線』驅動時 flat `cook.nodeCount`(point_graph.cpp:54-63)只讀 params、對線瞎眼→2048,cookResident 跟連線→6;cookResident 與 `evalParam` 值權威(graph.cpp:212-219 認線)一致=對的那邊,flat nodeCount 是早存在的 production bug(wire→Count 被靜默忽略,正是 audio→粒子數 反應式用法會踩)。✅ **已修(commit `7d4b34e`，柏為定優先)**:flat `nodeCount` 改走值權威——連線→`evalFloat` 上溯、否則 stored param、否則 spec def,scoped to node,與 `evalParam`(graph.cpp:212-219)/`cookResident.resolveFloat` 一致;golden `--selftest-pointgraph count-wire`(Const(6)→RadialPoints.Count cook 6 非 2048,RED 看到 2048→GREEN 6)。此為「slice 2 純加紀律」的單一具名例外=refuter finding 收尾,非擴大 slice scope。Attack 1(真雙 Points 輸入 combine)concat 碼兩路逐行相同但無雙-Points NodeSpec 可測(CombineBuffers 待確認 arity),共用碼低風險、未測。**
1b. **version-chasing dirty + per-node cache + Command-always + diamond + LIVE-bump**(決策 6/7，地基核心)：每節點 version/cachedOutput；**eager 一趟後序 walk**（版本傳播+重算同趟，dirty→重算/否則回 cache）；**`isLiveSource` 推導＝driver(Automation) ∨ op 宣告（有狀態/Time/Audio）∨ per-output triggerOverride（照 S3 修正；LiveSource kind 已作廢）**；Command/present 永遠跑（被拉幾次跑幾次，照 C2）；值節點 per-pass 去重靠 version/cache；LIVE source 每 pass bump version；**有狀態 op 帶 FxTime 時間門（原語④）**。**selftest 有牙(count-based；健檢修正：單位=evaluation pass 非幀)**：靜態節點第 2 個 pass 起 cook 0 次、LIVE 每 pass 1 次、改靜態 param→重算 1 次→回 0、**值節點 diamond 同 pass 1 次；Command 節點被拉幾次跑幾次（兩父各拉一次→斷言 =2 不是 =1，照 C2）**、**🪤 漏 bump LIVE version → 卡舊的 RED 變體被抓**、**driver 從 Constant↔Automation toggle → LIVE/STATIC 同步翻（證 binding 與 dirty 不分叉）**、**一趟結果 == TiXL 語意兩趟結果 golden（範圍=純值圖；Command 圖等價不宣稱、照 C1 pull-driven）**。
   >
   > **slice 1b 第一刀 ✅（值圖 version-chasing cache，commits `371e8ab`→`cecdaba`）**：`resident_eval_cache.cpp`(111 行，runtime leaf，arch OK)——`ResidentOutputCache{sourceVersion,valueVersion,cachedFloat,isLiveSource}` 長在 resident 節點每個 output slot（C5 per-output；拍板「節點=slot」，非平行層）；`pullResidentFloat` **eager 後序一趟**(Connection 遞迴+走訪整 cone、slot 的 sourceVersion=自身 baseVersion+上游 sourceVersion **取和**(multi-input combine);**baseVersion 單調累積**(LIVE/edit-time push ++、不被上游和覆寫——slice-3 refuter A4 修正後機制 `5561e42`,見下;原『sourceVersion=純上游和覆寫』丟掉節點自身版本貢獻、derived 節點值編輯被抹)、dirty(`valueVersion!=sourceVersion`)才重算否則回 cache、不重算=省的就是這層)；`bumpLiveSources` 每幀 bump LIVE(Trigger=Always，🪤#1)；`initResidentCache` per-output cache + isLiveSource(op 宣告恆髒=Time)。golden `--selftest-residentcache`:靜態短路(改上游 const 無 bump→回 cache 15)、edit-push(bump→傳播取和→27)、LIVE 每幀(Time 14→35)、dangling(孤兒連線→算 5 不凍);`-bug`(漏 bumpLiveSources→LIVE 凍 14)FAIL 有牙。**純加:slice-1 `evalResidentFloat` 零改。** **獨立 refuter(opus)已否證:5 survive(diamond/同幀重拉/深 LIVE 鏈/部分髒/sum-aliasing)、1 BROKEN 已修(`cecdaba` D1:dangling Connection 上游 sum=0 撞初始 valueVersion=0→永久 false-clean 卡舊、連 edit-push 都救不回[下次 pull 又砸回 0];破壞 TiXL 不變式『sourceVersion 從 1 起只 ++、永不為 0』。修=無法解析上游貢獻固定版本 1、保 sourceVersion≥1 initially-dirty、算值與無 cache 路徑一致)。** **延後(named):Command-always(值圖不碰 Command)、四原語(trigger/loop/ForceInvalidate/FxTime 時間門)、TimeClip time-remap re-entry、automation-driven LIVE(S3 曲線)、外部 ForceInvalidate push(S8)、derived 兼 LIVE、production swap+GPU buffer cache(cookResident 跨幀)。** ⚠ **未蓋完 1b 全貌**(僅值圖 float cache 第一刀)——Command/四原語/diamond 的 count-based selftest 等仍是後續刀;但已不與 1b 全貌矛盾(值圖 cache 不碰 Command/flow，照 spec line 120 界線)。
   >
   > **slice 3 第一刀 ✅（增量 patch,commits `b526e1f`→`5561e42`）**：`resident_eval_patch.cpp`(45 行,runtime leaf,arch OK)=「常駐」的結構面(編輯時增量、不每幀重建),消費 1b cache(patch=局部失效)。兩個編輯(六種 S11 的頭兩種):`patchSetConstant`(S1 值編輯=改 Constant 值+`++baseVersion` edit-time push)、`patchAddConnection`(S11① 改 driver Connection+`valueVersion=sentinel` 強制首拉,**非 sourceVersion bump**,照健檢二補②)。golden `--selftest-residentpatch`:set-const(out-of-band 毒化未受影響兄弟的 const→patch 另一顆→pull=9×cached-3=27 非 9×99,證只失效編輯 cone、未受影響保 cache)、**derived 節點值編輯**(Mul.b 編輯、Mul.a 連線→5×10=50)、add-connection(接 Time→Mul.a,7→35),三者皆斷言 **== rebuild**(直接建含該編輯的圖);`-bug`(改 const 跳過 patch 失效→凍 15)有牙。**獨立 refuter(opus):6 survive(rewire/patch 序列/diamond/錯目標/多輸出過失效/dangling-add)、1 BROKEN 已修(`5561e42` A4:`patchSetConstant` 對 derived 節點[有 Connection input]的 `++sourceVersion` 被 pull 的 `sourceVersion=upstreamSum` 覆寫抹掉→值編輯靜默丟、卡舊、patch≠rebuild[15 vs 50];根因在 1b cache 機制[sourceVersion 覆寫丟自身貢獻]非 patch 單獨→根治=拆 baseVersion[自身單調累積]+sourceVersion=base+上游和,更忠 TiXL[SourceVersion 累積不被覆寫]。golden 漏=只測 leaf)。** **延後(named,後續刀):其餘四種 S11(斷線=恢復前 update action+ForceInvalidate、加/刪 child、改定義 default、IO 變更)+六種全 patch==rebuild golden、per-output 精確失效(現 bump 全 output)、拓樸排序穩健性。**
   >
   > **slice 3 rest ✅（S11 六操作補完,2026-06-11,commit `e4f5f7f`）**：`patchRemoveConnection`(S11② 斷線=恢復 KEPT constant[=TiXL `_actionBeforeAddingConnecting`/Input.Value 在線下存活]+**吸收 dropped 上游版本進 baseVersion**再 ForceInvalidate+sentinel)+新檔 `resident_eval_patch_lib.cpp`(249 行)=**定義層廣播 patch**:`patchLibSetDefault`(S11⑤,IsDefault 過濾照 `Symbol.Child.cs:677-698`;wired input 刷新 KEPT fallback 不 bump[TiXL 求值活讀 default `InputSlot.cs:27-30`];compound 走 migration)、`patchLibAddChild/RemoveChild/RemoveInputDef`(S11③④⑥+S13 收屍)=**lib 編輯+rebuildWithCacheMigration**(單一 wiring codepath;三規則:inputs 等價[含 Connection **resolvability**]→cache 整搬/變了→單調 floor force/新 path→fresh)。golden `--selftest-residentlibpatch` 11 組全==rebuild+cache 探針,`-bug` 有牙。**🪤 新不變式(泛化 D1/A4):slot 的 sourceVersion 跨「編輯序列」不可遞減**——斷線吸收 dropped 貢獻、migration 用 max(base,sv)+1 並鏡寫 sv 欄(該欄只在 pull 刷新;背靠背編輯無 pull=batch-4 命令組形狀會讀到 stale)。**獨立 refuter(opus,可執行 repro):8 survive、4 BROKEN 全修+repro 轉 golden**(A-1 連續結構編輯版本倒退卡舊/A-2 wired 下 default 編輯丟失→斷線恢復舊值/A-3 wired slot 的 setConstant 靜默丟值 vs TiXL `SetTypedInputValue` 不管接線都存/A-4 compound setDefault 靜默 lib-g desync)。**⚠ 具名契約義務(已落碼註):resident 層 patch 只動投影,命令層必須配對 lib 編輯**,否則後續 patchLib* 重投影丟掉它。延後(named):per-output 精確失效、compound child 的 AddChild(遞迴 inline)、換 type 編輯的 isLiveSource OR 黏著、per-edit 手術取代 O(graph) migration(優化,語意已由 golden 釘死)。交接=`2026-06-11-resident-cache-patch-batch1b-slice3.md` Cut 3 段。
2. **存檔 v2 + migration**：symbols[] 庫 + 兩階段 load + 舊檔升級。selftest：v2 roundtrip + 舊 flat 檔讀進來不破。
3. **編輯導航**：compositionPath 進出層級(雙擊/麵包屑)、單 EditorContext 切層、view state 外存。眼手驗：進子圖→出來→畫面對。
4. **combine-into-symbol**：邊界偵測 + 建 Symbol + 重接 + 刪原(undo 保留)。眼手驗：選 3 節點→combine→母節點 port 對、子圖內容對。
5. **跨層 undo + reuse 收尾**：command 帶 compositionId；reuse(同 Symbol 多 Child，改定義全變)眼手驗。
- **○ E2（可選，降級，原 1c/1d 分區短路，決策 2.6）**：LIVE/STATIC 分區 + pull 短路（走到 STATIC 邊界停）+ 5 條分區失效觸發清單。**只在 profiler 證走訪是瓶頸才做**（TiXL 規模幾乎不會）。selftest：STATIC 子圖走訪計數=0、5 條失效全測、stale RED 變體。
- **○ E3（可選，之後，GPU 層）**：靜態 RenderTarget 貼圖跨幀 cache（放寬「Command 一律重發」）、中間結果記憶體共用、render graph 剔除無人看的 pass。**要 GPU 資源生命週期單獨驗。**

## 驗收（compound + 增量求值「成立」的證據）

- 機器（compound）：求值 golden(巢狀==等價 flat)、reuse 狀態隔離 golden、**增量 patch==全重建** golden、存檔 v2 roundtrip + 舊檔 migration、跨層 undo selftest。
- 機器（增量求值地基，count-based 有牙）：靜態節點第 2 幀起 cook 0 次、LIVE 每幀算、改靜態 param→重算 1 次→回 0、diamond 一幀 1 次、漏 bump LIVE version 的 RED 變體被抓、**一趟==TiXL 語意兩趟 golden**。
- **柏為親手(完成定義)**：
  - compound：選幾顆節點 combine → 雙擊進子圖 → 出來 → 母節點接線渲染 → 複製第二份(reuse) → 改定義一處兩份都變 → 存檔關 app 重開還在。
  - 增量求值：放一塊貴的**靜態**東西 + 一塊**動的**粒子，看 perf 表——靜態那塊第 2 幀起時間掉到接近 0、動的照跑；拖靜態的參數→卡一格重算→回 0。**（這驗的是決策 6 的 cache，不需分區。）**
