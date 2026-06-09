# TiXL Legacy Node UI Contract (模樣合約 + 互動合約)

> **狀態:** 調查完成 / 合約定稿 / **尚未實作**。實作在獨立 **UI branch**（柏為 2026-06-09 拍板：先做 Legacy 自由連線、不做 Magnetic；其他 session 在動共享碼，這輪只唯讀產出）。
>
> **目的:** 給「每個節點」一套共用的 TiXL Legacy 模樣 + 行為，讓之後新增節點只要填這張合約就自動長成 TiXL 樣（含**收合**與**接線**）。
>
> **權威來源（TiXL Legacy 自由連線圖，非 Magnetic）:**
> - 外觀/收合：`external/tixl/Editor/Gui/Graph/Legacy/GraphNode.cs`、`Editor/UiModel/SymbolUi.Child.cs`、`Editor/Gui/Styling/{UiColors,ColorVariations}.cs`、`Editor/UiModel/InputsAndTypes/TypeUiRegistry.cs`
> - 接線：`Editor/Gui/Graph/Legacy/Interaction/Connections/{ConnectionMaker,ConnectionSplitHelper,ConnectionSnapEndHelper}.cs`、`Graph.cs`、`Graph.ConnectionSorter.cs`
>
> **我們的地基:** imgui-node-editor（自由連線，與 Legacy 同類）。`runtime/graph.{h,cpp}`（NodeSpec/PortSpec/Node/Connection）、`ui/editor_ui.cpp`（drawNodeCanvas）、`ui/node_faces.{h,cpp}`（per-operator custom face）、`app/command.h` + `app/graph_commands`（Add/Move/Connect/Delete/Macro）。

---

## A. 共用節點「模樣合約」（每個節點都填這張）

TiXL 的節點外觀**不是每個節點各畫各的**，是一套共用樣式 + 少量 per-spec 欄位。我們對應成 **NodeSpec 的模樣欄位**，新增節點 = 加一筆資料（資料驅動，ARCHITECTURE.md 鐵律 7）。

### A.1 視覺解剖（Legacy node anatomy）

| 部位 | TiXL Legacy 規則 | 來源 |
|---|---|---|
| **尺寸** | 寬固定 `110`；高 = `23 + 可見輸入數 × 13`（標題列 ~22px，每個可見輸入 +13px）；Resizable 模式用存下的 size | GraphNode.cs:940 |
| **型別色** | 取**第一個輸出的 type** 對應色（`TypeUiRegistry`），無輸出→`Gray`。這是整個節點的根色 | GraphNode.cs:138 |
| **色彩變換** | 用 HSV 因子套在根色上：背景 `b0.5/s0.7`、hover `b0.6/s1`、idle `b0.71/s1/a0.3`、label `b1.3/s0.4`、外框 `b0.1/s0.7/a0.5`、highlight `b1.2/s1.2` | ColorVariations.cs:11 |
| **標題列** | 高 ~22px，label FontBold，偏移 (4,2)，改名則加引號，隨 zoom 淡入 | GraphNode.cs:396 |
| **輸入 slot** | **左緣**，3px 寬直條，間距 2px，色隨 type（reactive） | GraphNode.cs:1390 |
| **輸出 slot** | **右緣**，直條，值更新時有掃描動畫 | GraphNode.cs:704 |
| **參數** | **不畫在臉上**（在 inspector）。**唯一例外：custom-UI 運算子** | GraphNode.cs:161 |
| **選取** | 外粗框 `BackgroundFull` + 內細亮線 `Selection` | GraphNode.cs:403 |
| **狀態點** | 右上角 10×10：animated(橘)/pinned/snapshot；disabled=對角 X 線；bypass=橫線+X | GraphNode.cs:197,351 |
| **preview** | 第一輸出是 Texture2D 且開 ShowThumbnails → 節點上方畫縮圖 | GraphNode.cs:876 |

### A.2 Custom-UI hook（這是 AudioReaction 頻譜臉的契約位置）

TiXL：節點呼叫 `instance.DrawCustomUi(drawList, nodeRect, view)`；回傳 `CustomUiResult` flags（`Rendered`／`PreventInputLabels`／`PreventTooltip`…）決定是否蓋掉預設 label。整個節點矩形交給 operator 自繪。

**我們對應：** `ui/node_faces.{h,cpp}` 的 `drawXxxFace(node)` 已是這個 hook（`drawAudioReactionFace` 是第一個 case）。**合約：** 每個需要自訂臉的節點在 node_faces 註冊一個 face fn；canvas loop 用「資料驅動 dispatch」呼叫（見 C.1），不要在 canvas loop 裡長一排 `if node.type==...`。

### A.3 NodeSpec 模樣欄位（要新增的合約欄位）

目前 `NodeSpec = {type, title, ports[], evalFn}`、`PortSpec = {id,name,dataType,isInput,def,min,max,widget,labels,pinless}`。**建議補的模樣欄位**（每個都有預設，新節點不填也能長出合理樣子）：

```
NodeSpec 補:
  categoryColorType : string   // 取哪個 type 當根色（預設=第一個輸出的 dataType）
  customFace        : enum/fn  // none | 註冊在 node_faces 的 face id（預設 none）
  previewPolicy     : enum     // none | textureFirstOutput（預設 none）
  defaultStyle      : enum     // Default | Expanded | Resizable | WithThumbnail（預設 Default）
PortSpec 已有的就夠（widget/labels/pinless 已支援 inspector 渲染）
```

> **律法檢查（node-contract-architect）:** 每個節點先回答一個問題（question→inputs→op→outputs→failure→evidence）。模樣欄位屬「UI display」層——**不得偷改存檔事實或 runtime 排程**。`customFace`/`previewPolicy`/`categoryColorType`/`defaultStyle` 純顯示，安全。

---

## B. 收合 / size 狀態合約

TiXL 用 **`SymbolUi.Child.Style` enum**（**存在節點上、會存檔、undo/redo 會還原**），不是 bool：

| Style | 顯示 | 切換 |
|---|---|---|
| **Default**（預設/緊湊） | 只顯示**必填**或**已接線**的輸入；其餘隱藏，底部一個閃爍點提示有隱藏輸入 | 右上 chevron → Expanded |
| **Expanded** | 顯示**所有**輸入 | 右上 chevron → Default |
| **Resizable** | 使用者拖右下角把手（10×10）設任意 size，存 `childUi.Size` | 拖把手 |
| **WithThumbnail** | 節點上方畫 preview（第一輸出 Texture2D） | 自動/設定 |

切換 UI：右上角 fold/unfold chevron（16×16），`zoom>0.7 且無 custom-UI` 才顯示。

**我們對應 + 缺口:**
- `Node` 要加 `style` 欄位（存檔 + undo）。
- 需要 **`SetNodeStyleCommand`**（do/undo 切 style）。
- canvas 繪製要依 style 過濾「可見輸入」（Default=必填∪已接線；Expanded=全部）。
- imgui-node-editor 的節點大小由內容撐出 → 我們用 Dummy/內容控制高度即可（已驗證 AudioReaction 臉就是這樣撐高）。

> **ui-skin-pressure-gate:** 收合是**持久狀態**（存檔 roundtrip 要保留）→ 必須走 command（L4）+ 存檔（L6），不能只藏在 imgui id 或 widget field。

---

## C. 接線（wiring）互動合約

### C.1 我們已有 vs 缺（對照 imgui-node-editor 現況）

**已有**（`editor_ui.cpp` drawNodeCanvas 的 BeginCreate/QueryNewLink + 既有 command）：
- ✅ output→input 拖線 + **dataType 精確比對**（已是 exact match，符合 TiXL）
- ✅ reconnect（換接，已做成 MacroCommand：刪舊+加新）
- ✅ delete link / node（Backspace 路由 + DeleteConnections/DeleteNodes）
- ✅ pin 視覺 snap、undo/redo

**缺**（TiXL Legacy 有、我們要補）：
| 缺的行為 | TiXL 來源 | 要補什麼 |
|---|---|---|
| **從 input 反向拖** | ConnectionMaker.cs:152 | imgui-node-editor 預設只從 output 拖；要攔 input pin 起手、反向建線 |
| **插入連線（split）** | ConnectionSplitHelper.cs | 把節點拖到線上 → 刪 A→C、加 A→New、New→C；hit-test=點到 bezier <50px;新 command `SplitConnectionCommand`（macro） |
| **多輸入排序** | Symbol.cs:169 | multi-input slot 依索引插入/append；我們 Connection 模型要支援同一 input 多條 + 順序 |
| **cycle 偵測** | Structure.cs:314 | 建線前 `CheckForCycle`；imgui-node-editor 不擋，要在 AcceptNewItem 前自驗、擋掉並提示 |
| **多選廣播** | ConnectionMaker.cs:114 | 多選節點同時從各自輸出拖到一個 input（低優先） |
| **拖到空白→開 browser** | ConnectionMaker.cs:488 | 放開在空白處彈出新增節點選單（接 Add 選單，已資料驅動） |
| **symbol input/output 節點** | InputNode/OutputNode.cs | 圖自己的輸入/輸出節點特殊樣式（compound 用，後期） |

### C.2 連線繪製規則（draw 合約）
- bezier（或 arc，UseArcConnections 設定）；tangent **水平**，長度 remap 距離 `[30,300]→[5,200]`，20 段。
- 色 = 連線 type 色（`ColorVariations.ConnectionLines`）；選取 → `Highlight`；即將被替換 → sin 振盪閃；未用 → 淡 0.6。
- 粗細隨「使用度」升（active 粗、50 frame 內淡出）。

### C.3 驗證梯（tooll3-interaction-compatibility L1–L7）— 要補的 behavior traces
每個手勢寫成可重播 trace（gesture → expectedCommands → expectedGraph → expectedUndoGraph）：

1. `output→input` 拖放 → `[AddConnection]`，undo 復原。**（已有）**
2. input(已接)→新 output 反向拖 → `[Delete, Add]`。**（缺反向起手）**
3. 拖到空白放開 → 不變更 OR 開 browser 插入。**（缺）**
4. 節點拖到線上 → `[Delete(A→C), Add(A→New), Add(New→C)]`。**（缺 split）**
5. 同一 multi-input 加第 2、3 條 → 依索引 append/insert。**（缺多輸入序）**
6. 接成環 → 無 command + 提示「would result in a cycle」。**（缺 cycle 檢查）**
7. 切 Default↔Expanded → `[SetNodeStyle]`，存檔 roundtrip 保留。**（缺 style）**

---

## D. 實作路線（UI branch；契約層順序鎖，每刀=柏為可親手測）

> 順序刻意「先骨後皮」：資料/命令合約 → 再上 TiXL 皮。不讓皮跑在行為前面（ui-skin-pressure-gate）。

- **刀 A · 模樣皮（純視覺，最便宜，先看到 TiXL 感）**：共用節點繪製器（尺寸 110×可變高、型別色 + ColorVariations、左輸入/右輸出直條 slot、選取/狀態點、標題列）。讀現有 NodeSpec/Node，零新狀態。eye 驗：各節點長成 TiXL 樣。
- **刀 B · 收合狀態**：`Node.style` + `SetNodeStyleCommand` + 依 style 過濾可見輸入 + chevron 切換 + 存檔 roundtrip。
- **刀 C · 連線繪製對齊**：bezier tangent/色/粗細/替換閃，照 C.2。
- **刀 D · 接線行為補完**：cycle 偵測（建線前擋）→ input 反向拖 → 拖空白開 browser。
- **刀 E · 進階接線**：split-into-connection（+自動排版位移）→ 多輸入排序。
- **刀 F（後期/compound 時再做）**：symbol input/output 節點、多選廣播。

每刀獨立可 build / selftest / 柏為親測；law check（五區/依賴單向/verify 一行 hook/單檔≤400/資料驅動）每 commit 前過。

---

## E. 殘留問題 / 待柏為定

1. **categoryColorType 的色表**：TiXL 的 type→色在 `TypeUiRegistry`（每個 .NET type 一個色）。我們 dataType 目前只有 `Float`/`Points`/`Buffer` 等少數 → 先給這幾個定 TiXL-equivalent 色即可，之後型別長出來再加。
2. **arc vs bezier**：TiXL 有設定切換；我們先做 bezier（imgui-node-editor 原生），arc 留設定。
3. **Resizable / WithThumbnail** 是否第一版就要：建議先只做 Default/Expanded（覆蓋柏為說的「收合」），Resizable/Thumbnail 後補。
