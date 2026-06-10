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

---

## 契約 2：求值 — 常駐增量求值圖（compound 攤平 + version/cache 合一）

### 2.0　TiXL 求值機制（對源碼逐條複驗，這是設計權威）

> 以下每條都對 `external/tixl` 源碼驗過，不是轉述。是新契約要對齊/取捨的基準。

- **version-chasing dirty，不是 content hash**：每個 slot 帶 `DirtyFlag{ SourceVersion, ValueVersion }`（`DirtyFlag.cs:61-62`）。`IsDirty => TriggerIsEnabled || ValueVersion != SourceVersion`（`DirtyFlag.cs:19`）。`Update()` 只在 `IsDirty` 時跑 `UpdateAction`，跑完 `Clear()` 把 `ValueVersion=SourceVersion`（`Slot.cs:160-169`）。**這就是省下靜態重算的那層——大錢在這。**
- **time/animation = always-dirty source**：`DirtyFlagTrigger{ None, Always, Animated }`（`DirtyFlagTrigger.cs:6-11`）。Trigger 非 None → `IsDirty` 恆真 → 每幀重算（`DirtyFlag.cs:67-76`、`Slot.cs:296-298`）。**這就是「會動的源頭」的 TiXL 對應物。**
- **每幀走訪整個「連到輸出」的子圖**：render loop 每幀 `GlobalInvalidationTick++`（`Program.RenderLoop.cs:53-54`）後呼叫 `output.InvalidateGraph()`（同檔:84）。`InvalidateGraph()`（`Slot.cs:266-323`）遞迴爬上游每條連線，把上游的 `SourceVersion` 抓下來比對。memoize（`InvalidationTick==GlobalInvalidationTick`，`Slot.cs:269`）**只防同一幀內 diamond 重訪、不跨幀**。→ 連到輸出的幾千節點，每幀每顆被踩一次。**但這個踩很便宜**：指針追 + 一個 int 比較、`AggressiveInlining`、零配置，**且不重算**（重算被上面的 `IsDirty` 擋掉）。
- **edit 期 push invalidate**：加/刪連線時當場 `targetSlot.InvalidateGraph()` 往下游標髒（`Instance.Connections.cs:230`、`Symbol.Child.cs:669-671`），值改動走 pull（求值期 version 比對）。混合 push/pull。
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
- **path-qualified id 一鑰三用**：①常駐節點的身份 ②cache key ③現有 per-id buffer map 的鍵。三者同一把鑰匙——這是 compound 與 cache 對齊的接縫。
- **不每幀重建**：editor 改圖（加/刪節點/連線、進出 compound、改 reuse 的 Symbol 定義）時，**增量 patch** 常駐圖（加/刪對應常駐節點、重接、標記受影響區重算分區+失效 cache）。圖沒被編輯的幀，常駐圖原封不動。
- **reuse 狀態隔離自然成立**：同 Symbol 多 Child → 不同 path-qualified id → 各自獨立的常駐節點 + buffer/state + cache，跨幀穩定。
- **runtime 仍受保護**：cook 的逐顆 operator body（render-target pivot 剛 land 的）不動；改的是「外層怎麼 walk + 何時跳過」。承重線：常駐圖 + 求值 walker 是新元件，operator kernel 不動。

### 2.4　★ 承重決策 6（新）：version-chasing dirty + per-node cache（地基；照搬 TiXL 語意）

**這是地基，也是你「滿屏靜態白燒」的真正解。** 照搬 TiXL 的 version-chasing（`DirtyFlag.cs:19/61-62`），實作換我們的結構。

- 每個常駐節點帶 `{ sourceVersion, valueVersion, cachedOutput }`。dirty = `valueVersion != sourceVersion`（**version、不用 hash**，照 TiXL）。
- 走到某節點：dirty → 重算、寫 cachedOutput、`valueVersion = sourceVersion`；不 dirty → 回 cachedOutput、**不重算**。→ 貴的靜態 op（mesh 細分 / texture 預處理鏈）第一幀算一次、之後每幀回 cache、**GPU dispatch 不再發**。**這條就是「算一次存著」，跟 TiXL 完全對齊。**

> **★ 底層對齊關鍵（源碼驗過，我前一版講糊了，這版釘死）：TiXL 每幀「兩趟」，我們「一趟」，但結果等價、且我們更簡單是有正當理由的，不是耍聰明。**
> - **TiXL 兩趟＝被 lazy-pull 逼的**：`output.GetValue()` 只在自己已 dirty 時才往下 recurse（op 的 update 內部才呼叫 `input.GetValue()`，如 `AddVec3.cs:19`、`Transform.cs:27-44`）。所以深處 leaf 的改動，必須**先**一趟 `InvalidateGraph()`（`Slot.cs:266-323`）把 dirty 從 leaf 傳到 top，否則 lazy pull 在 top 看到乾淨就回 cache、永不下探。兩趟＝版本傳播趟 + lazy 計算趟。
> - **我們一趟＝eager 後序的副產物**：我們的 re-cook 從終端 eager 後序走（先解 input 再算 node，連到終端的節點每幀都踩到）。走到一個 node 時 input 已解、版本已知 → **版本傳播與重算在同一趟完成**。TiXL 要分趟的理由（lazy 在 top 會停）我們天生沒有。
> - **等價性**：兩者重算的節點集合相同（dirty cone）、結果相同。我們省掉 TiXL 的第二趟 re-descend（marginal）。**這是低風險、可驗的簡化（golden：一趟結果==TiXL 語意的兩趟結果）。**

- **Command／副作用 = 永遠跑（不 cache），照搬 TiXL `_valueIsCommand`（`Slot.cs:162`）**：TiXL 對 Command 型 slot `if(IsDirty || _valueIsCommand)`——Command 是「要被觸發的動作」，每次 GetValue 都得執行、不能 cache。→ 我們的「Command/present 永遠重發」**不是保守 workaround、是 TiXL 字面行為**。
- **diamond 去重**：一節點一幀只算一次（per-frame visited 標記＝TiXL `InvalidationTick`，`DirtyFlag.cs:93`）。eager 一趟天然要這個。
- **multi-input / time-remap 的 version-combine**：照搬 TiXL `InvalidationOverride`（`MultiInputSlot.cs:46-83` 版本取**和**＝任一 input 變就 dirty；`TimeClipSlot.cs:124-149` time-range 進出 re-entry）。我們化成 walker 裡的分支，非 C# per-type 子類。
- **EvaluationContext = 單一可變物件穿透整趟**（`EvaluationContext.cs`：LocalTime/LocalFxTime/RequestedResolution/transform stack/材質/變數）。time-remap 節點 save/restore LocalTime 給子圖（`TimeClipSlot.cs:62-81`），**非 push/pop stack**。我們的 cook context 照此。

### 2.5　★ 承重決策 7（新）：LIVE source = always-dirty（地基的一小塊）

- **LIVE source（會動的源頭，少數、可枚舉）= TiXL 的 `DirtyFlagTrigger.Always/Animated`**（`DirtyFlagTrigger.cs`、`Animator.cs:201` 掛動畫曲線 → `Trigger |= Animated`、`RunTime.cs:8` Time op 宣告 `Animated`）。我們：節點規格 + per-input animated 狀態掛 `isLiveSource` 旗，**= 每幀 bump sourceVersion = 恆 dirty = 每幀重算**。Time / Audio / stateful sim（粒子，每幀推進狀態）/ 被動畫驅動的 input 屬此。
- **🪤 #1 每幀正確性不變式（最大雷，地基級）**：**每個 LIVE source 每幀開頭必須 bump sourceVersion**（= Trigger=Always 字面行為）。漏一個 → 下游誤判 not-dirty → 卡舊畫面。防禦一行：每幀開頭遍歷 LIVE source 集合 bump。寫死進 selftest（漏 bump 的 RED 變體）。
- 注意：**這塊不靠分區**。LIVE source 恆 dirty + 決策 6 的 eager 一趟 walk，就已經正確地「LIVE 每幀算、STATIC 回 cache」。下面的分區短路是**之上的可選優化**，不是正確性所需。

### 2.5b　★★ 承重決策 9（新，2026-06-10 柏為拍板 A）：resident 節點 = TiXL `Slot`（三 concern 匯流）+ ctx 兩鐘形狀

> **這條把 compound / 增量求值 / 時間 binding 三條原本當「並列 lane」的東西，收斂成同一個物件——因為 TiXL 的 `Slot` 本來就是一個物件扛三件事。** 柏為直覺「兩層牽在一起」逼出來的，源碼複驗確認。

**底層事實（源碼）**：TiXL 的 `Slot` 一個物件同時扛——①**接線拓樸**（`InputConnections`，= compound 攤平後的 wiring）②**dirty/version/cache**（`DirtyFlag`，= 增量求值）③**參數怎麼被驅動**（`UpdateAction` + `Trigger`，= 時間 binding）。「這 input 怎麼被驅動」**只存一份、在 slot 上**：常數=預設值、連線=`ConnectedUpdate`、**automation=`Animator.cs:186-189` 把 `UpdateAction` 換成 `curve.GetSampledValue(ctx.LocalTime)` 並 `Trigger|=Animated`**、override=再換 UpdateAction（`Slot.cs:91-117`，存原本的、re-enable 換回）。**沒有獨立 binding 解析層。dirtiness 從同一個 slot 的 Trigger 讀 → binding 與 dirty 是融的。**

**承重原則（拍板 A）**：**「參數怎麼被驅動」一個擁有者，dirty 從它推導、不獨立存。** 實作 = **driver 直接住在 resident 節點的 input 上**（= TiXL slot），不另開平行結構。

- **resident 節點的每個 input 帶 `driver`**：`{ kind: Constant | Connection | Automation(curveRef) | LiveSource(id) | Override(value) }`。`isLiveSource`（決策 7）**從 driver 推導**（Automation/LiveSource/有狀態節點 → LIVE），**不另存一份旗**。→ 杜絕 P2 的結構性 stale：automation 掛上 = 設 driver = 同一處同時決定「解析值」與「LIVE/dirty」，不可能 drift。
- **S1 `SourceRegistry` 收編（不是重做、是換家）**：它在 stateless era（無常駐節點可掛）是對的；batch 1 造出常駐節點這個家 → 其 `(nodeId,portId)→binding/override` 資料遷移到 resident input 的 driver。SourceRegistry 退成「寫 driver 的編輯期 API」或併入。
- **存檔/undo 的單一擁有者（P4 二階）**：driver 住 resident graph → `.swproj`（批次 2）從 resident graph 序列化 driver；命令層 undo 記在 resident graph 的 driver 上。binding 只有一個擁有者，存檔/undo/求值不分叉。
- **★ ctx 兩鐘形狀（從 batch 1 第一天就長對）**：TiXL 兩鐘——`TimeInBars`（**播放頭**，scrub/暫停凍）vs `FxTimeInBars`（**牆鐘**，暫停照跑 idle motion），`EvaluationContext` 同時帶 `LocalTime`+`LocalFxTime`（`Playback.cs:102-129`、`EvaluationContext.cs:48-49`）。**automation 取樣播放頭**（`Animator.cs:186` `ctx.LocalTime`）、**有狀態 sim 取樣牆鐘**（`AdsrEnvelope.cs:70` `ctx.LocalFxTime`、`Time.cs:38` enum）。→ 我們的 `EvaluationContext` 即刻長成 `{ localTime, localFxTime, ... }` 兩欄（現在都 = `g_time` placeholder），Transport（時間 lane S5）續留後面才造真兩鐘——但 ctx 形狀現在定，automation 上來**不必重塑 ctx**。
- **時間 lane 排序不動、只改接點**：Curve(S3)/scoreGraph(S4)/Transport(S5) 仍是上半部 authoring；它們**往下不接平行 SourceRegistry，直接寫 resident 節點 input 的 driver**（= TiXL `OverrideWithAnimationAction` 的等價）。

> **一句話**：batch 1 不再只是「常駐求值圖」——它是 **compound 攤平 + 增量 dirty/cache + 時間 binding** 三者的匯流節點（= slot）。三條原本各排的 lane，地基是同一個。

### 2.6　○ 可選優化 E 階（降級，原承重決策 7/8 的分區短路）：LIVE/STATIC 分區 + pull 短路

> **⚠ 2026-06-10 底層複驗後降級（我自己的修正）**：原把「分區短路」當地基承重 + 我們贏 TiXL 的點。**錯了。** TiXL **沒有**分區、每幀照走全圖、在幾千節點規模跑得好（走訪＝指針追+int 比較、ns 級、`AggressiveInlining`、不重算）。分區只多省「**走訪**靜態子圖」的二階 overhead（microsecond 級），卻扛起整份設計**最重的 stale-frame 風險**。**「對齊 TiXL」＝這條 TiXL 不放地基、我們也不放。** 降成可選 E 階。

- **內容（之後若 profiler 證走訪是瓶頸再做）**：把 `isLiveSource` 往下游傳染算出 LIVE/STATIC 分區（編輯時算一次存常駐）→ cook walk 走到 STATIC 邊界就停、不踏進靜態子圖。
- **省什麼**：只省「走訪」overhead，**不省重算**（重算決策 6 早跳掉了）。在「滿屏靜態 + 少數在動」的超大圖（遠超 TiXL 常見規模）才線性顯現。
- **代價（為何不放地基）**：要維護「分區何時失效」——①加/刪連線 ②加/刪節點 ③input 常數↔animated toggle ④LIVE 集合變 ⑤改 reuse Symbol 定義。漏任一 → 該 LIVE 的被當 STATIC → **卡舊畫面（最危險的 bug 類型）**。地基不該為二階 perf 扛這風險。
- **採納門檻**：profiler 顯示走訪 + per-node IsDirty 檢查在真實 simple_world 圖吃掉可感知 frame budget（估計 >5–10 萬節點才到）。在那之前不做。

### 2.7　★ Command / present 規則（render-target pivot 接縫，地基級）

- **終端 present（畫到 swapchain/螢幕）= 永遠跑**：back buffer 每幀 clear+重畫。= 決策 6 的 Command always-run（TiXL `_valueIsCommand`）。
- **中間 RenderTarget 貼圖（第一批保守）**：**Command 節點一律重發**——這是 TiXL 對 Command 的字面行為，不是我們偷懶。靜態 texture 預處理鏈「算一次跨幀 cache 貼圖」是 GPU 資源生命週期問題（要保證貼圖不被 alias/覆寫），屬 **E3（GPU 層）單獨驗**，那時才放寬。

### 2.8　誠實對帳：「更好更簡單」survive 什麼、retract 什麼（底層複驗後）

| 主張 | 結算 |
|---|---|
| pull 不閃中間狀態 | ❌ retract：TiXL 也是 pull、也不閃。pull 贏 push，非我們贏 TiXL。 |
| 用 version 不用 hash | ❌ retract：TiXL 用的就是 `SourceVersion`/`ValueVersion`。一樣。 |
| 砍 editor-only dirty 統計 | ✅ survive：`FramesSinceLastUpdate` 等確 editor-only（`DirtyFlag.cs:48-91`），求值核心不抄。 |
| 少 Slot 子類 | ⚠ 部分：資料驅動 walker 分支取代 per-type C# slot 子類，但 version-combine 邏輯（multi-input 取和 / time-clip re-entry）仍要。少型別、不少邏輯。 |
| **一趟 vs TiXL 兩趟** | ✅ **survive（這版新確立的真簡化）**：我們 eager 後序，版本傳播與重算同趟；TiXL 兩趟是被 lazy-pull 逼的。等價、可 golden 驗、低風險。 |
| 靜態分區 pull 短路 | ❌ **retract「是我們的 edge / 地基承重」→ 降可選 E 階**：TiXL 自己沒做、二階、扛最重風險。見 2.6。 |
| 常駐 vs 每幀重建 | ❌ retract「我們不用常駐」：TiXL 規模下也要常駐（決策 3）。補回來、不是贏。 |

**一句話對齊結論**：地基 = TiXL 的 version-chasing dirty + cache + Command-always + LIVE-source-trigger，**邏輯 100% 對齊**；唯一正當的簡化是「eager 一趟取代 lazy 兩趟」（結構使然、可驗）；分區短路 TiXL 不放地基、我們也不放。

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
1. **常駐求值圖 + 展平 + resident 節點=slot（決策 3/9）**：`buildEvalGraph(symbolLib,root)→ResidentEvalGraph`(path-qualified id + sentinel 解析，**常駐、編輯時增量 patch**，不每幀重建)。**resident 節點的 input 帶 `driver{Constant|Connection|Automation|LiveSource|Override}`（= TiXL slot，S1 SourceRegistry 收編進來）**；`EvaluationContext` 即刻長成 **兩鐘形狀 `{localTime, localFxTime}`**（現都=g_time placeholder）。cook/evalFloat 改 walk 常駐圖、解析值讀 driver。**selftest：巢狀圖求值 == 等價手寫 flat 圖**(golden)；reuse 兩實例狀態獨立；**編輯後增量 patch 結果 == 全重建結果**(golden)；**driver 解析回歸 == S1 `--selftest-resolve` 全綠（override→binding→constant 不退化）**。
1b. **version-chasing dirty + per-node cache + Command-always + diamond + LIVE-bump**(決策 6/7，地基核心)：每節點 version/cachedOutput；**eager 一趟後序 walk**（版本傳播+重算同趟，dirty→重算/否則回 cache）；**`isLiveSource` 從 driver 推導（Automation/LiveSource/有狀態→LIVE），不另存旗**；Command/present 永遠跑；diamond per-frame visited；LIVE source 每幀 bump version。**selftest 有牙(count-based)**：靜態節點第 2 幀起 cook 0 次、LIVE 每幀 1 次、改靜態 param→重算 1 次→回 0、diamond 一幀 1 次、**🪤 漏 bump LIVE version → 卡舊的 RED 變體被抓**、**driver 從 Constant↔Automation toggle → LIVE/STATIC 同步翻（證 binding 與 dirty 不分叉）**、**一趟結果 == TiXL 語意兩趟結果 golden**。
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
