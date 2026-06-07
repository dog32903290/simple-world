# 節點編輯命令層 — 設計 spec

> 日期：2026-06-08
> 對症：編輯器目前所有圖改動都是「UI 直接改資料」，沒有命令層、沒有 undo。
> 權威：TiXL（`external/tixl`）。validation ladder 見 `tooll3-interaction-compatibility` skill。

## 目標

讓柏為能在節點編輯器裡做完整的基本編輯，且**每個編輯都能 Cmd+Z 反悔**：

1. 刪節點、刪線（已有手勢，需搬上命令層）
2. 加節點、移動節點（已有手勢，需搬上命令層）
3. reconnect — 拖已接線的端點改接到別的 port（新）
4. 插入節點到線中間 — TiXL 式線上浮圓點觸發（新，複合動作）
5. 框選多個 → 一起刪 / 一起移動（新）
6. 複製貼上 / duplicate（新）

**完成定義**：柏為親手開 app，能做上述每個操作，且每個都能 Cmd+Z 退回、Cmd+Shift+Z 重做。不是 selftest 綠燈就算完。

## 非目標（這一刀不碰）

- 對齊、群組、收合成 compound
- 自動排版、minimap、線上呼吸動畫的視覺細節打磨（圓點先求能按，不求 ease 動畫）
- 跨 compound 的複製貼上

## 現況（為什麼這樣設計）

`app/src/ui/editor_ui.cpp` 目前：
- `addNode()`（L38）直接 `g_graph.nodes.push_back`
- 建線（L122-139）直接 `g_graph.connections.push_back`
- 刪線/刪節點（L142-169）直接 `erase`
- 移動（L171-180）每幀把 editor 位置寫回 graph

全部繞過任何命令層，**沒有 undo**。刪錯一個節點救不回來。插入節點是「一次改三樣」（刪舊線+加節點+接兩條新線），沒有 undo 層去綁，做錯會留下斷線（dangling）。

> 2026-06-08 修正：刪除手勢原本在 macOS 上完全無效——root cause 是 Mac 的 delete 鍵送 Backspace，而 imgui-node-editor 只聽 forward-Delete。已在 editor_ui.cpp 加一段 Backspace→`ed::DeleteNode`/`ed::DeleteLink` 的路由（柏為親手確認可刪節點/線/多選）。此修正是直接改圖、仍無 undo，屬階段 1 要搬上命令層的既有操作之一。

TiXL 的對應做法（已查證）：
- 所有改動走命令，疊在 `Editor/UiModel/Commands/UndoRedoStack.cs` 上
- 命令集：`AddConnectionCommand` / `DeleteConnectionCommand` / `AddSymbolChildCommand` / `DeleteSymbolChildrenCommand` / `ModifyCanvasElementsCommand`（移動）等
- 插入節點 = `ConnectionSplitHelper`：懸停線上→浮出圓點→按下→`SplitConnectionWithSymbolBrowser`，把刪線+加節點+接兩線綁成一個可反悔動作

## 架構

### 命令層放哪一區

`app/` 區（產品行為，改 runtime 的圖）。新增：

```
app/src/app/command.h      Command 介面 + CommandStack + MacroCommand
app/src/app/command.cpp    stack 實作
app/src/app/graph_commands.h/.cpp   各具體命令（操作 sw::doc::g_graph）
```

依賴方向自檢：命令在 app 改 runtime/graph（app→runtime ✓）；editor_ui 在 ui 呼叫命令（ui→app ✓）。符合架構憲法。
鐵律 4：若 `graph_commands.cpp` 逼近 400 行或命令逼近 12 個，按操作類別拆檔。

### 核心型別

```cpp
struct Command {
  virtual ~Command() = default;
  virtual void doIt() = 0;     // 執行
  virtual void undo() = 0;     // 退回
  virtual const char* name() const = 0;  // for command log / debug
};

class CommandStack {
  // push(cmd): cmd->doIt(); 推入 undo 堆；清空 redo 堆
  // undo(): 退一格，呼叫 undo()
  // redo(): 前進一格，呼叫 doIt()
  // 命令日誌可讀（鐵律：command log is readable）
};

struct MacroCommand : Command {  // 把多個命令綁成一個
  std::vector<std::unique_ptr<Command>> children;
  // doIt: 依序 doIt；undo: 反序 undo
};
```

**editor_ui 改動原則**：手勢偵測不變，但偵測到「該改圖」時，不再直接改 `g_graph`，改成 `g_stack.push(std::make_unique<XxxCommand>(...))`。

### 命令清單（資料/職責）

| 命令 | 職責 | undo 怎麼退 |
|---|---|---|
| `AddNodeCommand` | 加一個節點 | 移除該節點 |
| `DeleteNodesCommand` | 刪 N 個節點 + 它們的連線 | 還原節點與被刪的連線 |
| `AddConnectionCommand` | 接一條線 | 移除該線 |
| `DeleteConnectionsCommand` | 刪 N 條線 | 還原這些線 |
| `MoveNodesCommand` | 移動 N 個節點 | 還原舊座標 |
| `ReconnectCommand` | 改接（記錄舊 from/to → 新 from/to） | 還原舊接法 |
| `PasteCommand` | 貼上一組節點+內部連線（新 id） | 移除貼上的節點與連線 |
| 插入節點 | `MacroCommand{ DeleteConnections(舊線), AddNode, AddConnection×2 }` | macro 反序退 |
| duplicate | = 複製選取 → `PasteCommand`（位移一點貼出） | 同 PasteCommand |

**移動進 undo 的策略**：拖動過程中不記命令；**滑鼠放開時**記一個 `MoveNodesCommand`（含拖動前的舊座標），避免連續拖動塞爆 undo 堆。

### 圖不變式（每個命令做完後可驗，鐵律 5 / ladder L5）

- node id 唯一
- 連線兩端 pin 都指向存在的節點與存在的 port
- 無斷線（dangling）
- input port cardinality（一個 input 最多一條線——接新線時若已有舊線，先刪舊線，TiXL 行為）

## 分五階段交付（每階段柏為親手測 + 一個 selftest）

> 順序鎖：階段 1 是地基，必須先過。其餘可順序往上接。

| 階段 | 做什麼 | 柏為親手驗收 | selftest |
|---|---|---|---|
| 1 | command.h/.cpp + CommandStack；把現有 加/刪/移 搬上命令；Cmd+Z / Cmd+Shift+Z 綁鍵 | 刪一個節點 → Cmd+Z 它回來；加、移動同樣可反悔 | `--selftest-command`：each command do/undo/redo 後圖等同預期 + 不變式 |
| 2 | `ReconnectCommand` + 拖端點手勢 | 拖已接線的 input 端點到別的 port → 改接；Cmd+Z 退回原接法；中途放掉到空白 → 圖不變 | reconnect do/undo trace |
| 3 | 插入節點（TiXL 式線上浮圓點 → 選單 → MacroCommand） | 兩接好的節點間插一個，三條線狀態正確；Cmd+Z 一次全退 | split macro do/undo：刪1線→加1節點→加2線；undo 後完全還原 |
| 4 | 框選 → `DeleteNodesCommand` / `MoveNodesCommand`（複數） | 框三個一起刪，Cmd+Z 一次全回；框三個一起拖，Cmd+Z 一次全回 | multi-delete / multi-move do/undo |
| 5 | 複製貼上 / duplicate（`PasteCommand`） | 選兩個有連線的節點，複製貼上 → 得到含內部連線的副本（新 id） | paste do/undo；貼出的圖內部連線正確、id 不撞 |

每階段結束跑既有的 `--selftest-graph` + `--selftest-save`，確認存檔往返（ladder L6）沒被弄壞。

## 風險 / 待驗（誠實標記）

- **imgui-node-editor 的 reconnect**：拖已連接的 input pin 時，library 會觸發一次新 link 建立（`QueryNewLink`）。我們要偵測「來源 pin 已有舊線」並在同一動作裡刪舊線+接新線。需先用小實驗確認 library 的事件順序（記憶踩雷：`ed::EndCreate` 區塊行為）。
- **線上浮圓點 hit-test**：需偵測滑鼠到哪條線最近（用 imgui-node-editor 的 link hover / 自算點到 bezier 距離）。TiXL 用前景 drawlist 畫圓點 + InvisibleButton 接點擊。先求能按，動畫 ease 後補。
- **快捷鍵與 macOS**：Cmd+Z / Cmd+Shift+Z / Cmd+C / Cmd+V / Delete。imgui 的 io 取 Cmd（Super）鍵。

## 不做什麼（YAGNI）

- 不抄 TiXL 的 Symbol/Instance 結構，圖 schema 維持我們自己的 `sw::Graph`
- 不做 macro 命令的合併/壓縮優化
- 不做命令序列化到檔（undo 堆不存進 .swproj，存檔只存最終圖狀態，沿用現有 toJson）
