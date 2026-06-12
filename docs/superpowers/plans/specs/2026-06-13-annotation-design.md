# Annotation 子系統 設計契約（TiXL Annotation 全套，doc-only）

> **這是批次13 lane A 的設計契約（契約先行、不寫碼）。** 功能 100% 照 TiXL，
> 設計權威 = `external/tixl`（唯讀，鎖 SHA 395c4c55）。本檔每節附 TiXL 原文引述 +
> 我方對映 + fork 候選具名。格式照 `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md`。
> 實作計畫待 writing-plans 產。對應 Resume 候選 = `PARITY_GAP_SCAN_2026-06-13.md` §B5（+ B4 idle fade 捎帶）。
>
> **本檔範圍**：Annotation 是**純 UI/編輯期物件**——畫布上的標題框，框住一群節點、給它們起名/上色/收合。
> **它不參與求值、不進 cook、不上 GPU。** 這條決定了它整個架構歸屬（見契約 0）。

## 北極星 / 範圍

- **完整 TiXL Annotation**：在畫布上框一塊區域、給標題（Title）+ 小標籤（Label）+ 顏色，
  拖標題移動（**連框內節點一起搬**）、拖右下角縮放、雙擊改名、收合（折疊框內節點）、存檔還在、
  copy/paste 跟著走、undo/redo 全程可逆。
- **決策權威 = TiXL 源碼**。邏輯/語意 100% 照 TiXL，皮換我們的技術棧（imgui / imgui-node-editor，
  非 C# ImGui.NET + 自製 MagGraphCanvas）。
- **捎帶設計（非 Annotation 本體）**：批次12-V 缺的 idle fade cook 訊號接縫（B4），一節講清楚介面、不實作。

## TiXL Annotation 的精華（一句話）

`Annotation` 是 **`SymbolUi` 層**（UI 模型，非 `Symbol` 執行層）的一個物件，
存進 **`.t3ui` sidecar**（不影響執行）；框與節點的關係是**幾何包含**（rect contains rect，
拖移時即時算、不持久存成員關係），**唯一持久的歸屬旗**是收合時寫到 child 上的
`CollapsedIntoAnnotationFrameId`。

---

## 契約 0：架構歸屬（這條先釘死，決定後面每一刀屬哪一區）

**TiXL 機制**（`Editor/UiModel/Annotation.cs:7`、`SymbolUi.cs:321`、`SymbolUiJson.cs:171-207`）：

> `Annotation.cs:7-15`：
> ```csharp
> public sealed class Annotation : ISelectableCanvasObject
> {
>     internal string Label = "";
>     internal string Title = "";
>     internal Color Color = UiColors.Gray;
>     public Guid Id { get; internal init; }
>     public Vector2 PosOnCanvas { get; set; }
>     public Vector2 Size { get; set; }
>     public bool Collapsed = false;
> ```
> `SymbolUi.cs:321`：`internal OrderedDictionary<Guid, Annotation> Annotations { get; private set; }`

- Annotation **住 `SymbolUi`**（編輯器 UI 模型），**不住 `Symbol`**（執行定義）。`Symbol` 完全不知道 annotation 存在。
- 序列化進 **`.t3ui`**（`SymbolUiJson.cs`），與位置/Comment/pinning 同層；**`.t3`（執行定義）裡沒有 annotation 段**。
- 求值期（`Slot`/`DirtyFlag`/cook）**從不碰** annotation。

**我方對映**：

| TiXL | 我方現況 | 改成 |
|---|---|---|
| `SymbolUi.Annotations`（UI 模型層，獨立 `.t3ui`） | 我方無 SymbolUi 分層——UI 位置 (x,y) 目前**內嵌在 compound_save 的 child/boundary** 物件裡（`compound_save.cpp:56-57,153`，「single-file inline analog of .t3ui」） | annotation 進 **`runtime/` 的資料模型**（純資料 struct，無 imgui 依賴），序列化走 **savev2 的 ui 內嵌段**（沿用「single-file inline .t3ui analog」既成路線，**不新開 sidecar 檔**——見契約 2 fork-A）。**繪製/互動**的肉在 `ui/`。 |

> **承重決策 0（架構歸屬，過自檢三題）**：
> - **①屬哪一區**：資料模型（struct + 序列化）= `runtime`（純資料，零 imgui）；繪製 + 拖/縮/改名/收合互動 = `ui`；命令（建/刪/改/移/縮）= `app`（command + undo，與既有 `graph_commands`/`rename_commands` 同層）。
> - **②依賴方向**：`ui → app → runtime` 單向，全部合規。annotation 資料是葉子，不往上依賴。
> - **③要 hook 驗證嗎**：要——畫面/拖曳要眼手驗。業務碼只留一行 `eye::recordItem(...)`（annotation rect 進 map），肉在 `verify/`。
>
> **⚠ fork-A0（具名，承重）：我方無 `SymbolUi` 分層 = annotation 資料層歸屬與 TiXL 不同構。** TiXL 嚴格分 `Symbol`（exec）/ `SymbolUi`（editor），annotation 只屬後者。我方目前 UI 位置內嵌進 savev2 的 child（無獨立 UI 模型）。**我方照「資料 struct 住 runtime、序列化內嵌 savev2 ui 段、繪製互動住 ui」走**——保「annotation 不污染執行模型」的 TiXL 本質（cook 永不見它），但物理上不分檔。代價：annotation 與 child 位置同檔（diff 粒度、未來分檔成本）= 已是 compound_save 既有 fork（S20 檔案佈局分岔）的延伸，不新增風險。**這是拍板佇列候選 #1（見文末）。**

---

## 契約 1：資料模型（照 TiXL `Annotation`）

**TiXL 機制**（`Annotation.cs:7-29`、`SymbolUi.cs:321`、`MagGraphAnnotation.cs:11-25`）：

- **欄位**（全在 `Annotation.cs:9-15`）：`Id`(Guid)、`Title`(string，主文字/描述)、`Label`(string，小標題)、
  `Color`(預設 `UiColors.Gray`)、`PosOnCanvas`(Vector2)、`Size`(Vector2)、`Collapsed`(bool)。
- **`Title` vs `Label` 兩個文字欄**（繪製端區分，`DrawAnnotation.cs:167-203`）：`Label` = 大字小標題（FontLarge，畫在頂部）；
  `Title` = 描述本文（FontNormal，或以 `"# "` 開頭時升 FontLarge，`DrawAnnotation.cs:187`）。改名 dialog 同時編兩欄（`AnnotationRenaming.cs:76,88`）。
- **與 child 的關係 = 幾何包含，不是持久成員**：框住誰**每次拖移當下即時算**（`FindAnnotatedOps` 做 `aRect.Contains(nRect)`，`AnnotationDragging.cs:193-233`）。
  Annotation **不存** child 列表。**唯一持久的歸屬旗** = child 上的 `CollapsedIntoAnnotationFrameId`（收合時寫，`DrawAnnotation.cs:93,106`），這住 `Symbol.Child` 的 UI 端、不住 annotation。
- **`MagGraphAnnotation`** = MagGraph 視圖對 `Annotation` 的 wrapper，加 `DampedPosOnCanvas`/`DampedSize`（拖移阻尼動畫，`MagGraphAnnotation.cs:19-20`）+ snap attractor（`:27-39`）。**純視圖層、不序列化。**
- **`Clone()`**（`Annotation.cs:17-29`）：複製時**換新 Guid**、其餘欄照搬（copy/paste 用，見契約 3）。

**我方對映**（最小形狀，照 TiXL）：

```cpp
// runtime/annotation.h（純資料，零 imgui）
struct Annotation {
    std::string id;        // 穩定 UUID（= TiXL Guid）
    std::string title;     // 描述本文（"# " 前綴 = 大字）
    std::string label;     // 小標題
    float       colorRGBA[4] = {gray};  // 預設灰（= UiColors.Gray）
    float       x, y;      // PosOnCanvas
    float       w, h;      // Size
    bool        collapsed = false;
};
// 容器：SymbolLib 的每個 compound Symbol 帶 vector<Annotation>（= SymbolUi.Annotations，
// 對映「annotation 屬某一層 composition」）。root flat 圖 = root Symbol 的 annotations。
```

| TiXL | 我方 | 對映 |
|---|---|---|
| `OrderedDictionary<Guid,Annotation>` | `std::vector<Annotation>`（per-Symbol） | 我方用 vector + id 查；保「插入序 = 序列化序」（TiXL 寫檔另 `OrderBy(Id)`，見契約 2）。 |
| `CollapsedIntoAnnotationFrameId`（child UI 端） | child 加 `collapsedIntoAnnotationId` 欄（內嵌 savev2 child ui 段） | 收合歸屬旗。**僅 collapse 功能要**——若批次1只做框/移/改名/縮放、collapse 排後批，此欄可緩（見契約 7 分批）。 |
| `MagGraphAnnotation`（damp wrapper） | ui 層的 transient view state（不序列化） | 阻尼是手感糖，可 fork 簡化（見 fork-D'）。 |

> **承重決策 1（照 TiXL）**：annotation 是扁平資料 struct（id + 兩文字欄 + color + pos + size + collapsed），
> **不存框內節點列表**——框住誰是繪製/拖移當下的幾何查詢，唯一持久歸屬是 child 上的 collapse 旗。
> 這是 TiXL 的精華（annotation 與節點**鬆耦合**，移動節點不需通知 annotation、反之亦然）。

---

## 契約 2：序列化（照 TiXL `SymbolUiJson` → 對映 savev2）

**TiXL 機制**（`SymbolUiJson.cs:171-207` 寫、`:497-533` 讀）：

> 寫（`SymbolUiJson.cs:178-203`）：`Annotations` 是陣列，每項 `{Id, Title?, Label?, Collapsed?, Color, Position, Size}`。
> **空字串/false 省略**（`if (!string.IsNullOrEmpty(...))`、`if (annotation.Collapsed)`）。`Color` 寫 RGBA vec4，
> `Position`/`Size` 寫 vec2。陣列序 = `OrderBy(x => x.Id)`（`:178`，穩定檔）。
>
> 讀（`SymbolUiJson.cs:505-530`）：缺/壞 `Id` → `Log.Warning` **跳過該項、不死**（`:507-511`）；
> 缺 `Title`/`Label` → `?? string.Empty`；缺 `Color` → 保預設灰（`:522-526`）；缺 `Position`/`Size` → `GetVec2OrDefault`。

**我方對映**（v2 schema 擴欄，照 TiXL 容錯哲學）：

- savev2 的每個 compound symbol 物件加 **`annotations` 陣列**（與既有 `children`/`connections` 平級，
  寫進 `compound_save.cpp` 的 symbol object 組裝段，`:122-197` 附近）。每項：
  ```json
  { "id": "...", "title": "...", "label": "...", "color": [r,g,b,a],
    "x": .., "y": .., "w": .., "h": .., "collapsed": true }
  ```
  **空 title/label、false collapsed、預設灰 color 省略**（照 TiXL 的省略規則，保檔乾淨 + diff 小）。
- **陣列序**：照 TiXL `OrderBy(Id)`（穩定檔 = 乾淨 diff，與 symbols/connections 既有排序同 intent，`compound_save.cpp:120,159,178`）。
- **讀檔容錯照搬 S15 哲學**（`compound_save` 既已落地，contract 3 §S15）：缺/壞 id → 跳過該 annotation + 警告、load 不死；
  缺欄 → 拿預設（title/label="" / color=gray / pos/size=0 / collapsed=false）；**整檔絕不因一個壞 annotation 而 false**。
- **舊檔零警告開**：v2 舊檔（批次2-12 寫的、無 annotations 段）→ `annotations` 鍵缺失 = 空陣列，**零警告**
  （照 `SymbolUiJson.cs:502` `is not JArray → return empty`）。v1 flat 舊檔經 migration → root symbol 空 annotations。

> **承重決策 2（照 TiXL 容錯）**：annotations 是 savev2 的**可選擴充段**——缺段 = 空、不警告；壞項 = 局部丟棄 + 警告、不死整檔。
> 舊檔（任何先前版本）開起來零 annotation、零警告，**不需 bump formatVersion**（純加可選段，與 S15「下次存檔自癒」同構）。
>
> **⚠ fork-A（具名，承重）：annotations 進 savev2 主檔的 ui 內嵌段，不開 `.t3ui` sidecar。** TiXL annotation 嚴格住 `.t3ui`（與 `.t3` 執行檔物理分離）。我方沿用 compound_save 既定的「single-file inline .t3ui analog」（boundary `x`/child `x` 已內嵌，`compound_save.cpp:56,153`）——**保「annotation 不進執行模型 symbols/connections/inputDefs」的本質**（cook 路徑永不見它），代價 = 與 child UI 同檔。= S20 既有檔案佈局分岔的延伸，**不新增 fork、只是覆蓋到 annotation**。
>
> **✅ fork-B 解除（orchestrator 拍板, 2026-06-13 合流時校正）：前提過時。** crude_json
> 非 ASCII assert 已在**批次4 根治**（sw-patch utf8：arm64 signed-char peek 修 unsigned/raw
> UTF-8 直通/\u→UTF-8 含 surrogate pair；golden=CJK byte-stable/中文/😀），批次6 rename 再
> 復證 CJK roundtrip 位元等同（lane-state memory + Cut 帳）。**annotation 中文 title/label
> 已無阻擋項**——實作批照常走，補一條「中文 annotation title 存讀」golden 即可（與 rename
> CJK golden 同款）。下文各處 fork-B 引用一併視為已解除。
>
> **⚠ fork-B（具名）：crude_json 非 ASCII assert 風險。** 批次2 已知缺陷（contract 2「⚠具名風險」）：`crude_json` parse 對非 ASCII byte assert→abort，「中文名寫得出讀不回」。**annotation 的 `title`/`label` 是使用者自由文字、極可能含中文/emoji**（柏為幫框取中文名 = 第一天就踩）。**此 spec 把「annotation 文字 = 高機率非 ASCII 入口」標為承重前提**：annotation 出貨**前**必先解 crude_json 非 ASCII（寫端 `\u` 跳脫 / 換 parser / 消毒）——與 combine 出貨同一個阻擋項，**不可各自繞**。拍板佇列候選 #2。

---

## 契約 3：命令層（照 TiXL `Commands/Annotations/` + copy/paste）

**TiXL 機制**：TiXL 把 annotation 命令分**兩類**——

**(a) 結構/文字命令（`Commands/Annotations/`，自帶 Do/Undo）**：

> - `AddAnnotationCommand`（`:14-36`）：Do = `symbolUi.Annotations[id] = annotation` + `FlagAsModified`；Undo = `Annotations.Remove(id)`。**靠 symbolId 查回 SymbolUi**（`SymbolUiRegistry.TryGetSymbolUi`，symbol 沒了則 warn+no-op）。
> - `DeleteAnnotationCommand`（`:14-34`）：Do/Undo 與 Add **完全鏡像**（Do=remove, Undo=re-add 原物件）。
> - `ChangeAnnotationTextCommand`（`:16-24`）：Do = `annotation.Title = NewText`；Undo = 還原 `_originalText`。**直持物件引用**（非 by-id，因 annotation 物件在編輯期不會被替換）。**只管 Title**——`Label` 由 renaming 端直寫（見契約 4 + 「不確定的點」#2）。

**(b) 移動/縮放命令 = 共用 `ModifyCanvasElementsCommand`（不在 Annotations/ 下）**：

> 拖移（`AnnotationDragging.cs:62`）/縮放（`AnnotationResizing.cs:42`）都 `new ModifyCanvasElementsCommand(symbolUi, [被動元素], selector)`，
> 拖完 `StoreCurrentValues()` + `UndoRedoStack.Add`（`Dragging.cs:162-163`、`Resizing.cs:109-110`）。
> **移動/縮放沒有專屬 annotation command**——annotation 與節點同走「改畫布元素位置/尺寸」這條泛用命令（因 `Annotation : ISelectableCanvasObject`，與節點同介面）。

**(c) copy/paste 帶 annotation**（`CopySymbolChildrenCommand.cs`，批次6 已引）：

> - 建構子收 `List<Annotation>? selectedAnnotations`（`:26`）；null → 複製全部（`:75`）。
> - **collapsed annotation 連帶複製框內 child**：`:57-72` 掃 `CollapsedIntoAnnotationFrameId == a.Id` 的 child 補進複製集（收合框搬走時框內節點跟著走）。
> - 每 annotation `Clone()`（換新 Guid）+ `PosOnCanvas += PositionOffset`（`:113-119`）；建 `OldToAnnotationIds` 重映射表（`:15,118`）。
> - **只複製兩端都在選取內的連線**（`:100-109`，與 child copy 同規則）；multi-input 保序靠 `_connectionsToCopy.Reverse()`（`:110`）。

**我方對映**：

| TiXL command | 我方對映（`app/` 區，與既有 command 同層） |
|---|---|
| `AddAnnotationCommand` | `AddAnnotationCommand`：Do = push 進 Symbol 的 annotations vector；Undo = 按 id 移除。**by-id 查回 Symbol**（compound 時代 = compositionId，照既有跨層 undo 的 `compositionId` 機制，contract 4）。 |
| `DeleteAnnotationCommand` | 鏡像 Add（存原物件、Undo 復原）。 |
| `ChangeAnnotationTextCommand` | 改名 command：存原 title(+label，見 fork-F)、Do 設新值、Undo 還原。**我方按 id 定位**（非 C# 物件引用）= fork-C。 |
| 移動/縮放（`ModifyCanvasElementsCommand`） | 沿用既有 canvas-element 位置命令（與節點拖移同路）——若我方尚無泛用「改元素位置」命令，**移動/縮放各自一個小 command 帶 (oldPos/Size, newPos/Size)**，與 `rename_commands` 同形（小工）。 |
| copy/paste annotation | 收編進既有 `copy_paste`（批次6 `CopySymbolChildrenCommand` 對應物）：annotation 換新 id + 偏移；collapsed 框連帶搬框內 child；建 oldToNew annotation id 表。 |

> **承重決策 3（照 TiXL）**：①建/刪/改名各一 undoable command（建刪鏡像、改名存原文字）；**②移動/縮放不另立 annotation 專屬命令、走泛用「改畫布元素位置/尺寸」命令**（annotation 與節點同介面 = `ISelectableCanvasObject` 等價物）；③copy/paste 帶 annotation（換新 id、偏移、collapsed 連帶框內 child、oldToNew 表）。
>
> **⚠ fork-C（具名）：改名/改色 command 按 id 定位、非物件引用。** TiXL `ChangeAnnotationTextCommand` 持 `Annotation` 物件直引用（`:27`）——成立是因 TiXL 編輯期 annotation 物件不被替換。我方 v2 save/load + rebuild 可能換物件實例 → **按 (compositionId, annotationId) 定位**（與我方既有 command 全用 id 的紀律一致，contract 4 跨層 undo）。語意同 TiXL（可逆改名），定位機制照我方 id-everywhere。
>
> **⚠ fork-D（具名）：改色 command。** TiXL `Commands/Annotations/` 下**只有 Add/Delete/ChangeText 三個**——**沒有獨立的 ChangeAnnotationColorCommand**（改色入口我未在源碼驗到專屬命令，見「不確定的點」#1）。我方**新增 `ChangeAnnotationColorCommand`（存原 RGBA、Do/Undo 還原）**= 具名 fork（TiXL 無對應專屬物 → 拍板候選 #3，但低風險、照改名鏡像即可）。

---

## 契約 4：互動（照 TiXL 手勢 + 狀態機）

**TiXL 機制**（`FactoryKeyMap.cs:53`、`KeyActionHandling.cs:141`、`NodeActions.cs:AddAnnotation`、`DrawAnnotation.cs`、`AnnotationDragging/Resizing/Renaming.cs`）：

- **建 = Shift+A**（`FactoryKeyMap.cs:53`：`new(UserActions.AddAnnotation, new KeyCombination(Key.A, shift: true))`），
  flags = `NeedsWindowFocus | KeyPressOnly`（`KeyActionHandling.cs:141`）。也可右鍵選單建（`GraphContextMenu.cs:454-460`）。
  建完**立即進改名狀態**（`KeyboardActions.cs:138`、`GraphContextMenu.cs:460`：`SetState(RenameAnnotation)`）。
- **建的尺寸/位置**（`NodeActions.cs:AddAnnotation`）：**無選取** → 100×140、左上角 = 滑鼠位置；
  **有選取** → 框 = 選取的 bounding box，再 `Expand(60,120)`（框住選取 + 留邊）。
- **拖標題移動（帶框內節點！）**（`AnnotationDragging.cs:49-60`）：
  - annotation 已選 → 搬「全部選取的節點」。
  - annotation 未選 → 搬 `FindAnnotatedOps`（幾何包含的 child/input/output/**巢狀 annotation**，`:193-233`）+ annotation 自己。
  - **Ctrl 按住拖 = 只搬框、不搬框內**（`:56` `if (!KeyCtrl) AddRange(FindAnnotatedOps...)`）。
  - 拖移帶 snap（對齊其他 annotation 邊 + grid raster，`:85-147`）。
  - **點一下沒拖（click 非 drag）= 選取語意**（`:165-182`）：未選 → 清選取 + 選此框（Shift 則加選）；已選 → Shift 取消選。
- **拖右下角縮放**（`DrawAnnotation.cs:206-228` resize handle 10×10 三角 + `AnnotationResizing.cs`）：
  `annotation.Size = newDragPos - PosOnCanvas`（`:99`），帶 snap。click 非 drag 同樣是選取語意（`:112-129`）。
- **雙擊標題改名**（`DrawAnnotation.cs:153-159`：`IsMouseDoubleClicked → SetState(RenameAnnotation)`）：
  進 dialog 編 Label（單行）+ Title（多行），`Esc`/點外/失焦關閉，改了才 push `ChangeAnnotationTextCommand`（`AnnotationRenaming.cs:104-118`）。
- **收合 toggle**（`DrawAnnotation.cs:75-114`）：標題左的 chevron 按鈕；收合 → 框內 child 標 `CollapsedIntoAnnotationFrameId`；展開 → 清旗。
- **選取與節點選取共存**：`Annotation : ISelectableCanvasObject`（`Annotation.cs:7`）—— annotation **進同一個 selection 集**，與節點同套選取/框選/Shift 加減選邏輯。

**我方對映**：

| TiXL 互動 | 我方對映（`ui/` 繪製 + 狀態） |
|---|---|
| Shift+A 建（NeedsWindowFocus, KeyPressOnly） | 進 keymap 資料表（`ui/keymap.cpp`，**照鐵律7 資料驅動**，不散打 io.KeyCtrl）。Shift+A 無 Cmd，不踩 Cmd↔Ctrl 對調雷。建完進 inline 改名。 |
| 建尺寸：無選 100×140@mouse / 有選 bbox+Expand(60,120) | 照搬數值常數。 |
| 拖標題帶框內節點（Ctrl=只框） | `ui` 拖移狀態：未選算幾何包含集（rect-contains，含巢狀 annotation）+ 自己；已選搬選取集；**Ctrl 切「只搬框」**（Mac Ctrl/Cmd 對映 = fork-E）。 |
| 拖角縮放 | resize handle（右下三角）+ size = dragPos − pos。 |
| 雙擊改名（Label + Title 兩欄） | inline `InputText`（單行 label）+ `InputTextMultiline`（title）；Esc/失焦關；改了 push command。**UI placeholder = 英文**（"Label..."/"Description..." 照 TiXL `:82,92`；imgui 無 CJK，**但使用者輸入的中文要能存讀 = fork-B 阻擋項**）。 |
| 收合 toggle | chevron 按鈕 → 寫/清 child collapse 旗。 |
| 選取共存 | annotation 進我方既有 selection 集（與節點同套；selection 集要能容異質，見「不確定的點」#6）。 |

> **承重決策 4（照 TiXL）**：①Shift+A 建（資料驅動 keymap）、建完進改名；②拖標題**預設連框內節點一起搬**（幾何包含集）、Ctrl 只搬框；③拖角縮放；④雙擊改名兩欄；⑤annotation 與節點共用同一選取集。
>
> **⚠ fork-E（具名雷，承重）：「Ctrl 拖 = 只搬框」的 Ctrl 在 Mac 上是真 Ctrl 還是 Cmd。** imgui Mac 把 Cmd 換進 `io.KeyCtrl`（記憶體雷 imgui-macos-cmd-ctrl-swap）。TiXL 讀 `ImGui.GetIO().KeyCtrl`（`AnnotationDragging.cs:56`）= 在 Mac 上**實際對應 Cmd 鍵**。**我方要決定**：對齊 TiXL「按 io.KeyCtrl」(= Mac 上按 Cmd) 還是對齊「按物理 Ctrl」。**傾向照 io.KeyCtrl**（= TiXL 字面行為 + 與我方其他快捷鍵一致 = Mac 按 Cmd），但這是手感、需柏為親測拍板 → 拍板佇列候選 #4。
>
> **⚠ fork-D'（具名，手感）：拖移阻尼（DampedPosOnCanvas/DampedSize）。** TiXL `MagGraphAnnotation` 對 pos/size 做阻尼動畫（`:19-20`）。**第一批可不做阻尼**（直接設 pos/size），手感稍硬但功能完整——阻尼是糖，排後批或對齊既有 node damp。具名、低風險。

---

## 契約 5：繪製（批次11 已掃常數 + 本檔補全）

**TiXL 機制**（`DrawAnnotation.cs`、`ColorVariations.cs:19-20`）：

- **背景**（`DrawAnnotation.cs:38-47`）：`ColorVariations.AnnotationBackground.Apply(annotation.Color).Fade(0.8f)`；
  `AnnotationBackground = (brightness 0.12, saturation 1.0, opacity 0.7)`（`ColorVariations.cs:19`）。
  圓角 **8px**（`:40`，**固定、不隨 zoom**——與節點的 5px×zoom 不同），flags = `RoundCornersTop | RoundCornersBottomLeft`（`:41`，**只圓三角**）。`AddRectFilled` 偏移 `+Vector2.One`（`:44`）。
- **邊框**（`:52-59`）：選中 = `ForegroundFull`（白）；未選 = `AnnotationOutline.Apply(color)`（`AnnotationOutline = (brightness 1.0, sat 0.0, opacity 0.0)` = 顏色去飽和當邊，`ColorVariations.cs:20`）。
- **小標題（`Label`，`:167-182`）**：`FontLarge`，畫在頂部 `pMin + (18, 3)`；fade = `SmootherStep(0.1, 0.2, scale) * 0.8 * GraphOpacity`（zoom 越小越淡）；
  字級隨 zoom 三段式（`:170-174`）。
- **描述（`Title`，`:185-203`）**：`"# "` 開頭 → FontLarge 否則 FontNormal（`:187`）；畫在 label 下方 `pMin + (8, 8+labelHeight)`；fade = `SmootherStep(0.25, 0.6, scale) * 0.8`。
- **收合 chevron**（`:75`）：`ToggleTwoIconsButton(ChevronDown/ChevronRight)`，左上角。
- **header hover 高亮**（`:139-143`）：hover 時頂部疊 `ForegroundFull.Fade(0.1)`。
- **resize 三角**（`:225`）：右下角 10px `AddTriangleFilled`，色 `BackgroundButton`。
- **收合時高度**（`:17-19,31-33`）：collapsed → 只畫 `LineHeight` 高（標題條），否則全 Size。
- **clip rect**（`:36`）：整框 push clip（內容不溢出）。

**我方對映**（`ui/` 繪製，**英文 placeholder、使用者文字另解**）：

- 背景/邊框 color variation 公式照搬（我方 `node_draw.cpp` 已有 ColorVariation 等價物，`:40-49`）。
- **圓角 fork**：annotation = **固定 8px**（不抄節點的 zoom-aware 5px）——TiXL 兩者本就不同（`DrawAnnotation.cs:40` 把 `* canvas.Scale.X` 註解掉了）。
- 小標題/描述 fade-by-zoom 三段式 + Label/Title 雙文字 + `"# "` 升大字、收合單行高、resize 三角、hover 高亮、chevron 收合鈕——逐項照搬常數。
- **map hook**：annotation rect 進 eye map（一行 `eye::recordItem`），供眼手驗框位置/拖移生效。

> **承重決策 5（照 TiXL，皮換 imgui）**：繪製常數逐項照搬（8px 固定圓角、三角圓角 flags、AnnotationBackground/Outline 公式、Label/Title 雙文字 + zoom fade 三段、收合單行、resize 三角、hover）。**UI placeholder 英文；使用者輸入文字的中文支援 = fork-B 阻擋項。**

---

## 契約 6：idle fade cook 訊號接縫（捎帶設計，批次12-V 缺的最後一塊，**不實作**）

> **這節不是 Annotation 本體**——是 PARITY §B4「idle fade（60 幀無 output 更新→暗 60%）」缺的 cook 端訊號，
> 趁本批捎帶把**介面**定清楚（lane V 視覺包當時缺這塊訊號源而擱置）。**本批不實作，只定接縫。**

**TiXL 機制**（`DrawNode.cs:42-50`、`DirtyFlag.cs:48-65`）：

> `DrawNode.cs:42-50`：每個節點掃自己**所有 output slot**的 `DirtyFlag.FramesSinceLastUpdate`，取 **min**；
> `idleFadeFactor = RemapAndClamp(framesSinceLastUpdate, 0, 60, 1.0, 0.6)`（60 幀沒更新 → 暗到 0.6）。
> 訊號源 `DirtyFlag.FramesSinceLastUpdate`（`:65`）= `(_globalTickCount - 1 - _lastUpdateTick) / GlobalTickDiffPerFrame`。
> `_lastUpdateTick` 由 `SetUpdated()`（`:49-58`，**`// editor-specific function`**，`:48`）在 op **真的重算了**（cook 跑了 UpdateAction）時寫。
> **關鍵**：這整組（`FramesSinceLastUpdate`/`SetUpdated`/`_lastUpdateTick`）TiXL 自註 **editor-specific**（`DirtyFlag.cs:48,64`），
> 求值核心**不靠它**——它純粹餵節點邊框的「最近有沒有在動」視覺。（呼應 compound-graph-design 契約 2.0「偶然複雜 editor-coupled，可不抄求值、但要抄這個視覺」。）

**我方對映（最薄接縫，不實作）**：

- 我方 resident cook（`app/frame_cook.cpp` 每幀 `cookResident`，`:190`）+ per-output cache（`resident_eval_cache.cpp` 的
  `{sourceVersion, valueVersion, ...}`）已有「這個 output 這 pass **有沒有重算**」的天然訊號——**重算發生 = dirty 命中 = `valueVersion` 從舊跳到新**。
- **最薄縫**：在 cache slot 加一個 **`lastUpdatePass`(int)** 欄，cook 端**重算那一刻**寫當前 pass/frame 計數（= TiXL `SetUpdated` 的等價，**editor-only，不影響求值**）。
  UI 端讀 `framesSince = currentFrame − node 全 output 的 max(lastUpdatePass)` → 餵 `node_draw.cpp` 既有 idle fade（`node_draw.cpp` 已有 fade 機制、只缺這個輸入）。
- **歸屬**：欄住 `runtime`（cache slot）但**標 editor-only**（與 TiXL 同——求值不讀）；寫入是 cook 端**一行 hook**（重算分支末尾）；讀取在 `ui/node_draw.cpp`。**不進 GPU、不影響 cook 結果。**

> **承重決策 6（捎帶介面，照 TiXL）**：idle fade 訊號 = per-output「最近重算在哪個 frame/pass」(`lastUpdatePass`)，
> cook 重算時寫一行、UI 取 node 全 output 的 max 算 framesSince → 餵既有 fade。**editor-only、不碰求值正確性**（照 TiXL 自註）。
> **本批只定接縫，lane B 視覺批實作。**
>
> **⚠ 具名注意**：我方求值單位是 **evaluation pass** 非幀（compound-graph-design 契約 2.0 健檢修正 C1-c：一幀可多 pass）。
> idle fade 的「60 幀」要對齊到**幀**還是 **pass** = 小決策（傾向幀，與 TiXL 視覺意圖一致），lane B 實作時定 → 拍板候選 #5。

---

## 本質複雜 vs 偶然複雜（誠實標）

- **Annotation 本體 = 偶然簡單**：扁平資料 struct + 序列化 + 三個小 command + 幾何包含查詢 + 照搬繪製常數。
  **沒有求值、沒有 GPU、沒有 dirty/version**——這是它與 compound/resident 那條本質難地基的**乾淨隔離**（cook 永不見 annotation）。
- **唯一的本質接縫 = collapse 的雙向歸屬**：收合旗寫 child、複製 collapsed 框要連帶搬框內 child（`CopySymbolChildrenCommand.cs:57-72`）——
  這是 annotation 與 child 唯一的持久耦合，要小心（漏了 = 複製收合框框內節點掉）。**若 collapse 排後批，本批零本質複雜。**
- **真正的雷在邊緣、不在核心**：fork-B（中文文字 = crude_json 非 ASCII 阻擋項，與 combine 同一個）、
  fork-E（Mac Ctrl/Cmd 對調）、fork-A（無 SymbolUi 分層的資料歸屬）。這些是接縫對齊問題，不是 annotation 難。

---

## 分批施工建議（2-3 批，照「資料先、命令互動中、繪製收尾」）

> 每批可獨立驗收（牙 + scn）。批間契約已鎖、葉子並行。

**批 A：資料模型 + 存讀（runtime + savev2）**
- `runtime/annotation.h`（struct）+ Symbol 帶 `vector<Annotation>`；savev2 `annotations` 段（寫/讀/容錯/舊檔零警告）。
- **前置阻擋**：fork-B（crude_json 非 ASCII）——若批 A 要存中文 title 就**先解**；若批 A 先只測 ASCII，標明中文延後。
- **驗收**：
  - 牙：`--selftest-annotation-save`——roundtrip 位元穩（含省略規則）、缺段舊檔零警告開、壞 annotation 局部丟棄不死整檔、reuse/多 symbol 各自 annotation 隔離。`-bug`（漏讀 collapsed 旗 / 缺段報警告）有牙。
  - scn：存檔→關→重開 annotation 還在（活體可證 → 附 `.scn`）。

**批 B：命令 + 互動（app + ui）**
- 命令：Add/Delete/ChangeText/ChangeColor/Move/Resize（各 undoable，by-id 定位，compositionId 跨層）。
- 互動：Shift+A 建（資料驅動 keymap）+ 建完改名、拖標題帶框內節點（Ctrl 只框，fork-E）、拖角縮放、雙擊改名、選取共存。
- copy/paste 帶 annotation（換 id、偏移、collapsed 連帶框內 child）。
- **驗收**：
  - 牙：`--selftest-annotation-commands`——建/刪/改名/改色/移動/縮放各 do→undo→redo 還原；copy 換新 id + 偏移 + collapsed 連帶；幾何包含集正確（含巢狀）。`-bug`（undo 不還原 / copy 不換 id）有牙。
  - scn：Shift+A 建框→拖標題→框內節點跟著移→雙擊改名→undo 鏈（活體 → 附 `.scn`）。

**批 C：繪製 + eye hook（ui + verify）**
- 繪製常數全套（背景/邊框/Label/Title fade/收合/resize 三角/hover/chevron）；annotation rect 進 eye map（一行 hook）。
- collapse 視覺（收合單行高 + 框內 child 隱藏）若批 B 未做則收這。
- **驗收**：
  - 牙：`--selftest-map` annotation rect 入 map、非空、收合切換 rect 變。
  - scn + 眼驗：截圖框繪製對（顏色/圓角/標題）、拖移後 map 重抓對。

**（捎帶，非本子系統）批 V'**：idle fade `lastUpdatePass` 接縫 → 餵 `node_draw` idle fade（lane B 視覺批做，本批只交介面）。

---

## 拍板佇列候選（柏為定，品味/不可逆/TiXL 無對應物才上）

1. **fork-A0 / fork-A：annotation 資料歸屬 + 不開 sidecar**（我方無 SymbolUi 分層；annotation struct 住 runtime、序列化內嵌 savev2 ui 段、繪製互動住 ui，不開 `.t3ui` 獨立檔）。= S20 檔案佈局分岔的延伸。**傾向：照既定 single-file inline，不新開檔。** 需確認可接受同檔 diff 粒度代價。
2. **fork-B：annotation 中文文字 = crude_json 非 ASCII 阻擋項**（與 combine 同一個既知缺陷）。**承重、不可繞**：annotation title/label 是高機率中文入口，出貨前必解 crude_json 非 ASCII。需定何時解（批 A 前 or 與 combine 一起）。
3. **fork-D：ChangeAnnotationColorCommand**（TiXL `Commands/Annotations/` 下無此專屬命令 = 「TiXL 無對應物」）。**傾向：照改名命令鏡像新增**（低風險），但屬無對應物 → 報備。
4. **fork-E：Mac「Ctrl 拖 = 只搬框」的 Ctrl 對應 Cmd 還是物理 Ctrl**（imgui Cmd↔Ctrl 對調雷 + 手感）。**傾向：照 io.KeyCtrl = Mac 按 Cmd = TiXL 字面行為**，但手感需柏為親測。
5. **fork-D'：拖移阻尼第一批做不做**（TiXL 有 DampedPos/Size；糖、可後批）。**傾向：第一批不做，功能完整後對齊既有 node damp。** 低優先。
6. **idle fade 單位（幀 vs pass）**（契約6）——我方一幀可多 pass，TiXL 是幀。**傾向幀**，lane B 實作時定。

---

## 不確定的點（refuter 攻擊清單從這裡長）

1. **改色入口我未在 TiXL 源碼驗到專屬 command**（fork-D）——TiXL 改色可能走某泛用路徑或直寫，我只確認 `Commands/Annotations/` 下**只有 Add/Delete/ChangeText 三個**。若 refuter 找到 TiXL 改色的真實命令路徑（例如某個 color picker 直寫 + 包進泛用 command），對齊它。
2. **`Label` 的 undo 覆蓋（fork-F 候選）**：TiXL `ChangeAnnotationTextCommand` **只存/還原 `Title`**（`:11,18,23`），但改名 dialog 同時改 `Label`（`AnnotationRenaming.cs:96` `annotation.Label = _labelBuffer` 直寫）——**Label 的改動似乎不進 undo**。這是 TiXL 自身的不對稱（undo 改名只回 Title 不回 Label）。**我方是否照抄這個不對稱、還是補上 Label undo** = 未定，傾向**補上**（更一致）但標為具名 fork。refuter 請對 `AnnotationRenaming.cs:96-118` 復驗 Label 是否真的漏 undo、有無別處補。
3. **collapse 旗住哪**：TiXL 住 child UI（`CollapsedIntoAnnotationFrameId`）。我方無 SymbolUi → 我提議內嵌進 savev2 child ui 段。若 collapse 排後批，此欄與 child schema 的擴充時機未定。
4. **idle fade 單位（幀 vs pass）**（契約6）——傾向幀，lane B 定。
5. **annotation 在 root flat 圖 vs compound 子圖的容器歸屬**：我提議「per-Symbol vector」。root flat 圖目前無顯式 Symbol 容器（走 flat Graph 路徑，contract「production swap」殼層仍讀 flat、`frame_cook` viewTarget 讀 flat）——annotation 掛 root 的物理位置需與 flat 殼層對齊，未細驗。
6. **selection 集共存的具體機制**：TiXL annotation `: ISelectableCanvasObject` 進同一選取集。我方 selection 集目前是否能容異質（節點 id + annotation id）未驗——若只收 node id，要擴成可容兩類。

## 拍板記錄（orchestrator, 2026-06-13 合流）
- fork-B：**解除**（前提過時，批次4 已根治 crude_json 非 ASCII；見上方校正框）。實作批補中文 title golden。
- fork-A：**採**——annotation 內嵌 savev2，不開 sidecar（S20 既有分岔的一致延伸）。
- fork-D：**採**——ChangeAnnotationColorCommand 鏡像改名命令補上（TiXL 無對應物；可逆、工程一致性級，不上柏為佇列）。
- fork-E：**照 TiXL 字面**（io.KeyCtrl=Mac Cmd 拖=只搬框）；手感列柏為親測欄。
- fork-F：**傾向補 Label undo**；實作批 refuter 先對 AnnotationRenaming.cs:96-118 復驗 TiXL 是否真漏，再定。
