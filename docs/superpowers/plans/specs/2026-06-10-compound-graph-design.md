# Compound Graph 設計契約（完整 TiXL compound 結構）

> **這是 compound lane 的設計契約（active lane 的驗收合約）。** 2026-06-10 柏為拍板：
> 暫停所有節點/render-target 製作，先把 compound 圖模型這層做好；**功能 100% 照 TiXL**，
> **所有設計決策權威 = `external/tixl` 源碼**（不自創）。本檔是「先設計、不動碼」的產物。
> 實作計畫待 writing-plans 產。SSOT dashboard 仍是 `2026-06-07-imgui-metal-pivot-master-progress.md`。

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

## 契約 2：求值（cook）— 唯一不能直接照搬 TiXL 的地方

**TiXL 機制**（`Slot.cs:160-174`、`Instance.Connections.cs:54-198`、`EvaluationContext.cs`）：
TiXL 是 **persistent instance tree + lazy dirty-pull**。建圖時為每個 Child 建持久 instance(slot 物件)，**接線期**把 slot 指針 wire 好——compound 邊界(`Guid.Empty`)在此解析成直接指針穿透，**求值期完全看不到 compound 邊界**。EvaluationContext 同一物件穿透，無 push/pop(operator 自己 save/restore)。

**我們的真實差異**：我們是 **stateless re-cook**——每幀 `PointGraph::cook(graph, targetNode)` 從終端往回 walk connections 重新煮(per-node-id 的 buffer/state 跨幀複用)。沒有持久 instance、沒有 dirty flag。**這是不能假裝照搬 TiXL slot-pointer-network 的地方**（北極星：邏輯照 TiXL、實作換技術棧）。

> **★ 承重決策 3（核心架構判斷）：compound 是「圖模型/編輯/存檔」層的概念；cook 前先「展平」(flatten) 成等價 flat working graph，runtime cook 完全不變。**
>
> - 展平器 `flatten(symbolLib, rootSymbolId) -> flatWorkingGraph`：遞迴 inline 每個 compound Child 的子圖，用 **path-qualified node id**（compositionPath 的 hash，如 `childId₁/childId₂/localNodeId`）給每個 inlined 實例唯一且**跨幀穩定**的 id。sentinel 邊界(nodeId=0)在展平時就地解析成直連(母 Child 的外部輸入連線 ↔ 子圖內部)。
> - **這 = TiXL「接線期解析邊界、求值期透明」的 stateless 等價物**。展平後 cook 看不到 compound，跟 TiXL 求值期看不到 compound 同構。
> - **reuse 狀態隔離自然成立**：同一 Symbol 被多 Child 引用 → 展平後是不同 path-qualified id → PointGraph 的 per-id buffer/state(ParticleSystem sim 等)自然各自獨立、跨幀穩定複用。
> - **保護剛穩定的 runtime**：render-target pivot 剛 land，cook/PointGraph 不動一行。compound 隔離在展平器 + 圖模型 + 編輯/存檔層。承重線乾淨。
> - 成本：每幀重建 flat working graph（CPU 指針操作，便宜；圖沒變可快取）。evalFloat(值流) 同樣吃展平後的 flat graph。

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

- compound 是改圖模型的大梁，TiXL 長了多年。我們有 TiXL 源碼當藍圖會快很多，但這是**多 session 的大 lane**，不是一天實作完。今天交付 = 本設計契約鎖死。
- 展平器是新承重元件(雖然隔離了 runtime)：path-qualified id 穩定性、reuse 狀態隔離、每幀展平 perf(快取)要驗。
- 連線定址從 pin-id 改成 (node,slot) 四元組 = 動承重結構，命令層/存檔/UI 連線碼都要跟著改(blast radius，趁節點還少時做正是此因)。
- imgui-node-editor 撐單層切換已驗(TiXL 模式)；多層同框未驗(暫不做)。

## 實作批次順序（contract 先、葉子後；每批可獨立驗收）

0. **資料模型契約鎖**(Opus 順序)：Graph→Symbol(加 id/inputDefs/outputDefs)、Node→Child(symbolId+override)、Connection→四元組+sentinel。原子 Symbol = 舊 NodeSpec 包一層。selftest：純資料 roundtrip。
1. **展平器**：`flatten(symbolLib,root)→flatWorkingGraph`(path-qualified id + sentinel 解析)。cook/evalFloat 改吃展平結果(body 不變、前面加 flatten)。**selftest：一個巢狀圖展平後 cook 結果 == 等價手寫 flat 圖**(golden)；reuse 兩實例狀態獨立。
2. **存檔 v2 + migration**：symbols[] 庫 + 兩階段 load + 舊檔升級。selftest：v2 roundtrip + 舊 flat 檔讀進來不破。
3. **編輯導航**：compositionPath 進出層級(雙擊/麵包屑)、單 EditorContext 切層、view state 外存。眼手驗：進子圖→出來→畫面對。
4. **combine-into-symbol**：邊界偵測 + 建 Symbol + 重接 + 刪原(undo 保留)。眼手驗：選 3 節點→combine→母節點 port 對、子圖內容對。
5. **跨層 undo + reuse 收尾**：command 帶 compositionId；reuse(同 Symbol 多 Child，改定義全變)眼手驗。

## 驗收（compound「成立」的證據）

- 機器：展平 golden(巢狀==等價 flat)、reuse 狀態隔離 golden、存檔 v2 roundtrip + 舊檔 migration、跨層 undo selftest。
- **柏為親手(完成定義)**：選幾顆節點 combine 成一個 compound → 雙擊進去看子圖 → 出來 → 母節點能接線渲染 → 複製這個 compound 第二份(reuse) → 改定義一處兩份都變 → 存檔關 app 重開還在。
